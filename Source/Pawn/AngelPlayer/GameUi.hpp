#pragma once
#include "raylib.h"
#include <string>
#include <vector>

// GameUi — C++ bridge to the declarative `GameUI` .ozls entity
// (EntityType::GAMEUI).
//
// The .ozls entity is a data + hook layer: it declares the HUD texture and
// layout stats and may run on_tick logic. LightningScript cannot draw or call
// C++ directly, so all rendering and input stays in the AngelPlayer behaviours
// (SlotBar / InventoryBehaviour / PlayerController). This class only exposes
// the entity's authored data to those C++ systems.
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

    // --- Object/slot bar geometry -----------------------------------------
    // Cells come from the `slot_rects` stat: a comma-separated list of
    // (x, y, w, h) quadruples in SOURCE TEXTURE pixels, left to right. Parsed
    // lazily and cached; re-parsed if the resolved instance changes.
    int SlotCount() const;
    // Cell `i` in source-texture pixels. False when out of range.
    bool SlotRect(int i, Rectangle& out) const;

    // True when a bar texture AND at least one cell are available, i.e. the
    // authored slotbar can be drawn. When false, callers fall back to their
    // own chrome.
    bool HasBar() const;

    // Bar width as a fraction of screen width, clamped to (0, 1].
    float WidthPct() const;
    // Gap between the bar's bottom edge and the bottom of the screen, px.
    float BottomMargin() const;
    // Inset applied to a cell rect before placing its icon, source-texture px.
    float IconInset() const;
    bool ShowSlotNumbers() const;
    bool ShowSlotName() const;

private:
    GameUi() = default;
    void ParseSlots() const;

    int m_instanceIndex = -1;
    // Parsed slot_rects. mutable so the const accessors can fill it lazily.
    mutable std::vector<Rectangle> m_slots;
    mutable bool m_slotsParsed = false;
    mutable int m_slotsForInstance = -2;  // instance the cache was built from
};
