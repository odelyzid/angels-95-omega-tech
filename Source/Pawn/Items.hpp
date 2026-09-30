#ifndef OMEGA_ITEMS_HPP
#define OMEGA_ITEMS_HPP

#include <string>
#include "raylib.h"
#include "../Script/LightningEntityManager.hpp"

enum class ItemCategory {
    WEAPON = 0,
    HEALTH_VIAL,
    MANA_VIAL,
    ENERGY_CRYSTAL,
    KEY,
    COIN,
    POWERUP,
    ARMOR,
    JEWELRY,
    HELMET,
    BOOTS,
    LEGS,
    ACCESSORY
};

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

static constexpr int EQUIP_SLOT_COUNT = 8;
static constexpr int BACKPACK_COLS = 5;
static constexpr int BACKPACK_ROWS = 4;
static constexpr int BACKPACK_SLOTS = 20;
static constexpr int HOTBAR_SLOTS = 8;
static constexpr int WEAPON_SLOTS = 5;
static constexpr int ITEM_DB_SIZE = 20;

struct BackpackSlot {
    int itemId;
    int quantity;
};

struct ItemDBEntry {
    int id;
    ItemCategory category;
    EquipSlotType equipSlot;
    const char* name;
    const char* iconPath;
    int value;
    const char* description;
    int maxStack;
};

static const ItemDBEntry ItemDB[ITEM_DB_SIZE] = {
    {1, ItemCategory::HEALTH_VIAL,     EquipSlotType::NONE, "Health Vial",          "GameData/Global/Items/HealthVial.png",     25,  "Restores 25 HP",                 10},
    {2, ItemCategory::MANA_VIAL,       EquipSlotType::NONE, "Mana Vial",            "GameData/Global/Items/ManaVial.png",       25,  "Restores 25 Mana",               10},
    {3, ItemCategory::ENERGY_CRYSTAL,  EquipSlotType::NONE, "Energy Crystal (111)", "GameData/Global/Items/EnergyCrystal.png",  111, "Grants 111 Psychic Energy",      5},
    {4, ItemCategory::ENERGY_CRYSTAL,  EquipSlotType::NONE, "Energy Crystal (222)", "GameData/Global/Items/EnergyCrystal.png",  222, "Grants 222 Psychic Energy",      5},
    {5, ItemCategory::ENERGY_CRYSTAL,  EquipSlotType::NONE, "Energy Crystal (333)", "GameData/Global/Items/EnergyCrystal.png",  333, "Grants 333 Psychic Energy",      5},
    {6, ItemCategory::ENERGY_CRYSTAL,  EquipSlotType::NONE, "Energy Crystal (444)", "GameData/Global/Items/EnergyCrystal.png",  444, "Grants 444 Psychic Energy",      5},
    {7, ItemCategory::ENERGY_CRYSTAL,  EquipSlotType::NONE, "Energy Crystal (555)", "GameData/Global/Items/EnergyCrystal.png",  555, "Grants 555 Psychic Energy",      5},
    {8, ItemCategory::ENERGY_CRYSTAL,  EquipSlotType::NONE, "Energy Crystal (666)", "GameData/Global/Items/EnergyCrystal.png",  666, "Grants 666 Psychic Energy",      5},
    {9, ItemCategory::ENERGY_CRYSTAL,  EquipSlotType::NONE, "Energy Crystal (777)", "GameData/Global/Items/EnergyCrystal.png",  777, "Grants 777 Psychic Energy",      5},
    {10,ItemCategory::ENERGY_CRYSTAL,  EquipSlotType::NONE, "Energy Crystal (888)", "GameData/Global/Items/EnergyCrystal.png",  888, "Grants 888 Psychic Energy",      5},
    {11,ItemCategory::ENERGY_CRYSTAL,  EquipSlotType::NONE, "Energy Crystal (999)", "GameData/Global/Items/EnergyCrystal.png",  999, "Grants 999 Psychic Energy",      5},
    {12,ItemCategory::KEY,             EquipSlotType::NONE, "Key",                  "GameData/Global/Items/Key.png",            1,   "Opens locked doors and chests",  20},
    {13,ItemCategory::COIN,            EquipSlotType::NONE, "Coin",                 "GameData/Global/Items/Coin.png",           1,   "Currency",                        99},
    {14,ItemCategory::POWERUP,         EquipSlotType::NONE, "Powerup",              "GameData/Global/Items/Powerup.png",        0,   "Mysterious power",                5},
};

inline const ItemDBEntry* GetItemDef(int id) {
    for (int i = 0; i < ITEM_DB_SIZE; i++)
        if (ItemDB[i].name && ItemDB[i].id == id) return &ItemDB[i];
    return nullptr;
}

struct InventorySystem {
    BackpackSlot backpack[BACKPACK_SLOTS];
    int coins;

    InventorySystem() {
        for (auto& s : backpack)  { s.itemId = -1; s.quantity = 0; }
        coins = 0;
    }

    // Add item to first free backpack slot
    bool AddToBackpack(int itemId, int qty = 1) {
        const ItemDBEntry* def = GetItemDef(itemId);
        if (!def) return false;
        // Try to stack on existing
        for (auto& s : backpack) {
            if (s.itemId == itemId && s.quantity < def->maxStack) {
                int canAdd = def->maxStack - s.quantity;
                int add = (qty < canAdd) ? qty : canAdd;
                s.quantity += add;
                qty -= add;
                if (qty <= 0) return true;
            }
        }
        // Fill empty slots
        for (auto& s : backpack) {
            if (s.itemId == -1) {
                int add = (qty < def->maxStack) ? qty : def->maxStack;
                s.itemId = itemId;
                s.quantity = add;
                qty -= add;
                if (qty <= 0) return true;
            }
        }
        return qty <= 0;
    }

    // Remove one item from backpack
    bool RemoveFromBackpack(int slot) {
        if (slot < 0 || slot >= BACKPACK_SLOTS) return false;
        if (backpack[slot].itemId == -1) return false;
        if (--backpack[slot].quantity <= 0) {
            backpack[slot].itemId = -1;
            backpack[slot].quantity = 0;
        }
        return true;
    }

    // NOTE: equipment (armor/upgrade) is handled by the .ozls-driven
    // LightningEntityManager equipment slots (EquipmentAssign etc.), not here.

    // Summon a pickup item by name (for /summon command)
    int SummonItem(const char* name) {
        for (int i = 0; i < ITEM_DB_SIZE; i++) {
            const char* n = ItemDB[i].name;
            if (!n) continue; // uninitialized tail entries
            // Compare lowercase
            const char* a = name;
            const char* b = n;
            bool match = true;
            while (*a && *b) {
                char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
                char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
                if (ca != cb) { match = false; break; }
                a++; b++;
            }
            if (match && *a == *b) return ItemDB[i].id;
        }
        return -1;
    }
};

inline InventorySystem gInventory;

#endif
