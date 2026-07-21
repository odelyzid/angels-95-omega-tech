#include "oz_pawn_system.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
PawnSystem& PawnSystem::Instance() {
    static PawnSystem instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Find a registered PawnDef by name (case-insensitive)
// ---------------------------------------------------------------------------
PawnDef* PawnSystem::FindDef(const char* name) {
    for (auto& d : m_defs) {
        const char* a = name;
        const char* b = d.name;
        while (*a && *b) {
            if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
                break;
            a++; b++;
        }
        if (*a == *b) return &d;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// AllocSlot — find a free slot or grow the vector
// ---------------------------------------------------------------------------
int PawnSystem::AllocSlot() {
    if (!m_freeIds.empty()) {
        int id = m_freeIds.back();
        m_freeIds.pop_back();
        return id;
    }
    int id = (int)m_pawns.size();
    m_pawns.emplace_back();
    return id;
}

// ---------------------------------------------------------------------------
// RegisterDef
// ---------------------------------------------------------------------------
void PawnSystem::RegisterDef(const PawnDef& def) {
    // Don't register duplicates
    if (FindDef(def.name)) return;
    m_defs.push_back(def);
}

// ---------------------------------------------------------------------------
// Spawn
// ---------------------------------------------------------------------------
int PawnSystem::Spawn(Vector3 pos, const char* defName) {
    PawnDef* def = FindDef(defName);
    if (!def) {
        fprintf(stderr, "PawnSystem: unknown def '%s'\n", defName);
        return -1;
    }

    int slot = AllocSlot();
    Pawn& p = m_pawns[slot];
    p.id = m_nextId++;
    p.active = true;
    p.position = pos;
    p.spawnPosition = pos;
    p.velocity = {0, 0, 0};
    p.yaw = 0.0f;
    p.state = PawnState::IDLE;
    p.prevState = PawnState::IDLE;

    p.speed = def->speed;
    p.aggroRange = def->aggroRange;
    p.attackRange = def->attackRange;
    p.damage = def->damage;
    p.health = def->maxHealth;
    p.maxHealth = def->maxHealth;

    p.patrolTimer = 0.0f;
    p.stateTimer = 0.0f;
    p.sprite = {0};
    p.scream = {0};

    return (int)p.id;
}

// ---------------------------------------------------------------------------
// Despawn
// ---------------------------------------------------------------------------
void PawnSystem::Despawn(int id) {
    for (size_t i = 0; i < m_pawns.size(); i++) {
        if (m_pawns[i].active && m_pawns[i].id == (uint32_t)id) {
            m_pawns[i].active = false;
            m_freeIds.push_back((int)i);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// DespawnAll
// ---------------------------------------------------------------------------
void PawnSystem::DespawnAll() {
    for (auto& p : m_pawns) {
        p.active = false;
    }
    m_freeIds.clear();
    for (size_t i = 0; i < m_pawns.size(); i++)
        m_freeIds.push_back((int)i);
}

// ---------------------------------------------------------------------------
// Get
// ---------------------------------------------------------------------------
Pawn* PawnSystem::Get(int id) {
    for (auto& p : m_pawns) {
        if (p.active && p.id == (uint32_t)id)
            return &p;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// FSM transitions
// ---------------------------------------------------------------------------
void PawnSystem::TransitionState(Pawn& p, PawnState newState) {
    if (p.state == newState) return;
    p.prevState = p.state;
    p.state = newState;
    p.stateTimer = 0.0f;
}

// ---------------------------------------------------------------------------
// Patrol state: wander in a circle around spawn
// ---------------------------------------------------------------------------
void PawnSystem::TickPatrol(Pawn& p, float dt) {
    PatrolState& ps = PState(p);
    ps.angle += dt * 0.8f;
    if (ps.angle > 6.2832f) ps.angle -= 6.2832f;

    float radius = 3.0f;
    float tx = p.spawnPosition.x + cosf(ps.angle) * radius;
    float tz = p.spawnPosition.z + sinf(ps.angle) * radius;

    Vector3 dir = {tx - p.position.x, 0, tz - p.position.z};
    float dist = sqrtf(dir.x * dir.x + dir.z * dir.z);
    if (dist > 0.1f) {
        dir.x /= dist; dir.z /= dist;
        p.position.x += dir.x * p.speed * dt;
        p.position.z += dir.z * p.speed * dt;
        p.yaw = atan2f(dir.x, dir.z);
    }
}

// ---------------------------------------------------------------------------
// Chase state: move toward player
// ---------------------------------------------------------------------------
void PawnSystem::TickChase(Pawn& p, const Vector3& playerPos, float dt) {
    Vector3 dir = {playerPos.x - p.position.x, 0, playerPos.z - p.position.z};
    float dist = sqrtf(dir.x * dir.x + dir.z * dir.z);
    if (dist > 0.1f) {
        dir.x /= dist; dir.z /= dist;
        p.position.x += dir.x * p.speed * 1.5f * dt;
        p.position.z += dir.z * p.speed * 1.5f * dt;
        p.yaw = atan2f(dir.x, dir.z);
    }
}

// ---------------------------------------------------------------------------
// Return state: go back to spawn
// ---------------------------------------------------------------------------
void PawnSystem::TickReturn(Pawn& p, float dt) {
    Vector3 dir = {p.spawnPosition.x - p.position.x, 0, p.spawnPosition.z - p.position.z};
    float dist = sqrtf(dir.x * dir.x + dir.z * dir.z);
    if (dist > 0.5f) {
        dir.x /= dist; dir.z /= dist;
        p.position.x += dir.x * p.speed * dt;
        p.position.z += dir.z * p.speed * dt;
        p.yaw = atan2f(dir.x, dir.z);
    } else {
        // Arrived — go back to patrol
        TransitionState(p, PawnState::PATROL);
    }
}

// ---------------------------------------------------------------------------
// Per-pawn patrol state storage (keyed by Pawn pointer)
// ---------------------------------------------------------------------------
PawnSystem::PatrolState& PawnSystem::PState(Pawn& p) {
    static PatrolState defaultPs;
    // Use a simple map keyed by pawn id
    static std::unordered_map<uint32_t, PatrolState> psMap;
    return psMap[p.id];
}

// ---------------------------------------------------------------------------
// Update — tick all active pawn FSM
// ---------------------------------------------------------------------------
void PawnSystem::Update(Vector3 playerPos, float dt) {
    for (auto& p : m_pawns) {
        if (!p.active || p.state == PawnState::DEAD) continue;

        float dx = playerPos.x - p.position.x;
        float dz = playerPos.z - p.position.z;
        float distToPlayer = sqrtf(dx * dx + dz * dz);

        p.stateTimer += dt;

        switch (p.state) {
            case PawnState::IDLE:
                if (distToPlayer < p.aggroRange) {
                    TransitionState(p, PawnState::CHASE);
                } else if (p.stateTimer > 2.0f) {
                    TransitionState(p, PawnState::PATROL);
                }
                break;

            case PawnState::PATROL:
                TickPatrol(p, dt);
                if (distToPlayer < p.aggroRange) {
                    TransitionState(p, PawnState::CHASE);
                }
                break;

            case PawnState::CHASE:
                TickChase(p, playerPos, dt);
                if (distToPlayer > p.aggroRange * 1.5f) {
                    TransitionState(p, PawnState::RETURN);
                }
                break;

            case PawnState::RETURN:
                TickReturn(p, dt);
                if (distToPlayer < p.aggroRange) {
                    TransitionState(p, PawnState::CHASE);
                }
                break;

            default:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// DrawAll — render billboards for every active pawn
// ---------------------------------------------------------------------------
void PawnSystem::DrawAll(Camera3D& camera) {
    for (auto& p : m_pawns) {
        if (!p.active || p.state == PawnState::DEAD) continue;
        if (p.sprite.id > 0) {
            DrawBillboard(camera, p.sprite, p.position, 10.0f, WHITE);
        } else {
            // Fallback cube
            DrawCube(p.position, 1.0f, 2.0f, 1.0f, RED);
        }
    }
}
