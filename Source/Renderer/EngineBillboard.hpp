#ifndef ENGINE_BILLBOARD_HPP
#define ENGINE_BILLBOARD_HPP

#include "raylib.h"
#include "raymath.h"
#include "OzAssetMapper.hpp"
#include "../Package/PackageAssetLoader.hpp"
#include "../Script/LightningEntityRegistry.hpp"
#include <string>
#include <unordered_map>

// EngineBillboard - Helper class for drawing billboard sprites with optional shader support
// Used for displaying entity icons (Light, Sound, Music, NPCs, pickups, etc.)
// When a shader is provided, renders via a mesh plane model so the shader applies.

class EngineBillboard {
private:
    inline static Model s_BillboardModel{0};

    // A 1x1 quad in the XY plane (normal +Z), centred on the origin.
    //
    // This deliberately does NOT use GenMeshPlane(): that lays the quad out on
    // the XZ plane with its normal along +Y, so a Y-axis yaw left it lying flat
    // on the ground. Every sprite drawn through DrawSprite with a lit shader
    // (which is what AngelEd always uses, and what the game uses in LIT mode)
    // was therefore edge-on to the camera and effectively invisible - the whole
    // gizmo/billboard layer silently rendered nothing. A vertical quad yawed to
    // face the camera is the only correct shape here.
    static Mesh MakeBillboardQuad() {
        static const float v[] = {
            // x     y     z      u     v
            -0.5f, -0.5f, 0.0f,  0.0f, 1.0f,
             0.5f, -0.5f, 0.0f,  1.0f, 1.0f,
             0.5f,  0.5f, 0.0f,  1.0f, 0.0f,
            -0.5f,  0.5f, 0.0f,  0.0f, 0.0f,
        };
        static const unsigned short idx[] = { 0, 1, 2, 0, 2, 3 };
        Mesh mesh = {0};
        mesh.vertexCount = 4;
        mesh.triangleCount = 6;
        mesh.vertices = (float*)v;
        mesh.texcoords = (float*)(v + 12);
        mesh.indices = (unsigned short*)idx;
        return mesh;
    }

public:
    static void Init() {
        AssetMapper::Instance().PreloadCategory("engine");
        AssetMapper::Instance().PreloadCategory("items");
        s_BillboardModel = LoadModelFromMesh(MakeBillboardQuad());
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
// Resolve a pickup's icon. AssetMapper is name-based, so it only finds items
// that happen to have a file named after them (HealthVial.png, Coin.png, ...).
// Items whose .ozls aliases a different texture - Medkit reuses
// HealthVial.png, PistonPart reuses key.png - fall through to the magenta
// missing-icon grid. Consult the entity def's own `icon` first and cache it.
inline static Texture2D ResolvePickupIcon(const char* itemName) {
    static std::unordered_map<std::string, Texture2D> s_defIconCache;
    auto cached = s_defIconCache.find(itemName ? itemName : "");
    if (cached != s_defIconCache.end())
        return cached->second;

    Texture2D tex{0};
    const EntityDef* def = itemName ? LightningEntityRegistry::Instance().Find(itemName) : nullptr;
    if (def && !def->icon.empty())
        tex = LoadTextureWithFallback(def->icon.c_str());
    s_defIconCache[itemName ? itemName : ""] = tex;
    return tex;
}

static void DrawPickup(Camera3D camera, const char* itemName, Vector3 position,
  float size = 1.2f, Shader litShader = {0}) {
  Texture2D tex = ResolvePickupIcon(itemName);
  if (tex.id == 0)
    tex = AssetMapper::Instance().GetTexture(itemName);
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
