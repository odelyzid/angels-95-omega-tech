#pragma once
#include "raylib.h"
#include <functional>
#include <string>

// InventoryBehaviour — HUD + inventory overlay + item/weapon pickup handling for
// the AngelPlayer. Part of the AngelPlayer player stack.
//
// Core.hpp / Data.hpp are single-translation-unit globals, so this behaviour
// receives its rendering context (camera / ticker / stance) as arguments and
// emits log + feedback through injected sinks instead of reaching into
// OmegaTechTextSystem / SoundManager directly. Item/inventory data comes
// from the shared Pawn/Items.hpp.
class InventoryBehaviour {
public:
    static InventoryBehaviour& Instance() {
        static InventoryBehaviour instance;
        return instance;
    }

    // Output sinks, wired by the host once at startup.
    void SetMessageSink(std::function<void(const std::string&)> sink) {
        m_messageSink = std::move(sink);
    }
    void SetFeedbackSink(std::function<void()> sink) {
        m_feedbackSink = std::move(sink);
    }

    // Consume one backpack item and apply its effect. Returns true if used.
    bool UseBackpackItem(int slot);

    // Always-visible stat bars (draw inside the 2D pass).
    void DrawHud(const Camera3D& cam, int ticker, bool isCrouching, bool isSprinting);

    // Diablo-style inventory overlay (draw when the inventory is open).
    void DrawOverlay();

    // Network pickup hooks (delegated from OmegaClient callbacks).
    void OnItemCollected(int item_id, int quantity);
    void OnWeaponCollected(const char* weapon_def_name);

private:
    InventoryBehaviour() = default;

    void Emit(const std::string& msg);

    int m_selectedBpSlot = -1;
    std::function<void(const std::string&)> m_messageSink;
    std::function<void()> m_feedbackSink;
};
