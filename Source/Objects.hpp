#include "PPGIO.hpp"
#include <cstdio>

class GameObject
{
    public:
        Font BarFont;
        Texture2D ObjectBar;

        Model Object1;
        wstring Object1Name;
        Texture2D Object1Texture;
        Texture2D Object1Icon;
        bool Object1Owned;

        Model Object2;
        wstring Object2Name;
        Texture2D Object2Texture;
        Texture2D Object2Icon;
        bool Object2Owned;

        Model Object3;
        wstring Object3Name;
        Texture2D Object3Texture;
        Texture2D Object3Icon;
        bool Object3Owned;

        Model Object4;
        wstring Object4Name;
        Texture2D Object4Texture;
        Texture2D Object4Icon;
        bool Object4Owned;

        Model Object5;
        wstring Object5Name;
        Texture2D Object5Texture;
        Texture2D Object5Icon;
        bool Object5Owned;

        // Armory slots
        Model Armory1;
        wstring Armory1Name;
        Texture2D Armory1Texture;
        Texture2D Armory1Icon;
        bool Armory1Owned;

        Model Armory2;
        wstring Armory2Name;
        Texture2D Armory2Texture;
        Texture2D Armory2Icon;
        bool Armory2Owned;

        // Jewelry slots
        Model Jewelry1;
        wstring Jewelry1Name;
        Texture2D Jewelry1Texture;
        Texture2D Jewelry1Icon;
        bool Jewelry1Owned;

        Model Jewelry2;
        wstring Jewelry2Name;
        Texture2D Jewelry2Texture;
        Texture2D Jewelry2Icon;
        bool Jewelry2Owned;

        // RPG expansion slots
        Model Helmet;
        wstring HelmetName;
        Texture2D HelmetTexture;
        Texture2D HelmetIcon;
        bool HelmetOwned;

        Model Boots;
        wstring BootsName;
        Texture2D BootsTexture;
        Texture2D BootsIcon;
        bool BootsOwned;

        Model Legs;
        wstring LegsName;
        Texture2D LegsTexture;
        Texture2D LegsIcon;
        bool LegsOwned;

        Model Accessory1;
        wstring Accessory1Name;
        Texture2D Accessory1Texture;
        Texture2D Accessory1Icon;
        bool Accessory1Owned;

        Model Accessory2;
        wstring Accessory2Name;
        Texture2D Accessory2Texture;
        Texture2D Accessory2Icon;
        bool Accessory2Owned;

        // Item icon textures for consumables
        Texture2D HealthVialIcon;
        Texture2D ManaVialIcon;
        Texture2D EnergyCrystalIcon;
        Texture2D KeyIcon;
        Texture2D CoinIcon;
        Texture2D PowerupIcon;
};

static GameObject OmegaTechGameObjects;

void InitObjects(){
    OmegaTechGameObjects.ObjectBar = LoadTexture("GameData/Global/ObjectBar.png");
    OmegaTechGameObjects.BarFont = LoadFont("GameData/Global/Font.ttf");


    // Object1: Wand / Energy Weapon (always owned, always present)
    if (IsPathFile("GameData/Global/Objects/Object1.obj")){
        OmegaTechGameObjects.Object1 = LoadModel("GameData/Global/Objects/Object1.obj");
        OmegaTechGameObjects.Object1Texture = LoadTexture("GameData/Global/Objects/Object1Texture.png");
    }
    OmegaTechGameObjects.Object1Icon = LoadTexture("GameData/Global/Objects/Object1Icon.png");
    OmegaTechGameObjects.Object1Owned = true;
    OmegaTechGameObjects.Object1Name = WSplitValue(LoadFile("GameData/Global/Objects/Object1.info") , 0);

    if (IsPathFile("GameData/Global/Objects/Object2.obj")){
        OmegaTechGameObjects.Object2 = LoadModel("GameData/Global/Objects/Object2.obj");
        OmegaTechGameObjects.Object2Texture = LoadTexture("GameData/Global/Objects/Object2Texture.png");
        OmegaTechGameObjects.Object2Icon = LoadTexture("GameData/Global/Objects/Object2Icon.png");
        OmegaTechGameObjects.Object2.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = OmegaTechGameObjects.Object2Texture; 
        OmegaTechGameObjects.Object2Owned = false;
        OmegaTechGameObjects.Object2Name = WSplitValue(LoadFile("GameData/Global/Objects/Object2.info") , 0);
    }

    if (IsPathFile("GameData/Global/Objects/Object3.obj")){
        OmegaTechGameObjects.Object3 = LoadModel("GameData/Global/Objects/Object3.obj");
        OmegaTechGameObjects.Object3Texture = LoadTexture("GameData/Global/Objects/Object3Texture.png");
        OmegaTechGameObjects.Object3Icon = LoadTexture("GameData/Global/Objects/Object3Icon.png");
        OmegaTechGameObjects.Object3.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = OmegaTechGameObjects.Object3Texture; 
        OmegaTechGameObjects.Object3Owned = false;
        OmegaTechGameObjects.Object3Name = WSplitValue(LoadFile("GameData/Global/Objects/Object3.info") , 0);
    }

    if (IsPathFile("GameData/Global/Objects/Object4.obj")){
        OmegaTechGameObjects.Object4 = LoadModel("GameData/Global/Objects/Object4.obj");
        OmegaTechGameObjects.Object4Texture = LoadTexture("GameData/Global/Objects/Object4Texture.png");
        OmegaTechGameObjects.Object4Icon = LoadTexture("GameData/Global/Objects/Object4Icon.png");
        OmegaTechGameObjects.Object4.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = OmegaTechGameObjects.Object4Texture; 
        OmegaTechGameObjects.Object4Owned = false;
        OmegaTechGameObjects.Object4Name = WSplitValue(LoadFile("GameData/Global/Objects/Object4.info") , 0);
    }

    if (IsPathFile("GameData/Global/Objects/Object5.obj")){
        OmegaTechGameObjects.Object5 = LoadModel("GameData/Global/Objects/Object5.obj");
        OmegaTechGameObjects.Object5Texture = LoadTexture("GameData/Global/Objects/Object5Texture.png");
        OmegaTechGameObjects.Object5Icon = LoadTexture("GameData/Global/Objects/Object5Icon.png");
        OmegaTechGameObjects.Object5.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = OmegaTechGameObjects.Object5Texture; 
        OmegaTechGameObjects.Object5Owned = false;
        OmegaTechGameObjects.Object5Name = WSplitValue(LoadFile("GameData/Global/Objects/Object5.info") , 0);
    }

    // Armory slots
    if (IsPathFile("GameData/Global/Objects/Armory1.obj")){
        OmegaTechGameObjects.Armory1 = LoadModel("GameData/Global/Objects/Armory1.obj");
        OmegaTechGameObjects.Armory1Texture = LoadTexture("GameData/Global/Objects/Armory1Texture.png");
        OmegaTechGameObjects.Armory1Icon = LoadTexture("GameData/Global/Objects/Armory1Icon.png");
        OmegaTechGameObjects.Armory1.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = OmegaTechGameObjects.Armory1Texture; 
        OmegaTechGameObjects.Armory1Owned = false;
        OmegaTechGameObjects.Armory1Name = WSplitValue(LoadFile("GameData/Global/Objects/Armory1.info") , 0);
    }

    if (IsPathFile("GameData/Global/Objects/Armory2.obj")){
        OmegaTechGameObjects.Armory2 = LoadModel("GameData/Global/Objects/Armory2.obj");
        OmegaTechGameObjects.Armory2Texture = LoadTexture("GameData/Global/Objects/Armory2Texture.png");
        OmegaTechGameObjects.Armory2Icon = LoadTexture("GameData/Global/Objects/Armory2Icon.png");
        OmegaTechGameObjects.Armory2.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = OmegaTechGameObjects.Armory2Texture; 
        OmegaTechGameObjects.Armory2Owned = false;
        OmegaTechGameObjects.Armory2Name = WSplitValue(LoadFile("GameData/Global/Objects/Armory2.info") , 0);
    }

    // Jewelry slots
    if (IsPathFile("GameData/Global/Objects/Jewelry1.obj")){
        OmegaTechGameObjects.Jewelry1 = LoadModel("GameData/Global/Objects/Jewelry1.obj");
        OmegaTechGameObjects.Jewelry1Texture = LoadTexture("GameData/Global/Objects/Jewelry1Texture.png");
        OmegaTechGameObjects.Jewelry1Icon = LoadTexture("GameData/Global/Objects/Jewelry1Icon.png");
        OmegaTechGameObjects.Jewelry1.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = OmegaTechGameObjects.Jewelry1Texture; 
        OmegaTechGameObjects.Jewelry1Owned = false;
        OmegaTechGameObjects.Jewelry1Name = WSplitValue(LoadFile("GameData/Global/Objects/Jewelry1.info") , 0);
    }

    if (IsPathFile("GameData/Global/Objects/Jewelry2.obj")){
        OmegaTechGameObjects.Jewelry2 = LoadModel("GameData/Global/Objects/Jewelry2.obj");
        OmegaTechGameObjects.Jewelry2Texture = LoadTexture("GameData/Global/Objects/Jewelry2Texture.png");
        OmegaTechGameObjects.Jewelry2Icon = LoadTexture("GameData/Global/Objects/Jewelry2Icon.png");
        OmegaTechGameObjects.Jewelry2.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = OmegaTechGameObjects.Jewelry2Texture; 
        OmegaTechGameObjects.Jewelry2Owned = false;
        OmegaTechGameObjects.Jewelry2Name = WSplitValue(LoadFile("GameData/Global/Objects/Jewelry2.info") , 0);
    }

    // RPG expansion equipment slots
    if (IsPathFile("GameData/Global/Objects/Helmet.obj")){
        OmegaTechGameObjects.Helmet = LoadModel("GameData/Global/Objects/Helmet.obj");
        OmegaTechGameObjects.HelmetTexture = LoadTexture("GameData/Global/Objects/HelmetTexture.png");
        OmegaTechGameObjects.HelmetIcon = LoadTexture("GameData/Global/Objects/HelmetIcon.png");
        OmegaTechGameObjects.Helmet.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = OmegaTechGameObjects.HelmetTexture; 
    }
    OmegaTechGameObjects.HelmetOwned = false;
    OmegaTechGameObjects.HelmetName = L"Helmet";

    if (IsPathFile("GameData/Global/Objects/Boots.obj")){
        OmegaTechGameObjects.Boots = LoadModel("GameData/Global/Objects/Boots.obj");
        OmegaTechGameObjects.BootsTexture = LoadTexture("GameData/Global/Objects/BootsTexture.png");
        OmegaTechGameObjects.BootsIcon = LoadTexture("GameData/Global/Objects/BootsIcon.png");
        OmegaTechGameObjects.Boots.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = OmegaTechGameObjects.BootsTexture; 
    }
    OmegaTechGameObjects.BootsOwned = false;
    OmegaTechGameObjects.BootsName = L"Boots";

    if (IsPathFile("GameData/Global/Objects/Legs.obj")){
        OmegaTechGameObjects.Legs = LoadModel("GameData/Global/Objects/Legs.obj");
        OmegaTechGameObjects.LegsTexture = LoadTexture("GameData/Global/Objects/LegsTexture.png");
        OmegaTechGameObjects.LegsIcon = LoadTexture("GameData/Global/Objects/LegsIcon.png");
        OmegaTechGameObjects.Legs.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = OmegaTechGameObjects.LegsTexture; 
    }
    OmegaTechGameObjects.LegsOwned = false;
    OmegaTechGameObjects.LegsName = L"Legs";

    if (IsPathFile("GameData/Global/Objects/Accessory1.obj")){
        OmegaTechGameObjects.Accessory1 = LoadModel("GameData/Global/Objects/Accessory1.obj");
        OmegaTechGameObjects.Accessory1Texture = LoadTexture("GameData/Global/Objects/Accessory1Texture.png");
        OmegaTechGameObjects.Accessory1Icon = LoadTexture("GameData/Global/Objects/Accessory1Icon.png");
        OmegaTechGameObjects.Accessory1.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = OmegaTechGameObjects.Accessory1Texture; 
    }
    OmegaTechGameObjects.Accessory1Owned = false;
    OmegaTechGameObjects.Accessory1Name = L"Accessory 1";

    if (IsPathFile("GameData/Global/Objects/Accessory2.obj")){
        OmegaTechGameObjects.Accessory2 = LoadModel("GameData/Global/Objects/Accessory2.obj");
        OmegaTechGameObjects.Accessory2Texture = LoadTexture("GameData/Global/Objects/Accessory2Texture.png");
        OmegaTechGameObjects.Accessory2Icon = LoadTexture("GameData/Global/Objects/Accessory2Icon.png");
        OmegaTechGameObjects.Accessory2.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = OmegaTechGameObjects.Accessory2Texture; 
    }
    OmegaTechGameObjects.Accessory2Owned = false;
    OmegaTechGameObjects.Accessory2Name = L"Accessory 2";

    // Load consumable item icons (PNG)
    auto loadIcon = [](const char* path) -> Texture2D {
        if (!IsPathFile(path)) {
            fprintf(stderr, "ICON missing: %s\n", path);
            return Texture2D{0};
        }
        Texture2D t = LoadTexture(path);
        if (t.id > 0) SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
        fprintf(stderr, "ICON %s -> id=%d %dx%d\n", path, t.id, t.width, t.height);
        return t;
    };
    OmegaTechGameObjects.HealthVialIcon    = loadIcon(ItemDB[0].iconPath);
    OmegaTechGameObjects.ManaVialIcon      = loadIcon(ItemDB[1].iconPath);
    OmegaTechGameObjects.EnergyCrystalIcon = loadIcon(ItemDB[2].iconPath);
    OmegaTechGameObjects.KeyIcon           = loadIcon(ItemDB[11].iconPath);
    OmegaTechGameObjects.CoinIcon          = loadIcon(ItemDB[12].iconPath);
    OmegaTechGameObjects.PowerupIcon       = loadIcon(ItemDB[13].iconPath);
}

static int SelectedObject = 1;

static void DrawHotbarSlot(int index, int x, int y, Texture2D* icon, const char* label) {
    Color slotColor = (icon && icon->id > 0) ? WHITE : (Color){60, 60, 60, 200};
    Color bgColor   = (icon && icon->id > 0) ? (Color){30, 30, 45, 220} : (Color){15, 15, 20, 180};

    DrawRectangle(x, y, 96, 96, bgColor);
    DrawRectangleLines(x, y, 96, 96, slotColor);

    if (SelectedObject == index + 1) {
        DrawRectangleLinesEx((Rectangle){(float)x-1, (float)y-1, 98, 98}, 3, (Color){255, 255, 0, 220});
    }

    if (icon && icon->id > 0) {
        float pad = 10.0f;
        float maxSide = 96.0f - pad * 2.0f;
        float scale = maxSide / (float)((icon->width > icon->height) ? icon->width : icon->height);
        float dw = icon->width * scale;
        float dh = icon->height * scale;
        DrawTextureEx(*icon, (Vector2){(float)x + (96.0f - dw) * 0.5f, (float)y + (96.0f - dh) * 0.5f - 4.0f}, 0, scale, WHITE);
    }

    if (label) {
        DrawText(label, x + 6, y + 96 - 16, 10, slotColor);
    }
}

void UpdateObjectBar(){
    int barX = 10;
    int barY = GetScreenHeight() - 5 - 96;
    int slotW = 96;
    int gap = 0;

    DrawTextureEx(OmegaTechGameObjects.ObjectBar, (Vector2){(float)barX, (float)barY}, 0, 3, WHITE);

    // Slots 1-5: Weapons
    auto drawWeaponSlot = [&](int i, bool owned, Texture2D& icon, const wstring& name) {
        Texture2D* ip = owned ? &icon : nullptr;
        const char* lbl = owned ? "Weapon" : nullptr;
        if (owned && !name.empty()) {
            static char buf[64];
            std::string n(name.begin(), name.end());
            snprintf(buf, sizeof(buf), "%.6s", n.c_str());
            lbl = buf;
        }
        DrawHotbarSlot(i, barX + slotW * i, barY, ip, lbl);
    };

    drawWeaponSlot(0, OmegaTechGameObjects.Object1Owned, OmegaTechGameObjects.Object1Icon, OmegaTechGameObjects.Object1Name);
    drawWeaponSlot(1, OmegaTechGameObjects.Object2Owned, OmegaTechGameObjects.Object2Icon, OmegaTechGameObjects.Object2Name);
    drawWeaponSlot(2, OmegaTechGameObjects.Object3Owned, OmegaTechGameObjects.Object3Icon, OmegaTechGameObjects.Object3Name);
    drawWeaponSlot(3, OmegaTechGameObjects.Object4Owned, OmegaTechGameObjects.Object4Icon, OmegaTechGameObjects.Object4Name);
    drawWeaponSlot(4, OmegaTechGameObjects.Object5Owned, OmegaTechGameObjects.Object5Icon, OmegaTechGameObjects.Object5Name);

    // Slots 6-8: Backpack consumables
    for (int si = 5; si < 8; si++) {
        int bpIdx = si - 5;
        int foundSlot = -1;
        int count = 0;
        // Find the bpIdx-th non-empty backpack slot
        for (int b = 0; b < BACKPACK_SLOTS; b++) {
            if (gInventory.backpack[b].itemId != -1) {
                if (count == bpIdx) { foundSlot = b; break; }
                count++;
            }
        }
        const char* lbl = nullptr;
        Texture2D* icon = nullptr;
        if (foundSlot >= 0) {
            int itemId = gInventory.backpack[foundSlot].itemId;
            const ItemDBEntry* def = GetItemDef(itemId);
            if (def) {
                // Find the icon texture
                Texture2D* icons[] = {
                    &OmegaTechGameObjects.HealthVialIcon,
                    &OmegaTechGameObjects.ManaVialIcon,
                    &OmegaTechGameObjects.EnergyCrystalIcon,
                    &OmegaTechGameObjects.KeyIcon,
                    &OmegaTechGameObjects.CoinIcon,
                    &OmegaTechGameObjects.PowerupIcon
                };
                int iconIdx = -1;
                switch (def->category) {
                    case ItemCategory::HEALTH_VIAL:    iconIdx = 0; break;
                    case ItemCategory::MANA_VIAL:      iconIdx = 1; break;
                    case ItemCategory::ENERGY_CRYSTAL: iconIdx = 2; break;
                    case ItemCategory::KEY:            iconIdx = 3; break;
                    case ItemCategory::COIN:           iconIdx = 4; break;
                    case ItemCategory::POWERUP:        iconIdx = 5; break;
                    default: break;
                }
                if (iconIdx >= 0) icon = icons[iconIdx];
                lbl = def->name;
            }
        }
        DrawHotbarSlot(si, barX + slotW * si, barY, icon, lbl);
    }

    // Input handling
    if (IsKeyPressed(KEY_LEFT)){
        if (SelectedObject != 1) SelectedObject --;
    }
    if (IsKeyPressed(KEY_RIGHT)){ 
        if (SelectedObject != 8) SelectedObject ++;
    }

    if (IsKeyPressed(KEY_ONE))   SelectedObject = 1;
    if (IsKeyPressed(KEY_TWO))   SelectedObject = 2;
    if (IsKeyPressed(KEY_THREE)) SelectedObject = 3;
    if (IsKeyPressed(KEY_FOUR))  SelectedObject = 4;
    if (IsKeyPressed(KEY_FIVE))  SelectedObject = 5;
    if (IsKeyPressed(KEY_SIX))   SelectedObject = 6;
    if (IsKeyPressed(KEY_SEVEN)) SelectedObject = 7;
    if (IsKeyPressed(KEY_EIGHT)) SelectedObject = 8;

    if (GetMouseWheelMove() != 0){
        if (GetMouseWheelMove() < 0){
            if (SelectedObject != 8)SelectedObject ++;
        }
        else {
            if (SelectedObject != 1)SelectedObject --;
        }
    }

    if (IsGamepadAvailable(0)){
        if (IsGamepadButtonPressed(0 , GAMEPAD_BUTTON_RIGHT_TRIGGER_1)){
            if (SelectedObject != 8) SelectedObject ++;
        }
        if (IsGamepadButtonPressed(0 , GAMEPAD_BUTTON_LEFT_TRIGGER_1)){
            if (SelectedObject != 1) SelectedObject --;
        }
    }

    // Use selected slot
    if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (SelectedObject >= 6 && SelectedObject <= 8) {
            int bpIdx = SelectedObject - 6;
            int count = 0;
            for (int b = 0; b < BACKPACK_SLOTS; b++) {
                if (gInventory.backpack[b].itemId != -1) {
                    if (count == bpIdx) { gInventory.UseItem(b); break; }
                    count++;
                }
            }
        }
    }
}