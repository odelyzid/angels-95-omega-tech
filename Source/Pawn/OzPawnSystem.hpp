#pragma once
#include "raylib.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

// ---------------------------------------------------------------------------
// PawnSystem â€” dynamic entity / NPC manager
//
// Replaces the hard-coded EntityCount=10 / Enemys[10] array with a
// growable vector of Pawn objects, each with its own FSM state.
//
// States: IDLE â†’ PATROL (circle around spawn) â†’ CHASE (follow player) â†’
//         RETURN (go back to spawn) â†’ PATROL
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

// Zone volume types
enum class ZoneType : uint8_t {
    ZONE_WATER = 0,
    ZONE_LADDER = 1,
    ZONE_SKY = 2,
    ZONE_REVERB = 3,
    ZONE_GAMEPLAY_SOUND = 4
};

// Sound profile - maps game types to music/sound actions
struct GameplaySoundProfile {
    std::string music_on_enter;   // Music to crossfade to on zone enter
    std::string music_on_exit;    // Music to restore on zone exit
    std::string sfx_on_enter;     // One-shot sound played on enter
    std::string sfx_on_combat;    // Combat stinger (DM/TDM modes)
    std::string ambience_loop;    // Ambience loop while inside zone
    float volume_mult = 1.0f;     // Volume multiplier for this zone
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
    std::string defName;  // name of the pawn definition (e.g., "Walker", "Skaarj")
};

// Player start node - position and orientation for player spawn
struct PlayerStartNode {
    uint32_t id = 0;
    Vector3 position{0, 0, 0};
    float yaw = 0.0f;
};

// Pickup node - collectible items in the world
struct PickupNode {
    uint32_t id = 0;
    Vector3 position{0, 0, 0};
    std::string typeName;   // e.g., "HealthVial", "ManaVial", "EnergyCrystal", "Key", "Coin", "Powerup"
    bool active = true;
    float respawnTimer = 0.0f;
    float respawnTime = 30.0f;  // default respawn time
};

// Emitter type for sound/music markers
enum class EmitterType : uint8_t {
    SOUND = 0,
    MUSIC = 1
};

// Emitter node - positional sound/music markers in the world
struct EmitterNode {
    uint32_t id = 0;
    Vector3 position{0, 0, 0};
    EmitterType type = EmitterType::SOUND;
};

// Zone volume node - AABB volumes with behavior flags
struct ZoneVolumeNode {
    uint32_t id = 0;
    BoundingBox bounds;
    ZoneType zoneType = ZoneType::ZONE_WATER;
    float intensity = 1.0f;  // e.g., water density, ladder speed
    std::string name;        // logical name for LightningScript zone lookups
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
    const std::vector<Pawn>& GetPawns() const { return m_pawns; }

    // Check if any pawn is attacking the player at given position
    bool IsPlayerAttacked(Vector3 playerPos, float& outDamage);

    // --- Entity node management ---

    // Player start nodes
    void AddPlayerStart(const PlayerStartNode& node);
    void ClearPlayerStarts();
    const std::vector<PlayerStartNode>& GetPlayerStarts() const { return m_playerStarts; }
    PlayerStartNode* GetFirstPlayerStart();

    // Pickup nodes
    int AddPickup(const PickupNode& node);
    void RemovePickup(int id);
    void ClearPickups();
    std::vector<PickupNode>& GetPickups() { return m_pickups; }
    const std::vector<PickupNode>& GetPickups() const { return m_pickups; }
    PickupNode* GetPickup(int id);
    void UpdatePickups(float dt, Vector3 playerPos, BoundingBox playerBounds);

    // Zone volume nodes
    int AddZone(const ZoneVolumeNode& node);
    void RemoveZone(int id);
    void ClearZones();
    std::vector<ZoneVolumeNode>& GetZones() { return m_zones; }
    const std::vector<ZoneVolumeNode>& GetZones() const { return m_zones; }
    ZoneVolumeNode* GetZone(int id);
    ZoneVolumeNode* CheckZoneCollision(Vector3 pos, BoundingBox bounds);

    // Sound/music emitter nodes
    int AddEmitter(const EmitterNode& node);
    void RemoveEmitter(int id);
    void ClearEmitters();
    std::vector<EmitterNode>& GetEmitters() { return m_emitters; }
    const std::vector<EmitterNode>& GetEmitters() const { return m_emitters; }

    // Draw entities (billboards for player starts, pickups, zones, emitters)
    void DrawEntities(Camera3D& camera);

    // Sky zone tracking â€” returns true if player is inside a ZONE_SKY volume
    bool IsInSkyZone() const { return m_inSkyZone; }
    BoundingBox GetSkyZoneBounds() const { return m_activeSkyZoneBounds; }
    void UpdateSkyZone(Vector3 playerPos, BoundingBox playerBounds);

    // Access registered definitions
    const std::vector<PawnDef>& GetDefs() const { return m_defs; }

    // Singleton
    static PawnSystem& Instance();

private:
    std::vector<Pawn> m_pawns;
    std::vector<int> m_freeIds;
    std::vector<PawnDef> m_defs;
    uint32_t m_nextId = 1;

    // Entity node storage
    std::vector<PlayerStartNode> m_playerStarts;
    std::vector<PickupNode> m_pickups;
    std::vector<ZoneVolumeNode> m_zones;
    std::vector<EmitterNode> m_emitters;
    uint32_t m_nextEntityId = 1;

    // Sky zone state
    bool m_inSkyZone = false;
    BoundingBox m_activeSkyZoneBounds = {{0,0,0},{0,0,0}};

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
