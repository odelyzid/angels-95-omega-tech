#include "OzPawnSystem.hpp"
#include "../Package/OzAssetMapper.hpp"
#include "../Renderer/EngineBillboard.hpp"
#include "../Script/LightningEntityManager.hpp"
#include "Player.hpp"
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
    for (auto& d : m_defs) {
        const char* a = name;
        const char* b = d.name;
        while (*a && *b) {
            if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
                break;
            a++; b++;
        }
        if (*a == *b) return &d;
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
    // Don't register duplicates
    if (FindDef(def.name)) return;
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
    p.sprite = {0};
    p.scream = {0};
    p.defName = defName;

    OZ_DEBUG("Pawn spawned: id=%d def=%s at (%.1f, %.1f, %.1f)", p.id, defName, pos.x, pos.y, pos.z);
    return (int)p.id;
}

// ---------------------------------------------------------------------------
// Despawn
// ---------------------------------------------------------------------------
void PawnSystem::Despawn(int id) {
    for (size_t i = 0; i < m_pawns.size(); i++) {
        if (m_pawns[i].active && m_pawns[i].id == (uint32_t)id) {
            OZ_DEBUG("Pawn despawned: id=%d def=%s", id, m_pawns[i].defName.c_str());
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
// IsPlayerAttacked â€” check if any pawn is close enough to damage the player
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

void PawnSystem::ClearPlayerStarts() {
    m_playerStarts.clear();
}

PlayerStartNode* PawnSystem::GetFirstPlayerStart() {
    if (!m_playerStarts.empty()) return &m_playerStarts[0];
    return nullptr;
}

// ---------------------------------------------------------------------------
// Pickup Nodes
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

            // Map typeName to item ID, then apply effect
            int itemId = 0;
            if (n.typeName == "HealthVial") itemId = 1;
            else if (n.typeName == "ManaVial") itemId = 2;
            else if (n.typeName == "Coin") itemId = 13;
            else if (n.typeName == "Key") itemId = 12;
            else if (n.typeName == "Powerup") itemId = 14;

            const ItemDBEntry* def = (itemId > 0) ? GetItemDef(itemId) : nullptr;
            if (def) {
                switch (def->category) {
                    case ItemCategory::HEALTH_VIAL:
                        OmegaPlayer.Health = std::min(OmegaPlayer.Health + (float)def->value, OmegaPlayer.MaxHealth);
                        break;
                    case ItemCategory::MANA_VIAL:
                        OmegaPlayer.Mana = std::min(OmegaPlayer.Mana + (float)def->value, OmegaPlayer.MaxMana);
                        break;
                    case ItemCategory::ENERGY_CRYSTAL:
                        OmegaPlayer.PsychicEnergy = std::min(OmegaPlayer.PsychicEnergy + (float)def->value, OmegaPlayer.MaxPsychicEnergy);
                        break;
                    case ItemCategory::COIN:
                        gInventory.coins += def->value;
                        break;
                    default:
                        gInventory.AddToBackpack(itemId, 1);
                        break;
                }
            }
            OZ_INFO("Pickup collected: %s (itemId=%d)", n.typeName.c_str(), itemId);
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
// UpdateSkyZone â€” detect if player is inside a ZONE_SKY volume
// ---------------------------------------------------------------------------
void PawnSystem::UpdateSkyZone(Vector3 playerPos, BoundingBox playerBounds) {
    bool wasInSky = m_inSkyZone;
    m_inSkyZone = false;
    for (auto& n : m_zones) {
        if (n.zoneType != ZoneType::ZONE_SKY) continue;
        // Check if player position is inside the sky zone
        if (playerPos.x >= n.bounds.min.x && playerPos.x <= n.bounds.max.x &&
            playerPos.y >= n.bounds.min.y && playerPos.y <= n.bounds.max.y &&
            playerPos.z >= n.bounds.min.z && playerPos.z <= n.bounds.max.z) {
            m_inSkyZone = true;
            m_activeSkyZoneBounds = n.bounds;
            if (!wasInSky) {
                // Trigger sky zone enter action (uses configured zone .ozls name)
                LightningEntityManager::Instance().TriggerZoneAction(
                    n.name.empty() ? "skyzone_default" : n.name.c_str(), "on_enter");
            }
            return;
        }
    }
    if (wasInSky && !m_inSkyZone) {
        // Trigger sky zone exit action
        // Find the last zone we were in
        LightningEntityManager::Instance().TriggerZoneAction(
            "skyzone_default", "on_exit");
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
void PawnSystem::DrawAll(Camera3D& camera) {
    for (auto& p : m_pawns) {
        if (!p.active || p.state == PawnState::DEAD) continue;

        EngineBillboard::Draw(camera, "PawnNode", p.position, 2.0f);
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
// DrawEntities - draw player starts, pickups, zones, emitters as billboards
// ---------------------------------------------------------------------------
void PawnSystem::DrawEntities(Camera3D& camera) {
    // Player start billboards
    for (auto& n : m_playerStarts) {
        EngineBillboard::Draw(camera, "PlayerStart",
            {n.position.x, n.position.y + 0.5f, n.position.z}, 1.2f);
    }

    // Pickup billboards with bobbing
    for (auto& n : m_pickups) {
        if (!n.active) continue;
        EngineBillboard::DrawPickup(camera, n.typeName.c_str(), n.position, 0.8f);
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
        EngineBillboard::Draw(camera, icon, center, 1.0f);
    }

    // Sound / music emitter billboards
    for (auto& n : m_emitters) {
        const char* icon = (n.type == EmitterType::SOUND) ? "Sound" : "Music";
        EngineBillboard::Draw(camera, icon, {n.position.x, n.position.y + 0.5f, n.position.z}, 1.0f);
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
}

PawnSystem::PatrolState& PawnSystem::PState(Pawn& p) {
    static std::unordered_map<uint32_t, PatrolState> states;
    return states[p.id];
}

