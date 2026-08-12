#include "OzPawnSystem.hpp"
#include "../Package/OzAssetMapper.hpp"
#include "../Package/PackageAssetLoader.hpp"
#include "../Renderer/EngineBillboard.hpp"
#include "../Script/LightningEntityManager.hpp"
#include "../Script/LightningEntityRegistry.hpp"
#include "PlayerMovement.hpp"
#include "Items.hpp"
#include "../Log.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
PawnSystem& PawnSystem::Instance() {
    static PawnSystem instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Find a registered PawnDef by name (case-insensitive)
// ---------------------------------------------------------------------------
PawnDef* PawnSystem::FindDef(const char* name) {
    if (!name) return nullptr;
    for (auto& d : m_defs) {
        if (d.name.size() == strlen(name) &&
            std::equal(d.name.begin(), d.name.end(), name,
                       [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); }))
            return &d;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// AllocSlot â€” find a free slot or grow the vector
// ---------------------------------------------------------------------------
int PawnSystem::AllocSlot() {
    if (!m_freeIds.empty()) {
        int id = m_freeIds.back();
        m_freeIds.pop_back();
        return id;
    }
    int id = (int)m_pawns.size();
    m_pawns.emplace_back();
    return id;
}

// ---------------------------------------------------------------------------
// RegisterDef
// ---------------------------------------------------------------------------
void PawnSystem::RegisterDef(const PawnDef& def) {
    if (FindDef(def.name.c_str())) return;
    m_defs.push_back(def);
}

// ---------------------------------------------------------------------------
// Spawn
// ---------------------------------------------------------------------------
int PawnSystem::Spawn(Vector3 pos, const char* defName) {
    PawnDef* def = FindDef(defName);
    if (!def) {
        OZ_WARN("PawnSystem: unknown def '%s'", defName);
        return -1;
    }

    int slot = AllocSlot();
    Pawn& p = m_pawns[slot];
    p.id = m_nextId++;
    p.active = true;
    p.position = pos;
    p.spawnPosition = pos;
    p.velocity = {0, 0, 0};
    p.yaw = 0.0f;
    p.state = PawnState::IDLE;
    p.prevState = PawnState::IDLE;

    p.speed = def->speed;
    p.aggroRange = def->aggroRange;
    p.attackRange = def->attackRange;
    p.damage = def->damage;
    p.health = def->maxHealth;
    p.maxHealth = def->maxHealth;

    p.patrolTimer = 0.0f;
    p.stateTimer = 0.0f;
    p.defName = defName;

    // Load sprite and scream using def paths or convention (with package fallback)
    {
        if (!def->sprite_path.empty())
            p.sprite = LoadTextureWithFallback(def->sprite_path.c_str());
        if (p.sprite.id == 0) {
            std::string fallbackSprite = std::string("GameData/Global/Pawn/") + defName + ".png";
            p.sprite = LoadTextureWithFallback(fallbackSprite.c_str());
        }

        if (!def->scream_path.empty())
            p.scream = LoadSoundWithFallback(def->scream_path.c_str());
        if (p.scream.frameCount == 0) {
            std::string fallbackScream = std::string("GameData/Global/Pawn/") + defName + ".wav";
            p.scream = LoadSoundWithFallback(fallbackScream.c_str());
        }
    }

    // Optionally create a LightningScript entity instance for script hooks
#ifndef OMEGA_TEST_ENV
    int lemIdx = LightningEntityManager::Instance().Spawn(defName);
    if (lemIdx >= 0) p.scriptInstanceIndex = lemIdx;
#endif

    OZ_DEBUG("Pawn spawned: id=%d def=%s sprite=%d scream=%d at (%.1f, %.1f, %.1f)",
             p.id, defName, p.sprite.id, p.scream.frameCount, pos.x, pos.y, pos.z);
    return (int)p.id;
}

// ---------------------------------------------------------------------------
// Despawn
// ---------------------------------------------------------------------------
void PawnSystem::Despawn(int id) {
    for (size_t i = 0; i < m_pawns.size(); i++) {
        if (m_pawns[i].active && m_pawns[i].id == (uint32_t)id) {
            OZ_DEBUG("Pawn despawned: id=%d def=%s", id, m_pawns[i].defName.c_str());
#ifndef OMEGA_TEST_ENV
            if (m_pawns[i].scriptInstanceIndex >= 0)
                LightningEntityManager::Instance().Despawn(m_pawns[i].scriptInstanceIndex);
#endif
            if (m_pawns[i].sprite.id != 0) UnloadTexture(m_pawns[i].sprite);
            if (m_pawns[i].scream.frameCount != 0) UnloadSound(m_pawns[i].scream);
            m_pawns[i].active = false;
            m_freeIds.push_back((int)i);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// DespawnAll
// ---------------------------------------------------------------------------
void PawnSystem::DespawnAll() {
    for (auto& p : m_pawns) {
        if (p.sprite.id != 0) UnloadTexture(p.sprite);
        if (p.scream.frameCount != 0) UnloadSound(p.scream);
        p.active = false;
    }
    m_freeIds.clear();
    for (size_t i = 0; i < m_pawns.size(); i++)
        m_freeIds.push_back((int)i);
}

// ---------------------------------------------------------------------------
// Get
// ---------------------------------------------------------------------------
Pawn* PawnSystem::Get(int id) {
    for (auto& p : m_pawns) {
        if (p.active && p.id == (uint32_t)id)
            return &p;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Light node management
// ---------------------------------------------------------------------------
int PawnSystem::AddLight(const LightNode& node) {
    LightNode n = node;
    if (n.id == 0) n.id = m_nextLightId++;
    m_lights.push_back(n);
    return (int)n.id;
}

void PawnSystem::RemoveLight(int id) {
    auto it = std::remove_if(m_lights.begin(), m_lights.end(),
        [id](const LightNode& n) { return n.id == (uint32_t)id; });
    m_lights.erase(it, m_lights.end());
}

void PawnSystem::ClearLights() {
    m_lights.clear();
}

LightNode* PawnSystem::GetLight(int id) {
    for (auto& n : m_lights) {
        if (n.id == (uint32_t)id) return &n;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// IsPlayerAttacked — check if any pawn is close enough to damage the player
// ---------------------------------------------------------------------------
bool PawnSystem::IsPlayerAttacked(Vector3 playerPos, float& outDamage) {
    outDamage = 0.0f;
    for (auto& p : m_pawns) {
        if (!p.active || p.state == PawnState::DEAD) continue;

        float dx = playerPos.x - p.position.x;
        float dz = playerPos.z - p.position.z;
        float dist = sqrtf(dx * dx + dz * dz);

        if (dist < p.attackRange) {
            outDamage = p.damage;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Player Start Nodes
// ---------------------------------------------------------------------------
void PawnSystem::AddPlayerStart(const PlayerStartNode& node) {
    PlayerStartNode n = node;
    if (n.id == 0) n.id = m_nextEntityId++;
    m_playerStarts.push_back(n);
}

void PawnSystem::RemovePlayerStart(int id) {
    auto it = std::remove_if(m_playerStarts.begin(), m_playerStarts.end(),
        [id](const PlayerStartNode& n) { return n.id == (uint32_t)id; });
    m_playerStarts.erase(it, m_playerStarts.end());
}

void PawnSystem::ClearPlayerStarts() {
    m_playerStarts.clear();
}

PlayerStartNode* PawnSystem::GetFirstPlayerStart() {
    if (!m_playerStarts.empty()) return &m_playerStarts[0];
    return nullptr;
}

void PawnSystem::RespawnPlayerAtStart(Camera3D& camera) {
    PlayerStartNode* ps = GetFirstPlayerStart();
    if (ps) {
        camera.position = ps->position;
        camera.position.y += 2.0f;
        camera.target = {ps->position.x, ps->position.y + 2.0f, ps->position.z - 5.0f};
    } else {
        camera.position = {0.0f, 5.0f, 0.0f};
        camera.target = {0.0f, 5.0f, -5.0f};
    }
}

// ---------------------------------------------------------------------------
// Projectile nodes
// ---------------------------------------------------------------------------
int PawnSystem::SpawnProjectile(const ProjectileNode& node) {
    ProjectileNode p = node;
    p.id = m_nextEntityId++;
    p.active = true;
    p.age = 0.0f;
    m_projectiles.push_back(p);
    return (int)m_projectiles.size() - 1;
}

void PawnSystem::UpdateProjectiles(float dt) {
    for (auto& p : m_projectiles) {
        if (!p.active) continue;
        p.age += dt;
        if (p.age >= p.lifetime) {
            p.active = false;
            continue;
        }
        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;
        p.position.z += p.velocity.z * dt;
        // Simple gravity on projectiles
        p.velocity.y -= 5.0f * dt;

        // Projectile vs Pawn collision
        for (auto& pawn : m_pawns) {
            if (!pawn.active || pawn.state == PawnState::DEAD) continue;
            float dist = Vector3Distance(p.position, pawn.position);
            if (dist < 1.5f) {
                pawn.health -= (int)p.damage;
                p.active = false;
                if (pawn.health <= 0) {
                    pawn.active = false;
                    pawn.state = PawnState::DEAD;
                }
                break;
            }
        }
    }
    // Remove inactive projectiles
    m_projectiles.erase(
        std::remove_if(m_projectiles.begin(), m_projectiles.end(),
                       [](const ProjectileNode& p) { return !p.active; }),
        m_projectiles.end());
}

void PawnSystem::DrawProjectiles(Camera3D& camera) {
    for (auto& p : m_projectiles) {
        if (!p.active) continue;
        // Draw as small glowing spheres
        Color c = {255, 200, 50, 255};
        float radius = 0.3f;
        DrawSphere(p.position, radius, c);
        // Optional glow sprite
        if (p.sprite && p.sprite->id > 0) {
            DrawBillboard(camera, *p.sprite, p.position, 0.5f, WHITE);
        }
    }
}

void PawnSystem::ClearProjectiles() {
    m_projectiles.clear();
}

// ---------------------------------------------------------------------------
// Pickup nodes
// ---------------------------------------------------------------------------
int PawnSystem::AddPickup(const PickupNode& node) {
    PickupNode n = node;
    if (n.id == 0) n.id = m_nextEntityId++;
    m_pickups.push_back(n);
    return (int)n.id;
}

void PawnSystem::RemovePickup(int id) {
    auto it = std::remove_if(m_pickups.begin(), m_pickups.end(),
        [id](const PickupNode& n) { return n.id == (uint32_t)id; });
    m_pickups.erase(it, m_pickups.end());
}

void PawnSystem::ClearPickups() {
    ClearWeaponPickupCache();
    m_pickups.clear();
}

PickupNode* PawnSystem::GetPickup(int id) {
    for (auto& n : m_pickups) {
        if (n.id == (uint32_t)id) return &n;
    }
    return nullptr;
}

void PawnSystem::UpdatePickups(float dt, Vector3 playerPos, BoundingBox playerBounds) {
    for (auto& n : m_pickups) {
        if (!n.active) {
            // Handle respawn timer
            if (n.respawnTimer > 0.0f) {
                n.respawnTimer -= dt;
                if (n.respawnTimer <= 0.0f) {
                    n.active = true;
                }
            }
            continue;
        }

        // Check AABB collision with player
        BoundingBox pickupBox = {
            {n.position.x - 0.5f, n.position.y - 0.5f, n.position.z - 0.5f},
            {n.position.x + 0.5f, n.position.y + 0.5f, n.position.z + 0.5f}
        };

        if (CheckCollisionBoxes(pickupBox, playerBounds)) {
            n.active = false;
            n.respawnTimer = n.respawnTime;

            // Map typeName to item ID via LightningScript entity registry
            int itemId = 0;
            const EntityDef* edef = LightningEntityRegistry::Instance().Find(n.typeName);
            if (edef) {
                auto it = edef->stats.floats.find("item_id");
                if (it != edef->stats.floats.end())
                    itemId = (int)it->second;
            }
            if (itemId == 0) {
                // Fallback: scan ItemDB by name
                for (int i = 0; i < ITEM_DB_SIZE; i++) {
                    std::string dbName(ItemDB[i].name);
                    // Remove spaces for comparison: "Health Vial" -> "HealthVial"
                    dbName.erase(std::remove(dbName.begin(), dbName.end(), ' '), dbName.end());
                    if (dbName == n.typeName) { itemId = ItemDB[i].id; break; }
                }
            }

            const ItemDBEntry* def = (itemId > 0) ? GetItemDef(itemId) : nullptr;
            if (def) {
                auto& lem = LightningEntityManager::Instance();
                switch (def->category) {
                    case ItemCategory::HEALTH_VIAL:
                        lem.SetPlayerHealth(std::min(lem.GetPlayerHealth() + (float)def->value, lem.GetPlayerMaxHealth()));
                        break;
                    case ItemCategory::MANA_VIAL:
                        lem.SetPlayerMana(std::min(lem.GetPlayerMana() + (float)def->value, lem.GetPlayerMaxMana()));
                        break;
                    case ItemCategory::ENERGY_CRYSTAL:
                        lem.SetPlayerPsychicEnergy(std::min(lem.GetPlayerPsychicEnergy() + (float)def->value, lem.GetPlayerMaxPsychicEnergy()));
                        break;
                    case ItemCategory::COIN:
                        gInventory.coins += def->value;
                        break;
                    default:
                        gInventory.AddToBackpack(itemId, 1);
                        break;
                }
            } else if (edef && edef->type == EntityType::WEAPON) {
                int instIdx = LightningEntityManager::Instance().Spawn(edef->name);
                if (instIdx >= 0) {
                    for (int s = 0; s < LightningEntityManager::HOTBAR_SIZE; s++) {
                        if (LightningEntityManager::Instance().HotbarAt(s) < 0) {
                            LightningEntityManager::Instance().HotbarAssign(s, instIdx);
                            break;
                        }
                    }
                }
            }
            OZ_INFO("Pickup collected: %s (itemId=%d)", n.typeName.c_str(), itemId);
            m_pickupFeedback.collected = true;
            m_pickupFeedback.typeName = n.typeName;
            m_pickupFeedback.itemId = itemId;
            m_pickupFeedback.flashTimer = 0.5f;
        }
    }
}

// ---------------------------------------------------------------------------
// Zone Volume Nodes
// ---------------------------------------------------------------------------
int PawnSystem::AddZone(const ZoneVolumeNode& node) {
    ZoneVolumeNode n = node;
    if (n.id == 0) n.id = m_nextEntityId++;
    m_zones.push_back(n);
    return (int)n.id;
}

void PawnSystem::RemoveZone(int id) {
    auto it = std::remove_if(m_zones.begin(), m_zones.end(),
        [id](const ZoneVolumeNode& n) { return n.id == (uint32_t)id; });
    m_zones.erase(it, m_zones.end());
}

void PawnSystem::ClearZones() {
    m_zones.clear();
}

ZoneVolumeNode* PawnSystem::GetZone(int id) {
    for (auto& n : m_zones) {
        if (n.id == (uint32_t)id) return &n;
    }
    return nullptr;
}

ZoneVolumeNode* PawnSystem::CheckZoneCollision(Vector3 pos, BoundingBox bounds) {
    // Check if point is inside any zone
    for (auto& n : m_zones) {
        if (pos.x >= n.bounds.min.x && pos.x <= n.bounds.max.x &&
            pos.y >= n.bounds.min.y && pos.y <= n.bounds.max.y &&
            pos.z >= n.bounds.min.z && pos.z <= n.bounds.max.z) {
            return &n;
        }
    }
    // Also check box collision
    for (auto& n : m_zones) {
        if (CheckCollisionBoxes(bounds, n.bounds)) {
            return &n;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// SkyZoneNode management
// ---------------------------------------------------------------------------
int PawnSystem::AddSkyZone(const SkyZoneNode& node) {
    SkyZoneNode n = node;
    if (n.id == 0) n.id = m_nextEntityId++;
    m_skyZones.push_back(n);
    return (int)m_skyZones.size() - 1;
}

void PawnSystem::RemoveSkyZone(int id) {
    // Unload texture before removal
    for (auto& z : m_skyZones) {
        if (z.id == (uint32_t)id && z.skyboxTex.id > 0) {
            UnloadTexture(z.skyboxTex);
            break;
        }
    }
    auto it = std::remove_if(m_skyZones.begin(), m_skyZones.end(),
        [id](const SkyZoneNode& n) { return n.id == (uint32_t)id; });
    m_skyZones.erase(it, m_skyZones.end());
    if (m_activeSkyZoneIndex >= (int)m_skyZones.size())
        m_activeSkyZoneIndex = -1;
}

void PawnSystem::ClearSkyZones() {
    for (auto& z : m_skyZones)
        if (z.skyboxTex.id > 0) UnloadTexture(z.skyboxTex);
    m_skyZones.clear();
    m_activeSkyZoneIndex = -1;
}

SkyZoneNode* PawnSystem::GetSkyZone(int id) {
    for (auto& n : m_skyZones) {
        if (n.id == (uint32_t)id) return &n;
    }
    return nullptr;
}

void PawnSystem::SetActiveSkyZone(int index) {
    m_activeSkyZoneIndex = (index >= 0 && index < (int)m_skyZones.size()) ? index : -1;
}

SkyZoneNode* PawnSystem::GetActiveSkyZone() {
    if (m_activeSkyZoneIndex >= 0 && m_activeSkyZoneIndex < (int)m_skyZones.size())
        return &m_skyZones[m_activeSkyZoneIndex];
    return nullptr;
}

// ---------------------------------------------------------------------------
// UpdateSkyZone detect if player is inside a SkyZoneNode trigger volume
// ---------------------------------------------------------------------------
void PawnSystem::UpdateSkyZone(Vector3 playerPos, BoundingBox playerBounds) {
    int prevActive = m_activeSkyZoneIndex;

    // Find the sky zone the player is inside
    m_activeSkyZoneIndex = -1;
    for (int i = 0; i < (int)m_skyZones.size(); i++) {
        auto& n = m_skyZones[i];
        if (playerPos.x >= n.bounds.min.x && playerPos.x <= n.bounds.max.x &&
            playerPos.y >= n.bounds.min.y && playerPos.y <= n.bounds.max.y &&
            playerPos.z >= n.bounds.min.z && playerPos.z <= n.bounds.max.z) {
            m_activeSkyZoneIndex = i;
            break;
        }
    }

    bool wasInSky = (prevActive >= 0);
    bool inSky = (m_activeSkyZoneIndex >= 0);

    if (inSky && !wasInSky) {
        auto& n = m_skyZones[m_activeSkyZoneIndex];
        LightningEntityManager::Instance().TriggerZoneAction(
            n.name.empty() ? "zone_sky_0" : n.name.c_str(), "on_enter");
    } else if (!inSky && wasInSky) {
        auto& n = m_skyZones[prevActive];
        LightningEntityManager::Instance().TriggerZoneAction(
            n.name.empty() ? "zone_sky_0" : n.name.c_str(), "on_exit");
    }
}

// ---------------------------------------------------------------------------
// SyncSkyboxState — pull pending script effects into active SkyZoneNode
// ---------------------------------------------------------------------------
void PawnSystem::SyncSkyboxState() {
    SkyZoneNode* sky = GetActiveSkyZone();
    if (!sky) return;
    auto& lem = LightningEntityManager::Instance();
    if (lem.HasPendingSkybox()) {
        sky->skyboxPath = lem.PendingSkybox();
        lem.ClearPendingSkybox();
    }
}

// ---------------------------------------------------------------------------
// Update - tick AI for all pawns
// ---------------------------------------------------------------------------
void PawnSystem::Update(Vector3 playerPos, float dt) {
    for (auto& p : m_pawns) {
        if (!p.active) continue;

        float dx = playerPos.x - p.position.x;
        float dz = playerPos.z - p.position.z;
        float distToPlayer = sqrtf(dx * dx + dz * dz);
        p.stateTimer += dt;

        switch (p.state) {
            case PawnState::IDLE:
                if (distToPlayer < p.aggroRange) TransitionState(p, PawnState::CHASE);
                else if (p.stateTimer > 2.0f) TransitionState(p, PawnState::PATROL);
                break;
            case PawnState::PATROL:
                TickPatrol(p, dt);
                if (distToPlayer < p.aggroRange) TransitionState(p, PawnState::CHASE);
                break;
            case PawnState::CHASE:
                TickChase(p, playerPos, dt);
                break;
            case PawnState::RETURN:
                TickReturn(p, dt);
                break;
            case PawnState::DEAD:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// DrawAll - draw pawn billboards
// ---------------------------------------------------------------------------
void PawnSystem::DrawAll(Camera3D& camera, Shader litShader) {
    for (auto& p : m_pawns) {
        if (!p.active || p.state == PawnState::DEAD) continue;

        if (p.sprite.id != 0) {
            if (litShader.id > 0) {
                EngineBillboard::DrawSprite(camera, p.sprite, p.position, 2.0f, WHITE, litShader);
            } else {
                DrawBillboard(camera, p.sprite, p.position, 2.0f, WHITE);
            }
        } else {
            EngineBillboard::Draw(camera, "PawnNode", p.position, 2.0f, litShader);
        }
    }
}

// ---------------------------------------------------------------------------
// Emitter Nodes (Sound / Music markers)
// ---------------------------------------------------------------------------
int PawnSystem::AddEmitter(const EmitterNode& node) {
    EmitterNode n = node;
    if (n.id == 0) n.id = m_nextEntityId++;
    m_emitters.push_back(n);
    return (int)n.id;
}

void PawnSystem::RemoveEmitter(int id) {
    auto it = std::remove_if(m_emitters.begin(), m_emitters.end(),
        [id](const EmitterNode& n) { return n.id == (uint32_t)id; });
    m_emitters.erase(it, m_emitters.end());
}

void PawnSystem::ClearEmitters() {
    m_emitters.clear();
}

// ---------------------------------------------------------------------------
// ClearWeaponPickupCache — unload all cached weapon pickup models
// ---------------------------------------------------------------------------
void PawnSystem::ClearWeaponPickupCache() {
    for (auto& [name, entry] : m_weaponPickupCache) {
        if (entry.model.meshCount > 0) UnloadModel(entry.model);
        if (entry.texture.id > 0) UnloadTexture(entry.texture);
    }
    m_weaponPickupCache.clear();
}

// ---------------------------------------------------------------------------
// DrawEntities - draw player starts, pickups, zones, emitters as billboards
// ---------------------------------------------------------------------------
void PawnSystem::DrawEntities(Camera3D& camera, Shader litShader) {
    // Player start billboards
    for (auto& n : m_playerStarts) {
        EngineBillboard::Draw(camera, "PlayerStart",
            {n.position.x, n.position.y + 0.5f, n.position.z}, 1.2f, litShader);
    }

    // Pickup billboards with bobbing
    for (auto& n : m_pickups) {
        if (!n.active) continue;
        const EntityDef* edef = LightningEntityRegistry::Instance().Find(n.typeName);
        if (edef && edef->type == EntityType::WEAPON) {
            float bob = sinf((float)GetTime() * 3.0f) * 0.15f;
            Vector3 pos = {n.position.x, n.position.y + 0.5f + bob, n.position.z};
            auto meshIt = edef->stats.strings.find("mesh");
            auto texIt = edef->stats.strings.find("texture");
            if (meshIt != edef->stats.strings.end() && texIt != edef->stats.strings.end()) {
                // Use cached model+texture per weapon type
                auto cacheIt = m_weaponPickupCache.find(n.typeName);
                if (cacheIt == m_weaponPickupCache.end()) {
                    // First encounter — load and cache
                    std::string meshPath = "GameData/Global/gun/" + n.typeName + "/" + meshIt->second;
                    std::string texPath = "GameData/Global/gun/" + n.typeName + "/" + texIt->second;
                    WeaponPickupCache entry;
                    entry.model = LoadModel(meshPath.c_str());
                    if (entry.model.meshCount > 0) {
                        entry.texture = LoadTexture(texPath.c_str());
                        if (entry.texture.id > 0)
                            entry.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = entry.texture;
                        if (litShader.id > 0)
                            entry.model.materials[0].shader = litShader;
                    }
                    cacheIt = m_weaponPickupCache.emplace(n.typeName, entry).first;
                }
                if (cacheIt->second.model.meshCount > 0) {
                    float yaw = atan2f(camera.position.x - pos.x, camera.position.z - pos.z) * RAD2DEG;
                    DrawModelEx(cacheIt->second.model, pos, {0, 1, 0}, yaw, {1.5f, 1.5f, 1.5f}, WHITE);
                }
            }
        } else {
            EngineBillboard::DrawPickup(camera, n.typeName.c_str(), n.position, 0.8f, litShader);
        }
    }

    // Zone billboards at center of bounding box
    for (auto& n : m_zones) {
        Vector3 center = {
            (n.bounds.min.x + n.bounds.max.x) * 0.5f,
            (n.bounds.min.y + n.bounds.max.y) * 0.5f,
            (n.bounds.min.z + n.bounds.max.z) * 0.5f
        };
        const char* icon = "ZoneInfo";
        if (n.zoneType == ZoneType::ZONE_WATER) icon = "ZoneWater";
        else if (n.zoneType == ZoneType::ZONE_LADDER) icon = "ZoneLadder";
        else if (n.zoneType == ZoneType::ZONE_SKY) icon = "ZoneSky";
        else if (n.zoneType == ZoneType::ZONE_GAMEPLAY_SOUND) icon = "ZoneSound";
        else if (n.zoneType == ZoneType::ZONE_REVERB) icon = "ZoneReverb";
        EngineBillboard::Draw(camera, icon, center, 1.0f, litShader);
    }

    // Sound / music emitter billboards
    for (auto& n : m_emitters) {
        const char* icon = (n.type == EmitterType::SOUND) ? "Sound" : "Music";
        EngineBillboard::Draw(camera, icon, {n.position.x, n.position.y + 0.5f, n.position.z}, 1.0f, litShader);
    }
}

// ---------------------------------------------------------------------------
// FSM tick helpers
// ---------------------------------------------------------------------------
void PawnSystem::TickPatrol(Pawn& p, float dt) {
    p.patrolTimer += dt;
    if (p.patrolTimer > 2.0f) {
        // Random direction change
        float angle = PState(p).angle;
        p.velocity.x = cosf(angle) * p.speed * 0.5f;
        p.velocity.z = sinf(angle) * p.speed * 0.5f;
        PState(p).angle += dt * 1.5f;
        p.patrolTimer = 0.0f;
    }

    p.position.x += p.velocity.x * dt;
    p.position.z += p.velocity.z * dt;

}

void PawnSystem::TickChase(Pawn& p, const Vector3& playerPos, float dt) {
    Vector3 dir = {playerPos.x - p.position.x, 0, playerPos.z - p.position.z};
    float dist = sqrtf(dir.x * dir.x + dir.z * dir.z);
    if (dist > 0.1f) {
        dir.x /= dist;
        dir.z /= dist;
        p.position.x += dir.x * p.speed * dt;
        p.position.z += dir.z * p.speed * dt;
    }

    // Return to patrol if player out of aggro range
    if (dist > p.aggroRange * 1.5f) {
        TransitionState(p, PawnState::RETURN);
    }
}

void PawnSystem::TickReturn(Pawn& p, float dt) {
    Vector3 dir = {p.spawnPosition.x - p.position.x, 0, p.spawnPosition.z - p.position.z};
    float dist = sqrtf(dir.x * dir.x + dir.z * dir.z);
    if (dist < 1.0f) {
        TransitionState(p, PawnState::PATROL);
    } else if (dist > 0.1f) {
        dir.x /= dist;
        dir.z /= dist;
        p.position.x += dir.x * p.speed * dt;
        p.position.z += dir.z * p.speed * dt;
    }
}

void PawnSystem::TransitionState(Pawn& p, PawnState newState) {
    p.prevState = p.state;
    p.state = newState;
    p.stateTimer = 0.0f;

    // Fire LightningScript hook on state change if entity instance exists
#ifndef OMEGA_TEST_ENV
    if (p.scriptInstanceIndex >= 0) {
        const char* action = nullptr;
        switch (newState) {
            case PawnState::CHASE:  action = "on_chase"; break;
            case PawnState::PATROL: action = "on_patrol"; break;
            case PawnState::RETURN: action = "on_return"; break;
            case PawnState::DEAD:   action = "on_death"; break;
            default: break;
        }
        if (action) {
            auto* inst = LightningEntityManager::Instance().Get(p.scriptInstanceIndex);
            LightningEntityManager::Instance().RunAction(inst, action);
        }
    }
#endif
}

PawnSystem::PatrolState& PawnSystem::PState(Pawn& p) {
    static std::unordered_map<uint32_t, PatrolState> states;
    return states[p.id];
}

// ---------------------------------------------------------------------------
// GetActiveZones — returns all zones overlapping the given position/bounds,
// sorted by priority (highest first), then by volume (smallest first).
// ---------------------------------------------------------------------------
std::vector<ZoneVolumeNode*> PawnSystem::GetActiveZones(Vector3 pos, BoundingBox bounds) {
    std::vector<ZoneVolumeNode*> result;
    for (auto& z : m_zones) {
        // Point test
        bool inside = (pos.x >= z.bounds.min.x && pos.x <= z.bounds.max.x &&
                       pos.y >= z.bounds.min.y && pos.y <= z.bounds.max.y &&
                       pos.z >= z.bounds.min.z && pos.z <= z.bounds.max.z);
        // Fallback to box test
        if (!inside)
            inside = CheckCollisionBoxes(bounds, z.bounds);
        if (inside)
            result.push_back(&z);
    }
    // Sort: highest priority first, then smallest volume
    std::sort(result.begin(), result.end(), [](ZoneVolumeNode* a, ZoneVolumeNode* b) {
        if (a->priority != b->priority) return a->priority > b->priority;
        float va = (a->bounds.max.x - a->bounds.min.x) *
                   (a->bounds.max.y - a->bounds.min.y) *
                   (a->bounds.max.z - a->bounds.min.z);
        float vb = (b->bounds.max.x - b->bounds.min.x) *
                   (b->bounds.max.y - b->bounds.min.y) *
                   (b->bounds.max.z - b->bounds.min.z);
        return va < vb;
    });
    return result;
}

// ---------------------------------------------------------------------------
// PointRegion::Rebuild — rebuild active zone set from a sorted list
// ---------------------------------------------------------------------------
void PointRegion::Rebuild(const std::vector<ZoneVolumeNode*>& activeZones) {
    std::unordered_set<int> newIds;
    ZoneEnvOverrides merged;

    for (auto* z : activeZones) {
        if (!z) continue;
        newIds.insert((int)z->id);
        // Merge env overrides (later zones in sorted order override earlier)
        if (z->envOverrides.applyFog) {
            merged.applyFog = true;
            merged.fogR = z->envOverrides.fogR;
            merged.fogG = z->envOverrides.fogG;
            merged.fogB = z->envOverrides.fogB;
            merged.fogDensity = z->envOverrides.fogDensity;
            merged.fogStart = z->envOverrides.fogStart;
            merged.fogEnd = z->envOverrides.fogEnd;
        }
        if (z->envOverrides.applyAmbient) {
            merged.applyAmbient = true;
            merged.ambR = z->envOverrides.ambR;
            merged.ambG = z->envOverrides.ambG;
            merged.ambB = z->envOverrides.ambB;
            merged.ambIntensity = z->envOverrides.ambIntensity;
        }
        // Reverb always uses the highest-priority zone's values
        if (z->zoneType == ZoneType::ZONE_REVERB || z->envOverrides.reverbMix > 0.0f) {
            merged.reverbMix = z->envOverrides.reverbMix;
            merged.reverbDecay = z->envOverrides.reverbDecay;
        }
    }

    // Compute enter/exit sets
    enteredZoneIds.clear();
    exitedZoneIds.clear();
    for (int id : newIds) {
        if (activeZoneIds.find(id) == activeZoneIds.end())
            enteredZoneIds.insert(id);
    }
    for (int id : activeZoneIds) {
        if (newIds.find(id) == newIds.end())
            exitedZoneIds.insert(id);
    }

    activeZoneIds = std::move(newIds);
    combinedEnv = merged;
    lastPrimaryZoneId = primaryZoneId;
    if (activeZoneIds.empty()) {
        primaryZoneId = -1;
        primaryZoneType = ZoneType::ZONE_WATER;
    } else {
        primaryZoneId = *activeZoneIds.begin();
        // Find type from the first active zone (highest priority after sorting)
        // Since we received sorted zones, the first entry is highest priority
        primaryZoneType = (!activeZones.empty() && activeZones[0])
            ? activeZones[0]->zoneType : ZoneType::ZONE_WATER;
    }
}

bool PointRegion::HasZoneId(int id) const {
    return activeZoneIds.find(id) != activeZoneIds.end();
}

void PointRegion::CommitFrame() {
    enteredZoneIds.clear();
    exitedZoneIds.clear();
}

// ---------------------------------------------------------------------------
// UpdatePlayerRegion — single-pass zone scan for the player
// ---------------------------------------------------------------------------
void PawnSystem::UpdatePlayerRegion(Vector3 playerPos, BoundingBox playerBounds) {
    auto active = GetActiveZones(playerPos, playerBounds);
    m_playerRegion.Rebuild(active);
}

