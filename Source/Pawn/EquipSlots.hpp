#ifndef OZ_EQUIP_SLOTS_HPP
#define OZ_EQUIP_SLOTS_HPP

#include <cstddef>

// ---------------------------------------------------------------------------
// Equipment slot identity, shared by the entity manager (which owns the
// m_equipment[] array) and the inventory UI (which draws a row per slot).
//
// Both used to declare their own `static constexpr int EQUIP_SLOT_COUNT = 8`
// and Items.hpp a third copy, so adding a ninth slot meant finding all three
// by hand. EQUIP_SLOT_COUNT is now derived from the enum, and the labels array
// is sized by it, so the count cannot drift from either.
//
// Deliberately raylib-free: Source/Script/LightningEntityManager.hpp includes
// this and must stay independent of the client-only Items.hpp.
// ---------------------------------------------------------------------------

enum class EquipSlotType {
    NONE = -1,
    ARMOR = 0,
    JEWELRY1,
    JEWELRY2,
    HELMET,
    BOOTS,
    LEGS,
    ACCESSORY1,
    ACCESSORY2,
    COUNT
};

static constexpr int EQUIP_SLOT_COUNT = static_cast<int>(EquipSlotType::COUNT);

// Draw labels, in slot order. Sized by EQUIP_SLOT_COUNT so a new enum entry
// without a label is a compile error rather than a silently blank row.
inline constexpr const char* kEquipSlotLabels[EQUIP_SLOT_COUNT] = {
    "Armor", "Jewelry 1", "Jewelry 2", "Helmet",
    "Boots", "Legs", "Accessory 1", "Accessory 2"
};

// True when `type` addresses a real equipment slot (not NONE / COUNT).
inline constexpr bool IsEquipSlot(EquipSlotType t) {
    return static_cast<int>(t) >= 0 &&
           static_cast<int>(t) < EQUIP_SLOT_COUNT;
}

#endif  // OZ_EQUIP_SLOTS_HPP