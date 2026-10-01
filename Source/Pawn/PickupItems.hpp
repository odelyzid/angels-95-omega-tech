#ifndef OZ_PICKUP_ITEMS_HPP
#define OZ_PICKUP_ITEMS_HPP

#include <cstdint>

// ---------------------------------------------------------------------------
// Pickup -> inventory item id mapping, shared by the server and the client.
//
// This mapping used to live only in Source/Server/Server.cpp as a switch over
// PickupType, while Source/Pawn/Items.hpp hardcoded the matching numeric ids in
// its ItemDB table with a comment reminding the reader to keep the two in sync.
// Nothing enforced it: retuning ItemDB alone silently desynced what the server
// grants from what the client can display, and the single-player walk-over path
// in OzPawnSystem bypassed the table entirely by scanning ItemDB by name.
//
// The enum and this function are deliberately raylib-free so the standalone
// server (no raylib dependency) and the client can both include it.
// ---------------------------------------------------------------------------

enum class PickupType : uint8_t {
    HEALTH = 0,
    MANA,
    PSYCHIC,
    ARMOR,
    WEAPON,
    AMMO,
    KEY,
    COIN,
    POWERUP
};

constexpr uint32_t PICKUP_TYPE_COUNT = 9;

// Item ids. These are the values ItemDB is keyed by and the values the client
// receives in net::PickupCollectedData::item_id.
namespace item_id {
    constexpr int HEALTH_VIAL = 1;
    constexpr int MANA_VIAL = 2;
    // PSYCHIC maps onto a tier of energy crystals by value (111..999).
    constexpr int ENERGY_FIRST = 3;
    constexpr int ENERGY_LAST = 11;
    constexpr int KEY = 12;
    constexpr int COIN = 13;
    constexpr int POWERUP = 14;
    constexpr int WEAPON = 15;
    constexpr int ARMOR = 16;
    constexpr int AMMO = 17;
}

// Result of mapping a collected pickup onto a grant.
struct PickupGrant {
    int itemId = -1;   // -1 = the pickup has no inventory item (consumed for effect only)
    int quantity = 1;  // stack size for COIN / ARMOR / AMMO
};

// Energy crystal tiers step by one crystal value (111, 222, ... 999). A value
// outside that range clamps into the first/last tier rather than escaping the
// item table.
inline int PickupEnergyCrystalId(int value) {
    // Tiers start one below ENERGY_FIRST: 111/111 == 1, so the offset form is
    // (ENERGY_FIRST - 1) + value/111. Writing it as ENERGY_FIRST + value/111
    // shifts every crystal up a tier (333 would land on the 444 crystal).
    int id = (item_id::ENERGY_FIRST - 1) + (value / 111);
    if (id < item_id::ENERGY_FIRST) id = item_id::ENERGY_FIRST;
    if (id > item_id::ENERGY_LAST)   id = item_id::ENERGY_LAST;
    return id;
}

// Map a pickup type (+ its value, for the psychic tiers) onto an inventory
// grant. `value` is the pickup's stored value; only PSYCHIC reads it today, and
// only COIN / ARMOR / AMMO use it as a quantity.
//
// ARMOR and AMMO previously fell through to `default: break`, leaving itemId
// -1: the pickup was consumed and hidden for everyone but granted to nobody.
inline PickupGrant ResolvePickupItemId(PickupType type, int value) {
    PickupGrant g;
    switch (type) {
        case PickupType::HEALTH:  g.itemId = item_id::HEALTH_VIAL; break;
        case PickupType::MANA:    g.itemId = item_id::MANA_VIAL; break;
        case PickupType::PSYCHIC: g.itemId = PickupEnergyCrystalId(value); break;
        case PickupType::KEY:     g.itemId = item_id::KEY; break;
        case PickupType::COIN:
            g.itemId = item_id::COIN;
            g.quantity = value > 0 ? value : 1;
            break;
        case PickupType::POWERUP: g.itemId = item_id::POWERUP; break;
        case PickupType::ARMOR:
            g.itemId = item_id::ARMOR;
            g.quantity = value > 0 ? value : 1;
            break;
        case PickupType::AMMO:
            g.itemId = item_id::AMMO;
            g.quantity = value > 0 ? value : 1;
            break;
        case PickupType::WEAPON:  g.itemId = item_id::WEAPON; break;
        default: break;  // stays -1
    }
    return g;
}

#endif  // OZ_PICKUP_ITEMS_HPP