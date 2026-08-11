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
private:
    inline static Model s_BillboardModel{0};

public:
    static void Init() {
        AssetMapper::Instance().PreloadCategory("engine");
        AssetMapper::Instance().PreloadCategory("items");
        Mesh plane = GenMeshPlane(1.0f, 1.0f, 1, 1);
        s_BillboardModel = LoadModelFromMesh(plane);
    }

    static void Shutdown() {
        AssetMapper::Instance().UnloadCategory("engine");
        AssetMapper::Instance().UnloadCategory("items");
        if (s_BillboardModel.meshCount > 0)
            UnloadModel(s_BillboardModel);
    }

    // Draw a shader-lit billboard using the cached model.
    // For use when you already have a Texture2D handle.
    static void DrawSprite(Camera3D camera, Texture2D tex, Vector3 position,
                           float size, Color tint, Shader shader) {
        if (s_BillboardModel.meshCount == 0) return;
        s_BillboardModel.materials[0].shader = shader;
        s_BillboardModel.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
        Vector3 cp = camera.position;
        float yaw = atan2f(cp.x - position.x, cp.z - position.z) * RAD2DEG;
        DrawModelEx(s_BillboardModel, position, {0, 1, 0}, yaw, {size, size, size}, tint);
    }

    // Draw a billboard at position, optionally with a lit shader.
    // If litShader.id != 0 uses the cached mesh model so lighting/fog applies.
    static void Draw(Camera3D camera, const char* entityName, Vector3 position,
                     float size = 1.0f, Shader litShader = {0}) {
        Texture2D tex = AssetMapper::Instance().GetTexture(entityName);
        if (tex.id == 0) return;

        if (litShader.id > 0) {
            DrawSprite(camera, tex, position, size, WHITE, litShader);
        } else {
            DrawBillboard(camera, tex, position, size, WHITE);
        }
    }

    // Draw a billboard with a tint color, optionally with a lit shader.
    static void DrawTinted(Camera3D camera, const char* entityName, Vector3 position,
                           float size, Color tint, Shader litShader = {0}) {
        Texture2D tex = AssetMapper::Instance().GetTexture(entityName);
        if (tex.id == 0) return;

        if (litShader.id > 0) {
            DrawSprite(camera, tex, position, size, tint, litShader);
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
            DrawSprite(camera, tex, bobPos, size, WHITE, litShader);
        } else {
            DrawBillboardPro(camera, tex,
                (Rectangle){0, 0, (float)tex.width, (float)tex.height},
                bobPos, {0, 1, 0}, {size, size}, {0.5f, 0.5f}, 0, WHITE);
        }
    }
};

#endif
