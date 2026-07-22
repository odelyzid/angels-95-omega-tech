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

// LightningEntityManager — runtime entity instances + dynamic hotbar
// Replaces Objects.hpp with a fully dynamic 512-entity / 8-slot system
class LightningEntityManager {
public:
    static constexpr int MAX_ENTITIES = 512;
    static constexpr int HOTBAR_SIZE = 8;

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

    // Returns the model pointer from the cache by index
    void* GetModel(int idx) const;
    void* GetTexture(int idx) const;
    void* GetIcon(int idx) const;

    // --- Input + rendering ---
    void HandleInput();
    void DrawHotbar();

    // --- Zone actions (called by PawnSystem on zone enter/exit) ---
    void TriggerZoneAction(const std::string& zoneName, const std::string& actionName);

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

private:
    LightningEntityManager() = default;
    std::vector<EntityInstance> m_instances;
    int m_hotbar[HOTBAR_SIZE];
    int m_selectedSlot = 0;

    // Cached model/texture/icon handles
    struct CachedResource {
        int type = 0; // 0=unused, 1=model, 2=texture
        Model model;
        Texture2D texture;
    };
    std::vector<CachedResource> m_resources;

    // Pending fog/skybox state (set by script execution, read by host)
    bool m_pendingFog = false;
    float m_fogR = 0.0f, m_fogG = 0.0f, m_fogB = 0.0f, m_fogDensity = 0.0f;
    std::string m_pendingSkybox;

    // Simple one-shot sound cache: path → loaded Sound
    struct CachedSound { Sound sound; float timer = 0.0f; };
    std::unordered_map<std::string, CachedSound> m_soundCache;

    int CacheModel(const std::string& path);
    int CacheTexture(const std::string& path);
    int CacheSound(const std::string& path);
    void UncacheResource(int idx);
    void UnloadAllResources();
    void PruneSoundCache();
};
