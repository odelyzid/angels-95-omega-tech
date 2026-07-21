#pragma once
#include "raylib.h"
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// OzoneLoader — client-side OZONE format loader + mesh renderer
//
// Parses .ozone files (reusing the server-side OzoneParser) and generates
// raylib Mesh / Model objects for each primitive so they can be drawn
// in the 3D world alongside standard WDL models.
//
// Supported primitives:
//   Box, Cylinder, Sphere, Pyramid, Plane
//
// Usage:
//   OzoneLoader::Instance().LoadFile("GameData/Worlds/World1/World.ozone");
//   OzoneLoader::Instance().Draw(camera);
// ---------------------------------------------------------------------------

// A renderable OZONE primitive with its generated raylib model.
struct OzoneRenderable {
    int typeId = 0;              // cast from OzonePrimitiveType
    Vector3 position{0, 0, 0};
    float scale = 1.0f;
    float rotation = 0.0f;
    Model model;                 // generated raylib Model (meshes + materials)
    bool loaded = false;
};

class OzoneLoader {
public:
    bool LoadFile(const char* path);
    bool LoadString(const char* data);
    void Draw(Camera3D& camera);
    void Unload();

    int Count() const { return (int)m_renderables.size(); }
    OzoneRenderable* Get(int index);

    static OzoneLoader& Instance();

private:
    std::vector<OzoneRenderable> m_renderables;

    Texture2D m_floorTex{0};
    Texture2D m_wallTex{0};
    Texture2D m_columnTex{0};
    Texture2D m_ceilTex{0};

    void LoadWorldTextures(const std::string& worldDir);
    void UnloadTextures();

    Model BuildFromPrimitive(int type, const std::vector<float>& args);

    Model BuildBox(float w, float h, float d);
    Model BuildCylinder(float rTop, float rBot, float h, int slices);
    Model BuildSphere(float r, int segments);
    Model BuildPyramid(float w, float d, float h);
    Model BuildPlane(float nx, float ny, float nz, float dist);
};
