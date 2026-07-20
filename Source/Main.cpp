#include "Core.hpp"
#include "Log.hpp"
#include "Client/Client.hpp"
#include <cmath>
#include <algorithm>

static OmegaClient g_client;
static bool g_network_enabled = false;

int main(){
    SetConfigFlags(FLAG_VSYNC_HINT);

    InitWindow(1280 , 720 , "Project Omega");
    SetTargetFPS(60);

    InitAudioDevice();

    if (!IsAudioDeviceReady()){
        CloseAudioDevice();
    }

    OmegaTechInit();
    PlayHomeScreen();
    
    LoadWorld();

    // Try connecting to dedicated server (hardcoded for now)
    g_client.set_on_chat_received([](const std::string& msg) {
        OmegaTechTextSystem.Write(msg);
    });

    g_network_enabled = g_client.connect("127.0.0.1", 27015);
    if (g_network_enabled) {
        OZ_INFO("Network: connected to 127.0.0.1:27015");
    }

    HideCursor(); 
    DisableCursor();  
    
    while (!WindowShouldClose())
    {
        OmegaPlayer.OldX = OmegaTechData.MainCamera.position.x;
        OmegaPlayer.OldY = OmegaTechData.MainCamera.position.y;
        OmegaPlayer.OldZ = OmegaTechData.MainCamera.position.z;

        if (!OmegaInputController.InteractDown && !ShowSettings){
            for (int i = 0 ; i <= OmegaTechData.CameraSpeed; i ++){
                UpdateCamera(&OmegaTechData.MainCamera, CAMERA_FIRST_PERSON);
            }
        }

        OmegaInputController.UpdateInputs();
        UpdateLightSources();

        if (Direction == 1)
        {
            R += 1;
            G += 1;
            B += 1;

            if (R == 254)
            {
                FadeDone = true;
                Direction = 3;
            }
        }

        FadeColor = (Color){R, G, B, 255};
    

        DrawWorld();
        

        BeginDrawing();  

        ClearBackground(BLACK);

        DrawTexturePro(Target.texture, (Rectangle){ 0, 0, Target.texture.width, -Target.texture.height }, (Rectangle){ 0, 0, float(GetScreenWidth()), float(GetScreenHeight())}, (Vector2){ 0, 0 } , 0.f , WHITE);
        UpdateObjectBar();
        EndShaderMode();

        if (ParticlesEnabled){
            OmegaTechData.RainParticles.Update(0,0);
            OmegaTechData.RainParticles.TriggerEffect({0,0} , RainEffect);
        }

        if (FPSEnabled){
            DrawFPS(0,0);
        }

        UpdateSettings();


        OmegaTechTextSystem.Update();

        // Network client update
        if (g_network_enabled) {
            Camera3D& cam = OmegaTechData.MainCamera;
            g_client.update(cam.position.x, cam.position.y, cam.position.z,
                           0.0f, 0.0f);

            if (g_client.is_connected()) {
                // Sync XP/level from server to local player
                OmegaPlayer.Level = g_client.get_level();
                OmegaPlayer.XP = g_client.get_xp();
                OmegaPlayer.XPToNext = g_client.get_xp_to_next();

                // Copy server NPCs into OmegaEnemy array for rendering
                const auto& npcs = g_client.npcs();
                int limit = std::min((int)npcs.size(), EntityCount);
                for (int i = 0; i < limit; i++) {
                    OmegaEnemy[i].X = npcs[i].position.x;
                    OmegaEnemy[i].Y = npcs[i].position.y;
                    OmegaEnemy[i].Z = npcs[i].position.z;
                    OmegaEnemy[i].IsActive = npcs[i].active;
                }

                // Pickup collection check (press E near a pickup)
                if (IsKeyPressed(KEY_E)) {
                    const auto& pickups = g_client.pickups();
                    Vector3 cp = OmegaTechData.MainCamera.position;
                    float nearest_dist = 5.0f;
                    int nearest_pickup = -1;
                    int nearest_world = 0;
                    for (const auto& p : pickups) {
                        if (!p.active) continue;
                        float dx = p.position.x - cp.x;
                        float dz = p.position.z - cp.z;
                        float dist = sqrtf(dx*dx + dz*dz);
                        if (dist < nearest_dist) {
                            nearest_dist = dist;
                            nearest_pickup = p.id;
                            nearest_world = p.world_index;
                        }
                    }
                    if (nearest_pickup >= 0) {
                        g_client.send_pickup_collect(nearest_pickup, nearest_world);
                    }
                }
            }

            // HUD: connection + stats
            if (g_client.is_connected()) {
                DrawText(TextFormat("Ping:%d | Lv.%d XP:%d/%d",
                          g_client.get_ping_ms(),
                          g_client.get_level(), g_client.get_xp(),
                          g_client.get_xp_to_next()),
                         10, 10, 16, GREEN);

                // XP bar
                if (g_client.get_xp_to_next() > 0) {
                    float xp_pct = (float)g_client.get_xp() / g_client.get_xp_to_next();
                    DrawRectangle(10, 30, 256, 8, DARKGRAY);
                    DrawRectangle(10, 30, (int)(xp_pct * 256), 8, SKYBLUE);
                }

                // Health bar
                DrawRectangle(10, GetScreenHeight() - 40, 200, 12, DARKGRAY);
                DrawRectangle(10, GetScreenHeight() - 40,
                              (int)((OmegaPlayer.Health / 100.0f) * 200), 12, RED);
            } else {
                DrawText("Disconnected", 10, 10, 16, RED);
            }
        }

        EndDrawing();

        if (IsKeyPressed(KEY_F11))ToggleFullscreen();

    }
    
    g_client.disconnect();
    CloseWindow();
}