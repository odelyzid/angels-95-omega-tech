#include "SkeletalMesh.hpp"
#include "../../Log.hpp"
#include <cctype>
#include <cmath>

// raylib renamed ModelAnimation.frameCount -> keyframeCount in 6.0. Support both
// so the engine builds against the pinned 5.5 (CI) and newer local toolchains.
#if defined(RAYLIB_VERSION_MAJOR) && RAYLIB_VERSION_MAJOR >= 6
    #define OZ_ANIM_KEYFRAME_COUNT(a) ((a).keyframeCount)
#else
    #define OZ_ANIM_KEYFRAME_COUNT(a) ((a).frameCount)
#endif

namespace oz {

SkeletalMesh::~SkeletalMesh() {
    if (m_anims && m_animCount > 0) {
        UnloadModelAnimations(m_anims, m_animCount);
    }
    m_anims = nullptr;
    m_animCount = 0;
}

bool SkeletalMesh::LoadSkeletal(const std::string& meshPath, const std::string& texPath,
                                const std::string& baseDir, bool applyPointFilter) {
    if (!Mesh::Load(meshPath, texPath, baseDir, applyPointFilter)) return false;

    int count = 0;
    m_anims = LoadModelAnimations(m_diskPath.c_str(), &count);
    m_animCount = (m_anims && count > 0) ? count : 0;
    if (m_animCount <= 0) {
        OZ_WARN("SkeletalMesh: no animation clips in '%s' (static fallback)", m_diskPath.c_str());
    } else {
        OZ_INFO("SkeletalMesh: loaded %d clip(s) from '%s'", m_animCount, m_diskPath.c_str());
    }
    return true;
}

const char* SkeletalMesh::ClipName(int index) const {
    if (index < 0 || index >= m_animCount) return "";
    return m_anims[index].name;
}

int SkeletalMesh::FindClip(const std::string& name) const {
    if (name.empty() || m_animCount <= 0) return -1;
    auto lower = [](std::string s) {
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    std::string want = lower(name);
    for (int i = 0; i < m_animCount; i++) {
        if (lower(m_anims[i].name) == want) return i;
    }
    for (int i = 0; i < m_animCount; i++) {
        if (lower(m_anims[i].name).find(want) != std::string::npos) return i;
    }
    return -1;
}

void SkeletalMesh::ApplyPose(int clipIndex, float timeSeconds, float fps) const {
    if (!m_valid || clipIndex < 0 || clipIndex >= m_animCount) return;
    const ModelAnimation& anim = m_anims[clipIndex];
    const int frames = OZ_ANIM_KEYFRAME_COUNT(anim);
    if (frames <= 0) return;
    if (!IsModelAnimationValid(m_model, anim)) return;

    if (fps <= 0.0f) fps = 30.0f;
    float frame = std::fmod(timeSeconds * fps, (float)frames);
    if (frame < 0.0f) frame += (float)frames;
    UpdateModelAnimation(m_model, anim, frame);
}

} // namespace oz
