#include "MeshCache.hpp"
#include "../../Log.hpp"

namespace oz {

MeshCache& MeshCache::Instance() {
    // Intentionally leaked: destroying the cache at static-destruction time
    // would run UnloadModel after CloseWindow() has torn down the GL context.
    // Explicit Clear() (called from PawnSystem while the context is alive) is
    // the only place assets are released.
    static MeshCache* instance = new MeshCache();
    return *instance;
}

std::shared_ptr<Mesh> MeshCache::GetStatic(const std::string& meshPath, const std::string& texPath,
                                           const std::string& baseDir, bool pointFilter) {
    std::string key = "S|" + meshPath + "|" + texPath;
    return GetInternal(key, meshPath, texPath, baseDir, false, pointFilter);
}

std::shared_ptr<Mesh> MeshCache::GetSkeletal(const std::string& meshPath, const std::string& texPath,
                                             const std::string& baseDir, bool pointFilter) {
    std::string key = "K|" + meshPath + "|" + texPath;
    return GetInternal(key, meshPath, texPath, baseDir, true, pointFilter);
}

std::shared_ptr<Mesh> MeshCache::GetAnimated(const std::string& meshPath, const std::string& texPath,
                                             const std::string& animFile, const std::string& baseDir,
                                             bool pointFilter) {
    std::string key = "A|" + meshPath + "|" + texPath + "|" + animFile;
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    std::shared_ptr<Mesh> mesh;
    auto am = std::make_shared<AnimatedMesh>();
    if (am->LoadAnimated(meshPath, texPath, animFile, baseDir, pointFilter)) {
        if (am->ClipCount() > 0) mesh = am;
    }
    if (!mesh) {
        auto st = std::make_shared<StaticMesh>();
        if (st->Load(meshPath, texPath, baseDir, pointFilter)) mesh = st;
    }

    m_cache[key] = mesh;
    return mesh;
}

std::shared_ptr<Mesh> MeshCache::GetInternal(const std::string& key, const std::string& meshPath,
                                             const std::string& texPath, const std::string& baseDir,
                                             bool skeletal, bool pointFilter) {
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    std::shared_ptr<Mesh> mesh;
    if (skeletal) {
        auto sm = std::make_shared<SkeletalMesh>();
        if (sm->LoadSkeletal(meshPath, texPath, baseDir, pointFilter)) {
            // Only advertise as skeletal when clips actually exist; otherwise
            // fall through to a plain static mesh ("static unless animated").
            if (sm->ClipCount() > 0) {
                mesh = sm;
            }
        }
    }
    if (!mesh) {
        auto st = std::make_shared<StaticMesh>();
        if (st->Load(meshPath, texPath, baseDir, pointFilter)) mesh = st;
    }

    m_cache[key] = mesh; // negative results cached as nullptr
    return mesh;
}

void MeshCache::Clear() {
    size_t n = m_cache.size();
    m_cache.clear();
    if (n > 0) OZ_INFO("MeshCache: cleared %zu cached mesh asset(s)", n);
}

} // namespace oz
