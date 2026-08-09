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
    void Despawn(int index);
    EntityInstance* Get(int index);
    int Count() const { return (int)m_instances.size(); }

    // --- Hotbar ---
    void HotbarAssign(int slot, int instanceIndex);
    void HotbarSwap(int slotA, int slotB);
    int  HotbarAt(int slot) const;
    void SelectSlot(int slot);
    int  SelectedSlot() const { return m_selectedSlot; }
    EntityInstance* SelectedEntity() const;

    // --- Equipment slots ---
    int  EquipmentAt(int slot) const;           // returns instance index or -1
    void EquipmentAssign(int slot, int instanceIndex);
    void EquipmentUnequip(int slot);            // despawns equipped instance
    void EquipmentClear();                      // despawns all equipped
    int  EquipmentFindFreeSlot() const;         // first empty slot, or -1

    // Returns the model pointer from the cache by index
    void* GetModel(int idx) const;
    void* GetTexture(int idx) const;
    void* GetIcon(int idx) const;

    // Pre-load a model for a named entity def and return resource slot index (or -1)
    int PrecacheModelForDef(const std::string& defName);
    // Get model by pre-cached index
    Model* GetModelByResourceIdx(int idx) const;

    // --- Input + rendering ---
    void HandleInput();
    void DrawHotbar();

    // --- Projectile spawning (for weapon entities) ---
    int FireSelectedWeapon(const Vector3& origin, const Vector3& direction);

    // --- Zone actions (called by PawnSystem on zone enter/exit) ---
    void TriggerZoneAction(const std::string& zoneName, const std::string& actionName);

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

    // Equipment slots — instance index, -1 = empty
    int m_equipment[EQUIP_SLOT_COUNT];

    // Player entity index (set during Init)
    int m_playerEntityIndex = -1;

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

    // Simple one-shot sound cache: path -> loaded Sound
    struct CachedSound { Sound sound; float timer = 0.0f; };
    std::unordered_map<std::string, CachedSound> m_soundCache;

    int CacheModel(const std::string& path);
    int CacheTexture(const std::string& path);
    int CacheSound(const std::string& path);
    void UncacheResource(int idx);
    void UnloadAllResources();
    void PruneSoundCache();
};
