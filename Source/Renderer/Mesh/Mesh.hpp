#pragma once
#include "raylib.h"
#include <string>
#include <vector>
#include <memory>

// ---------------------------------------------------------------------------
// oz::Mesh — base render primitive for the GameEngine.Mesh taxonomy.
//
//   GameEngine.Mesh
//     GameEngine.Mesh.Static    -> oz::StaticMesh   (props / map objects)
//     GameEngine.Mesh.Skeletal  -> oz::SkeletalMesh (animated NPC/player/item)
//
// The asset (Model + animation clips) is shared through oz::MeshCache; per
// instance playback state lives on the owning node (Pawn / mesh object), never
// inside the shared Mesh.
//
// NOTE: raylib already defines global ::Mesh, ::Model and ::Transform, hence the
// `oz` namespace and the oz::MeshTransform spelling.
// ---------------------------------------------------------------------------

namespace oz {

// Transform used for drawing a mesh instance (engine is Y-up, yaw = degrees).
struct MeshTransform {
    Vector3 position{0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    Vector3 scale{1.0f, 1.0f, 1.0f};
};

enum class MeshRenderMode : unsigned char {
    LIT = 0,
    UNLIT = 1,
    WIRE = 2
};

// Resolve a def-relative asset path: absolute and GameData-rooted paths are used
// verbatim, relative paths are first tried beside the declaring file, otherwise
// handed to the filesystem/package loader.
std::string ResolveMeshAsset(const std::string& baseDir, const std::string& rel);

class Mesh {
public:
    Mesh() = default;
    virtual ~Mesh();

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // Load a model and optional diffuse texture. `baseDir` resolves relative
    // paths (must exist for path-relative assets). `applyPointFilter` applies
    // TEXTURE_FILTER_POINT for the PS1 look.
    bool Load(const std::string& meshPath, const std::string& texPath,
              const std::string& baseDir, bool applyPointFilter);

    virtual void Update(float dt) { (void)dt; }

    // Bind the shader to every material slot and draw at the transform.
    virtual void Draw(const MeshTransform& t, Shader litShader);

    bool Valid() const { return m_valid; }
    Model& GetModel() { return m_model; }
    const Model& GetModel() const { return m_model; }
    const BoundingBox& Bounds() const { return m_bounds; }
    const std::string& DiskPath() const { return m_diskPath; }

    MeshRenderMode RenderMode = MeshRenderMode::LIT;
    bool WindAffected = false;

protected:
    void Unload();

    Model m_model{0};
    std::vector<Texture2D> m_ownedTextures;
    std::vector<Shader> m_baseShaders;   // original shader per material slot
    BoundingBox m_bounds{};
    std::string m_diskPath;
    bool m_valid = false;
};

} // namespace oz
