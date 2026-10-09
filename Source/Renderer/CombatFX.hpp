#pragma once

// ---------------------------------------------------------------------------
// CombatFX — transient combat visuals: muzzle flashes, impact bursts,
// lingering tracers and surface decals (bullet holes).
//
// Header-only singleton, updated + drawn inside DrawWorld()'s BeginMode3D
// pass. Decals are surface-aligned quads drawn via DrawBillboardPro with an
// in-plane up vector; a procedural scorch texture is generated on Init so no
// GameData asset is required.
//
// Usage:
//   CombatFX::Instance().ArmMuzzleFlash(origin);          // on weapon fire
//   CombatFX::Instance().SpawnImpact(hitPos, normal, ..); // on hit
//   CombatFX::Instance().AddDecal(hitPos, normal);        // wall hit
//   // per frame inside BeginMode3D:
//   CombatFX::Instance().Update(dt);
//   CombatFX::Instance().Draw3D(camera);
// ---------------------------------------------------------------------------

#include "raylib.h"
#include "raymath.h"
#include "LitLightning.hpp"
#include "../Particle/OzParticleSimulationManager.hpp"
#include "../Log.hpp"
#include <vector>

// Swept segment vs AABB slab test.
// Returns true if the segment p0->p1 intersects box; outputs hit distance as
// a parametric t in [0,1] along the segment and the entry face normal.
// Segments starting fully inside report no hit (axis < 0).
inline bool SegmentVsAABB(Vector3 p0, Vector3 p1, const BoundingBox& box,
                          float& outT, Vector3& outNormal) {
    float d[3] = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
    float p[3] = {p0.x, p0.y, p0.z};
    float bmin[3] = {box.min.x, box.min.y, box.min.z};
    float bmax[3] = {box.max.x, box.max.y, box.max.z};

    float tmin = 0.0f, tmax = 1.0f;
    int hitAxis = -1;
    float hitSign = 0.0f;

    for (int i = 0; i < 3; i++) {
        if (fabsf(d[i]) < 1e-8f) {
            if (p[i] < bmin[i] || p[i] > bmax[i]) return false;
        } else {
            float inv = 1.0f / d[i];
            float t1 = (bmin[i] - p[i]) * inv;
            float t2 = (bmax[i] - p[i]) * inv;
            float sign = -1.0f;
            if (t1 > t2) {
                float tt = t1; t1 = t2; t2 = tt;
                sign = 1.0f;
            }
            if (t1 > tmin) { tmin = t1; hitAxis = i; hitSign = sign; }
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return false;
        }
    }

    if (hitAxis < 0) return false;
    outT = tmin;
    outNormal = {0.0f, 0.0f, 0.0f};
    if (hitAxis == 0) outNormal.x = hitSign;
    else if (hitAxis == 1) outNormal.y = hitSign;
    else outNormal.z = hitSign;
    return true;
}

class CombatFX {
public:
    static CombatFX& Instance() {
        static CombatFX fx;
        return fx;
    }

    void Init() {
        if (m_decalTex.id != 0) return;

        Image img = GenImageColor(64, 64, BLANK);
        Color* px = (Color*)img.data;
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                float dx = (x - 31.5f) / 31.5f;
                float dy = (y - 31.5f) / 31.5f;
                float r = sqrtf(dx * dx + dy * dy);
                float alpha = 0.0f, bright = 30.0f;
                if (r < 0.55f) {
                    alpha = 225.0f;
                    bright = 25.0f + 45.0f * (r / 0.55f);
                } else if (r < 0.82f) {
                    float f = 1.0f - (r - 0.55f) / 0.27f;
                    alpha = 225.0f * f;
                    bright = 70.0f + 40.0f * f;
                }
                unsigned char b = (unsigned char)bright;
                px[y * 64 + x] = {b, b, b, (unsigned char)alpha};
            }
        }
        m_decalTex = LoadTextureFromImage(img);
        UnloadImage(img);

        m_flashes.reserve(8);
        m_decals.resize(kMaxDecals);
    }

    void Shutdown() {
        if (m_decalTex.id != 0) { UnloadTexture(m_decalTex); m_decalTex = {0}; }
        m_flashes.clear();
        m_tracers.clear();
        m_decals.clear();
    }

    void ClearAll() {
        m_flashes.clear();
        m_tracers.clear();
        m_nextDecal = 0;
        for (auto& d : m_decals) d.age = -1.0f;
        // Transient lights belong to the world that fired them; carrying one
        // across a map change would leave a glow in empty space.
        LitLightning_ClearTransientLights();
    }

    void ArmMuzzleFlash(Vector3 pos, float duration = 0.12f) {
        // Reject a non-positive duration at ARM time, not at draw time. Draw3D
        // computes `f.timer / f.duration`; a zero duration there is 0/0 -> NaN,
        // which feeds a NaN radius into DrawSphere and a NaN into the colour
        // channels. ArmTransientLight, two functions down, already guarded this;
        // ArmMuzzleFlash did not.
        if (duration <= 0.0f) return;
        EnsureInit();
        Flash f{pos, duration, duration};
        if (m_flashes.size() >= 8) m_flashes.erase(m_flashes.begin());
        m_flashes.push_back(f);
        // Also cast real light. The visible flash was an unlit DrawSphere, so it
        // ignored the room entirely: a shot in an unlit corridor lit nothing.
        // Transient lights take priority over world lights in the shader budget,
        // so this cannot be evicted by a busy level.
        ArmTransientLight(pos, {255, 200, 120, 255}, duration * 1.4f, 7.0f);
    }

    // Add a short-lived effect light to the transient pool. Reuses an inactive
    // slot when one is available, otherwise drops the request (MAX_TRANSIENT_LIGHTS).
    void ArmTransientLight(Vector3 pos, Color color, float duration, float radius) {
        if (duration <= 0.0f) return;
        auto& pool = LitLightning_TransientLights();
        for (auto& n : pool) {
            if (n.active) continue;
            n.active = true;
            n.type = LitLightType::POINT;
            n.position = pos;
            n.target = pos;
            n.color = color;
            n.intensity = 1.0f;
            n.radius = radius;
            n.isStatic = false;
            n.effect = LitLightEffect::NONE;
            n.timer = duration;
            return;
        }
        OZ_WARN("CombatFX: transient light pool full, dropping light at %.2f,%.2f,%.2f",
                pos.x, pos.y, pos.z);
    }

    // A tracer streak. Deliberately does not call EnsureInit(): tracers are drawn
    // with DrawLine3D and need no texture, and every projectile step emits one,
    // so this must stay allocation-and-GL-free. Decals are what need Init().
    //
    // Capped because Update() is only reached from the draw path: a frame that
    // ticks projectiles without drawing (tests, a paused world) would otherwise
    // grow this vector without bound.
    void AddTracer(Vector3 start, Vector3 end, Color color, float life = 0.05f) {
        if (life <= 0.0f) return;
        if (m_tracers.size() >= kMaxTracers) m_tracers.erase(m_tracers.begin());
        Tracer t{start, end, color, life, life};
        m_tracers.push_back(t);
    }

    void SpawnImpact(Vector3 pos, Vector3 normal, Color color,
                     int count = 10, float speed = 6.0f) {
        // Sparks go through the shared particle pool instead of a private list.
        // They used to be drawn here with DrawBillboard, which is unlit, so a
        // spark burst lit by a muzzle flash or a torch just glowed at full
        // brightness. Pool particles are lit (see
        // OzParticleSimulationManager::ApplyLighting) and fade out on their own.
        Color end = Fade(color, 0.0f);
        end.r = (unsigned char)(end.r / 2);
        end.g = (unsigned char)(end.g / 3);
        end.b = (unsigned char)(end.b / 3);
        OzParticleSimulationManager::Instance().Burst(
            pos, normal, count, speed, /*spread*/ 55.0f,
            color, end,
            /*sizeStart*/ 0.07f, /*sizeEnd*/ 0.015f,
            /*lifetime*/ 0.30f, /*gravity*/ -9.0f);
    }

    void AddDecal(Vector3 pos, Vector3 normal, float size = 0.35f) {
        EnsureInit();
        if (m_decals.empty()) return;
        Decal& d = m_decals[m_nextDecal];
        m_nextDecal = (m_nextDecal + 1) % m_decals.size();
        d.pos = Vector3Add(pos, Vector3Scale(normal, 0.03f));
        d.normal = normal;
        d.size = size;
        d.age = 0.0f;
    }

    void Update(float dt) {
        // Age out transient effect lights; intensity ramps down with the timer so
        // a muzzle flash fades rather than popping off.
        for (auto& n : LitLightning_TransientLights()) {
            if (!n.active) continue;
            n.timer -= dt;
            if (n.timer <= 0.0f) {
                n.active = false;
            } else if (n.timer < 0.05f) {
                n.intensity = n.timer / 0.05f;
            }
        }
        for (size_t i = 0; i < m_flashes.size();) {
            m_flashes[i].timer -= dt;
            if (m_flashes[i].timer <= 0.0f)
                m_flashes.erase(m_flashes.begin() + i);
            else
                i++;
        }
        for (size_t i = 0; i < m_tracers.size();) {
            m_tracers[i].life -= dt;
            if (m_tracers[i].life <= 0.0f)
                m_tracers.erase(m_tracers.begin() + i);
            else
                i++;
        }
        for (auto& d : m_decals)
            if (d.age >= 0.0f) d.age += dt;
    }

    void Draw3D(Camera3D& camera) {
        EnsureInit();

        for (const auto& f : m_flashes) {
            float t = f.timer / f.duration;
            Color c = {(unsigned char)(255),
                       (unsigned char)(120 + 130 * t),
                       (unsigned char)(40 * t), 255};
            DrawSphere(f.pos, 0.45f * t + 0.08f, c);
        }

        for (const auto& tr : m_tracers) {
            float a = tr.life / tr.maxLife;
            DrawLine3D(tr.a, tr.b, Fade(tr.color, a));
        }

        Rectangle src = {0, 0, (float)m_decalTex.width, (float)m_decalTex.height};
        for (const auto& d : m_decals) {
            if (d.age < 0.0f) continue;
            Vector3 ref = (fabsf(d.normal.y) > 0.9f) ? Vector3{0, 0, 1} : Vector3{0, 1, 0};
            Vector3 t1 = Vector3Normalize(Vector3CrossProduct(ref, d.normal));
            Vector3 up = Vector3Normalize(Vector3CrossProduct(d.normal, t1));
            DrawBillboardPro(camera, m_decalTex, src, d.pos, up,
                             Vector2{d.size, d.size}, Vector2{d.size * 0.5f, d.size * 0.5f},
                             0.0f, WHITE);
        }
    }

// Live tracer count. Exposed so the projectile tick's tracer emission can be
    // asserted without reaching into the private vector.
    size_t TracerCount() const { return m_tracers.size(); }

    // Live flash count. Same purpose as TracerCount: lets a headless test assert
    // that a zero-duration ArmMuzzleFlash is rejected and that Update() reaps.
    size_t FlashCount() const { return m_flashes.size(); }

private:
    struct Flash { Vector3 pos; float timer; float duration; };
    struct Decal { Vector3 pos{0, 0, 0}; Vector3 normal{0, 1, 0}; float size = 0.35f; float age = -1.0f; };
    struct Tracer { Vector3 a, b; Color color; float life, maxLife; };

    bool EnsureInit() {
        if (m_decalTex.id == 0) Init();
        return m_decalTex.id != 0;
    }

    static constexpr size_t kMaxDecals = 64;
    static constexpr size_t kMaxTracers = 256;

    std::vector<Flash> m_flashes;
    std::vector<Tracer> m_tracers;
    std::vector<Decal> m_decals;
    size_t m_nextDecal = 0;

    Texture2D m_decalTex{0};
};
