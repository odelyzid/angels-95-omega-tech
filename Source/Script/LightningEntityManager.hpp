#pragma once
#include "LightningEntityDef.hpp"
#include "LightningScriptContext.hpp"
#include "raylib.h"
#include <string>
#include <vector>
#include <unordered_map>

// A runtime instance of an entity definition
struct EntityInstance {
    const EntityDef* def = nullptr;
    LightningScriptContext ctx;
    int modelIdx = -1;       // slot in manager's model cache
    int textureIdx = -1;
    int iconIdx = -1;
    bool owned = false;
    int variantIndex = 0;
    std::unordered_map<std::string, float> runtimeStats;
    float cooldownRemaining = 0.0f;
};

// LightningEntityManager — runtime entity instances + dynamic hotbar + equipment
// Replaces Objects.hpp with a fully dynamic 512-entity / 8-slot system
class LightningEntityManager {
public:
    static constexpr int MAX_ENTITIES = 512;
    static constexpr int HOTBAR_SIZE = 8;
    static constexpr int EQUIP_SLOT_COUNT = 8;

    static LightningEntityManager& Instance() {
        static LightningEntityManager instance;
        return instance;
    }

    void Init();
    void Update(float dt);

    // --- Lifecycle ---
    int Spawn(const std::string& defName);
    int Spawn(const EntityDef* def);
    void Despawn(int index);
    EntityInstance* Get(int index);
    int Count() const { return (int)m_instances.size(); }

    // --- Hotbar ---
    void HotbarAssign(int slot, int instanceIndex);
    void HotbarSwap(int slotA, int slotB);
    int  HotbarAt(int slot) const;
    // First slot holding no instance, or -1 when the bar is full. Single source
    // of truth for "where does a newly collected item go" — the walk-over, the
    // collect reply and the /summon console command each had their own copy,
    // and only one of them reported a full bar.
    int  HotbarFirstFreeSlot() const;
    void SelectSlot(int slot);
    // Move `dir` slots from `from`, skipping empty ones and wrapping. If every
    // slot is empty the selection is left where it was.
    void SelectSlotSkippingEmpty(int from, int dir);
    // Place `instanceIndex` in the first free slot. Returns false (leaving the
    // caller to despawn or refund) when the bar is full.
    bool HotbarPlaceFirstFree(int instanceIndex);
    int  SelectedSlot() const { return m_selectedSlot; }
    EntityInstance* SelectedEntity() const;

    // --- Equipment slots ---
    int  EquipmentAt(int slot) const;           // returns instance index or -1
    void EquipmentAssign(int slot, int instanceIndex);
    void EquipmentUnequip(int slot);            // despawns equipped instance
    void EquipmentClear();                      // despawns all equipped
    int  EquipmentFindFreeSlot() const;         // first empty slot, or -1

    // Map an authored `equip_slot` string (e.g. "helmet") to a slot index, or -1.
    static int EquipmentSlotFromName(const std::string& name);
    // Spawn an armor/upgrade def and assign it to its authored equip_slot.
    // Returns false when the def has no usable equip_slot.
    bool AutoEquip(const EntityDef* def);
    // Summed `defense` of all equipped items (incoming-damage mitigation).
    float GetPlayerDefense() const;

    // Returns the texture/icon pointer from the cache by index. Models go
    // through GetModelByResourceIdx, which returns a typed pointer.
    void* GetTexture(int idx) const;
    void* GetIcon(int idx) const;

    // Pre-load a model for a named entity def and return resource slot index (or -1)
    int PrecacheModelForDef(const std::string& defName);
    // Get model by pre-cached index
    Model* GetModelByResourceIdx(int idx) const;

    // --- Input + rendering ---
    // Hotbar input: number keys 1-8 plus mouse-wheel cycling.
    // `uiBlocking` is the host's "a modal is open" flag (inventory, skill tree,
    // console, screenshot mode). Without it these keys fired underneath menus.
    void HandleInput(bool uiBlocking = false);
    void DrawHotbar();

    // --- Projectile spawning (for weapon entities) ---
    int FireSelectedWeapon(const Vector3& origin, const Vector3& direction);
    bool ReloadSelectedWeapon();

    // Ammo-change hook (fire/reload) for network sync; action: 0=fire, 1=reload.
    // Signatures: (hotbarSlot, ammoInMag, magazineSize, action)
    void set_on_ammo_changed(std::function<void(int, int, int, int)> cb) { m_on_ammo_changed = std::move(cb); }

    // --- Zone actions (called by PawnSystem on zone enter/exit) ---
    void TriggerZoneAction(const std::string& zoneName, const std::string& actionName);
    void TriggerZoneAction(const EntityDef* def, const std::string& actionName);

    // --- Generic entity action (not SKYZONE-restricted; used for on_collect) ---
    void TriggerEntityAction(const EntityDef* def, const std::string& actionName);

    // --- Pickup collect hook (called by PawnSystem::UpdatePickups) ---
    void TriggerCollectAction(const std::string& defName);

    // --- Execute a named action label on an instance ---
    void RunAction(EntityInstance* inst, const std::string& actionName);

    // --- Player entity ---
    bool HasPlayerEntity() const { return m_playerEntityIndex >= 0; }
    int  PlayerEntityIndex() const { return m_playerEntityIndex; }

    // Player stat accessors (read/write to player entity runtimeStats)
    float GetPlayerStat(const std::string& name, float defaultVal = 0.0f) const;
    void  SetPlayerStat(const std::string& name, float val);
    float GetPlayerHealth() const;
    float GetPlayerMaxHealth() const;
    void  SetPlayerHealth(float v);
    float GetPlayerMana() const;
    float GetPlayerMaxMana() const;
    void  SetPlayerMana(float v);
    float GetPlayerPsychicEnergy() const;
    float GetPlayerMaxPsychicEnergy() const;
    void  SetPlayerPsychicEnergy(float v);
    int   GetPlayerLevel() const;
    void  SetPlayerLevel(int v);
    int   GetPlayerXP() const;
    void  SetPlayerXP(int v);
    int   GetPlayerXPToNext() const;
    void  SetPlayerXPToNext(int v);
    // Authored walk-speed multiplier from the Player entity def (`movement_speed`).
    float GetPlayerMovementSpeed() const;

    // --- Skills (ethereal / angelic tree) ---
    bool IsSkillUnlocked(const std::string& name) const;
    void UnlockSkill(const std::string& name);
    const std::vector<std::string>& UnlockedSkills() const { return m_unlockedSkills; }
    // Refund every unlocked node (cost back to mana/psychic, bonuses removed)
    // and clear the tree so it can be re-specced.
    void RespecSkills();

    // --- Script result routing (read by host after Update) ---
    const std::string& PendingMessage() const { return m_pendingMessage; }
    void ClearPendingMessage() { m_pendingMessage.clear(); }
    bool PlayerHurt() const { return m_playerHurt; }
    void ClearPlayerHurt() { m_playerHurt = false; }

    // External stat provider for script $name tokens (player + selected weapon stats)
    float ResolveScriptStat(const std::string& name) const;
    void ApplyPlayerStatOps(const std::vector<LightningScriptContext::PlayerStatOp>& ops);

    // --- Serialization (for save/load) ---
    std::string SerializeState() const;  // compact string of hotbar + equipment state
    bool DeserializeState(const std::string& data);  // restore from SerializeState()

    // --- Script side-effect query (called by host after Update) ---
    bool HasPendingFog() const { return m_pendingFog; }
    float PendingFogR() const { return m_fogR; }
    float PendingFogG() const { return m_fogG; }
    float PendingFogB() const { return m_fogB; }
    float PendingFogDensity() const { return m_fogDensity; }
    void ClearPendingFog() { m_pendingFog = false; }

    bool HasPendingSkybox() const { return !m_pendingSkybox.empty(); }
    const std::string& PendingSkybox() const { return m_pendingSkybox; }
    void ClearPendingSkybox() { m_pendingSkybox.clear(); }

    bool HasPendingAmbient() const { return m_pendingAmbient; }
    float PendingAmbientR() const { return m_ambientR; }
    float PendingAmbientG() const { return m_ambientG; }
    float PendingAmbientB() const { return m_ambientB; }
    void ClearPendingAmbient() { m_pendingAmbient = false; }

private:
    LightningEntityManager();
    std::vector<EntityInstance> m_instances;
    int m_hotbar[HOTBAR_SIZE];
    int m_selectedSlot = 0;

    // Mouse-wheel slot cycling. m_wheelDir latches the last scroll direction so
    // a high-resolution wheel emitting several sub-notch values per flick still
    // advances one slot; m_wheelLock throttles repeats while held down.
    int  m_wheelDir = 0;
    float m_wheelLock = 0.0f;

    // Equipment slots — instance index, -1 = empty
    int m_equipment[EQUIP_SLOT_COUNT];

    // Player entity index (set during Init)
    int m_playerEntityIndex = -1;

    // Unlocked ethereal skill node names (persisted in TF.sav).
    std::vector<std::string> m_unlockedSkills;

    // Cached model/texture/icon handles
    struct CachedResource {
        int type = 0; // 0=unused, 1=model, 2=texture
        Model model;
        Texture2D texture;
    };
    std::vector<CachedResource> m_resources;

    // Pending fog/skybox/ambient state (set by script execution, read by host)
    bool m_pendingFog = false;
    float m_fogR = 0.0f, m_fogG = 0.0f, m_fogB = 0.0f, m_fogDensity = 0.0f;
    std::string m_pendingSkybox;
    bool m_pendingAmbient = false;
    float m_ambientR = 0.0f, m_ambientG = 0.0f, m_ambientB = 0.0f;
    std::string m_pendingMessage;
    bool m_playerHurt = false;

    // Ammo-change hook (bridged by the host to the network layer)
    std::function<void(int, int, int, int)> m_on_ammo_changed;

    int CacheModel(const std::string& path);
    int CacheTexture(const std::string& path);
    void UncacheResource(int idx);

    // Sum a numeric stat over all equipped items (`key`, e.g. "max_health_bonus").
    float EquipmentStatSum(const char* key) const;

    // Drain an instance's pending script side-effects (sound/msg/stats/pickup spawn)
    void ApplyEntityScriptEffects(EntityInstance& inst);
};
