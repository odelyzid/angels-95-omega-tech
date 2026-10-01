#include "../../WindowsCompat.hpp"   // must precede raylib + winsock2 includes
#include "WeaponBehaviour.hpp"
#include "../../Script/LightningEntityManager.hpp"
#include "../../Client/Client.hpp"
#include "../../Renderer/ViewModel.hpp"
#include "../../Renderer/CombatFX.hpp"
#include "raymath.h"
#include "../../Particle/OzParticleSimulationManager.hpp"
#include <cstdlib>
#include <cmath>

// ---------------------------------------------------------------------------
// Read a stat from the selected weapon entity
// ---------------------------------------------------------------------------
float WeaponBehaviour::SelectedWeaponStat(const std::string& key, float defVal) const {
    EntityInstance* ent = LightningEntityManager::Instance().SelectedEntity();
    if (!ent || !ent->def) return defVal;
    auto dit = ent->def->stats.floats.find(key);
    return (dit != ent->def->stats.floats.end()) ? dit->second : defVal;
}

// ---------------------------------------------------------------------------
// Fire weapon — delegates to LightningEntityManager
// ---------------------------------------------------------------------------
void WeaponBehaviour::FireWeapon(Camera3D& cam) {
    Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 origin = Vector3Add(cam.position, Vector3Scale(forward, 2.0f));

    int result = LightningEntityManager::Instance().FireSelectedWeapon(origin, forward);
    if (result < 0) return; // didn't fire

    oz::ViewModel::Instance().TriggerFire();

    // Muzzle flash, anchored to the weapon rather than the camera. It used to
    // reuse `origin` (camera + 2u forward), which put the glow inside the
    // view-model instead of at the barrel; fall back to that only when no
    // weapon is loaded. The projectile origin is deliberately unchanged.
    Vector3 muzzle = origin;
    if (!oz::ViewModel::Instance().MuzzleWorldPos(cam, &muzzle))
        muzzle = origin;
    CombatFX::Instance().ArmMuzzleFlash(muzzle, 0.12f);

    // Muzzle smoke. Ranged weapons get a small hot-gas puff; melee swings do
    // not, since a sword has no muzzle. Particles are lit now (see
    // OzParticleSimulationManager::ApplyLighting), so this catches the flash
    // light instead of floating at full brightness.
    if (SelectedWeaponStat("reach", 0.0f) <= 0.0f) {
        OzParticleSimulationManager::Instance().Burst(
            muzzle, forward, 5,
            /*speed*/ 2.2f, /*spread*/ 22.0f,
            /*color*/ {255, 220, 150, 200}, /*colorEnd*/ {120, 110, 100, 0},
            /*sizeStart*/ 0.01f, /*sizeEnd*/ 0.32f,
            /*lifetime*/ 0.28f, /*gravity*/ -0.4f);
    }

    // Apply recoil
    float recoilKick = SelectedWeaponStat("recoil", result > 0 ? 2.0f : 1.0f);
    m_recoilPitch += -recoilKick + (float)(rand() % 100 - 50) / 100.0f * recoilKick * 0.3f;
    m_recoilYaw += (float)(rand() % 100 - 50) / 100.0f * recoilKick * 0.2f;
    m_crosshairBloom += recoilKick * 1.5f;

    bool isMelee = (result == 0);
    float damage = SelectedWeaponStat("damage", 10.0f);
    float reach = isMelee ? SelectedWeaponStat("reach", 3.0f) : 0.0f;

    // Send to server
    if (m_client && m_networkEnabled && *m_networkEnabled && m_client->is_connected()) {
        if (isMelee) {
            // Melee: range check against network NPCs
            int hitIdx = -1, hitPart = -1;
            float hitDist = 1e9f;
            const auto& cnpc = m_client->npcs();
            for (size_t i = 0; i < cnpc.size(); i++) {
                if (!cnpc[i].active) continue;
                Vector3 np = {cnpc[i].position.x, cnpc[i].position.y, cnpc[i].position.z};
                Vector3 toNpc = Vector3Subtract(np, origin);
                float t = Vector3DotProduct(toNpc, forward);
                if (t < 0 || t > reach) continue;
                Vector3 closest = Vector3Add(origin, Vector3Scale(forward, t));
                float d = Vector3Distance(closest, np);
                if (d < 2.0f && t < hitDist) {
                    hitDist = t;
                    hitIdx = static_cast<int>(i);
                    hitPart = cnpc[i].partition_index;
                }
            }
            if (hitIdx >= 0) {
                m_client->send_npc_damage(0, hitIdx, hitPart, (int)damage);
            }
        } else {
            // Ranged: send weapon fire + raycast hit
            m_client->send_weapon_fire(
                origin.x, origin.y, origin.z,
                forward.x, forward.y, forward.z,
                1, (int)damage);

            int hitIdx = -1, hitPart = -1;
            float hitDist = 1e9f;
            const auto& cnpc = m_client->npcs();
            for (size_t i = 0; i < cnpc.size(); i++) {
                if (!cnpc[i].active) continue;
                Vector3 np = {cnpc[i].position.x, cnpc[i].position.y, cnpc[i].position.z};
                Vector3 toNpc = Vector3Subtract(np, origin);
                float t = Vector3DotProduct(toNpc, forward);
                if (t < 0) continue;
                Vector3 closest = Vector3Add(origin, Vector3Scale(forward, t));
                float d = Vector3Distance(closest, np);
                if (d < 2.0f && t < hitDist) {
                    hitDist = t;
                    hitIdx = static_cast<int>(i);
                    hitPart = cnpc[i].partition_index;
                }
            }
            if (hitIdx >= 0) {
                m_client->send_npc_damage(0, hitIdx, hitPart, (int)damage);
            }
        }
    }
}

void WeaponBehaviour::HandleInput(bool uiBlocking, Camera3D& cam) {
    if (uiBlocking) return;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) return;
    EntityInstance* wep = LightningEntityManager::Instance().SelectedEntity();
    if (wep && wep->def && wep->def->type == EntityType::WEAPON)
        FireWeapon(cam);
}

// ---------------------------------------------------------------------------
// Recoil recovery + camera application
// ---------------------------------------------------------------------------
void WeaponBehaviour::Update(Camera3D& cam) {
    const float RECOIL_DECAY = 0.82f;
    m_recoilPitch *= RECOIL_DECAY;
    m_recoilYaw *= RECOIL_DECAY;
    m_crosshairBloom *= (m_adsActive ? 0.75f : 0.90f);
    if (fabsf(m_recoilPitch) < 0.01f) m_recoilPitch = 0.0f;
    if (fabsf(m_recoilYaw) < 0.01f) m_recoilYaw = 0.0f;
    if (m_crosshairBloom < 0.1f) m_crosshairBloom = 0.0f;

    // Apply recoil to camera target
    if (m_recoilPitch != 0.0f || m_recoilYaw != 0.0f) {
        Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, {0, 1, 0}));
        cam.target = Vector3Add(cam.target, Vector3Scale({0, 1, 0}, m_recoilPitch * 0.1f));
        cam.target = Vector3Add(cam.target, Vector3Scale(right, m_recoilYaw * 0.1f));
    }
}
