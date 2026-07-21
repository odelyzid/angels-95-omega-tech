#ifndef ENGINE_BILLBOARD_HPP
#define ENGINE_BILLBOARD_HPP

#include "raylib.h"
#include "raymath.h"
#include "../Package/OzAssetMapper.hpp"
#include <string>

// EngineBillboard - Helper class for drawing lit billboard sprites
// Used for displaying entity icons (Light, Sound, Music, etc.) in both client and editor

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

    static void Draw(Camera3D camera, const char* entityName, Vector3 position, float size = 1.0f) {
        Texture2D tex = AssetMapper::Instance().GetTexture(entityName);
        if (tex.id == 0) return;
        DrawBillboard(camera, tex, position, size, WHITE);
    }

    static void DrawLit(Camera3D camera, const char* entityName, Vector3 position, float size, Shader litShader) {
        Texture2D tex = AssetMapper::Instance().GetTexture(entityName);
        if (tex.id == 0) return;
        Mesh plane = GenMeshPlane(size, size, 1, 1);
        Model model = LoadModelFromMesh(plane);
        model.materials[0].shader = litShader;
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
        Vector3 cameraPos = camera.position;
        float yaw = atan2f(cameraPos.x - position.x, cameraPos.z - position.z) * RAD2DEG;
        DrawModelEx(model, position, {0, 1, 0}, yaw, {1, 1, 1}, WHITE);
        UnloadModel(model);
    }

    static void DrawLitTinted(Camera3D camera, const char* entityName, Vector3 position, float size, Shader litShader, Color tint) {
        Texture2D tex = AssetMapper::Instance().GetTexture(entityName);
        if (tex.id == 0) return;
        Mesh plane = GenMeshPlane(size, size, 1, 1);
        Model model = LoadModelFromMesh(plane);
        model.materials[0].shader = litShader;
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
        Vector3 cameraPos = camera.position;
        float yaw = atan2f(cameraPos.x - position.x, cameraPos.z - position.z) * RAD2DEG;
        DrawModelEx(model, position, {0, 1, 0}, yaw, {1, 1, 1}, tint);
        UnloadModel(model);
    }

    static void DrawPickup(Camera3D camera, const char* itemName, Vector3 position, float size = 1.2f) {
        Texture2D tex = AssetMapper::Instance().GetTexture(itemName);
        if (tex.id == 0) return;
        float bob = sinf((float)GetTime() * 3.0f) * 0.15f;
        Vector3 bobPos = {position.x, position.y + 0.9f + bob, position.z};
        DrawBillboardPro(camera, tex,
            (Rectangle){0, 0, (float)tex.width, (float)tex.height},
            bobPos, {0, 1, 0}, {size, size}, {0.5f, 0.5f}, 0, WHITE);
    }
};

#endif
