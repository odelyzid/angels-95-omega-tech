#pragma once
#include "raylib.h"
#include "Mesh/Mesh.hpp"
#include "Mesh/SkeletalMesh.hpp"
#include <memory>
#include <string>

struct EntityDef;

namespace oz {

// ---------------------------------------------------------------------------
// ViewModel — first-person weapon model drawn attached to the camera.
//
// The weapon mesh/clip files and the camera-space transform come from the
// selected weapon EntityDef's `stats`:
//   viewmodel_mesh / viewmodel_texture        (strings, default to mesh/texture)
//   viewmodel_offset / viewmodel_rot          (vec3, camera space: x right, y up, z forward)
//   viewmodel_scale / recip                   (floats)
//   recoil                                     (float, kick amount)
// Clips are named Idle/Fire/Reload inside the (merged) model GLB.
//
// Client-only. Draw() refreshes the weapon from the currently selected entity.
// ---------------------------------------------------------------------------
class ViewModel {
public:
    static ViewModel& Instance();

    void Update(float dt);
    void Draw(Camera3D& camera, Shader litShader);

    void TriggerFire();
    void TriggerReload();
    void Clear();

private:
    ViewModel() = default;

    void SetWeapon(const EntityDef* def);

    std::shared_ptr<Mesh> m_mesh;
    SkeletalMesh* m_skel = nullptr; // alias into m_mesh when it has clips
    int m_clipIdle = -1, m_clipFire = -1, m_clipReload = -1, m_active = -1;
    float m_time = 0.0f;
    bool m_loop = true;

    float m_recoil = 0.0f;       // 0..1 impulse
    float m_recoilMax = 1.0f;
    float m_reloadT = 0.0f;      // reload motion timer (s)
    float m_reloadSeconds = 1.2f;

    Vector3 m_offset{0.22f, -0.18f, 0.45f};
    Vector3 m_rot{0.0f, 0.0f, 0.0f};
    float m_scale = 1.0f;

    std::string m_key; // def+mesh+tex, to avoid reloading each frame
};

} // namespace oz
