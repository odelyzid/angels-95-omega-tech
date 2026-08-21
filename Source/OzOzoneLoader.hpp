#pragma once
#include "raylib.h"
#include "Physics/WorldChunk.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

// Surface behavior flags for OZONE brush primitives
#define SURF_FAKEBACKDROP (1 << 3)  // brush renders as sky backdrop

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
    Shader defaultShader = {0};  // saved original shader before LitFogShader override
    bool loaded = false;
    int csgOp = 0;               // CSG operation (0=SOLID, 1=ADD, 2=SUB, 3=INTERSECT, 4=DE_RESC)
    int texSlot = 0;             // 0=auto, 1..N = index into tileset textures
    int surfaceFlags = 0;        // SURF_* bitmask for surface behavior
    float texScaleU = 1.0f;      // texture tiling/repeat U
    float texScaleV = 1.0f;      // texture tiling/repeat V
    float texOffsetU = 0.0f;     // texture shift U
    float texOffsetV = 0.0f;     // texture shift V
    std::string texPath;         // filesystem/package path to custom texture (empty = use tileset)
    Texture2D customTex = {0};   // loaded custom texture (id=0 if using tileset)
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
    bool isHeightmap = false; // true = terrain volume (support via ground clamp, never an obstacle)
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

    // Apply UV transform to a renderable's mesh (updates texcoords on GPU)
    void ApplyRenderableUV(int idx, float su, float sv, float ou, float ov);

    // Editor: remove a brush renderable by index (used for delete)
    void RemoveRenderable(int idx);

    // Editor: find the renderable index whose world AABB best matches a collision volume
    int FindRenderableByCollisionVol(int cvIdx);

    // Editor: regenerate a brush renderable with new position/size/rotation
    void UpdateBrushRenderable(int idx, const Vector3& pos, const Vector3& size, float rot);

    // Editor: apply a custom texture file to a renderable's model material
    bool ApplyRenderableTexture(int idx, const char* path);

    // Spatial partitioning for efficient collision queries
    const WorldChunkManager& GetChunkManager() const { return m_chunkManager; }

    // Heightmap terrain (loaded from OZONE heightmap primitive)
    bool HasHeightmap() const { return m_hmReady; }
    float SampleHeightmapY(float px, float pz) const;
    const Model& GetHeightmapModel() const { return m_hmModel; }
    Vector3 GetHeightmapPosition() const { return m_hmPosition; }
    float GetHeightmapScale() const { return m_hmScale; }

    // Heightmap grid access for terrain editing
    int GetHeightmapGridW() const { return m_hmGridW; }
    int GetHeightmapGridH() const { return m_hmGridH; }
    float GetHeightmapCellSize() const {
        return (m_hmGridW > 1) ? (m_hmSize.x / (float)(m_hmGridW - 1)) : 0.0f;
    }
    float GetHeightAtGrid(int col, int row) const;
    void SetHeightAtGrid(int col, int row, float height, bool triggerRebuild = true);
    void RebuildHeightmapMesh();

    // Draw only the renderables with SURF_FAKEBACKDROP flag set
    // (used for skybox rendering from SkyZone camera)
    void DrawZoneGeometry(Camera3D& camera, const BoundingBox& zoneBounds);
    void DrawZoneGeometry(Camera3D& camera); // without bounds filter

    // Draw all non-FAKEBACKDROP renderables (main world pass)
    void DrawWorldGeometry(Camera3D& camera);

    // Editor integration — heightmap generation (called from editor main loop)
    Model BuildHeightmap(const std::string& imagePath, const std::string& texPath,
                         const std::vector<float>& args);

    void LoadWorldTextures(const std::string& worldDir);
    void SetLitFogShader(Shader shader);
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
    int m_hmGridW = 0;           // grid columns (image width)
    int m_hmGridH = 0;           // grid rows (image height)
    std::vector<float> m_hmHeights; // CPU-side height values [row * w + col]

    std::vector<Texture2D> m_tilesetTex;

    std::string m_worldDir;        // current world directory (for .ozls def matching)
    std::unordered_map<std::string, int> m_zoneCounters; // per-load zone name counters

    void UnloadTextures();
    void UnloadHeightmap();
    void ComputeCollisionAABB(int type, const std::vector<float>& args, Vector3 position, BoundingBox& out);

    Model BuildFromPrimitive(int type, const std::vector<float>& args);

    Model BuildBox(float w, float h, float d);
    Model BuildCylinder(float rTop, float rBot, float h, int slices);
    Model BuildSphere(float r, int segments);
    Model BuildPyramid(float w, float d, float h);
    Model BuildPlane(float nx, float ny, float nz, float dist);
    Model BuildHeightmapMesh(const std::vector<float>& heights, int gw, int gh,
                             float cellX, float cellZ, float heightScale,
                             float uvTileSize = 8.0f);
};
