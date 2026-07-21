#include "Editor.hpp"
#include "raylib.h"

enum class PlaceMode { MODEL, PICKUP, NODE, ENV };
static PlaceMode g_placeMode = PlaceMode::MODEL;

// Forward declarations for overlay UI functions
static void DrawOverlay();
static void RenderPreview();
static void DrawEnvPanel();
static void DrawPickupPanel();
static void DrawNodePanel();

int main(int argc, char **argv){
    SetWindowState(FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "oz_editor");
    SetTargetFPS(60);
    GuiLoadStyleDark();

    if (argc > 1 && argv[1] != nullptr) {
        int pathLen = strlen(argv[1]);
        if (pathLen >= 10) {
            for (int i = 0; i <= pathLen - 10; i++)
                OTEditor.Path[i] = argv[1][i];
        }
    } else {
        const char* defaultPath = "GameData/";
        for (int i = 0; defaultPath[i] != '\0'; i++)
            OTEditor.Path[i] = defaultPath[i];
    }

    Init();
    CacheWDL();

    DisableCursor();

    int LastClickTime = 0;
    bool DoubleClick = false;

    while (!WindowShouldClose())
    {
        if (IsMouseButtonPressed(MOUSE_RIGHT_BUTTON))
        {
            if (LastClickTime != 0) DoubleClick = true;
            else LastClickTime = 1;
        }
        if (LastClickTime != 0){
            LastClickTime ++;
            if (LastClickTime == 30){ LastClickTime = 0; DoubleClick = false; }
        }

        for (int i = 0; i <= 7; i++){
            if(!OverlayEnabled && !IsMouseButtonDown(0) && !IsMouseButtonDown(1))
                UpdateCamera(&OTEditor.MainCamera, CAMERA_FIRST_PERSON);
        }
        
        BeginTextureMode(Target);
        ClearBackground(BLACK);

        BeginMode3D(OTEditor.MainCamera);

        DrawGrid(1000, 10.0f);

        CWDLProcess();
        WDLProcess();

        // --- Placement visuals ---
        if (OmegaTechEditor.DrawModel)
        {
            float px = OmegaTechEditor.X, py = OmegaTechEditor.Y, pz = OmegaTechEditor.Z;
            float ps = OmegaTechEditor.S, pr = OmegaTechEditor.R;

            if (g_placeMode == PlaceMode::MODEL) {
                if (EMID > 0 && EMID <= 20) {
                    switch (EMID) {
                        case 1:  DrawModelEx(WDLModels.Model1,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 2:  DrawModelEx(WDLModels.Model2,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 3:  DrawModelEx(WDLModels.Model3,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 4:  DrawModelEx(WDLModels.Model4,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 5:  DrawModelEx(WDLModels.Model5,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 6:  DrawModelEx(WDLModels.Model6,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 7:  DrawModelEx(WDLModels.Model7,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 8:  DrawModelEx(WDLModels.Model8,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 9:  DrawModelEx(WDLModels.Model9,  {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 10: DrawModelEx(WDLModels.Model10, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 11: DrawModelEx(WDLModels.Model11, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 12: DrawModelEx(WDLModels.Model12, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 13: DrawModelEx(WDLModels.Model13, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 14: DrawModelEx(WDLModels.Model14, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 15: DrawModelEx(WDLModels.Model15, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 16: DrawModelEx(WDLModels.Model16, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 17: DrawModelEx(WDLModels.Model17, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 18: DrawModelEx(WDLModels.Model18, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 19: DrawModelEx(WDLModels.Model19, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                        case 20: DrawModelEx(WDLModels.Model20, {px,py,pz},{0,pr,0},pr,{ps,ps,ps},WHITE); break;
                    }
                } else if (EMID == -1) {
                    DrawBoundingBox((BoundingBox){{px,py,pz},{OmegaTechEditor.W,OmegaTechEditor.H,OmegaTechEditor.L}}, ORANGE);
                    DrawCubeWires({OmegaTechEditor.W,OmegaTechEditor.H,OmegaTechEditor.L}, ps, ps, ps, PINK);
                } else if (EMID == -2) {
                    DrawBoundingBox((BoundingBox){{px,py,pz},{OmegaTechEditor.W,OmegaTechEditor.H-5,OmegaTechEditor.L}}, ORANGE);
                    DrawCubeWires({OmegaTechEditor.W,OmegaTechEditor.H-5,OmegaTechEditor.L}, ps, ps, ps, PINK);
                }
            } else if (g_placeMode == PlaceMode::PICKUP) {
                // Draw pickup preview as a colored cube
                Color c = GREEN;
                switch (OmegaTechEditor.ActivePickupType) {
                    case EditorPickupType::HEALTH_VIAL:    c = RED;  break;
                    case EditorPickupType::MANA_VIAL:      c = BLUE; break;
                    case EditorPickupType::ENERGY_CRYSTAL: c = PURPLE; break;
                    case EditorPickupType::KEY:            c = GOLD; break;
                    case EditorPickupType::COIN:           c = YELLOW; break;
                    case EditorPickupType::POWERUP:        c = MAGENTA; break;
                }
                DrawCube({px, py + 0.5f, pz}, 0.4f, 0.6f, 0.4f, c);
                DrawCubeWires({px, py + 0.5f, pz}, 0.4f, 0.6f, 0.4f, (Color){c.r,c.g,c.b,80});
            } else if (g_placeMode == PlaceMode::NODE) {
                Color c = BLUE;
                switch (OmegaTechEditor.ActiveNodeType) {
                    case EditorNodeType::SPAWN: c = BLUE;    break;
                    case EditorNodeType::NPC:   c = MAGENTA; break;
                    case EditorNodeType::LIGHT: c = YELLOW;  break;
                }
                DrawCube({px, py, pz}, 0.5f, 0.2f, 0.5f, c);
                DrawCubeWires({px, py, pz}, 0.5f, 0.2f, 0.5f, (Color){c.r,c.g,c.b,80});
            }

            DrawCubeWires({px, py, pz}, ps, ps, ps, (EMID <= 100 || EMID == -3) ? PINK : ORANGE);
            DrawLine3D({px, py, pz}, {px + ps*3, py, pz}, RED);
            DrawLine3D({px, py, pz}, {px, py + ps*3, pz}, BLUE);
            DrawLine3D({px, py, pz}, {px, py, pz + ps*3}, GREEN);

            // Movement controls
            if (!IsMouseButtonDown(2)) {
                if (IsKeyPressed(KEY_U)) OmegaTechEditor.X -= 2.0f;
                if (IsKeyPressed(KEY_J)) OmegaTechEditor.X += 2.0f;
                if (IsKeyPressed(KEY_H)) OmegaTechEditor.Z += 2.0f;
                if (IsKeyPressed(KEY_K)) OmegaTechEditor.Z -= 2.0f;
                if (IsKeyPressed(KEY_Y)) OmegaTechEditor.Y -= 2.0f;
                if (IsKeyPressed(KEY_I)) OmegaTechEditor.Y += 2.0f;
            } else {
                if (IsKeyPressed(KEY_U)) OmegaTechEditor.W -= 2.0f;
                if (IsKeyPressed(KEY_J)) OmegaTechEditor.W += 2.0f;
                if (IsKeyPressed(KEY_H)) OmegaTechEditor.L += 2.0f;
                if (IsKeyPressed(KEY_K)) OmegaTechEditor.L -= 2.0f;
                if (IsKeyPressed(KEY_Y)) OmegaTechEditor.H -= 2.0f;
                if (IsKeyPressed(KEY_I)) OmegaTechEditor.H += 2.0f;
            }

            if (IsKeyPressed(KEY_O)) OmegaTechEditor.R += 90.0f;
            if (IsKeyPressed(KEY_L)) OmegaTechEditor.R -= 90.0f;
            if (IsKeyDown(KEY_T)) OmegaTechEditor.S += 0.5f;
            if (IsKeyDown(KEY_G)) OmegaTechEditor.S -= 0.5f;

            // Commit placement
            if (IsKeyPressed(KEY_ENTER) || DoubleClick)
            {
                wstring WDLCommand;
                if (g_placeMode == PlaceMode::MODEL) {
                    if (CollisionToggle) WDLCommand += L":C:";
                    if (EMID > 0) {
                        if (EMID != 100) {
                            WDLCommand += (EMID < 100 ? L"Model" : L"Script") + to_wstring(EMID < 100 ? EMID : EMID - 100) + L":";
                        } else {
                            WDLCommand += L"Collision:";
                        }
                        WDLCommand += to_wstring(OmegaTechEditor.X) + L":" + to_wstring(OmegaTechEditor.Y) + L":" +
                                      to_wstring(OmegaTechEditor.Z) + L":" + to_wstring(OmegaTechEditor.S) + L":" +
                                      to_wstring(OmegaTechEditor.R) + L":";
                    } else {
                        if (EMID == -1) {
                            WDLCommand += L"AdvCollision:" +
                                to_wstring(OmegaTechEditor.X) + L":" + to_wstring(OmegaTechEditor.Y) + L":" +
                                to_wstring(OmegaTechEditor.Z) + L":" + to_wstring(OmegaTechEditor.S) + L":" +
                                to_wstring(OmegaTechEditor.R) + L":" + to_wstring(OmegaTechEditor.W) + L":" +
                                to_wstring(OmegaTechEditor.H) + L":" + to_wstring(OmegaTechEditor.L) + L":";
                        }
                        if (EMID == -2) {
                            WDLCommand += L"ClipBox:" +
                                to_wstring(OmegaTechEditor.X) + L":" + to_wstring(OmegaTechEditor.Y) + L":" +
                                to_wstring(OmegaTechEditor.Z) + L":" + to_wstring(OmegaTechEditor.S) + L":" +
                                to_wstring(OmegaTechEditor.R) + L":" + to_wstring(OmegaTechEditor.W) + L":" +
                                to_wstring(OmegaTechEditor.H) + L":" + to_wstring(OmegaTechEditor.L) + L":";
                        }
                    }
                } else if (g_placeMode == PlaceMode::PICKUP) {
                    WDLCommand += L"Pickup:" + to_wstring((int)OmegaTechEditor.ActivePickupType) + L":" +
                        to_wstring(OmegaTechEditor.X) + L":" + to_wstring(OmegaTechEditor.Y) + L":" +
                        to_wstring(OmegaTechEditor.Z) + L":" + to_wstring(OmegaTechEditor.S) + L":" +
                        to_wstring(OmegaTechEditor.R) + L":";
                } else if (g_placeMode == PlaceMode::NODE) {
                    wstring nodePrefix;
                    switch (OmegaTechEditor.ActiveNodeType) {
                        case EditorNodeType::SPAWN: nodePrefix = L"Spawn:";   break;
                        case EditorNodeType::NPC:   nodePrefix = L"NPC:";     break;
                        case EditorNodeType::LIGHT: nodePrefix = L"Light:";   break;
                    }
                    // Node format: type:subtype:x:y:z:0:0:
                    WDLCommand += nodePrefix + to_wstring((int)OmegaTechEditor.ActiveNodeType) + L":" +
                        to_wstring(OmegaTechEditor.X) + L":" + to_wstring(OmegaTechEditor.Y) + L":" +
                        to_wstring(OmegaTechEditor.Z) + L":0:0:";
                }

                wcout << WDLCommand << "\n";
                OTEditor.WorldData += WDLCommand;
                OmegaTechEditor.DrawModel = false;
                CacheWDL();
            }

            if (IsMouseButtonDown(0)) {
                OmegaTechEditor.X += GetMouseDelta().x / 8;
                OmegaTechEditor.Y += GetMouseDelta().y / 8;
                OmegaTechEditor.Z -= (GetMouseWheelMove() * 2);
            }
            if (IsMouseButtonDown(1)) {
                OmegaTechEditor.W += GetMouseDelta().x / 8;
                OmegaTechEditor.H += GetMouseDelta().y / 8;
                OmegaTechEditor.L -= (GetMouseWheelMove() * 2);
            }
        }

        // Terrain-following camera
        {
            float gy = SampleHeightmapGroundY(OTEditor.MainCamera.position.x, OTEditor.MainCamera.position.z);
            if (gy > -50000.0f)
                OTEditor.MainCamera.position.y = gy + 2.0f;
        }

        EndMode3D();
        EndTextureMode();

        BeginDrawing();
        DrawTexturePro(Target.texture, (Rectangle){0, 0, (float)Target.texture.width, -(float)Target.texture.height},
                       (Rectangle){0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()}, (Vector2){0,0}, 0, WHITE);
        DrawFPS(10, 10);
        DrawText(TextFormat("%.1f, %.1f, %.1f", OTEditor.MainCamera.position.x, OTEditor.MainCamera.position.y, OTEditor.MainCamera.position.z), 10, 40, 15, PURPLE);
        DrawText(TextFormat("Mode: %s  (1=MODEL 2=PICKUP 3=NODE 4=ENV)",
                g_placeMode == PlaceMode::MODEL ? "MODEL" :
                g_placeMode == PlaceMode::PICKUP ? "PICKUP" :
                g_placeMode == PlaceMode::NODE ? "NODE" : "ENV"), 10, 60, 15, WHITE);
        DrawText("oz_editor (TAB=Overlay H=Help)", 10, 80, 15, WHITE);

        if (OverlayEnabled) {
            DrawOverlay();
            if (!IsMouseButtonReleased(0) && GetCollision(0, 0, 144, 720, GetMouseX(), GetMouseY(), 5, 5))
                RenderPreview();
        }

        EndDrawing();

        // Mode switching
        if (IsKeyPressed(KEY_ONE))   g_placeMode = PlaceMode::MODEL;
        if (IsKeyPressed(KEY_TWO))   g_placeMode = PlaceMode::PICKUP;
        if (IsKeyPressed(KEY_THREE)) g_placeMode = PlaceMode::NODE;
        if (IsKeyPressed(KEY_FOUR))  { g_placeMode = PlaceMode::ENV; OTEditor.ShowEnvPanel = !OTEditor.ShowEnvPanel; }

        if (IsKeyPressed(KEY_F11)) ToggleFullscreen();

        if (IsKeyPressed(KEY_TAB)) {
            OverlayEnabled = !OverlayEnabled;
            if (OverlayEnabled) EnableCursor(); else DisableCursor();
        }
    }
    
    CloseWindow();
}

static void DrawEnvPanel() {
    if (!OTEditor.ShowEnvPanel) return;
    GuiWindowBox((Rectangle){288, 72, 400, 320}, "Environment Settings");

    GuiLabel((Rectangle){300, 100, 120, 20}, "Fog Color (R G B)");
    static float fogR = 200, fogG = 200, fogB = 210;
    fogR = GuiSliderBar((Rectangle){420, 100, 200, 20}, "R", nullptr, fogR, 0, 255);
    fogG = GuiSliderBar((Rectangle){420, 125, 200, 20}, "G", nullptr, fogG, 0, 255);
    fogB = GuiSliderBar((Rectangle){420, 150, 200, 20}, "B", nullptr, fogB, 0, 255);
    OTEditor.FogColor = (Color){(unsigned char)fogR, (unsigned char)fogG, (unsigned char)fogB, 255};

    GuiLabel((Rectangle){300, 180, 120, 20}, "Fog Density");
    OTEditor.FogDensity = GuiSliderBar((Rectangle){420, 180, 200, 20}, nullptr, nullptr, OTEditor.FogDensity, 0.0f, 0.2f);

    GuiLabel((Rectangle){300, 210, 120, 20}, "Ambient R G B");
    static float ambR = 180, ambG = 180, ambB = 200;
    ambR = GuiSliderBar((Rectangle){420, 210, 200, 20}, "R", nullptr, ambR, 0, 255);
    ambG = GuiSliderBar((Rectangle){420, 235, 200, 20}, "G", nullptr, ambG, 0, 255);
    ambB = GuiSliderBar((Rectangle){420, 260, 200, 20}, "B", nullptr, ambB, 0, 255);
    OTEditor.AmbientColor = (Color){(unsigned char)ambR, (unsigned char)ambG, (unsigned char)ambB, 255};

    GuiLabel((Rectangle){300, 290, 120, 20}, "Ambient Intensity");
    OTEditor.AmbientIntensity = GuiSliderBar((Rectangle){420, 290, 200, 20}, nullptr, nullptr, OTEditor.AmbientIntensity, 0.0f, 1.0f);

    if (GuiButton((Rectangle){300, 340, 100, 30}, "Apply Fog")) {
        wstring fogCmd = L"Fog:" + to_wstring(OTEditor.FogColor.r) + L":" +
                         to_wstring(OTEditor.FogColor.g) + L":" +
                         to_wstring(OTEditor.FogColor.b) + L":" +
                         to_wstring(OTEditor.FogDensity) + L":0:";
        OTEditor.WorldData += fogCmd;
        CacheWDL();
    }
    if (GuiButton((Rectangle){420, 340, 120, 30}, "Apply Ambient")) {
        wstring ambCmd = L"Ambient:" + to_wstring(OTEditor.AmbientColor.r) + L":" +
                         to_wstring(OTEditor.AmbientColor.g) + L":" +
                         to_wstring(OTEditor.AmbientColor.b) + L":" +
                         to_wstring(OTEditor.AmbientIntensity) + L":0:";
        OTEditor.WorldData += ambCmd;
        CacheWDL();
    }
}

static void DrawPickupPanel() {
    GuiWindowBox((Rectangle){144, 400, 180, 240}, "Pickups");
    if (GuiButton((Rectangle){152, 424, 160, 24}, "Health Vial")) {
        g_placeMode = PlaceMode::PICKUP;
        OmegaTechEditor.ActivePickupType = EditorPickupType::HEALTH_VIAL;
        OmegaTechEditor.DrawModel = true;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    if (GuiButton((Rectangle){152, 452, 160, 24}, "Mana Vial")) {
        g_placeMode = PlaceMode::PICKUP;
        OmegaTechEditor.ActivePickupType = EditorPickupType::MANA_VIAL;
        OmegaTechEditor.DrawModel = true;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    if (GuiButton((Rectangle){152, 480, 160, 24}, "Energy Crystal")) {
        g_placeMode = PlaceMode::PICKUP;
        OmegaTechEditor.ActivePickupType = EditorPickupType::ENERGY_CRYSTAL;
        OmegaTechEditor.DrawModel = true;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    if (GuiButton((Rectangle){152, 508, 160, 24}, "Key")) {
        g_placeMode = PlaceMode::PICKUP;
        OmegaTechEditor.ActivePickupType = EditorPickupType::KEY;
        OmegaTechEditor.DrawModel = true;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    if (GuiButton((Rectangle){152, 536, 160, 24}, "Coin")) {
        g_placeMode = PlaceMode::PICKUP;
        OmegaTechEditor.ActivePickupType = EditorPickupType::COIN;
        OmegaTechEditor.DrawModel = true;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    if (GuiButton((Rectangle){152, 564, 160, 24}, "Powerup")) {
        g_placeMode = PlaceMode::PICKUP;
        OmegaTechEditor.ActivePickupType = EditorPickupType::POWERUP;
        OmegaTechEditor.DrawModel = true;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
}

static void DrawNodePanel() {
    GuiWindowBox((Rectangle){144, 120, 180, 160}, "Pawn Nodes");
    if (GuiButton((Rectangle){152, 144, 160, 24}, "Player Spawn")) {
        g_placeMode = PlaceMode::NODE;
        OmegaTechEditor.ActiveNodeType = EditorNodeType::SPAWN;
        OmegaTechEditor.DrawModel = true;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    if (GuiButton((Rectangle){152, 172, 160, 24}, "NPC Spawn")) {
        g_placeMode = PlaceMode::NODE;
        OmegaTechEditor.ActiveNodeType = EditorNodeType::NPC;
        OmegaTechEditor.DrawModel = true;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    if (GuiButton((Rectangle){152, 200, 160, 24}, "Point Light")) {
        g_placeMode = PlaceMode::NODE;
        OmegaTechEditor.ActiveNodeType = EditorNodeType::LIGHT;
        OmegaTechEditor.DrawModel = true;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
}

// Help text boxes
bool TextmultiBox005EditMode = false;
char TextmultiBox005Text[128] = "Tab - Opens Overlay  1/2/3/4 - Switch Mode  Shift - Places Last Item";
bool TextmultiBox006EditMode = false;
char TextmultiBox006Text[128] = "U/J - X   H/K - Z   Y/I - Y   Hold MMB - Scale W/H/L";
bool TextmultiBox007EditMode = false;
char TextmultiBox007Text[128] = "Hold RMB - Move X/Y   Scroll - Z  T/G - Scale   O/L - Rotate";
bool TextmultiBox008EditMode = false;
char TextmultiBox008Text[128] = "Hold LMB - Resize W/H   Scroll - L   Enter/DblClick - Place";

void DrawOverlay(){
    if (IsKeyDown(KEY_H)){
        GuiWindowBox((Rectangle){288, 72, 672, 384}, "Help");
        GuiLabel((Rectangle){312, 112, 152, 24}, "oz_editor v.1.0");
        GuiLine((Rectangle){312, 136, 624, 24}, NULL);
        GuiLabel((Rectangle){312, 160, 120, 24}, "Editor Controls");
        if (GuiTextBox((Rectangle){320, 192, 184, 112}, TextmultiBox005Text, 128, TextmultiBox005EditMode)) TextmultiBox005EditMode = !TextmultiBox005EditMode;
        if (GuiTextBox((Rectangle){320, 304, 192, 88}, TextmultiBox006Text, 128, TextmultiBox006EditMode)) TextmultiBox006EditMode = !TextmultiBox006EditMode;
        if (GuiTextBox((Rectangle){520, 192, 168, 112}, TextmultiBox007Text, 128, TextmultiBox007EditMode)) TextmultiBox007EditMode = !TextmultiBox007EditMode;
        if (GuiTextBox((Rectangle){520, 304, 168, 88}, TextmultiBox008Text, 128, TextmultiBox008EditMode)) TextmultiBox008EditMode = !TextmultiBox008EditMode;
        GuiLabel((Rectangle){696, 240, 120, 24}, "Models - Scripts - Pickups");
        GuiLabel((Rectangle){696, 336, 120, 24}, "Nodes - Collisions - Env");
    }

    GuiWindowBox((Rectangle){0, 0, 144, 720}, "Model Menu");
    if (GuiButton((Rectangle){8, 32, 120, 24}, "Model 1")){EMID = 1; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 64, 120, 24}, "Model 2")){EMID = 2; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 96, 120, 24}, "Model 3")){EMID = 3; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 128, 120, 24}, "Model 4")){EMID = 4; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 160, 120, 24}, "Model 5")){EMID = 5; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 192, 120, 24}, "Model 6")){EMID = 6; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 224, 120, 24}, "Model 7")){EMID = 7; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 256, 120, 24}, "Model 8")){EMID = 8; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 288, 120, 24}, "Model 9")){EMID = 9; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 320, 120, 24}, "Model 10")){EMID = 10; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 352, 120, 24}, "Model 11")){EMID = 11; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 384, 120, 24}, "Model 12")){EMID = 12; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 416, 120, 24}, "Model 13")){EMID = 13; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 448, 120, 24}, "Model 14")){EMID = 14; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 480, 120, 24}, "Model 15")){EMID = 15; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 512, 120, 24}, "Model 16")){EMID = 16; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 544, 120, 24}, "Model 17")){EMID = 17; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 576, 120, 24}, "Model 18")){EMID = 18; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 608, 120, 24}, "Model 19")){EMID = 19; g_placeMode = PlaceMode::MODEL;}
    if (GuiButton((Rectangle){8, 640, 120, 24}, "Model 20")){EMID = 20; g_placeMode = PlaceMode::MODEL;}
    GuiLine((Rectangle){8, 664, 120, 24}, NULL);

    GuiWindowBox((Rectangle){144, 624, 532, 96}, "Actions");
    if (GuiButton((Rectangle){152, 672, 80, 32}, "Undo")){
        OTEditor.WorldData = LoadFile(TextFormat("%s/World.wdl", OTEditor.Path));
        CacheWDL();
    }
    if (GuiButton((Rectangle){240, 672, 64, 32}, "Save")){
        wofstream Outfile;
        Outfile.open(TextFormat("%s/World.wdl", OTEditor.Path));
        Outfile << OTEditor.WorldData << "\n";
    }
    if (GuiButton((Rectangle){336, 672, 112, 32}, "Reset Camera")) OTEditor.MainCamera.position = {0, 10, 0};
    if (GuiButton((Rectangle){456, 672, 40, 32}, "UP")) OTEditor.MainCamera.position.y += 2;
    if (GuiButton((Rectangle){496, 672, 48, 32}, "DOWN")) OTEditor.MainCamera.position.y -= 2;
    if (GuiButton((Rectangle){552, 672, 116, 32}, "Env Settings")) {
        OTEditor.ShowEnvPanel = !OTEditor.ShowEnvPanel;
        g_placeMode = PlaceMode::ENV;
    }

    GuiWindowBox((Rectangle){912 + 184, 232, 184, 304}, "Scripts");
    if (GuiButton((Rectangle){944+184, 264, 120, 24}, "Script 1")){EMID = 101; LoadEditor(TextFormat("%s/Scripts/Script1.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 288, 120, 24}, "Script 2")){EMID = 102; LoadEditor(TextFormat("%s/Scripts/Script2.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 312, 120, 24}, "Script 3")){EMID = 103; LoadEditor(TextFormat("%s/Scripts/Script3.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 336, 120, 24}, "Script 4")){EMID = 104; LoadEditor(TextFormat("%s/Scripts/Script4.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 360, 120, 24}, "Script 5")){EMID = 105; LoadEditor(TextFormat("%s/Scripts/Script5.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 384, 120, 24}, "Script 6")){EMID = 106; LoadEditor(TextFormat("%s/Scripts/Script6.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 408, 120, 24}, "Script 7")){EMID = 107; LoadEditor(TextFormat("%s/Scripts/Script7.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 432, 120, 24}, "Script 8")){EMID = 108; LoadEditor(TextFormat("%s/Scripts/Script8.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 456, 120, 24}, "Script 9")){EMID = 109; LoadEditor(TextFormat("%s/Scripts/Script9.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 480, 120, 24}, "Script 10")){EMID = 110; LoadEditor(TextFormat("%s/Scripts/Script10.ps", OTEditor.Path));}
    if (GuiButton((Rectangle){944+184, 504, 120, 24}, "Launch Script")) LoadEditor(TextFormat("%s/Scripts/Launch.ps", OTEditor.Path));

    if(GetCollision(912+184, 232, 184, 304, GetMouseX(), GetMouseY(), 5, 5)){
        GuiWindowBox((Rectangle){600, 50, 352, 500}, "Script Preview");
        GuiTextBox((Rectangle){600, 70, 352, 480}, ScriptEditorBuffer, sizeof(ScriptEditorBuffer), 1);
    }

    GuiWindowBox((Rectangle){912+184, 24, 184, 192}, "Collisions");
    if (GuiButton((Rectangle){944+184, 56, 120, 24}, "Box Collision")){
        EMID = 100; g_placeMode = PlaceMode::MODEL;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    if (GuiButton((Rectangle){944+184, 88, 120, 24}, "Adv Collision")){
        EMID = -1; g_placeMode = PlaceMode::MODEL;
        OmegaTechEditor.W = OTEditor.MainCamera.position.x + 5;
        OmegaTechEditor.H = OTEditor.MainCamera.position.y + 5;
        OmegaTechEditor.L = OTEditor.MainCamera.position.z + 5;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    if (GuiButton((Rectangle){944+184, 120, 120, 24}, "Height Clip Box")){
        EMID = -2; g_placeMode = PlaceMode::MODEL;
        OmegaTechEditor.W = OTEditor.MainCamera.position.x + 5;
        OmegaTechEditor.H = OTEditor.MainCamera.position.y + 5;
        OmegaTechEditor.L = OTEditor.MainCamera.position.z + 5;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    CollisionToggle = GuiToggle((Rectangle){920+184, 176, 160, 32}, "Collision", CollisionToggle);

    // Draw extra panels
    DrawPickupPanel();
    DrawNodePanel();
    DrawEnvPanel();

    // Click handler for model/pickup/node panels
    if (IsMouseButtonPressed(0) && (
        GetCollision(0, 0, 144, 720, GetMouseX(), GetMouseY(), 5, 5) ||
        GetCollision(912+184, 232, 184, 304, GetMouseX(), GetMouseY(), 5, 5)))
    {
        OmegaTechEditor.DrawModel = true;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
    if (IsMouseButtonPressed(0) && GetCollision(912+184, 24, 184, 192, GetMouseX(), GetMouseY(), 5, 5)){
        OmegaTechEditor.DrawModel = true;
        OmegaTechEditor.W = OTEditor.MainCamera.position.x + 5;
        OmegaTechEditor.H = OTEditor.MainCamera.position.y + 5;
        OmegaTechEditor.L = OTEditor.MainCamera.position.z + 5;
        OmegaTechEditor.X = OTEditor.MainCamera.position.x;
        OmegaTechEditor.Y = OTEditor.MainCamera.position.y;
        OmegaTechEditor.Z = OTEditor.MainCamera.position.z;
        OmegaTechEditor.R = 1;
    }
}

int PreviewEMID = 0;

void RenderPreview(){
    DrawLine(288, 200, GetMouseX(), GetMouseY(), WHITE);
    BeginTextureMode(PreviewTarget);
    ClearBackground(BLACK);
    UpdateCamera(&OTEditor.PreviewCamera, CAMERA_ORBITAL);
    BeginMode3D(OTEditor.PreviewCamera);

    DrawGrid(10, 1.0f);

    PreviewEMID = int((GetMouseY() - 32) / 32);
    switch (PreviewEMID){
        case 1:  CurrentModel = WDLModels.Model1;  break;
        case 2:  CurrentModel = WDLModels.Model2;  break;
        case 3:  CurrentModel = WDLModels.Model3;  break;
        case 4:  CurrentModel = WDLModels.Model4;  break;
        case 5:  CurrentModel = WDLModels.Model5;  break;
        case 6:  CurrentModel = WDLModels.Model6;  break;
        case 7:  CurrentModel = WDLModels.Model7;  break;
        case 8:  CurrentModel = WDLModels.Model8;  break;
        case 9:  CurrentModel = WDLModels.Model9;  break;
        case 10: CurrentModel = WDLModels.Model10; break;
        case 11: CurrentModel = WDLModels.Model11; break;
        case 12: CurrentModel = WDLModels.Model12; break;
        case 13: CurrentModel = WDLModels.Model13; break;
        case 14: CurrentModel = WDLModels.Model14; break;
        case 15: CurrentModel = WDLModels.Model15; break;
        case 16: CurrentModel = WDLModels.Model16; break;
        case 17: CurrentModel = WDLModels.Model17; break;
        case 18: CurrentModel = WDLModels.Model18; break;
        case 19: CurrentModel = WDLModels.Model19; break;
        case 20: CurrentModel = WDLModels.Model20; break;
        default: break;
    }
    DrawModelEx(CurrentModel, {0,0,0}, {0,0,0}, 0, {1,1,1}, WHITE);
    DrawLine3D({0,0,0}, {3,0,0}, RED);
    DrawLine3D({0,0,0}, {0,3,0}, BLUE);
    DrawLine3D({0,0,0}, {0,0,3}, GREEN);

    EndMode3D();
    EndTextureMode();
    DrawTextureRec(PreviewTarget.texture, {0, 0, 256, -256}, {288, 200}, WHITE);
    DrawRectangleLines(288, 200, 256, 256, WHITE);
    if(CurrentModel.meshes != NULL){
        DrawText(TextFormat("Triangles:%i", CurrentModel.meshes[0].triangleCount), 288, 456, 15, WHITE);
        DrawText(TextFormat("Model ID:%i", PreviewEMID), 288, 471, 15, WHITE);
        int tc = CurrentModel.meshes[0].triangleCount;
        DrawText(tc > 2000 ? "Very Low Perf" : tc > 1000 ? "Low Perf" : tc > 500 ? "Med Perf" : tc > 100 ? "High Perf" : "Very High Perf",
                 288, 486, 15, tc > 2000 ? DARKBLUE : tc > 1000 ? BLUE : tc > 500 ? ORANGE : tc > 100 ? RED : PINK);
    }
}
