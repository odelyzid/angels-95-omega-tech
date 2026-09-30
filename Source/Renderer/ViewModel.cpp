#include "ViewModel.hpp"
#include "Mesh/MeshCache.hpp"
#include "raymath.h"
#include <rlgl.h>
#include "../Script/LightningEntityManager.hpp"
#include "../Script/LightningEntityDef.hpp"
#include "../Log.hpp"
#include <cmath>
#include <cstdlib>

namespace oz {

ViewModel& ViewModel::Instance() {
    // Intentionally leaked: the mesh is owned by MeshCache anyway, and we must
    // not run GL unloads during static destruction (after CloseWindow).
    static ViewModel* instance = new ViewModel();
    return *instance;
}

static std::string VmDefDir(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return (s == std::string::npos) ? std::string() : p.substr(0, s + 1);
}
static std::string VmStatStr(const EntityDef* d, const char* k, const std::string& def = "") {
    if (!d) return def;
    auto it = d->stats.strings.find(k);
    return (it != d->stats.strings.end()) ? it->second : def;
}
static float VmStatFloat(const EntityDef* d, const char* k, float def) {
    if (!d) return def;
    auto it = d->stats.floats.find(k);
    return (it != d->stats.floats.end()) ? it->second : def;
}
static Vector3 VmStatVec3(const EntityDef* d, const char* k, Vector3 def) {
    if (!d) return def;
    auto it = d->stats.vec3s.find(k);
    if (it != d->stats.vec3s.end()) return {it->second[0], it->second[1], it->second[2]};
    return def;
}

void ViewModel::Clear() {
    m_mesh.reset();
    m_skel = nullptr;
    m_clipIdle = m_clipFire = m_clipReload = m_active = -1;
    m_time = 0.0f;
    m_recoil = m_reloadT = 0.0f;
    m_key.clear();
}

void ViewModel::SetWeapon(const EntityDef* def) {
    static std::string s_lastDef;
    if (!def || def->type != EntityType::WEAPON) {
        if (!s_lastDef.empty()) {
            OZ_INFO("[CHAIN] VM::SetWeapon def=(none) -> Clear (was '%s')", s_lastDef.c_str());
            s_lastDef.clear();
        }
        Clear();
        return;
    }
    if (def->name != s_lastDef) {
        OZ_INFO("[CHAIN] VM::SetWeapon def='%s' mesh='%s' tex='%s'",
                def->name.c_str(), def->mesh.c_str(), def->texture.c_str());
        s_lastDef = def->name;
    }

    std::string mesh = VmStatStr(def, "viewmodel_mesh", def->mesh);
    std::string tex  = VmStatStr(def, "viewmodel_texture", def->texture);

    // Transform params refresh cheaply each call.
    m_offset   = VmStatVec3(def, "viewmodel_offset", m_offset);
    m_rot      = VmStatVec3(def, "viewmodel_rot", m_rot);
    m_scale    = VmStatFloat(def, "viewmodel_scale", m_scale);
    m_recoilMax = VmStatFloat(def, "recoil", m_recoilMax);
    m_reloadSeconds = VmStatFloat(def, "reload_time", 1.2f);

    std::string key = def->name + "|" + mesh + "|" + tex;
    if (key == m_key) return;
    m_key = key;
    m_mesh.reset();
    m_skel = nullptr;
    m_clipIdle = m_clipFire = m_clipReload = m_active = -1;
    m_time = 0.0f;

    if (mesh.empty()) {
        OZ_WARN("[CHAIN] VM::SetWeapon mesh empty for '%s'", def->name.c_str());
        return;
    }
    std::string baseDir = VmDefDir(def->sourcePath);
    OZ_INFO("[CHAIN] VM::SetWeapon loading mesh='%s' tex='%s' baseDir='%s'",
            mesh.c_str(), tex.c_str(), baseDir.c_str());
    auto m = MeshCache::Instance().GetStatic(mesh, tex, baseDir, true);
    if (!m || !m->Valid()) {
        OZ_WARN("[CHAIN] VM::SetWeapon could not load '%s'", mesh.c_str());
        return;
    }
    m_mesh = m;
    // Skeletal clip playback is intentionally disabled: the FBX2glTF weapon
    // exports contain MULTIPLE skins (one per mesh part), which raylib loads
    // inconsistently ("can only load one skin") and can crash in
    // UpdateModelAnimation on some builds. The weapon is drawn static with
    // procedural recoil/reload motion instead.
    m_skel = nullptr;
    m_active = -1;
    OZ_INFO("ViewModel: '%s' mesh=%s (static + procedural anim)", def->name.c_str(), mesh.c_str());
}

void ViewModel::TriggerFire() {
    OZ_INFO("[CHAIN] VM::TriggerFire key='%s'", m_key.c_str());
    m_recoil = 1.0f;
    if (m_skel && m_clipFire >= 0) {
        m_active = m_clipFire;
        m_time = 0.0f;
        m_loop = false;
    }
}

void ViewModel::TriggerReload() {
    OZ_INFO("[CHAIN] VM::TriggerReload key='%s'", m_key.c_str());
    m_reloadT = m_reloadSeconds;
    if (m_skel && m_clipReload >= 0) {
        m_active = m_clipReload;
        m_time = 0.0f;
        m_loop = false;
    }
}

void ViewModel::Update(float dt) {
    if (m_recoil > 0.0f) m_recoil = fmaxf(0.0f, m_recoil - dt * 7.0f);
    if (m_reloadT > 0.0f) m_reloadT = fmaxf(0.0f, m_reloadT - dt);

    if (!m_skel || m_active < 0) return;
    m_time += dt;
    if (!m_loop) {
        float dur = m_skel->ClipSeconds(m_active);
        if (dur > 0.0f && m_time >= dur) {
            m_active = m_clipIdle;
            m_time = 0.0f;
            m_loop = true;
        }
    }
}

void ViewModel::Draw(Camera3D& camera, Shader litShader) {
    // Bind to the currently selected weapon (cheap when unchanged).
    EntityInstance* ent = LightningEntityManager::Instance().SelectedEntity();
    SetWeapon(ent ? ent->def : nullptr);
    if (!m_mesh || !m_mesh->Valid()) return;

    static std::string s_loggedKey;
    static const bool s_trace = (getenv("OZ_VM_TRACE") != nullptr);
    if (m_key != s_loggedKey) {
        OZ_INFO("[CHAIN] VM::Draw begin key='%s' meshes=%d", m_key.c_str(), m_mesh->MeshCount());
        s_loggedKey = m_key;
    }

    // Camera world matrix (view -> world).
    Matrix camWorld = MatrixInvert(GetCameraMatrix(camera));

    // Procedural recoil kick (toward the viewer) + a small pitch pop.
    float kick = m_recoil * 0.05f * (m_recoilMax > 0.0f ? m_recoilMax : 1.0f);
    float rpitch = -m_recoil * 9.0f;

    // Reload dip / tilt.
    float dip = 0.0f, rtilt = 0.0f;
    if (m_reloadT > 0.0f && m_reloadSeconds > 0.0f) {
        float p = fminf(1.0f, m_reloadT / m_reloadSeconds);
        dip = -0.09f * p;
        rtilt = 28.0f * p;
    }

    // Local transform in camera space: x right, y up, z forward (view -Z).
    Vector3 off = {m_offset.x, m_offset.y + dip, -m_offset.z + kick};
    Vector3 rotDeg = {m_rot.x + rpitch, m_rot.y, m_rot.z + rtilt};
    Matrix local = MatrixMultiply(
        MatrixMultiply(
            MatrixTranslate(off.x, off.y, off.z),
            MatrixRotateXYZ({rotDeg.x * DEG2RAD, rotDeg.y * DEG2RAD, rotDeg.z * DEG2RAD})),
        MatrixScale(m_scale, m_scale, m_scale));
    Matrix world = MatrixMultiply(local, camWorld);

    if (m_skel && m_active >= 0)
        m_skel->ApplyPose(m_active, m_time);

    if (s_trace)
        OZ_INFO("[CHAIN] VM::Draw pre-DrawMatrix key='%s' recoil=%.2f reloadT=%.2f",
                m_key.c_str(), m_recoil, m_reloadT);

    // Draw on top of the world (no depth test) but over the 2D HUD passes later.
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    m_mesh->DrawMatrix(world, litShader);
    rlEnableDepthTest();

    if (s_trace) OZ_INFO("[CHAIN] VM::Draw post-DrawMatrix");
}

} // namespace oz
