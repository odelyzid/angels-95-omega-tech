#pragma once
#include "raylib.h"
#include <string>

// GameUi — C++ bridge to the declarative `GameUI` .ozls entity
// (EntityType::GAMEUI).
//
// The .ozls entity is a data + hook layer: it declares the HUD texture and
// layout stats and may run on_tick logic. LightningScript cannot draw or call
// C++ directly, so all rendering and input stays in the AngelPlayer behaviours
// (InventoryBehaviour / PlayerController). This class only exposes the entity's
// authored data to those C++ systems.
class GameUi {
public:
    static GameUi& Instance() {
        static GameUi instance;
        return instance;
    }

    // Resolve (or spawn) the "GameUI" def into LightningEntityManager.
    // Idempotent; safe to call after a world reload.
    void Init();

    bool Ready() const { return m_instanceIndex >= 0; }
    int  InstanceIndex() const { return m_instanceIndex; }

    // Authored stat lookups (defaults returned when the entity/stat is absent).
    float StatFloat(const std::string& key, float def = 0.0f) const;
    std::string StatString(const std::string& key, const std::string& def = "") const;

    // Cached texture from the entity's top-level `texture =` key.
    // Returns a zeroed Texture2D when unset or the entity is not ready.
    Texture2D Texture() const;

private:
    GameUi() = default;
    int m_instanceIndex = -1;
};
