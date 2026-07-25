#pragma once
#include "raylib.h"
#include "Physics/WorldChunk.hpp"
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
    int csgOp = 0;               // CSG operation (0=SOLID, 1=ADD, 2=SUB, 3=INTERSECT, 4=DE_RESC)
    int texSlot = 0;             // 0=auto, 1..N = index into tileset textures
};

// Collision AABB for an OZONE brush primitive.
struct OzoneCollisionVolume {
    BoundingBox aabb;
    int typeId = 0;
    int texSlot = 0;       // 0 = auto/default, 1..6 = face texture tileset index
    int texSlots[6] = {0}; // per-face textures: +X,-X,+Y,-Y,+Z,-Z
    std::string texPath;   // filesystem/package path to texture (non-tileset)
    float texScaleU = 1.0f;  // texture tiling/repeat U
    float texScaleV = 1.0f;  // texture tiling/repeat V
    float texOffsetU = 0.0f; // texture shift U
    float texOffsetV = 0.0f; // texture shift V
};

class OzoneLoader {
public:
    bool LoadFile(const char* path);
    bool LoadString(const char* data);
    void Draw(Camera3D& camera);
    void Unload();

    int Count() const { return (int)m_renderables.size(); }
    OzoneRenderable* Get(int index);

    // Collision volumes for OZONE brush primitives
    const std::vector<OzoneCollisionVolume>& GetCollisionVolumes() const { return m_collisionVolumes; }
    std::vector<OzoneCollisionVolume>& GetCollisionVolumesMutable() { return m_collisionVolumes; }
    void RebuildCollisionVolumes();

    // Editor: add a brush renderable so it becomes visible in the viewport
    int AddBrushRenderable(int primType, const Vector3& pos, const Vector3& size,
                           float rot, float scale, int csgOp);

    // Spatial partitioning for efficient collision queries
    const WorldChunkManager& GetChunkManager() const { return m_chunkManager; }

    // Heightmap terrain (loaded from OZONE heightmap primitive)
    bool HasHeightmap() const { return m_hmReady; }
    float SampleHeightmapY(float px, float pz) const;
    const Model& GetHeightmapModel() const { return m_hmModel; }
    Vector3 GetHeightmapPosition() const { return m_hmPosition; }
    float GetHeightmapScale() const { return m_hmScale; }

    // Draw only the renderables whose AABBs intersect the given zone bounds
    // (used for skybox rendering from ZONE_SKY brushes)
    void DrawZoneGeometry(Camera3D& camera, const BoundingBox& zoneBounds);

    // Editor integration — heightmap generation (called from editor main loop)
    Model BuildHeightmap(const std::string& imagePath, const std::string& texPath,
                         const std::vector<float>& args);

    void LoadWorldTextures(const std::string& worldDir);
    void SetLitFogShader(Shader shader) { s_litFogShader = shader; }
    void SetLitFogShaderEnabled(bool enabled);
    void ApplyTexSlotToModel(Model& model, int slot);

    // Access loaded tileset textures by 0-based index (0=auto, 0+ = vector index-1)
    int TilesetCount() const { return (int)m_tilesetTex.size(); }
    Texture2D GetTilesetTex(int idx) const {
        if (idx < 1 || idx > (int)m_tilesetTex.size()) return Texture2D{0};
        return m_tilesetTex[idx - 1];
    }

    static OzoneLoader& Instance();
    
    static Shader GetLitFogShader() { return s_litFogShader; }

private:
    static Shader s_litFogShader;
    static Shader s_backupLitFogShader;
    std::vector<OzoneRenderable> m_renderables;
    std::vector<OzoneCollisionVolume> m_collisionVolumes;
    WorldChunkManager m_chunkManager;

    // Heightmap state (set from OZONE heightmap primitive)
    bool m_hmReady = false;
    Image m_hmImage{0};
    Texture2D m_hmTexture{0};
    Model m_hmModel;
    Vector3 m_hmPosition{0,0,0};
    Vector3 m_hmSize{100,50,100};
    float m_hmScale = 1.0f;

    std::vector<Texture2D> m_tilesetTex;

    void UnloadTextures();
    void UnloadHeightmap();
    void ComputeCollisionAABB(int type, const std::vector<float>& args, Vector3 position, BoundingBox& out);

    Model BuildFromPrimitive(int type, const std::vector<float>& args);

    Model BuildBox(float w, float h, float d);
    Model BuildCylinder(float rTop, float rBot, float h, int slices);
    Model BuildSphere(float r, int segments);
    Model BuildPyramid(float w, float d, float h);
    Model BuildPlane(float nx, float ny, float nz, float dist);
};
