#pragma once
#include "raylib.h"
#include "../Renderer/LitLightning.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

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

// Zone environment overrides — fog/ambient/reverb applied on zone entry at runtime.
// Separate from the editor's ZoneProperties (which has GameType/Particle fields too).
struct ZoneEnvOverrides {
    // Fog (defaults match legacy hardcoded fallback in Core.hpp)
    int fogR = 179, fogG = 179, fogB = 204;
    float fogDensity = 1.0f;
    float fogStart = 10.0f, fogEnd = 100.0f;
    bool applyFog = false;
    // Ambient
    int ambR = 180, ambG = 180, ambB = 200;
    float ambIntensity = 0.4f;
    bool applyAmbient = false;
    // Reverb
    float reverbMix = 0.0f;
    float reverbDecay = 0.0f;
};

// Zone volume types
enum class ZoneType : uint8_t {
    ZONE_WATER = 0,
    ZONE_LADDER = 1,
    ZONE_SKY = 2,
    ZONE_REVERB = 3,
    ZONE_GAMEPLAY_SOUND = 4,
    ZONE_PORTAL = 5
};

// Level metadata — per-world game rules + environment defaults (LevelInfo/Particles)
struct LevelSettings {
    int gameType = 0;              // matches editor GameType enum order
    int maxPlayers = 8;
    float respawnTime = 5.0f;
    bool timeLimitEnabled = false;
    float timeLimitMinutes = 10.0f;
    int scoreLimit = 50;
    bool friendlyFire = false;
    std::string skyboxPath;        // empty = world default (Models/Skybox.png)
    std::string skyboxSidePath;    // optional horizon/side sky texture (cap = skyboxPath)
    // Ambient particle weather
    int particleType = 0;          // 0=none, 1=snow, 2=rain, 3=void, 4=psychic
    float particleDensity = 50.0f;
    float particleSpeed = 1.0f;
    int particleR = 200, particleG = 200, particleB = 200;
    float particleWindX = 0.0f, particleWindZ = 0.0f;
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


// TODO: Gun/WEapon PawnDefs for stat block
// Template definition shared between spawn calls; stored in an internal
// registry so Spawn() can be called by name.
struct PawnDef {
    std::string name;           // logical name, e.g. "Walker"
    float speed = 1.5f;
    float aggroRange = 6.0f;
    float attackRange = 1.5f;
    float damage = 10.0f;
    int maxHealth = 100;
    std::string sprite_path;    // texture path (convention: GameData/Global/Pawn/<name>.png)
    std::string scream_path;    // sound path (convention: GameData/Global/Pawn/<name>.wav)
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
    std::string defName;    // name of the pawn definition (e.g., "Walker", "Skaarj")
    int scriptInstanceIndex = -1; // LightningEntityManager instance index, -1 = none
};

// Player start node - position and orientation for player spawn
struct PlayerStartNode {
    uint32_t id = 0;
    Vector3 position{0, 0, 0};
    float yaw = 0.0f;
};

// Projectile node - fired by weapons
struct ProjectileNode {
    uint32_t id = 0;
    Vector3 position{0, 0, 0};
    Vector3 velocity{0, 0, 0};
    float lifetime = 2.0f;
    float age = 0.0f;
    float damage = 10.0f;
    float speed = 20.0f;
    int ownerId = -1;          // pawn or player id that fired it
    bool active = true;
    Texture2D* sprite = nullptr; // optional trail/glow texture
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
    int priority = 0;         // higher = wins when overlapping
    std::string name;        // logical name for LightningScript zone lookups
    GameplaySoundProfile soundProfile; // game-type-specific audio profile
    ZoneEnvOverrides envOverrides; // environment overrides (fog, ambient, reverb)
};

// ZonePortal — connects two zones / two LEVELS (enable zone transitions + campaigns)
struct ZonePortal {
    BoundingBox bounds;           // trigger volume
    std::string fromZoneName;     // source zone name (or "" for any)
    std::string toZoneName;       // target zone name (or "" for world default)
    std::string targetWorld;      // destination level folder name in GameData/Worlds/ ("" = unassigned)
    Vector3 targetSpawn{0, 20, 0}; // player position on arrival in targetWorld
    Vector3 teleportOffset;       // legacy: position delta on same-level transition
    bool bidirectional = true;
    bool enabled = true;
};

// WorldInfo — global world metadata + default environment fallback
struct WorldInfo {
    ZoneEnvOverrides defaultEnv;  // defaults: fog, ambient, reverb
    std::string defaultSkybox;
    std::string defaultMusic;
    std::vector<ZonePortal> portals;  // legacy same-level portals (level links live in m_portals)
    BoundingBox worldBounds;
    std::string name;
    std::string author;
    LevelSettings settings;       // game rules + weather metadata (LevelInfo/Particles)
};

// PointRegion — per-entity zone tracking with stacking support
struct PointRegion {
    int lastPrimaryZoneId = -1;    // previous frame's primary zone
    int primaryZoneId = -1;        // current frame's primary zone
    ZoneType primaryZoneType = ZoneType::ZONE_WATER; // type of primary zone
    std::unordered_set<int> activeZoneIds;   // all overlapping zones this frame
    std::unordered_set<int> enteredZoneIds;  // zones entered this frame
    std::unordered_set<int> exitedZoneIds;   // zones exited this frame
    ZoneEnvOverrides combinedEnv;            // merged from all active zones

    void Rebuild(const std::vector<ZoneVolumeNode*>& activeZones);
    bool HasZoneType(ZoneType type) const { return primaryZoneId >= 0 && primaryZoneType == type; }
    bool HasZoneId(int id) const;
    bool HasChanged() const { return primaryZoneId != lastPrimaryZoneId; }
    void CommitFrame();
};

// Sky zone node — runtime state for isolated skybox chamber rendering
struct SkyZoneNode {
    uint32_t id = 0;
    Vector3 position{0,0,0};      // camera origin inside skybox chamber
    BoundingBox bounds;            // trigger volume for player detection
    Vector3 rotation{0,0,0};       // current sky orientation / angular velocity
    Vector3 scrollSpeed{0,0,0};    // UV scroll speed for cloud/stars layers
    float fov = 60.0f;             // sky camera field of view
    bool bHighDetail = false;      // high/low detail variant toggle
    uint32_t skyEntityInstance = UINT32_MAX; // index into LightningEntityManager
    std::string skyboxPath;        // current skybox texture path
    Texture2D skyboxTex{0};        // loaded skybox texture (unloaded on clear)
    std::string name;              // matching .ozls entity name for script hookup
    const struct EntityDef* def = nullptr; // resolved .ozls def (world-scoped)
    bool active = false;
    float intensity = 1.0f;
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

    // Draw billboard sprites for every active pawn (optional lit shader for fog/lighting)
    void DrawAll(Camera3D& camera, Shader litShader = {0});

    // Access individual pawns
    Pawn* Get(int id);
    int Count() const { return (int)m_pawns.size(); }
    const std::vector<Pawn>& GetPawns() const { return m_pawns; }

    // Check if any pawn is attacking the player at given position
    bool IsPlayerAttacked(Vector3 playerPos, float& outDamage);

    // Feedback state for UI (last collected pickup info)
    struct PickupFeedback {
        bool collected = false;
        std::string typeName;
        int itemId = 0;
        float flashTimer = 0.0f;
    };
    PickupFeedback m_pickupFeedback;

    // --- Entity node management ---

    // Player start nodes
    void AddPlayerStart(const PlayerStartNode& node);
    void RemovePlayerStart(int id);
    void ClearPlayerStarts();
    std::vector<PlayerStartNode>& GetPlayerStarts() { return m_playerStarts; }
    const std::vector<PlayerStartNode>& GetPlayerStarts() const { return m_playerStarts; }
    PlayerStartNode* GetFirstPlayerStart();
    void RespawnPlayerAtStart(Camera3D& camera);

    // Projectile nodes
    int SpawnProjectile(const ProjectileNode& node);
    void UpdateProjectiles(float dt);
    void DrawProjectiles(Camera3D& camera);
    void ClearProjectiles();
    std::vector<ProjectileNode>& GetProjectiles() { return m_projectiles; }
    const std::vector<ProjectileNode>& GetProjectiles() const { return m_projectiles; }

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

    // Portal nodes — level-to-level connections (campaign system)
    int AddPortal(const ZonePortal& node);
    void RemovePortal(int id);
    void ClearPortals();
    std::vector<ZonePortal>& GetPortals() { return m_portals; }
    const std::vector<ZonePortal>& GetPortals() const { return m_portals; }
    ZonePortal* GetPortal(int id);
    // Returns the first enabled portal whose volume contains pos/bounds, nullptr if none
    ZonePortal* CheckPortalCollision(Vector3 pos, BoundingBox bounds);

    // Consolidated multi-zone query — returns all overlapping zones sorted by priority
    std::vector<ZoneVolumeNode*> GetActiveZones(Vector3 pos, BoundingBox bounds);

    // WorldInfo management
    void SetWorldInfo(const WorldInfo& wi) { m_worldInfo = wi; }
    const WorldInfo& GetWorldInfo() const { return m_worldInfo; }
    WorldInfo& GetWorldInfo() { return m_worldInfo; }

    // Player point-region tracking (enter/exit detection + combined env)
    PointRegion& GetPlayerRegion() { return m_playerRegion; }
    const PointRegion& GetPlayerRegion() const { return m_playerRegion; }
    void UpdatePlayerRegion(Vector3 playerPos, BoundingBox playerBounds);

    // Sound/music emitter nodes
    int AddEmitter(const EmitterNode& node);
    void RemoveEmitter(int id);
    void ClearEmitters();
    std::vector<EmitterNode>& GetEmitters() { return m_emitters; }
    const std::vector<EmitterNode>& GetEmitters() const { return m_emitters; }

    // Draw entities (billboards for player starts, pickups, zones, emitters)
    void DrawEntities(Camera3D& camera, Shader litShader = {0});
    void ClearWeaponPickupCache();

    // Sky zone node management
    int AddSkyZone(const SkyZoneNode& node);
    void RemoveSkyZone(int id);
    void ClearSkyZones();
    SkyZoneNode* GetSkyZone(int id);
    std::vector<SkyZoneNode>& GetSkyZones() { return m_skyZones; }
    const std::vector<SkyZoneNode>& GetSkyZones() const { return m_skyZones; }
    void SetActiveSkyZone(int index);
    SkyZoneNode* GetActiveSkyZone();
    int GetActiveSkyZoneIndex() const { return m_activeSkyZoneIndex; }

    // Sky zone tracking â€” returns true if player is inside a ZONE_SKY volume
    bool IsInSkyZone() const { return m_activeSkyZoneIndex >= 0; }
    BoundingBox GetSkyZoneBounds() const {
        if (m_activeSkyZoneIndex >= 0 && m_activeSkyZoneIndex < (int)m_skyZones.size())
            return m_skyZones[m_activeSkyZoneIndex].bounds;
        return {{0,0,0},{0,0,0}};
    }
    void UpdateSkyZone(Vector3 playerPos, BoundingBox playerBounds);

    // Synchronize LightningEntityManager pending skybox state into active SkyZoneNode
    void SyncSkyboxState();

    // Light node management
    int AddLight(const LightNode& node);
    void RemoveLight(int id);
    void ClearLights();
    std::vector<LightNode>& GetLights() { return m_lights; }
    const std::vector<LightNode>& GetLights() const { return m_lights; }
    LightNode* GetLight(int id);

    // Access registered definitions
    const std::vector<PawnDef>& GetDefs() const { return m_defs; }

    // Singleton
    static PawnSystem& Instance();

private:
    std::vector<Pawn> m_pawns;
    std::vector<int> m_freeIds;
    std::vector<PawnDef> m_defs;
    std::vector<LightNode> m_lights;
    uint32_t m_nextId = 1;

    // Entity node storage
    std::vector<PlayerStartNode> m_playerStarts;
    std::vector<ProjectileNode> m_projectiles;
    std::vector<PickupNode> m_pickups;
    std::vector<ZoneVolumeNode> m_zones;
    std::vector<ZonePortal> m_portals;
    std::vector<EmitterNode> m_emitters;
    uint32_t m_nextEntityId = 1;
    uint32_t m_nextLightId = 1;

    // Sky zone state
    std::vector<SkyZoneNode> m_skyZones;
    int m_activeSkyZoneIndex = -1;

    // Weapon pickup model cache (keyed by typeName)
    struct WeaponPickupCache {
        Model model{0};
        Texture2D texture{0};
    };
    std::unordered_map<std::string, WeaponPickupCache> m_weaponPickupCache;

    // Portal visual: shared double-sided quad + EFX shimmer texture (lazy loaded)
    Model m_portalQuadModel{0};
    Texture2D m_portalTexture{0};
    bool m_portalVisualReady = false;
    void EnsurePortalVisual();
    void UnloadPortalVisual();

    // World metadata + zone portal system
    WorldInfo m_worldInfo;

    // Player zone tracking (enter/exit detection, combined env)
    PointRegion m_playerRegion;

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
