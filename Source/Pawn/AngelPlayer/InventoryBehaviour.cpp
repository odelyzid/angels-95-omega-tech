#include "InventoryBehaviour.hpp"
#include "../Items.hpp"
#include "SlotBar.hpp"
#include "../../Script/LightningEntityManager.hpp"
#include "../../Script/LightningEntityRegistry.hpp"
#include "../../Renderer/OzAssetMapper.hpp"
#include "../../Log.hpp"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

void InventoryBehaviour::Emit(const std::string& msg) {
    if (m_messageSink) m_messageSink(msg);
}

// ---------------------------------------------------------------------------
// Consume one backpack item and apply its effect
// ---------------------------------------------------------------------------
bool InventoryBehaviour::UseBackpackItem(int slot) {
    if (slot < 0 || slot >= BACKPACK_SLOTS) return false;
    int itemId = gInventory.backpack[slot].itemId;
    if (itemId < 0 || gInventory.backpack[slot].quantity <= 0) return false;
    const ItemDBEntry* def = GetItemDef(itemId);
    if (!def) return false;

    auto& lem = LightningEntityManager::Instance();
    switch (def->category) {
        case ItemCategory::HEALTH_VIAL:
            lem.SetPlayerHealth(fminf(lem.GetPlayerMaxHealth(),
                                      lem.GetPlayerHealth() + (float)def->value));
            break;
        case ItemCategory::MANA_VIAL:
            lem.SetPlayerMana(fminf(lem.GetPlayerMaxMana(),
                                    lem.GetPlayerMana() + (float)def->value));
            break;
        case ItemCategory::ENERGY_CRYSTAL:
            lem.SetPlayerPsychicEnergy(fminf(lem.GetPlayerMaxPsychicEnergy(),
                                             lem.GetPlayerPsychicEnergy() + (float)def->value));
            break;
        case ItemCategory::COIN:
            gInventory.coins += def->value;
            break;
        default:
            return false; // no on-the-spot use effect
    }
    gInventory.RemoveFromBackpack(slot);
    Emit(std::string("Used ") + def->name);
    return true;
}

// ---------------------------------------------------------------------------
// HUD: always-visible player stat bars
// ---------------------------------------------------------------------------
void InventoryBehaviour::DrawHud(const Camera3D& cam, int ticker, bool isCrouching, bool isSprinting) {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const int bar_w = 220;
    const int bar_h = 14;
    const int pad = 6;
    int x = pad;
    int y = pad;

    // Level
    DrawText(TextFormat("Lv.%d", LightningEntityManager::Instance().GetPlayerLevel()), x, y, 14, WHITE);
    y += 18;

    // XP bar
    int xp_w = bar_w - 40;
    DrawText("XP", x, y, 12, LIGHTGRAY);
    int xpToNext = LightningEntityManager::Instance().GetPlayerXPToNext();
    if (xpToNext > 0) {
        int xp = LightningEntityManager::Instance().GetPlayerXP();
        float xp_pct = (float)xp / xpToNext;
        DrawRectangle(x + 30, y, xp_w, bar_h, (Color){30, 30, 30, 255});
        DrawRectangle(x + 30, y, (int)(xp_pct * xp_w), bar_h, SKYBLUE);
        DrawText(TextFormat("%d/%d", xp, xpToNext),
                 x + 34, y + 1, 10, WHITE);
    }
    y += bar_h + pad;

    // Health bar
    float hpMax = LightningEntityManager::Instance().GetPlayerMaxHealth();
    float hp = LightningEntityManager::Instance().GetPlayerHealth();
    float hp_pct = (hpMax > 0) ? (hp / hpMax) : 0;
    DrawText(TextFormat("HP %d/%d", (int)hp, (int)hpMax),
             x, y, 12, WHITE);
    DrawRectangle(x, y + 14, bar_w, bar_h, (Color){50, 10, 10, 255});
    DrawRectangle(x, y + 14, (int)(hp_pct * bar_w), bar_h, RED);
    y += 14 + bar_h + pad;

    // Mana bar
    float mpMax = LightningEntityManager::Instance().GetPlayerMaxMana();
    float mp = LightningEntityManager::Instance().GetPlayerMana();
    float mp_pct = (mpMax > 0) ? (mp / mpMax) : 0;
    DrawText(TextFormat("MP %d/%d", (int)mp, (int)mpMax),
             x, y, 12, WHITE);
    DrawRectangle(x, y + 14, bar_w, bar_h, (Color){10, 10, 50, 255});
    DrawRectangle(x, y + 14, (int)(mp_pct * bar_w), bar_h, BLUE);
    y += 14 + bar_h + pad;

    // Psychic Energy bar
    float peMax = LightningEntityManager::Instance().GetPlayerMaxPsychicEnergy();
    float pe = LightningEntityManager::Instance().GetPlayerPsychicEnergy();
    float pe_pct = (peMax > 0) ? (pe / peMax) : 0;
    DrawText(TextFormat("PE %d/%d", (int)pe, (int)peMax),
             x, y, 12, WHITE);
    DrawRectangle(x, y + 14, bar_w, bar_h, (Color){40, 10, 50, 255});
    DrawRectangle(x, y + 14, (int)(pe_pct * bar_w), bar_h, PURPLE);
    y += 14 + bar_h + pad;

    // Current selected item/weapon (from EntityManager hotbar)
    {
        auto& lem = LightningEntityManager::Instance();
        EntityInstance* selEnt = lem.SelectedEntity();
        int selSlot = lem.SelectedSlot() + 1;
        const char* label = "Empty";
        if (selEnt && selEnt->def)
            label = selEnt->def->name.c_str();
        DrawText(TextFormat("Slot %d: %s", selSlot, label),
                 x, y, 12, YELLOW);
        y += 16;
        // Ammo display for weapons
        if (selEnt && selEnt->def && selEnt->def->type == EntityType::WEAPON) {
            auto ait = selEnt->runtimeStats.find("ammo");
            auto mit = selEnt->runtimeStats.find("magazine");
            if (ait != selEnt->runtimeStats.end() && mit != selEnt->runtimeStats.end()) {
                Color ammoCol = (ait->second <= 0.0f) ? RED : WHITE;
                DrawText(TextFormat("Ammo: %.0f/%.0f", ait->second, mit->second),
                         x, y, 12, ammoCol);
                y += 16;
            }
        }
    }

    // Weapon fire indicator (brief pulse)
    static double lastFireTime = 0;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        lastFireTime = GetTime();
    }
    if (GetTime() - lastFireTime < 0.15) {
        DrawText("FIRE", x, y, 20, RED);
    }

    // Movement stance indicator (sprint / crouch)
    if (isCrouching)
        DrawText("CROUCH", pad, sh - 46, 16, (Color){120, 200, 255, 255});
    else if (isSprinting)
        DrawText("SPRINT", pad, sh - 46, 16, (Color){255, 210, 120, 255});

    // Coordinates (top-right)
    float yaw = -atan2f(cam.target.x - cam.position.x, cam.target.z - cam.position.z) * RAD2DEG;
    float pitch = asinf((cam.target.y - cam.position.y) /
        Vector3Distance(cam.position, cam.target)) * RAD2DEG;
    int rx = sw - 280;
    DrawText(TextFormat("Pos: %.1f %.1f %.1f", cam.position.x, cam.position.y, cam.position.z),
             rx, 10, 14, LIGHTGRAY);
    DrawText(TextFormat("Rot: %.0f %.0f", yaw, pitch),
             rx, 28, 14, LIGHTGRAY);
    if ((ticker % 60) == 0) {
        fprintf(stderr, "POS: %.1f %.1f %.1f  ROT: %.0f %.0f\n",
                cam.position.x, cam.position.y, cam.position.z, yaw, pitch);
    }
}

// ---------------------------------------------------------------------------
// Inventory Overlay (Diablo I style)
// ---------------------------------------------------------------------------
void InventoryBehaviour::DrawOverlay() {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    const int panel_w = 720;
    const int panel_h = 520;
    const int px = (sw - panel_w) / 2;
    const int py = (sh - panel_h) / 2;

    DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 160});
    DrawRectangle(px, py, panel_w, panel_h, (Color){25, 25, 35, 245});
    DrawRectangleLines(px, py, panel_w, panel_h, (Color){180, 180, 200, 255});

    DrawText("INVENTORY", px + 15, py + 10, 22, WHITE);
    DrawText(TextFormat("Coins: %d", gInventory.coins), px + panel_w - 150, py + 14, 16, GOLD);

    int ex = px + 20;
    int ey = py + 50;
    int slotH = 38;
    int slotW = 150;

    // === EQUIPMENT (left side) ===
    DrawText("EQUIPMENT", ex, ey - 18, 12, LIGHTGRAY);

    const char* equipLabels[EQUIP_SLOT_COUNT] = {
        "Armor", "Jewelry 1", "Jewelry 2", "Helmet",
        "Boots", "Legs", "Accessory 1", "Accessory 2"
    };

    auto& lem = LightningEntityManager::Instance();
    for (int i = 0; i < EQUIP_SLOT_COUNT; i++) {
        int idx = lem.EquipmentAt(i);
        bool owned = (idx >= 0 && lem.Get(idx) && lem.Get(idx)->owned);
        Color c = owned ? WHITE : (Color){80, 80, 80, 255};
        Color bg = owned ? (Color){40, 50, 45, 255} : (Color){20, 25, 20, 255};

        Rectangle eqRect = {(float)ex, (float)ey, (float)slotW, (float)slotH};
        bool eqHover = CheckCollisionPointRec(GetMousePosition(), eqRect);
        if (eqHover && owned) {
            c = (Color){120, 200, 255, 255};
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                lem.EquipmentUnequip(i);
        }

        DrawRectangle(ex, ey, slotW, slotH, bg);
        DrawRectangleLines(ex, ey, slotW, slotH, c);
        DrawText(equipLabels[i], ex + 6, ey + 12, 12, c);

        if (owned && lem.Get(idx)->iconIdx >= 0) {
            Texture2D* iconTex = (Texture2D*)lem.GetIcon(lem.Get(idx)->iconIdx);
            if (iconTex && iconTex->id > 0)
                DrawTextureEx(*iconTex, (Vector2){(float)ex + slotW - 34, (float)ey + 2}, 0, 1.5f, WHITE);
        }
        ey += slotH + 4;
    }

    // === BACKPACK (right side) -> Legacy ===
    int bx = px + 190;
    int by = py + 50;
    int cellSize = 52;
    int cellGap = 4;

    DrawText("BACKPACK", bx, by - 18, 12, LIGHTGRAY);

    for (int row = 0; row < BACKPACK_ROWS; row++) {
        for (int col = 0; col < BACKPACK_COLS; col++) {
            int idx = row * BACKPACK_COLS + col;
            int cx = bx + col * (cellSize + cellGap);
            int cy = by + row * (cellSize + cellGap);

            int itemId = gInventory.backpack[idx].itemId;
            int qty = gInventory.backpack[idx].quantity;
            bool hasItem = itemId >= 0 && qty > 0;

            Rectangle cellRect = {(float)cx, (float)cy, (float)cellSize, (float)cellSize};
            bool hover = CheckCollisionPointRec(GetMousePosition(), cellRect);
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
                m_selectedBpSlot = hasItem ? idx : -1;
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && hasItem) {
                if (UseBackpackItem(idx)) { itemId = -1; qty = 0; hasItem = false; }
            }

            Color bgC = hasItem ? (Color){40, 40, 55, 255} : (Color){15, 15, 20, 255};
            Color borderC = hasItem ? WHITE : (Color){50, 50, 50, 255};

            if (hover) borderC = (Color){120, 200, 255, 255};
            if (m_selectedBpSlot == idx) {
                borderC = (Color){255, 255, 0, 255};
            }

            DrawRectangle(cx, cy, cellSize, cellSize, bgC);
            DrawRectangleLines(cx, cy, cellSize, cellSize, borderC);

            if (hasItem) {
                const ItemDBEntry* def = GetItemDef(itemId);
                if (def) {
                    Texture2D* icon = nullptr;
                    const char* iconAlias = nullptr;
                    switch (def->category) {
                        case ItemCategory::HEALTH_VIAL:    iconAlias = "HealthVial"; break;
                        case ItemCategory::MANA_VIAL:      iconAlias = "ManaVial"; break;
                        case ItemCategory::ENERGY_CRYSTAL: iconAlias = "EnergyCrystal"; break;
                        case ItemCategory::KEY:            iconAlias = "Key"; break;
                        case ItemCategory::COIN:           iconAlias = "Coin"; break;
                        case ItemCategory::POWERUP:        iconAlias = "Powerup"; break;
                        default: break;
                    }
                    if (iconAlias) {
                        Texture2D t = AssetMapper::Instance().GetTexture(iconAlias);
                        if (t.id > 0) {
                            static Texture2D s_cachedIcon = t;
                            s_cachedIcon = t;
                            icon = &s_cachedIcon;
                        }
                    }
                    if (icon && icon->id > 0) {
                        float scale = (float)cellSize / (float)icon->width * 0.7f;
                        DrawTextureEx(*icon, (Vector2){(float)cx + 6, (float)cy + 4}, 0, scale, WHITE);
                    }
                    // Quantity text
                    if (qty > 1) {
                        DrawText(TextFormat("%d", qty), cx + cellSize - 20, cy + cellSize - 16, 12, WHITE);
                    }
                }
            }
        }
    }

    // Keyboard navigation over the backpack (arrows move, Enter uses)
    {
        int sel = m_selectedBpSlot;
        int row = (sel >= 0) ? (sel / BACKPACK_COLS) : 0;
        int col = (sel >= 0) ? (sel % BACKPACK_COLS) : 0;
        bool moved = false;
        if (IsKeyPressed(KEY_RIGHT)) { col = (col + 1) % BACKPACK_COLS; moved = true; }
        if (IsKeyPressed(KEY_LEFT))  { col = (col + BACKPACK_COLS - 1) % BACKPACK_COLS; moved = true; }
        if (IsKeyPressed(KEY_DOWN))  { row = (row + 1) % BACKPACK_ROWS; moved = true; }
        if (IsKeyPressed(KEY_UP))    { row = (row + BACKPACK_ROWS - 1) % BACKPACK_ROWS; moved = true; }
        if (moved) m_selectedBpSlot = row * BACKPACK_COLS + col;
        if (IsKeyPressed(KEY_ENTER) && m_selectedBpSlot >= 0)
            UseBackpackItem(m_selectedBpSlot);
    }

    // Hover tooltip for backpack items
    {
        Vector2 mp = GetMousePosition();
        for (int index = 0; index < BACKPACK_SLOTS; index++) {
            int row = index / BACKPACK_COLS, col = index % BACKPACK_COLS;
            Rectangle r = {(float)(bx + col * (cellSize + cellGap)),
                           (float)(by + row * (cellSize + cellGap)),
                           (float)cellSize, (float)cellSize};
            if (!CheckCollisionPointRec(mp, r)) continue;
            int itemId = gInventory.backpack[index].itemId;
            if (itemId < 0) break;
            const ItemDBEntry* def = GetItemDef(itemId);
            if (!def) break;
            int nameW = MeasureText(def->name, 13);
            int descW = MeasureText(def->description, 11);
            int tw = (nameW > descW ? nameW : descW) + 16;
            int tipX = (int)mp.x + 14, tipY = (int)mp.y + 14;
            if (tipX + tw > sw) tipX = sw - tw - 4;
            if (tipY + 42 > sh) tipY = sh - 46;
            DrawRectangle(tipX, tipY, tw, 42, (Color){10, 10, 20, 240});
            DrawRectangleLines(tipX, tipY, tw, 42, (Color){210, 180, 90, 255});
            DrawText(def->name, tipX + 8, tipY + 5, 13, WHITE);
            DrawText(def->description, tipX + 8, tipY + 23, 11, LIGHTGRAY);
            break;
        }
    }

    // === STATS (between equipment and backpack) ===
    int sx = px + 530;
    int sy = py + 50;
    DrawText("STATS", sx, sy - 18, 12, LIGHTGRAY);

    sy += 4;
    DrawText(TextFormat("Level: %d", LightningEntityManager::Instance().GetPlayerLevel()), sx, sy, 14, WHITE); sy += 22;
    DrawText(TextFormat("XP: %d/%d", LightningEntityManager::Instance().GetPlayerXP(), LightningEntityManager::Instance().GetPlayerXPToNext()), sx, sy, 14, WHITE); sy += 22;

    float hpInv = LightningEntityManager::Instance().GetPlayerHealth();
    float hpMaxInv = LightningEntityManager::Instance().GetPlayerMaxHealth();
    DrawRectangle(sx, sy, 150, 10, (Color){50, 0, 0, 255});
    float hpPct = hpMaxInv > 0 ? hpInv / hpMaxInv : 0;
    DrawRectangle(sx, sy, (int)(150 * hpPct), 10, RED);
    DrawText(TextFormat("HP: %.0f/%.0f", hpInv, hpMaxInv), sx + 1, sy + 12, 12, RED); sy += 28;

    float mpInv = LightningEntityManager::Instance().GetPlayerMana();
    float mpMaxInv = LightningEntityManager::Instance().GetPlayerMaxMana();
    DrawRectangle(sx, sy, 150, 10, (Color){0, 0, 50, 255});
    float mpPct = mpMaxInv > 0 ? mpInv / mpMaxInv : 0;
    DrawRectangle(sx, sy, (int)(150 * mpPct), 10, BLUE);
    DrawText(TextFormat("MP: %.0f/%.0f", mpInv, mpMaxInv), sx + 1, sy + 12, 12, BLUE); sy += 28;

    float peInv = LightningEntityManager::Instance().GetPlayerPsychicEnergy();
    float peMaxInv = LightningEntityManager::Instance().GetPlayerMaxPsychicEnergy();
    DrawRectangle(sx, sy, 150, 10, (Color){30, 0, 30, 255});
    float pePct = peMaxInv > 0 ? peInv / peMaxInv : 0;
    DrawRectangle(sx, sy, (int)(150 * pePct), 10, PURPLE);
    DrawText(TextFormat("PE: %.0f/%.0f", peInv, peMaxInv), sx + 1, sy + 12, 12, PURPLE);

    // Selected item info
    if (m_selectedBpSlot >= 0) {
        int itemId = gInventory.backpack[m_selectedBpSlot].itemId;
        if (itemId >= 0) {
            const ItemDBEntry* def = GetItemDef(itemId);
            if (def) {
                DrawText(def->name, sx, sy + 40, 14, WHITE);
                DrawText(def->description, sx, sy + 58, 12, LIGHTGRAY);
            }
        }
    }

    // Hotbar preview at the bottom, drawn with the same authored bar art as the
    // in-world HUD (GameUI.ozls) so the two never drift apart.
    {
        const int  hy        = py + panel_h - 48;
        const float miniCell = 34.0f;
        const int  nSlots    = LightningEntityManager::HOTBAR_SIZE;

        SlotBarOptions opt;
        opt.firstSlot       = 0;
        opt.slotCount       = nSlots;
        opt.forcedWidth     = miniCell * (float)nSlots * 1.18f;
        opt.centerX         = (float)px + 20.0f + opt.forcedWidth * 0.5f;
        opt.bottomY         = (float)(hy + 40);
        opt.drawSlotNumbers = true;
        opt.drawSlotName    = false;   // the panel already labels things
        opt.clickToSelect   = false;   // the panel owns its own clicks

        int hover = -1;
        if (!DrawSlotBar(opt, hover)) {
            // No authored bar: fall back to plain cells (missing art / headless).
            int hx = px + 20;
            DrawText("HOTBAR:", hx, hy, 12, DARKGRAY);
            hx += 60;
            for (int i = 0; i < nSlots; i++) {
                int idx = lem.HotbarAt(i);
                bool hasItem = (idx >= 0 && lem.Get(idx) && lem.Get(idx)->def);
                Color c = hasItem ? WHITE : (Color){50, 50, 50, 255};
                DrawRectangle(hx, hy, 32, 32, (Color){20, 20, 30, 255});
                DrawRectangleLines(hx, hy, 32, 32, c);
                if (hasItem && lem.Get(idx)->iconIdx >= 0) {
                    Texture2D* iconTex = (Texture2D*)lem.GetIcon(lem.Get(idx)->iconIdx);
                    if (iconTex && iconTex->id > 0)
                        DrawTextureEx(*iconTex, (Vector2){(float)hx + 4, (float)hy + 4}, 0, 1.0f, WHITE);
                }
                hx += 36;
            }
        }
    }

    // Controls hint
    DrawText("TAB: close  |  Left-click select  |  Right-click / ENTER use  |  E: pickup  |  K: skills  |  ^: console",
             px + 20, py + panel_h - 22, 12, DARKGRAY);
}

// ---------------------------------------------------------------------------
// Network pickup hooks
// ---------------------------------------------------------------------------
void InventoryBehaviour::OnItemCollected(int item_id, int quantity) {
    const char* name = "unknown";
    const ItemDBEntry* def = GetItemDef(item_id);
    if (def) name = def->name;
    Emit(std::string(TextFormat("Collected: %s x%d", name, quantity)));

    auto& lem = LightningEntityManager::Instance();
    if (item_id == 13) {
        gInventory.coins += quantity;
    } else if (item_id == 1) {
        float curHp = lem.GetPlayerHealth();
        lem.SetPlayerHealth(std::min(curHp + 25.0f, lem.GetPlayerMaxHealth()));
    } else if (item_id == 2) {
        float curMp = lem.GetPlayerMana();
        lem.SetPlayerMana(std::min(curMp + 25.0f, lem.GetPlayerMaxMana()));
    } else if (item_id > 0) {
        gInventory.AddToBackpack(item_id, quantity);
    }

    if (m_feedbackSink) m_feedbackSink();
}

void InventoryBehaviour::OnWeaponCollected(const char* weapon_def_name) {
    auto& registry = LightningEntityRegistry::Instance();
    auto& lem = LightningEntityManager::Instance();
    const EntityDef* def = registry.Find(weapon_def_name);
    if (!def) {
        OZ_WARN("Weapon collect: unknown def '%s' — falling back to automag", weapon_def_name);
        def = registry.Find("automag");
        if (!def) return;
    }
    int instIdx = lem.Spawn(def->name);
    if (instIdx < 0) return;
    bool assigned = false;
    for (int s = 0; s < LightningEntityManager::HOTBAR_SIZE; s++) {
        if (lem.HotbarAt(s) < 0) { lem.HotbarAssign(s, instIdx); assigned = true; break; }
    }
    if (assigned) Emit(std::string("Picked up weapon: ") + def->name);
    else { OZ_WARN("Hotbar full — weapon %s lost", def->name.c_str()); lem.Despawn(instIdx); }

    if (m_feedbackSink) m_feedbackSink();
}
