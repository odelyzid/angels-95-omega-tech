#pragma once
#include "raylib.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

// ---------------------------------------------------------------------------
// OzParticleSimulationManager — GameEngine.ParticleEmitter simulation.
//
// Isolation contract: this manager is ticked once per frame at a fixed point
// *after* weapon/NPC/projectile simulation, and it only READS emitter
// definitions from PawnSystem. It never calls gameplay code, so particles can
// never re-enter or interrupt weapon mechanics or NPC ticks. Client-only and
// purely cosmetic (no networking).
//
// Emitter node definitions live in PawnSystem (m_particleEmitters); live
// particles live here.
// ---------------------------------------------------------------------------
class OzParticleSimulationManager {
public:
    static OzParticleSimulationManager& Instance();

    // Advance the simulation. Safely callable with dt <= 0 (no-op).
    void Update(float dt);

    // Draw all live particles in the 3D pass.
    void Draw(Camera3D& camera);

    // Drop every live particle (emitter nodes are cleared separately).
    void Clear();

    int LiveCount() const { return m_live; }

private:
    OzParticleSimulationManager() = default;

    struct Particle {
        Vector3 position{0, 0, 0};
        Vector3 velocity{0, 0, 0};
        float age = 0.0f;
        float lifetime = 1.0f;
        float sizeStart = 0.4f;
        float sizeEnd = 0.0f;
        Color colorStart{255, 255, 255, 255};
        Color colorEnd{255, 255, 255, 0};
        float gravity = 0.0f;
        Texture2D texture{0};
        bool active = false;
    };

    static constexpr int MAX_PARTICLES = 4096;

    Texture2D TextureFor(const std::string& path);
    Texture2D DefaultTexture();
    void SpawnFromEmitter(const struct ParticleEmitterNode& e, int count);

    std::vector<Particle> m_pool;
    int m_next = 0;
    int m_live = 0;
    Texture2D m_defaultTex{0};
    std::unordered_map<std::string, Texture2D> m_texCache;
    std::unordered_map<uint32_t, float> m_spawnAccum;
};
