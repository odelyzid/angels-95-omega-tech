#pragma once
#include "Mesh.hpp"

namespace oz {

// GameEngine.Mesh.Skeletal — animated weapons / players / NPCs / items.
//
// The Model + clips are shared via MeshCache; the caller owns playback state
// (clip index + elapsed time) and calls ApplyPose immediately before drawing so
// that N instances of one def do not fight over the shared pose buffers.
class SkeletalMesh : public Mesh {
public:
    SkeletalMesh() = default;
    ~SkeletalMesh() override;

    // Load model + textures + animation clips (GLB/GLTF/IQM only).
    bool LoadSkeletal(const std::string& meshPath, const std::string& texPath,
                      const std::string& baseDir, bool applyPointFilter);

    int ClipCount() const { return m_animCount; }
    const char* ClipName(int index) const;

    // Case-insensitive exact match, then substring; -1 when not found.
    int FindClip(const std::string& name) const;

    // Apply a looping pose at `timeSeconds` (default 30 fps keyframe rate).
    // No-op when the clip index is invalid or the skeleton does not match.
    void ApplyPose(int clipIndex, float timeSeconds, float fps = 30.0f) const;

private:
    ModelAnimation* m_anims = nullptr;
    int m_animCount = 0;
};

} // namespace oz
