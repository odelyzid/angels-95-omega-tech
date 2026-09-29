#pragma once
#include "Mesh.hpp"
#include "StaticMesh.hpp"
#include "SkeletalMesh.hpp"
#include "AnimatedMesh.hpp"
#include <string>
#include <unordered_map>

namespace oz {

// Internal mesh asset cache: path -> shared Mesh. Owned/released through
// PawnSystem (the single box for placed entities). Assets are shared across
// instances; per-instance animation state lives on the instance.
class MeshCache {
public:
    static MeshCache& Instance();

    // Returns the cached static mesh, or nullptr when the model cannot load.
    std::shared_ptr<Mesh> GetStatic(const std::string& meshPath, const std::string& texPath,
                                    const std::string& baseDir, bool pointFilter);

    // Returns a skeletal mesh when the asset has clips; otherwise falls back to
    // a StaticMesh so callers can still render it.
    std::shared_ptr<Mesh> GetSkeletal(const std::string& meshPath, const std::string& texPath,
                                      const std::string& baseDir, bool pointFilter);

    // Returns a vertex-keyframe AnimatedMesh for an external `.ozanim` clip;
    // falls back to a StaticMesh when the clip or mesh cannot be loaded.
    std::shared_ptr<Mesh> GetAnimated(const std::string& meshPath, const std::string& texPath,
                                      const std::string& animFile, const std::string& baseDir,
                                      bool pointFilter);

    // Unload every cached asset (clear PawnSystem nodes first).
    void Clear();

private:
    std::shared_ptr<Mesh> GetInternal(const std::string& key, const std::string& meshPath,
                                      const std::string& texPath, const std::string& baseDir,
                                      bool skeletal, bool pointFilter);

    std::unordered_map<std::string, std::shared_ptr<Mesh>> m_cache;
};

} // namespace oz
