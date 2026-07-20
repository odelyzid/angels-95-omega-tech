#include "PPGIO.hpp"

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
}

static int SelectedObject = 1;

void UpdateObjectBar(){

    DrawTextureEx(OmegaTechGameObjects.ObjectBar, {10, GetScreenHeight() - 5 - (32 * 3)} , 0 , 3, WHITE);

    DrawRectangleLines(10 + 2 + ((SelectedObject - 1) * 96) - ((SelectedObject * 3)) ,  GetScreenHeight() - 5 - (32 * 3) , 32*3, 32*3, WHITE);

    if (OmegaTechGameObjects.Object1Owned){
        DrawTextureEx(OmegaTechGameObjects.Object1Icon, {10, GetScreenHeight() - 5 - (32 * 3)} , 0 , 3, WHITE);
        if (SelectedObject == 1)DrawTextEx(OmegaTechGameObjects.BarFont, TextFormat("%ls" , OmegaTechGameObjects.Object1Name.c_str() ),{ 10, (GetScreenHeight() - 5 - (32 * 3)) - 20 } , 20 , 1, WHITE);
    }

    if (OmegaTechGameObjects.Object2Owned){
        DrawTextureEx(OmegaTechGameObjects.Object2Icon, {10 + (32 * 3), GetScreenHeight() - 5 - (32 * 3)} , 0 , 3, WHITE);
        if (SelectedObject == 2)DrawTextEx(OmegaTechGameObjects.BarFont, TextFormat("%ls" , OmegaTechGameObjects.Object2Name.c_str() ),{ 10 + 96, (GetScreenHeight() - 5 - (32 * 3)) - 20 } , 20 , 1, WHITE);
    }

    if (OmegaTechGameObjects.Object3Owned){
        DrawTextureEx(OmegaTechGameObjects.Object3Icon, {10 + 96 * 2, GetScreenHeight() - 5 - (32 * 3)} , 0 , 3, WHITE);
        if (SelectedObject == 3)DrawTextEx(OmegaTechGameObjects.BarFont, TextFormat("%ls" , OmegaTechGameObjects.Object3Name.c_str() ),{ 10 + 96 * 2, (GetScreenHeight() - 5 - (32 * 3)) - 20 } , 20 , 1, WHITE);
    }

    if (OmegaTechGameObjects.Object4Owned){
        DrawTextureEx(OmegaTechGameObjects.Object4Icon, {10+ 96 * 3, GetScreenHeight() - 5 - (32 * 3)} , 0 , 3, WHITE);
        if (SelectedObject == 4)DrawTextEx(OmegaTechGameObjects.BarFont, TextFormat("%ls" , OmegaTechGameObjects.Object4Name.c_str() ),{ 10 + 96 * 3, (GetScreenHeight() - 5 - (32 * 3)) - 20 } , 20 , 1, WHITE);
    }

    if (OmegaTechGameObjects.Object5Owned){
        DrawTextureEx(OmegaTechGameObjects.Object5Icon, {10+ 96 * 4, GetScreenHeight() - 5 - (32 * 3)} , 0 , 3, WHITE);
        if (SelectedObject == 5)DrawTextEx(OmegaTechGameObjects.BarFont, TextFormat("%ls" , OmegaTechGameObjects.Object5Name.c_str() ),{ 10 + 96 * 4, (GetScreenHeight() - 5 - (32 * 3)) - 20 } , 20 , 1, WHITE);
    }

    if (IsKeyPressed(KEY_LEFT)){
        if (SelectedObject != 1) SelectedObject --;
    }
    if (IsKeyPressed(KEY_RIGHT)){ 
        if (SelectedObject != 8) SelectedObject ++;
    }

    // Number keys 1-8 for direct slot selection
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

}