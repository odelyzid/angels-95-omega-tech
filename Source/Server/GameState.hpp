#ifndef OZGAME_STATE_HPP
#define OZGAME_STATE_HPP

#include "../Network/Network.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <ctime>

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------
using NetVec3 = net::NetVec3;

// ---------------------------------------------------------------------------
// Player gameplay state (server side)
// ---------------------------------------------------------------------------
struct ServerPlayer {
    uint32_t id;
    char name[64];
    uint16_t port;
    NetVec3 position{0, 0, 0};
    float yaw = 0, pitch = 0;
    float health = 100.0f;
    float max_health = 100.0f;
    float mana = 0.0f;
    float max_mana = 100.0f;
    float psychic_energy = 0.0f;
    float max_psychic_energy = 100.0f;
    int level = 1;
    int xp = 0;
    int xp_to_next = 100;
    int inventory[5] = {0, 0, 0, 0, 0};  // indices 0-4 match Objects 1-5
    int world_index = 0;
    double last_seen;

    bool connected = false;
    int health_ticks = 0; // for regen
    int mana_ticks = 0;
    int penergy_ticks = 0;
};

// ---------------------------------------------------------------------------
// NpcEntity (from Angels95, C++20)
// ---------------------------------------------------------------------------
enum class NpcState : uint8_t { IDLE, PATROL, CHASE, RETURN };

constexpr const char* npc_state_string(NpcState s) {
    switch (s) {
        case NpcState::IDLE:   return "idle";
        case NpcState::PATROL: return "patrol";
        case NpcState::CHASE:  return "chase";
        case NpcState::RETURN: return "return";
        default:               return "?";
    }
}

struct ServerNPC {
    bool active = true;
    NpcState state = NpcState::PATROL;
    NetVec3 position{0, 0, 0};
    NetVec3 velocity{0, 0, 0};
    float yaw = 0;
    float speed = 1.5f;
    float patrol_radius = 3.0f;
    NetVec3 spawn_pos{0, 0, 0};
    float state_timer = 0;
    float state_accumulator = 0; // for timed state transitions
    int health = 100;
    int max_health = 100;
    float aggro_range = 6.0f;    // distance to start chasing
    float return_range = 15.0f;  // distance from spawn to start returning
    float give_up_range = 20.0f; // distance from spawn to give up chase
    float damage = 10.0f;
    float attack_range = 1.5f;
    int attack_cooldown = 0;      // ticks between attacks
    int attack_cooldown_max = 30; // 3 seconds at 10 ticks/s
};

// ---------------------------------------------------------------------------
// Pickup system (from Angels95 pickup_system.h)
// ---------------------------------------------------------------------------
enum class PickupType : uint8_t {
    HEALTH = 0,
    MANA,
    PSYCHIC,
    ARMOR,
    WEAPON,
    AMMO,
    KEY,
    COIN,
    POWERUP
};

constexpr uint32_t PICKUP_TYPE_COUNT = 9;

constexpr const char* pickup_type_str(PickupType t) {
    switch (t) {
        case PickupType::HEALTH:   return "heal";
        case PickupType::MANA:     return "mana";
        case PickupType::PSYCHIC:  return "psychic";
        case PickupType::ARMOR:    return "armor";
        case PickupType::WEAPON:   return "weapon";
        case PickupType::AMMO:     return "ammo";
        case PickupType::KEY:      return "key";
        case PickupType::COIN:     return "coin";
        case PickupType::POWERUP:  return "powerup";
        default:                   return "?";
    }
}

constexpr int pickup_default_value(PickupType t) {
    switch (t) {
        case PickupType::HEALTH:   return 25;
        case PickupType::MANA:     return 20;
        case PickupType::PSYCHIC:  return 15;
        case PickupType::ARMOR:    return 50;
        case PickupType::WEAPON:   return 1;
        case PickupType::AMMO:     return 30;
        case PickupType::KEY:      return 1;
        case PickupType::COIN:     return 1;
        case PickupType::POWERUP:  return 1;
        default:                   return 0;
    }
}

constexpr float pickup_default_respawn(PickupType t) {
    switch (t) {
        case PickupType::HEALTH:   return 30.0f;
        case PickupType::MANA:     return 30.0f;
        case PickupType::PSYCHIC:  return 25.0f;
        case PickupType::ARMOR:    return 45.0f;
        case PickupType::WEAPON:   return 0.0f; // never respawn
        case PickupType::AMMO:     return 20.0f;
        case PickupType::KEY:      return 0.0f; // never respawn
        case PickupType::COIN:     return 0.0f; // never respawn
        case PickupType::POWERUP:  return 60.0f;
        default:                   return 30.0f;
    }
}

constexpr bool pickup_can_respawn(PickupType t) {
    return pickup_default_respawn(t) > 0.0f;
}

struct ServerPickup {
    int id;
    PickupType type;
    NetVec3 position{0, 0, 0};
    float rotation = 0;
    int value = 25;
    float respawn_time = 30.0f;
    float respawn_timer = 0.0f;  // counts up when inactive; respawns when >= respawn_time
    bool active = true;          // visible and collectable
    bool respawnable = true;
};

// ---------------------------------------------------------------------------
// World partition — coordinate-based area of interest management
// ---------------------------------------------------------------------------
constexpr int PARTITIONS_PER_WORLD = 64; // 8x8 grid
constexpr int PARTITION_ROWS = 8;
constexpr int PARTITION_COLS = 8;

struct WorldPartition {
    int id = -1;
    float min_x, max_x, min_z, max_z;
    std::vector<ServerNPC> npcs;
    std::vector<ServerPickup> pickups;
};

// ---------------------------------------------------------------------------
// World state — one per loaded world
// ---------------------------------------------------------------------------
struct WorldState {
    int world_index = 0;
    std::string name;
    float world_min_x = -2000.0f, world_max_x = 2000.0f;
    float world_min_z = -2000.0f, world_max_z = 2000.0f;
    size_t partition_rows = PARTITION_ROWS;
    size_t partition_cols = PARTITION_COLS;
    std::vector<WorldPartition> partitions;
    std::vector<ServerNPC> global_npcs;   // not partition-locked
    std::vector<ServerPickup> global_pickups; // not partition-locked
};

// XP constants
constexpr int XP_PER_KILL = 20;
constexpr int XP_BASE_TO_NEXT = 100;
constexpr float XP_GROWTH_FACTOR = 1.3f;

// ---------------------------------------------------------------------------
// Server Game State class
// ---------------------------------------------------------------------------
class GameState {
public:
    GameState();
    ~GameState();

    // Prevent copy
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;

    // Initialization
    void init_worlds(const std::string& gamedata_dir, const std::vector<std::string>& world_list);
    void init_global_npcs_and_pickups(WorldState& ws);

    // Player management
    uint32_t add_player(uint32_t id, const char* name); // returns player id
    void remove_player(uint32_t id);
    ServerPlayer* get_player(uint32_t id);
    void update_player_position(uint32_t id, float x, float y, float z, float yaw, float pitch);
    int player_count() const { return m_player_count; }
    const std::vector<ServerPlayer>& players() const { return m_players; }

    // NPC AI tick
    void tick_npcs(WorldState& ws, float dt);

    // Pickup tick
    void tick_pickups(WorldState& ws, float dt);

    // Collect a pickup
    bool collect_pickup(uint32_t player_id, int pickup_id, int world_index,
                        PickupType* out_type = nullptr, int* out_value = nullptr);
    void respawn_pickup(WorldState& ws, ServerPickup& pickup);

    // XP
    void add_xp(uint32_t player_id, int amount);
    int xp_needed_for_level(int level) const;

    // Damage / killing
    void damage_npc(ServerNPC& npc, int amount);
    void damage_player(ServerPlayer& player, int amount);

    // Partition helpers
    int get_partition_index(const WorldState& ws, float x, float z) const;
    WorldPartition* get_partition(WorldState& ws, int idx);
    void get_partitions_in_range(const WorldState& ws, int center_idx, std::vector<int>& out_indices, int range) const;

    // Tick all worlds
    void tick(float dt);

    // Access to worlds
    std::vector<WorldState>& worlds() { return m_worlds; }
    WorldState* get_world(int idx);

    // Save/load
    void save_world_state(const WorldState& ws, const std::string& gamedata_dir);
    void load_world_state(WorldState& ws, const std::string& gamedata_dir);
    void save_player_data();
    void load_player_data();

private:
    std::vector<ServerPlayer> m_players;
    int m_player_count = 0;
    uint32_t m_next_player_id = 1;
    std::vector<WorldState> m_worlds;

    static int get_player_xp_level(int xp);
};

#endif // OZGAME_STATE_HPP
