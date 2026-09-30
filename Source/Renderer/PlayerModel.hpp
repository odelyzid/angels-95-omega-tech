#pragma once
#include "raylib.h"
#include "Mesh/Mesh.hpp"
#include "Mesh/SkeletalMesh.hpp"
#include <memory>
#include <string>

namespace oz {

// ---------------------------------------------------------------------------
// PlayerModel — shared player-character model.
//
// STUB: the engine renders players in the world (remote players today; a local
// third-person body can reuse the same asset) using the model declared by the
// "Player" LightningScript def when it sets mesh/texture, otherwise the
// GameData/Global/Player/Character_Killer_01.glb convention. Assets with clips
// load as a SkeletalMesh; without clips they load as a static mesh (the current
// character export is static, so playback is a no-op).
//
// The asset is shared through MeshCache; per-instance animation state lives
// with the caller (see DrawRemotePlayers3D). Client-only.
// ---------------------------------------------------------------------------
class PlayerModel {
public:
    static PlayerModel& Instance();

    // Lazily resolve + load the shared asset. Never returns a dangling pointer:
    // callers fall back to a primitive placeholder when Get() is null.
    Mesh* Get();
    SkeletalMesh* Skeletal(); // non-null only when the asset actually has clips
    bool Ready();

    // Draw one instance with its feet at t.position, facing t.yaw (degrees).
    // `animClip` < 0 draws the bind pose. Applies the model's facing correction.
    void DrawInstance(const MeshTransform& t, Shader litShader,
                      int animClip = -1, float animTime = 0.0f);

    // Drop our reference (the asset stays cached in MeshCache).
    void Clear();

private:
    PlayerModel() = default;
    void EnsureLoaded();

    std::shared_ptr<Mesh> m_mesh;
    SkeletalMesh* m_skel = nullptr;
    bool m_tried = false;
    std::string m_sourcePath;

    // FBX/mixamo characters end up facing -Z after FBX2glTF bakes the Z-up ->
    // Y-up rotation; the engine's yaw 0 faces +Z, so correct by 180 degrees.
    static constexpr float kFacingOffsetDeg = 180.0f;
};

} // namespace oz
