#pragma once
#include "raylib.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

// ---------------------------------------------------------------------------
// PawnSystem — dynamic entity / NPC manager
//
// Replaces the hard-coded EntityCount=10 / Enemys[10] array with a
// growable vector of Pawn objects, each with its own FSM state.
//
// States: IDLE → PATROL (circle around spawn) → CHASE (follow player) →
//         RETURN (go back to spawn) → PATROL
//
// Usage:
//   PawnSystem::Instance().Spawn({10,0,10}, "Walker");
//   PawnSystem::Instance().Update(playerPos);
//   PawnSystem::Instance().DrawAll(camera);
// ---------------------------------------------------------------------------

enum class PawnState : uint8_t {
    IDLE,
    PATROL,
    CHASE,
    RETURN,
    DEAD
};

// Template definition shared between spawn calls; stored in an internal
// registry so Spawn() can be called by name.
struct PawnDef {
    const char* name;           // logical name, e.g. "Walker"
    float speed = 1.5f;
    float aggroRange = 6.0f;
    float attackRange = 1.5f;
    float damage = 10.0f;
    int maxHealth = 100;
};

// A single spawned pawn instance.
struct Pawn {
    uint32_t id = 0;
    bool active = false;
    PawnState state = PawnState::IDLE;
    PawnState prevState = PawnState::IDLE;

    Vector3 position{0, 0, 0};
    Vector3 velocity{0, 0, 0};
    Vector3 spawnPosition{0, 0, 0};
    float yaw = 0.0f;

    float speed = 1.5f;
    float aggroRange = 6.0f;
    float attackRange = 1.5f;
    float damage = 10.0f;
    int health = 100;
    int maxHealth = 100;

    float patrolTimer = 0.0f;
    float stateTimer = 0.0f;

    Texture2D sprite;       // billboard frame (loaded externally)
    Sound scream;           // aggro sound (loaded externally)
};

class PawnSystem {
public:
    // Register a PawnDef so Spawn() can use it by name
    void RegisterDef(const PawnDef& def);

    // Spawn a new pawn from a named template at world position
    int Spawn(Vector3 position, const char* defName);

    // Remove a single pawn by id
    void Despawn(int id);

    // Remove all pawns
    void DespawnAll();

    // Tick AI for every active pawn
    void Update(Vector3 playerPos, float dt);

    // Draw billboard sprites for every active pawn
    void DrawAll(Camera3D& camera);

    // Access individual pawns
    Pawn* Get(int id);
    int Count() const { return (int)m_pawns.size(); }

    // Singleton
    static PawnSystem& Instance();

private:
    std::vector<Pawn> m_pawns;
    std::vector<int> m_freeIds;
    std::vector<PawnDef> m_defs;
    uint32_t m_nextId = 1;

    PawnDef* FindDef(const char* name);
    int AllocSlot();

    // FSM tick helpers
    static void TickPatrol(Pawn& p, float dt);
    static void TickChase(Pawn& p, const Vector3& playerPos, float dt);
    static void TickReturn(Pawn& p, float dt);
    static void TransitionState(Pawn& p, PawnState newState);

    // Patrol wander angle (accumulated across frames for smooth circles)
    struct PatrolState {
        float angle = 0.0f;
    };
    static PatrolState& PState(Pawn& p);
};
