#include "LightningEntityManager.hpp"
#include "LightningEntityRegistry.hpp"
#include "../Package/PackageAssetLoader.hpp"
#include "../Log.hpp"
#ifndef OMEGA_TEST_ENV
#include "../Pawn/OzPawnSystem.hpp"
#endif
#include <algorithm>
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// FireSelectedWeapon — spawn projectiles (ranged) or swing-check (melee)
// Returns: >0 projectiles spawned (ranged), 0 melee swing, -1 didn't fire
// ---------------------------------------------------------------------------
int LightningEntityManager::FireSelectedWeapon(const Vector3& origin, const Vector3& direction) {
    EntityInstance* ent = SelectedEntity();
    if (!ent || !ent->def) return -1;
    if (ent->def->type != EntityType::WEAPON) return -1;

    // Check cooldown
    if (ent->cooldownRemaining > 0.0f) return -1;

    auto readStat = [&](const std::string& key, float defVal) -> float {
        auto rit = ent->runtimeStats.find(key);
        if (rit != ent->runtimeStats.end()) return rit->second;
        auto dit = ent->def->stats.floats.find(key);
        return (dit != ent->def->stats.floats.end()) ? dit->second : defVal;
    };

    float projectileDamage = readStat("damage", 10.0f);
    float reach = readStat("reach", 0.0f);

    if (reach > 0.0f) {
        // ---- MELEE ----
        float swingSpeed = readStat("swing_speed", readStat("fire_rate", 0.25f));
        ent->cooldownRemaining = swingSpeed;

        // Run on_swing action
        ent->ctx.RunAction("on_swing", 30);

        // Honor script-set cooldown override (set_cooldown N)
        float scriptCd = ent->ctx.GetFloat("__cooldown", 0.0f);
        if (scriptCd > 0.0f) ent->cooldownRemaining = scriptCd;

#ifndef OMEGA_TEST_ENV
        // Forward range check against PawnSystem NPCs (single-player)
        const auto& pawns = PawnSystem::Instance().GetPawns();
        for (const auto& pawn : pawns) {
            if (!pawn.active) continue;
            Vector3 toPawn = Vector3Subtract(pawn.position, origin);
            float t = Vector3DotProduct(toPawn, direction);
            if (t < 0 || t > reach) continue;
            Vector3 closest = Vector3Add(origin, Vector3Scale(direction, t));
            float d = Vector3Distance(closest, pawn.position);
            if (d < 2.0f) {
                ent->ctx.RunAction("on_hit", 30);
                break;
            }
        }
#endif
        return 0; // melee swing performed
    }

    // ---- RANGED ----
    float projectileSpeed = readStat("projectile_speed", 20.0f);
    float lifetime = readStat("lifetime", 2.0f);
    int projectileCount = (int)readStat("projectile_count", 1.0f);
    float spreadDeg = readStat("spread", 0.0f);
    float fireRate = readStat("fire_rate", 0.25f);

    // Ammo check
    float magazine = readStat("magazine", 0.0f);
    if (magazine > 0.0f) {
        // Initialize ammo on first fire
        auto ammoIt = ent->runtimeStats.find("ammo");
        if (ammoIt == ent->runtimeStats.end()) {
            ent->runtimeStats["ammo"] = magazine;
            ammoIt = ent->runtimeStats.find("ammo");
        }
        if (ammoIt->second <= 0.0f) {
            // Out of ammo — auto-reload
            float reloadTime = readStat("reload_time", 2.0f);
            ent->cooldownRemaining = reloadTime;
            ent->runtimeStats["ammo"] = magazine;
            ent->ctx.RunAction("on_reload", 30);
            float scriptCd = ent->ctx.GetFloat("__cooldown", 0.0f);
            if (scriptCd > 0.0f) ent->cooldownRemaining = scriptCd;
            return -1;
        }
        ammoIt->second -= 1.0f;
    }

    ent->cooldownRemaining = fireRate;

    // Trigger on_fire script action if defined
    ent->ctx.RunAction("on_fire", 30);

    // Honor script-set cooldown override (set_cooldown N)
    float scriptCd = ent->ctx.GetFloat("__cooldown", 0.0f);
    if (scriptCd > 0.0f) ent->cooldownRemaining = scriptCd;

#ifndef OMEGA_TEST_ENV
    for (int i = 0; i < projectileCount; i++) {
        float spreadRad = spreadDeg * DEG2RAD;
        float angleOffset = (i - (projectileCount - 1) * 0.5f) * spreadRad;
        float yawOffset = (float)(rand() % 1000 - 500) / 500.0f * spreadRad * 0.5f;

        Vector3 dir = direction;
        float cosA = cosf(angleOffset);
        float sinA = sinf(angleOffset);
        Vector3 up = {0, 1, 0};
        Vector3 right = Vector3CrossProduct(dir, up);
        right = Vector3Normalize(right);
        dir = Vector3Normalize(Vector3Add(dir, Vector3Scale(right, sinA)));

        dir.y += yawOffset * 0.3f;
        dir = Vector3Normalize(dir);

        ProjectileNode p;
        p.position = origin;
        p.velocity = Vector3Scale(dir, projectileSpeed);
        p.damage = projectileDamage;
        p.lifetime = lifetime;
        p.speed = projectileSpeed;
        p.ownerId = -1;
        PawnSystem::Instance().SpawnProjectile(p);
    }
#endif
    return projectileCount;
}

// ---------------------------------------------------------------------------
// ReloadSelectedWeapon — manually reload the selected weapon
// Returns: true if reload was performed
// ---------------------------------------------------------------------------
bool LightningEntityManager::ReloadSelectedWeapon() {
    EntityInstance* ent = SelectedEntity();
    if (!ent || !ent->def) return false;
    if (ent->def->type != EntityType::WEAPON) return false;

    float magazine = 0.0f;
    {
        auto rit = ent->runtimeStats.find("magazine");
        auto dit = ent->def->stats.floats.find("magazine");
        magazine = (rit != ent->runtimeStats.end()) ? rit->second
                 : (dit != ent->def->stats.floats.end()) ? dit->second : 0.0f;
    }
    if (magazine <= 0.0f) return false; // no magazine stat = no reload needed

    float currentAmmo = 0.0f;
    auto ammoIt = ent->runtimeStats.find("ammo");
    if (ammoIt != ent->runtimeStats.end()) currentAmmo = ammoIt->second;
    if (currentAmmo >= magazine) return false; // already full

    float reloadTime = 0.0f;
    {
        auto rit = ent->runtimeStats.find("reload_time");
        auto dit = ent->def->stats.floats.find("reload_time");
        reloadTime = (rit != ent->runtimeStats.end()) ? rit->second
                   : (dit != ent->def->stats.floats.end()) ? dit->second : 2.0f;
    }

    ent->cooldownRemaining = reloadTime;
    ent->runtimeStats["ammo"] = magazine;

    ent->ctx.RunAction("on_reload", 30);
    float scriptCd = ent->ctx.GetFloat("__cooldown", 0.0f);
    if (scriptCd > 0.0f) ent->cooldownRemaining = scriptCd;
    return true;
}

// ---------------------------------------------------------------------------
// Init — called at startup after registry is populated
// ---------------------------------------------------------------------------
LightningEntityManager::LightningEntityManager() {
    for (int i = 0; i < HOTBAR_SIZE; i++) m_hotbar[i] = -1;
    for (int i = 0; i < EQUIP_SLOT_COUNT; i++) m_equipment[i] = -1;
    m_selectedSlot = 0;
    m_playerEntityIndex = -1;
}

void LightningEntityManager::Init() {
    for (int i = 0; i < HOTBAR_SIZE; i++) m_hotbar[i] = -1;
    for (int i = 0; i < EQUIP_SLOT_COUNT; i++) m_equipment[i] = -1;
    m_selectedSlot = 0;
    m_playerEntityIndex = -1;
    m_instances.clear();
    m_resources.clear();
    m_soundCache.clear();
    m_pendingFog = false;
    m_pendingSkybox.clear();
    m_pendingAmbient = false;
    m_pendingMessage.clear();
    m_playerHurt = false;

    // Auto-spawn player entity
    int playerIdx = Spawn("Player");
    if (playerIdx >= 0) {
        m_playerEntityIndex = playerIdx;
        EntityInstance* p = Get(playerIdx);
        if (p && p->def) {
            // Populate runtimeStats from EntityDef defaults
            p->runtimeStats["health"] = p->def->defaultHealth;
            p->runtimeStats["max_health"] = p->def->defaultMaxHealth;
            p->runtimeStats["mana"] = p->def->defaultMana;
            p->runtimeStats["max_mana"] = p->def->defaultMaxMana;
            p->runtimeStats["psychic_energy"] = p->def->defaultPsychicEnergy;
            p->runtimeStats["max_psychic_energy"] = p->def->defaultMaxPsychicEnergy;
            p->runtimeStats["level"] = (float)p->def->defaultLevel;
            p->runtimeStats["xp"] = (float)p->def->defaultXP;
            p->runtimeStats["xp_to_next"] = (float)p->def->defaultXPToNext;
        }
        // Bind stat resolver so scripts can read $health/$mana/etc.
        auto& playerCtx = m_instances[m_playerEntityIndex].ctx;
        playerCtx.SetStatResolver([this](const std::string& name) -> float {
            return ResolveScriptStat(name);
        });
        OZ_INFO("LightningEntityManager: player entity spawned at idx %d", playerIdx);
    } else {
        OZ_WARN("LightningEntityManager: could not spawn player entity");
    }

    OZ_INFO("LightningEntityManager: ready, registry has %d defs",
            LightningEntityRegistry::Instance().Count());
}

// ---------------------------------------------------------------------------
// Update — tick cooldowns and active instance scripts
// ---------------------------------------------------------------------------
void LightningEntityManager::Update(float dt) {
    for (auto& inst : m_instances) {
        if (inst.def == nullptr) continue;
        if (inst.cooldownRemaining > 0) inst.cooldownRemaining -= dt;

        // Per-frame on_tick hook (only for defs that define it)
        if (inst.ctx.FindJumpLabel("on_tick") >= 0) {
            inst.ctx.RunAction("on_tick", 30);
            ApplyEntityScriptEffects(inst);
        }

        // Tick instance script context for active behaviors
        if (inst.ctx.HasMore()) {
            // Run up to 10 instructions per frame to avoid stalls
            for (int step = 0; step < 10 && inst.ctx.HasMore(); step++)
                inst.ctx.ExecuteNext();

            ApplyEntityScriptEffects(inst);
        }
    }

    // Clean up finished one-shot sounds
    PruneSoundCache();
}

// ---------------------------------------------------------------------------
// ApplyEntityScriptEffects — drain one instance's queued side-effects into the
// host-facing pending state (sound/fog/skybox/ambient/msg/stat ops/pickup spawn)
// ---------------------------------------------------------------------------
void LightningEntityManager::ApplyEntityScriptEffects(EntityInstance& inst) {
    if (!inst.def) return;

    std::string sound = inst.ctx.PopPendingSound();
    if (!sound.empty()) {
        if (CacheSound(sound) >= 0) {
            auto it = m_soundCache.find(sound);
            if (it != m_soundCache.end() && it->second.sound.frameCount > 0)
                PlaySound(it->second.sound);
        }
    }

    float fr=0, fg=0, fb=0, fd=0;
    if (inst.ctx.PopPendingFog(fr, fg, fb, fd)) {
        m_pendingFog = true;
        m_fogR = fr; m_fogG = fg; m_fogB = fb; m_fogDensity = fd;
    }

    std::string sky = inst.ctx.PopPendingSkybox();
    if (!sky.empty()) {
        m_pendingSkybox = sky;
    }

    float ar=0, ag=0, ab=0;
    if (inst.ctx.PopPendingAmbient(ar, ag, ab)) {
        m_pendingAmbient = true;
        m_ambientR = ar; m_ambientG = ag; m_ambientB = ab;
    }

    // HUD message (msg opcode)
    std::string msg = inst.ctx.PopPendingMessage();
    if (!msg.empty()) m_pendingMessage = msg;

    // Deferred player stat writes (heal/damage/playerstat opcodes)
    auto statOps = inst.ctx.PopPlayerStatOps();
    if (!statOps.empty()) ApplyPlayerStatOps(statOps);

    // Scripted damage flash
    if (inst.ctx.PopPendingHurt()) m_playerHurt = true;

#ifndef OMEGA_TEST_ENV
    // Process pending pawn spawn from spawn_pawn opcode
    auto pawnReq = inst.ctx.PopPendingPawnSpawn();
    if (pawnReq.valid && !pawnReq.name.empty()) {
        PawnSystem::Instance().Spawn(
            {pawnReq.x, pawnReq.y, pawnReq.z},
            pawnReq.name.c_str());
    }

    // Process pending pickup spawn from spawn_pickup opcode
    auto pickupReq = inst.ctx.PopPendingPickupSpawn();
    if (pickupReq.valid && !pickupReq.name.empty()) {
        PickupNode pn;
        pn.position = {pickupReq.x, pickupReq.y, pickupReq.z};
        pn.typeName = pickupReq.name;
        pn.respawnTime = pickupReq.respawnTime;
        pn.active = true;
        PawnSystem::Instance().AddPickup(pn);
    }
#endif
}

// ---------------------------------------------------------------------------
// Spawn — create a new instance from a registered entity definition
// ---------------------------------------------------------------------------
int LightningEntityManager::Spawn(const std::string& defName) {
    const EntityDef* def = LightningEntityRegistry::Instance().Find(defName);
    if (!def) {
        OZ_WARN("LightningEntityManager: unknown entity '%s'", defName.c_str());
        return -1;
    }
    return Spawn(def);
}

// Spawn — def-based variant (used when the caller already resolved the def,
// e.g. world-scoped zone defs that must not be re-looked-up by name)
int LightningEntityManager::Spawn(const EntityDef* def) {
    if (!def) return -1;

    if ((int)m_instances.size() >= MAX_ENTITIES) {
        OZ_WARN("LightningEntityManager: max entities (%d) reached", MAX_ENTITIES);
        return -1;
    }

    EntityInstance inst;
    inst.def = def;

    // Load resources
    if (!def->mesh.empty()) inst.modelIdx = CacheModel(def->mesh);
    if (!def->texture.empty()) inst.textureIdx = CacheTexture(def->texture);
    if (!def->icon.empty()) inst.iconIdx = CacheTexture(def->icon);

    // Initialize runtime stats from def
    for (auto& [k, v] : def->stats.floats)
        inst.runtimeStats[k] = v;

    // Initialize ammo for weapons
    if (def->type == EntityType::WEAPON) {
        auto magIt = def->stats.floats.find("magazine");
        if (magIt != def->stats.floats.end()) {
            inst.runtimeStats["ammo"] = magIt->second;
        }
    }

    // Load instance script from action blocks
    // We build a script that can be triggered by action name
    // For now, prep all action blocks as labels
    std::string scriptText;
    for (auto& action : def->actions) {
        scriptText += action.name + ":\n";
        for (auto& line : action.scriptLines)
            scriptText += line + "\n";
    }
    inst.ctx.Load(scriptText);
    char tag[128];
    snprintf(tag, sizeof(tag), "%s:%d", def->name.c_str(), (int)m_instances.size());
    inst.ctx.SetDebugTag(tag);

    inst.owned = true;
    inst.variantIndex = 0;
    m_instances.push_back(std::move(inst));

    int idx = (int)m_instances.size() - 1;
    OZ_INFO("LightningEntityManager: spawned '%s' at index %d", def->name.c_str(), idx);
    return idx;
}

// ---------------------------------------------------------------------------
// Despawn — remove an instance by index
// ---------------------------------------------------------------------------
void LightningEntityManager::Despawn(int index) {
    if (index < 0 || index >= (int)m_instances.size()) return;
    auto& inst = m_instances[index];

    // Free resources
    if (inst.modelIdx >= 0) UncacheResource(inst.modelIdx);
    if (inst.textureIdx >= 0) UncacheResource(inst.textureIdx);
    if (inst.iconIdx >= 0) UncacheResource(inst.iconIdx);

    // Remove from hotbar if present
    for (int s = 0; s < HOTBAR_SIZE; s++) {
        if (m_hotbar[s] == index) m_hotbar[s] = -1;
    }
    // Remove from equipment slots if present
    for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
        if (m_equipment[s] == index) m_equipment[s] = -1;
    }

    // Swap with last to keep array compact
    if (index < (int)m_instances.size() - 1) {
        m_instances[index] = std::move(m_instances.back());
        // Update hotbar references
        for (int s = 0; s < HOTBAR_SIZE; s++) {
            if (m_hotbar[s] == (int)m_instances.size() - 1) m_hotbar[s] = index;
        }
        // Update equipment references
        for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
            if (m_equipment[s] == (int)m_instances.size() - 1) m_equipment[s] = index;
        }
    }
    m_instances.pop_back();
}

EntityInstance* LightningEntityManager::Get(int index) {
    if (index < 0 || index >= (int)m_instances.size()) return nullptr;
    return &m_instances[index];
}

// ---------------------------------------------------------------------------
// RunAction — execute a named action label on an instance
// ---------------------------------------------------------------------------
void LightningEntityManager::RunAction(EntityInstance* inst, const std::string& actionName) {
    if (!inst || !inst->def) return;
    inst->ctx.RunAction(actionName, 30);
}

// ---------------------------------------------------------------------------
// Hotbar
// ---------------------------------------------------------------------------
void LightningEntityManager::HotbarAssign(int slot, int instanceIndex) {
    if (slot < 0 || slot >= HOTBAR_SIZE) return;
    int oldIdx = m_hotbar[slot];
    if (oldIdx >= 0 && oldIdx < (int)m_instances.size())
        RunAction(&m_instances[oldIdx], "on_unequip");
    m_hotbar[slot] = instanceIndex;
    if (instanceIndex >= 0 && instanceIndex < (int)m_instances.size())
        RunAction(&m_instances[instanceIndex], "on_equip");
}

void LightningEntityManager::HotbarSwap(int slotA, int slotB) {
    if (slotA < 0 || slotA >= HOTBAR_SIZE || slotB < 0 || slotB >= HOTBAR_SIZE) return;
    std::swap(m_hotbar[slotA], m_hotbar[slotB]);
}

int LightningEntityManager::HotbarAt(int slot) const {
    if (slot < 0 || slot >= HOTBAR_SIZE) return -1;
    return m_hotbar[slot];
}

void LightningEntityManager::SelectSlot(int slot) {
    if (slot < 0 || slot >= HOTBAR_SIZE) return;
    int oldIdx = m_hotbar[m_selectedSlot];
    if (oldIdx >= 0 && oldIdx < (int)m_instances.size())
        RunAction(&m_instances[oldIdx], "on_unequip");
    m_selectedSlot = slot;
    int newIdx = m_hotbar[slot];
    if (newIdx >= 0 && newIdx < (int)m_instances.size())
        RunAction(&m_instances[newIdx], "on_equip");
}

EntityInstance* LightningEntityManager::SelectedEntity() const {
    int idx = m_hotbar[m_selectedSlot];
    if (idx < 0 || idx >= (int)m_instances.size()) return nullptr;
    return const_cast<EntityInstance*>(&m_instances[idx]);
}

// ---------------------------------------------------------------------------
// Resource caching
// ---------------------------------------------------------------------------
int LightningEntityManager::CacheModel(const std::string& path) {
#ifndef OMEGA_TEST_ENV
    Model model = LoadModelWithFallback(path.c_str());
#else
    Model model = LoadModel(path.c_str());
#endif
    if (model.meshes == nullptr) return -1;
    CachedResource res;
    res.type = 1;
    res.model = model;
    m_resources.push_back(res);
    return (int)m_resources.size() - 1;
}

int LightningEntityManager::CacheTexture(const std::string& path) {
#ifndef OMEGA_TEST_ENV
    Texture2D tex = LoadTextureWithFallback(path.c_str());
#else
    Texture2D tex = LoadTexture(path.c_str());
#endif
    if (tex.id == 0) return -1;
    CachedResource res;
    res.type = 2;
    res.texture = tex;
    m_resources.push_back(res);
    return (int)m_resources.size() - 1;
}

int LightningEntityManager::CacheSound(const std::string& path) {
    auto it = m_soundCache.find(path);
    if (it != m_soundCache.end()) {
        // Reset timer so it isn't pruned
        it->second.timer = 0.0f;
        return 0; // arbitrary non-negative = success
    }
    Sound snd = LoadSound(path.c_str());
    if (snd.frameCount == 0) return -1;
    m_soundCache[path] = { snd, 0.0f };
    return 0;
}

void LightningEntityManager::PruneSoundCache() {
    auto it = m_soundCache.begin();
    while (it != m_soundCache.end()) {
        it->second.timer += GetFrameTime();
        // Unload after 5 seconds past playback
        if (it->second.timer > 5.0f) {
            UnloadSound(it->second.sound);
            it = m_soundCache.erase(it);
        } else {
            ++it;
        }
    }
}

void* LightningEntityManager::GetModel(int idx) const {
    if (idx < 0 || idx >= (int)m_resources.size() || m_resources[idx].type != 1) return nullptr;
    return (void*)&m_resources[idx].model;
}

void* LightningEntityManager::GetTexture(int idx) const {
    if (idx < 0 || idx >= (int)m_resources.size() || m_resources[idx].type != 2) return nullptr;
    return (void*)&m_resources[idx].texture;
}

void* LightningEntityManager::GetIcon(int idx) const {
    return GetTexture(idx);
}

int LightningEntityManager::PrecacheModelForDef(const std::string& defName) {
    const EntityDef* def = LightningEntityRegistry::Instance().Find(defName);
    if (!def || def->mesh.empty()) return -1;
    return CacheModel(def->mesh);
}

Model* LightningEntityManager::GetModelByResourceIdx(int idx) const {
    if (idx < 0 || idx >= (int)m_resources.size() || m_resources[idx].type != 1) return nullptr;
    return const_cast<Model*>(&m_resources[idx].model);
}

void LightningEntityManager::UncacheResource(int idx) {
    if (idx < 0 || idx >= (int)m_resources.size()) return;
    if (m_resources[idx].type == 1) UnloadModel(m_resources[idx].model);
    else if (m_resources[idx].type == 2) UnloadTexture(m_resources[idx].texture);
    m_resources[idx].type = 0; // Mark as unused instead of erasing (avoids index shift)
    m_resources[idx].model = Model{0};
    m_resources[idx].texture = Texture2D{0};
}

void LightningEntityManager::UnloadAllResources() {
    for (int i = (int)m_resources.size() - 1; i >= 0; i--)
        UncacheResource(i);
}

// ---------------------------------------------------------------------------
// HandleInput — keyboard 1-8 for hotbar
// ---------------------------------------------------------------------------
void LightningEntityManager::HandleInput() {
    if (IsKeyPressed(KEY_ONE))   SelectSlot(0);
    if (IsKeyPressed(KEY_TWO))   SelectSlot(1);
    if (IsKeyPressed(KEY_THREE)) SelectSlot(2);
    if (IsKeyPressed(KEY_FOUR))  SelectSlot(3);
    if (IsKeyPressed(KEY_FIVE))  SelectSlot(4);
    if (IsKeyPressed(KEY_SIX))   SelectSlot(5);
    if (IsKeyPressed(KEY_SEVEN)) SelectSlot(6);
    if (IsKeyPressed(KEY_EIGHT)) SelectSlot(7);

    // R to reload selected weapon
    if (IsKeyPressed(KEY_R)) {
        ReloadSelectedWeapon();
    }

    // Enter/E to use selected item
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_E)) {
        EntityInstance* sel = SelectedEntity();
        if (sel && sel->def) {
            // Trigger on_use action
            sel->ctx.RunAction("on_use", 30);

            // Apply side-effects immediately so consumables feel instant
            ApplyEntityScriptEffects(*sel);

            // 'consume' removes the item from the hotbar after use
            if (sel->ctx.ConsumeRequested()) {
                sel->ctx.ClearConsume();
                int idx = m_hotbar[m_selectedSlot];
                RunAction(sel, "on_unequip");
                m_hotbar[m_selectedSlot] = -1;
                Despawn(idx);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// DrawHotbar — render 8 hotbar slots (ported from Objects.hpp::DrawHotbarSlot)
// ---------------------------------------------------------------------------
void LightningEntityManager::DrawHotbar() {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int slotSize = 50;
    int margin = 4;
    int totalWidth = HOTBAR_SIZE * (slotSize + margin) - margin;
    int startX = (sw - totalWidth) / 2;
    int y = sh - slotSize - 20;

    for (int s = 0; s < HOTBAR_SIZE; s++) {
        int x = startX + s * (slotSize + margin);
        Color bg = (s == m_selectedSlot) ? (Color){60, 60, 120, 200} : (Color){30, 30, 30, 180};
        DrawRectangle(x, y, slotSize, slotSize, bg);
        DrawRectangleLines(x, y, slotSize, slotSize, (s == m_selectedSlot) ? WHITE : GRAY);

        int idx = m_hotbar[s];
        if (idx >= 0 && idx < (int)m_instances.size()) {
            EntityInstance& inst = m_instances[idx];
            if (inst.def) {
                // Draw icon if available
                if (inst.iconIdx >= 0 && inst.iconIdx < (int)m_resources.size() && m_resources[inst.iconIdx].type == 2) {
                    Texture2D& icon = m_resources[inst.iconIdx].texture;
                    if (icon.id > 0) {
                        float iconSize = slotSize - 8;
                        DrawTexturePro(icon,
                            (Rectangle){0,0,(float)icon.width,(float)icon.height},
                            (Rectangle){(float)x+4, (float)y+4, iconSize, iconSize},
                            (Vector2){0,0}, 0, WHITE);
                    }
                }
                // Draw name
                DrawText(inst.def->name.c_str(), x + 2, y + slotSize - 14, 10, LIGHTGRAY);
            }
        }

        // Draw slot number
        char num[2] = {(char)('1' + s), '\0'};
        DrawText(num, x + 2, y + 2, 12, (s == m_selectedSlot) ? WHITE : DARKGRAY);
    }
}

// ---------------------------------------------------------------------------
// TriggerZoneAction — called from PawnSystem when player enters/exits a zone
// ---------------------------------------------------------------------------
void LightningEntityManager::TriggerZoneAction(const std::string& zoneName,
                                                const std::string& actionName) {
    const EntityDef* def = LightningEntityRegistry::Instance().Find(zoneName);
    if (!def) return;
    TriggerZoneAction(def, actionName);
}

// TriggerZoneAction — def-based variant (world-scoped defs resolved at zone
// creation avoid the global registry's cross-world name collisions)
void LightningEntityManager::TriggerZoneAction(const EntityDef* def,
                                               const std::string& actionName) {
    if (!def) return;
    // Only allow zone-type entities to trigger via TriggerZoneAction
    if (def->type != EntityType::SKYZONE) return;
    TriggerEntityAction(def, actionName);
}

// ---------------------------------------------------------------------------
// TriggerEntityAction — run a named action on an instance of the given def
// (creates a persistent instance on first use, like zone scripts)
// ---------------------------------------------------------------------------
void LightningEntityManager::TriggerEntityAction(const EntityDef* def,
                                                 const std::string& actionName) {
    if (!def) return;

    // Skip if the def doesn't define this action (no instance needed)
    bool hasAction = false;
    for (const auto& a : def->actions) {
        if (a.name == actionName) { hasAction = true; break; }
    }
    if (!hasAction) return;

    // Find existing instance or create one
    int instIdx = -1;
    for (int i = 0; i < (int)m_instances.size(); i++) {
        if (m_instances[i].def == def) { instIdx = i; break; }
    }
    if (instIdx < 0) {
        instIdx = Spawn(def);
    }
    if (instIdx < 0) return;

    EntityInstance* inst = Get(instIdx);
    if (!inst) return;

    // Jump to the action label and execute (stops at the next action label)
    inst->ctx.RunAction(actionName, 50);
    ApplyEntityScriptEffects(*inst);
}

// ---------------------------------------------------------------------------
// TriggerCollectAction — fire a pickup's on_collect script when collected
// ---------------------------------------------------------------------------
void LightningEntityManager::TriggerCollectAction(const std::string& defName) {
    const EntityDef* def = LightningEntityRegistry::Instance().Find(defName);
    if (!def) return;
    if (def->type == EntityType::SKYZONE) return;
    TriggerEntityAction(def, "on_collect");
}

// ---------------------------------------------------------------------------
// Equipment slots
// ---------------------------------------------------------------------------
int LightningEntityManager::EquipmentAt(int slot) const {
    if (slot < 0 || slot >= EQUIP_SLOT_COUNT) return -1;
    return m_equipment[slot];
}

void LightningEntityManager::EquipmentAssign(int slot, int instanceIndex) {
    if (slot < 0 || slot >= EQUIP_SLOT_COUNT) return;
    // Fire on_unequip on old occupant, then despawn
    if (m_equipment[slot] >= 0) {
        int oldIdx = m_equipment[slot];
        if (oldIdx >= 0 && oldIdx < (int)m_instances.size())
            RunAction(&m_instances[oldIdx], "on_unequip");
        Despawn(oldIdx);
    }
    m_equipment[slot] = instanceIndex;
    if (instanceIndex >= 0 && instanceIndex < (int)m_instances.size())
        RunAction(&m_instances[instanceIndex], "on_equip");
}

void LightningEntityManager::EquipmentUnequip(int slot) {
    if (slot < 0 || slot >= EQUIP_SLOT_COUNT) return;
    if (m_equipment[slot] >= 0) {
        Despawn(m_equipment[slot]);
        m_equipment[slot] = -1;
    }
}

void LightningEntityManager::EquipmentClear() {
    for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
        if (m_equipment[s] >= 0) {
            Despawn(m_equipment[s]);
            m_equipment[s] = -1;
        }
    }
}

int LightningEntityManager::EquipmentFindFreeSlot() const {
    for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
        if (m_equipment[s] < 0) return s;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Player stat accessors
// ---------------------------------------------------------------------------
float LightningEntityManager::GetPlayerStat(const std::string& name, float defaultVal) const {
    if (m_playerEntityIndex < 0 || m_playerEntityIndex >= (int)m_instances.size())
        return defaultVal;
    auto it = m_instances[m_playerEntityIndex].runtimeStats.find(name);
    return (it != m_instances[m_playerEntityIndex].runtimeStats.end()) ? it->second : defaultVal;
}

void LightningEntityManager::SetPlayerStat(const std::string& name, float val) {
    if (m_playerEntityIndex >= 0 && m_playerEntityIndex < (int)m_instances.size())
        m_instances[m_playerEntityIndex].runtimeStats[name] = val;
}

float LightningEntityManager::GetPlayerHealth() const { return GetPlayerStat("health", 100.0f); }
float LightningEntityManager::GetPlayerMaxHealth() const { return GetPlayerStat("max_health", 100.0f); }
void LightningEntityManager::SetPlayerHealth(float v) { SetPlayerStat("health", v); }

float LightningEntityManager::GetPlayerMana() const { return GetPlayerStat("mana", 0.0f); }
float LightningEntityManager::GetPlayerMaxMana() const { return GetPlayerStat("max_mana", 100.0f); }
void LightningEntityManager::SetPlayerMana(float v) { SetPlayerStat("mana", v); }

float LightningEntityManager::GetPlayerPsychicEnergy() const { return GetPlayerStat("psychic_energy", 0.0f); }
float LightningEntityManager::GetPlayerMaxPsychicEnergy() const { return GetPlayerStat("max_psychic_energy", 100.0f); }
void LightningEntityManager::SetPlayerPsychicEnergy(float v) { SetPlayerStat("psychic_energy", v); }

int LightningEntityManager::GetPlayerLevel() const { return (int)GetPlayerStat("level", 1.0f); }
void LightningEntityManager::SetPlayerLevel(int v) { SetPlayerStat("level", (float)v); }

int LightningEntityManager::GetPlayerXP() const { return (int)GetPlayerStat("xp", 0.0f); }
void LightningEntityManager::SetPlayerXP(int v) { SetPlayerStat("xp", (float)v); }

int LightningEntityManager::GetPlayerXPToNext() const { return (int)GetPlayerStat("xp_to_next", 100.0f); }
void LightningEntityManager::SetPlayerXPToNext(int v) { SetPlayerStat("xp_to_next", (float)v); }

// ---------------------------------------------------------------------------
// ResolveScriptStat — external stat provider for script $name tokens.
// Reads the player entity's runtimeStats first, then falls back to the
// selected weapon/entity instance's runtimeStats (ammo etc.).
// ---------------------------------------------------------------------------
float LightningEntityManager::ResolveScriptStat(const std::string& name) const {
    if (m_playerEntityIndex >= 0 && m_playerEntityIndex < (int)m_instances.size()) {
        auto& rs = m_instances[m_playerEntityIndex].runtimeStats;
        auto it = rs.find(name);
        if (it != rs.end()) return it->second;
    }
    EntityInstance* sel = SelectedEntity();
    if (sel && sel->def) {
        auto it = sel->runtimeStats.find(name);
        if (it != sel->runtimeStats.end()) return it->second;
        auto dit = sel->def->stats.floats.find(name);
        if (dit != sel->def->stats.floats.end()) return dit->second;
    }
    return 0.0f;
}

// ---------------------------------------------------------------------------
// ApplyPlayerStatOps — apply deferred script stat writes to the player entity
// ---------------------------------------------------------------------------
void LightningEntityManager::ApplyPlayerStatOps(
    const std::vector<LightningScriptContext::PlayerStatOp>& ops) {
    for (const auto& op : ops) {
        float cur = GetPlayerStat(op.name, 0.0f);
        float nv = cur;
        switch (op.op) {
            case 0: nv = op.value; break;
            case 1: nv = cur + op.value; break;
            case 2: nv = cur - op.value; break;
            case 3: nv = cur * op.value; break;
            case 4: if (op.value != 0.0f) nv = cur / op.value; break;
            default: break;
        }
        // Clamp the resource pools to sane ranges
        if (op.name == "health") {
            float maxh = GetPlayerStat("max_health", 100.0f);
            nv = std::max(0.0f, std::min(nv, maxh));
        } else if (op.name == "mana") {
            float maxm = GetPlayerStat("max_mana", 100.0f);
            nv = std::max(0.0f, std::min(nv, maxm));
        } else if (op.name == "psychic_energy") {
            float maxp = GetPlayerStat("max_psychic_energy", 100.0f);
            nv = std::max(0.0f, std::min(nv, maxp));
        }
        SetPlayerStat(op.name, nv);
    }
}

// ---------------------------------------------------------------------------
// Serialization — compact hotbar + equipment state for TF.sav
// Format: "hotbar:def1,def2,...|equip:def1,,,,...|"
// Empty slots are empty strings (consecutive commas).
// ---------------------------------------------------------------------------
std::string LightningEntityManager::SerializeState() const {
    std::string result;
    // Hotbar
    result += "hotbar:";
    for (int s = 0; s < HOTBAR_SIZE; s++) {
        if (s > 0) result += ",";
        int idx = m_hotbar[s];
        if (idx >= 0 && idx < (int)m_instances.size() && m_instances[idx].def)
            result += m_instances[idx].def->name;
    }
    result += "|equip:";
    for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
        if (s > 0) result += ",";
        int idx = m_equipment[s];
        if (idx >= 0 && idx < (int)m_instances.size() && m_instances[idx].def)
            result += m_instances[idx].def->name;
    }
    result += "|";
    return result;
}

bool LightningEntityManager::DeserializeState(const std::string& data) {
    // Parse "hotbar:def1,def2,...|equip:def1,def2,...|"
    // First clear existing
    for (int s = 0; s < HOTBAR_SIZE; s++) m_hotbar[s] = -1;
    for (int s = 0; s < EQUIP_SLOT_COUNT; s++) m_equipment[s] = -1;

    auto parseSection = [&](const std::string& prefix, int* arr, int arrSize) {
        size_t p = data.find(prefix);
        if (p == std::string::npos) return;
        p += prefix.size();
        size_t end = data.find('|', p);
        if (end == std::string::npos) end = data.size();
        std::string section = data.substr(p, end - p);
        size_t start = 0;
        for (int s = 0; s < arrSize; s++) {
            size_t comma = section.find(',', start);
            std::string name = (comma == std::string::npos)
                ? section.substr(start)
                : section.substr(start, comma - start);
            // Trim whitespace
            while (!name.empty() && (name[0] == ' ' || name[0] == '\t')) name.erase(0, 1);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
            if (!name.empty()) {
                int idx = Spawn(name);
                if (idx >= 0) arr[s] = idx;
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
    };

    parseSection("hotbar:", m_hotbar, HOTBAR_SIZE);
    parseSection("equip:", m_equipment, EQUIP_SLOT_COUNT);
    return true;
}
