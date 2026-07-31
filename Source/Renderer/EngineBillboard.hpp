#ifndef ENGINE_BILLBOARD_HPP
#define ENGINE_BILLBOARD_HPP

#include "raylib.h"
#include "raymath.h"
#include "../Package/OzAssetMapper.hpp"
#include <string>

// EngineBillboard - Helper class for drawing billboard sprites with optional shader support
// Used for displaying entity icons (Light, Sound, Music, NPCs, pickups, etc.)
// When a shader is provided, renders via a mesh plane model so the shader applies.

class EngineBillboard {
public:
    static void Init() {
        AssetMapper::Instance().PreloadCategory("engine");
        AssetMapper::Instance().PreloadCategory("items");
    }

    static void Shutdown() {
        AssetMapper::Instance().UnloadCategory("engine");
        AssetMapper::Instance().UnloadCategory("items");
    }

    // Draw a billboard at position, optionally with a lit shader.
    // If litShader.id != 0 uses a mesh model path so lighting/fog applies.
    static void Draw(Camera3D camera, const char* entityName, Vector3 position,
                     float size = 1.0f, Shader litShader = {0}) {
        Texture2D tex = AssetMapper::Instance().GetTexture(entityName);
        if (tex.id == 0) return;

        if (litShader.id > 0) {
            // Use mesh model so the shader (lighting + fog) applies
            Mesh plane = GenMeshPlane(size, size, 1, 1);
            Model model = LoadModelFromMesh(plane);
            model.materials[0].shader = litShader;
            model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
            Vector3 cameraPos = camera.position;
            float yaw = atan2f(cameraPos.x - position.x, cameraPos.z - position.z) * RAD2DEG;
            DrawModelEx(model, position, {0, 1, 0}, yaw, {1, 1, 1}, WHITE);
            UnloadModel(model);
        } else {
            // Fast path — no shader needed
            DrawBillboard(camera, tex, position, size, WHITE);
        }
    }

    // Draw a billboard with a tint color, optionally with a lit shader.
    static void DrawTinted(Camera3D camera, const char* entityName, Vector3 position,
                           float size, Color tint, Shader litShader = {0}) {
        Texture2D tex = AssetMapper::Instance().GetTexture(entityName);
        if (tex.id == 0) return;

        if (litShader.id > 0) {
            Mesh plane = GenMeshPlane(size, size, 1, 1);
            Model model = LoadModelFromMesh(plane);
            model.materials[0].shader = litShader;
            model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
            Vector3 cameraPos = camera.position;
            float yaw = atan2f(cameraPos.x - position.x, cameraPos.z - position.z) * RAD2DEG;
            DrawModelEx(model, position, {0, 1, 0}, yaw, {1, 1, 1}, tint);
            UnloadModel(model);
        } else {
            DrawBillboard(camera, tex, position, size, tint);
        }
    }

    // Draw a pickup billboard (floating, bobbing), optionally with a lit shader.
    static void DrawPickup(Camera3D camera, const char* itemName, Vector3 position,
                           float size = 1.2f, Shader litShader = {0}) {
        Texture2D tex = AssetMapper::Instance().GetTexture(itemName);
        if (tex.id == 0) return;

        float bob = sinf((float)GetTime() * 3.0f) * 0.15f;
        Vector3 bobPos = {position.x, position.y + 0.9f + bob, position.z};

        if (litShader.id > 0) {
            Mesh plane = GenMeshPlane(size, size, 1, 1);
            Model model = LoadModelFromMesh(plane);
            model.materials[0].shader = litShader;
            model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
            Vector3 cameraPos = camera.position;
            float yaw = atan2f(cameraPos.x - bobPos.x, cameraPos.z - bobPos.z) * RAD2DEG;
            DrawModelEx(model, bobPos, {0, 1, 0}, yaw, {1, 1, 1}, WHITE);
            UnloadModel(model);
        } else {
            DrawBillboardPro(camera, tex,
                (Rectangle){0, 0, (float)tex.width, (float)tex.height},
                bobPos, {0, 1, 0}, {size, size}, {0.5f, 0.5f}, 0, WHITE);
        }
    }
};

#endif
