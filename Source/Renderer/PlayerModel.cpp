#include "PlayerModel.hpp"
#include "Mesh/MeshCache.hpp"
#include "raymath.h"
#include "../Script/LightningEntityRegistry.hpp"
#include "../Script/LightningEntityDef.hpp"
#include "../Log.hpp"

namespace oz {

PlayerModel& PlayerModel::Instance() {
    // Intentionally leaked: the mesh is owned by MeshCache anyway, and GL
    // unloads must not run during static destruction (after CloseWindow).
    static PlayerModel* instance = new PlayerModel();
    return *instance;
}

void PlayerModel::EnsureLoaded() {
    if (m_tried) return;
    m_tried = true;

    std::string meshPath, texPath;
    bool skeletal = false;
    if (const EntityDef* def = LightningEntityRegistry::Instance().Find("Player")) {
        meshPath = def->mesh;
        texPath = def->texture;
        skeletal = (def->meshType == "skeletal");
    }

    const std::string baseDir = "GameData/Global/Player/";
    // The texture fallback must only apply when the mesh is also the legacy
    // default. A def that names a mesh but no texture (a GLB with its own
    // embedded materials) would otherwise have Character_Killer_01.png forced
    // across every material by Mesh::Load.
    const bool usingDefaultMesh = meshPath.empty();
    if (meshPath.empty()) meshPath = baseDir + "Character_Killer_01.glb";
    if (texPath.empty() && usingDefaultMesh) texPath = baseDir + "Character_Killer_01.png";

    m_mesh = skeletal
        ? MeshCache::Instance().GetSkeletal(meshPath, texPath, baseDir, true)
        : MeshCache::Instance().GetStatic(meshPath, texPath, baseDir, true);
    m_skel = dynamic_cast<SkeletalMesh*>(m_mesh.get());

    if (m_mesh && m_mesh->Valid()) {
        m_sourcePath = meshPath;
        OZ_INFO("PlayerModel: using '%s' (%s)", meshPath.c_str(),
                (m_skel && m_skel->ClipCount() > 0) ? "skeletal" : "static");
    } else {
        OZ_WARN("PlayerModel: no player model at '%s' — placeholder in use", meshPath.c_str());
    }
}

Mesh* PlayerModel::Get() {
    EnsureLoaded();
    return (m_mesh && m_mesh->Valid()) ? m_mesh.get() : nullptr;
}

SkeletalMesh* PlayerModel::Skeletal() {
    Get();
    return (m_skel && m_skel->ClipCount() > 0) ? m_skel : nullptr;
}

bool PlayerModel::Ready() {
    return Get() != nullptr;
}

void PlayerModel::DrawInstance(const MeshTransform& t, Shader litShader,
                               int animClip, float animTime) {
    Mesh* mesh = Get();
    if (!mesh) return;

    if (animClip >= 0 && m_skel && m_skel->ClipCount() > 0)
        m_skel->ApplyPose(animClip, animTime);

    MeshTransform mt = t;
    mt.yaw += kFacingOffsetDeg;
    mesh->Draw(mt, litShader);
}

void PlayerModel::Clear() {
    m_mesh.reset();
    m_skel = nullptr;
    m_tried = false;
    m_sourcePath.clear();
}

} // namespace oz
