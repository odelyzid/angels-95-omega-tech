#include "GameState.hpp"
#include "../Log.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <limits>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
GameState::GameState() {
    m_players.reserve(net::MAX_PLAYERS);
}

GameState::~GameState() {
}

// ---------------------------------------------------------------------------
// XP calculation
// ---------------------------------------------------------------------------
int GameState::xp_needed_for_level(int level) const {
    if (level <= 0) return 1;
    // XP needed doubles each level approximately
    float needed = XP_BASE_TO_NEXT * std::pow(XP_GROWTH_FACTOR, level - 1);
    return std::max(1, (int)needed);
}

int GameState::get_player_xp_level(int xp) {
    int level = 1;
    int needed = XP_BASE_TO_NEXT;
    while (xp >= needed) {
        xp -= needed;
        level++;
        needed = (int)(needed * XP_GROWTH_FACTOR);
        if (level > 100) break; // hard cap
    }
    return level;
}

// ---------------------------------------------------------------------------
// World initialization
// ---------------------------------------------------------------------------
void GameState::init_worlds(const std::string& gamedata_dir,
                             const std::vector<std::string>& world_list) {
    m_worlds.clear();
    for (size_t i = 0; i < world_list.size(); i++) {
        WorldState ws;
        ws.world_index = (int)i;
        ws.name = world_list[i];

        // Default world bounds; could be extended with a config file
        ws.world_min_x = -2000.0f;
        ws.world_max_x = 2000.0f;
        ws.world_min_z = -2000.0f;
        ws.world_max_z = 2000.0f;

        // Create partitions
        ws.partitions.resize(PARTITIONS_PER_WORLD);
        float dx = (ws.world_max_x - ws.world_min_x) / PARTITION_COLS;
        float dz = (ws.world_max_z - ws.world_min_z) / PARTITION_ROWS;
        for (int p = 0; p < PARTITION_ROWS; p++) {
            for (int q = 0; q < PARTITION_COLS; q++) {
                int idx = p * PARTITION_COLS + q;
                ws.partitions[idx].id = idx;
                ws.partitions[idx].min_x = ws.world_min_x + q * dx;
                ws.partitions[idx].max_x = ws.world_min_x + (q + 1) * dx;
                ws.partitions[idx].min_z = ws.world_min_z + p * dz;
                ws.partitions[idx].max_z = ws.world_min_z + (p + 1) * dz;
            }
        }

        // Populate with NPCs and pickups
        init_global_npcs_and_pickups(ws);

        // Attempt to load saved state
        load_world_state(ws, gamedata_dir);

        m_worlds.push_back(ws);
        OZ_INFO("World %s initialized with %d partitions, %zu global NPCs, %zu pickups",
                ws.name.c_str(), PARTITIONS_PER_WORLD,
                 ws.global_npcs.size(), ws.global_pickups.size());
    }
}

void GameState::init_global_npcs_and_pickups(WorldState& ws) {
    // Spawn NPCs for this world (10 NPCs, similar to Angels95's spawn_npcs)
    for (int i = 0; i < 10; i++) {
        ServerNPC npc;
        float angle = (2 * 3.14159265f / 10) * i;
        float radius = 10.0f + (i % 5) * 2.0f;
        npc.spawn_pos.x = radius * std::cos(angle);
        npc.spawn_pos.y = 0;
        npc.spawn_pos.z = radius * std::sin(angle);
        npc.position = npc.spawn_pos;
        npc.speed = 1.5f + (i % 3) * 0.5f;
        npc.patrol_radius = 3.0f;
        npc.state = NpcState::PATROL;
        ws.global_npcs.push_back(npc);
    }

    // Spawn pickups: first ring near spawn for playability, rest across world
    srand(static_cast<unsigned>(ws.world_index + 1));
    static const PickupType kNearTypes[] = {
        PickupType::HEALTH, PickupType::MANA, PickupType::PSYCHIC,
        PickupType::KEY, PickupType::COIN, PickupType::POWERUP,
        PickupType::HEALTH, PickupType::MANA, PickupType::PSYCHIC,
        PickupType::COIN, PickupType::PSYCHIC, PickupType::POWERUP
    };
    for (int i = 0; i < 40; i++) {
        ServerPickup pickup;
        pickup.id = i;
        if (i < 12) {
            pickup.type = kNearTypes[i];
        } else {
            pickup.type = static_cast<PickupType>(rand() % PICKUP_TYPE_COUNT);
        }
        pickup.value = pickup_default_value(pickup.type);
        if (pickup.type == PickupType::PSYCHIC) {
            int idx = rand() % 9;
            pickup.value = (idx + 1) * 111;
        }
        pickup.respawn_time = pickup_default_respawn(pickup.type);
        pickup.respawnable = pickup_can_respawn(pickup.type);
        pickup.active = true;
        pickup.respawn_timer = 0.0f;

        float x, z, y;
        if (i < 12) {
            float angle = (2.0f * 3.14159265f / 12.0f) * (float)i;
            x = 8.0f * std::cos(angle);
            z = -10.0f + 8.0f * std::sin(angle);
            y = 18.0f;
        } else {
            x = ws.world_min_x + (rand() % 4000) - 2000.0f;
            z = ws.world_min_z + (rand() % 4000) - 2000.0f;
            x = std::max(ws.world_min_x, std::min(ws.world_max_x, x));
            z = std::max(ws.world_min_z, std::min(ws.world_max_z, z));
            y = 0.0f;
        }
        pickup.position = {x, y, z};
        pickup.rotation = rand() % 360;

        int part_idx = get_partition_index(ws, x, z);
        if (part_idx >= 0) {
            ws.partitions[part_idx].pickups.push_back(pickup);
        } else {
            ws.global_pickups.push_back(pickup);
        }
    }
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Player management
// ---------------------------------------------------------------------------
uint32_t GameState::add_player(uint32_t id, const char* name)
{
    if (m_player_count >= net::MAX_PLAYERS) {
        OZ_WARN("Cannot add player, server full");
        return 0;
    }
    ServerPlayer player;
    player.id = id;
    strncpy(player.name, name, sizeof(player.name) - 1);
    player.name[sizeof(player.name) - 1] = '\0';
    player.health = player.max_health;
    player.mana = 0.0f;
    player.psychic_energy = 0.0f;
    player.level = 1;
    player.xp = 0;
    player.xp_to_next = xp_needed_for_level(1);
    player.connected = true;
    player.last_seen = time(nullptr);
    player.world_index = 0;
    memset(player.inventory, 0, sizeof(player.inventory));

    // Find first free slot
    for (int i = 0; i < net::MAX_PLAYERS; i++) {
        if (!m_players[i].connected) {
            m_players[i] = player;
            m_player_count++;
            OZ_INFO("Player %s (id=%u) added at slot %d", player.name, player.id, i);
            return player.id;
        }
    }

    // Append (shouldn't reach here if MAX_PLAYERS is accurate)
    m_players.push_back(player);
    m_player_count++;
    return player.id;
}

void GameState::remove_player(uint32_t id) {
    for (auto &p : m_players) {
        if (p.id == id && p.connected) {
            p.connected = false;
            m_player_count--;
            OZ_INFO("Player %s (id=%u) removed", p.name, p.id);
            return;
        }
    }
}

ServerPlayer* GameState::get_player(uint32_t id) {
    for (auto &p : m_players) {
        if (p.id == id && p.connected) return &p;
    }
    return nullptr;
}

void GameState::update_player_position(uint32_t id, float x, float y, float z,
                                        float yaw, float pitch) {
    ServerPlayer* p = get_player(id);
    if (!p) return;
    p->position = {x, y, z};
    p->yaw = yaw;
    p->pitch = pitch;
    p->last_seen = time(nullptr);
}

// ---------------------------------------------------------------------------
// NPC state update from network
// ---------------------------------------------------------------------------
void GameState::update_npc_state(int world_index, int npc_index,
                                 const NetVec3& position, float yaw,
                                 NpcState state, int health, bool active) {
    WorldState* ws = get_world(world_index);
    if (!ws) return;
    // Check partition NPCs first, then global NPCs
    for (auto& part : ws->partitions) {
        if (npc_index >= 0 && npc_index < (int)part.npcs.size()) {
            ServerNPC& npc = part.npcs[npc_index];
            npc.position = position;
            npc.yaw = yaw;
            npc.state = state;
            npc.health = health;
            npc.active = active;
            return;
        }
    }
    // Try global NPCs
    if (npc_index >= 0 && npc_index < (int)ws->global_npcs.size()) {
        ServerNPC& npc = ws->global_npcs[npc_index];
        npc.position = position;
        npc.yaw = yaw;
        npc.state = state;
        npc.health = health;
        npc.active = active;
    }
}

// ---------------------------------------------------------------------------
// Partition helpers
// ---------------------------------------------------------------------------
int GameState::get_partition_index(const WorldState& ws, float x, float z) const {
    float fx = (x - ws.world_min_x) / (ws.world_max_x - ws.world_min_x);
    float fz = (z - ws.world_min_z) / (ws.world_max_z - ws.world_min_z);
    if (fx < 0 || fx >= 1 || fz < 0 || fz >= 1) return -1;
    int col = (int)(fx * PARTITION_COLS);
    int row = (int)(fz * PARTITION_ROWS);
    col = std::max(0, std::min(col, PARTITION_COLS - 1));
    row = std::max(0, std::min(row, PARTITION_ROWS - 1));
    return row * PARTITION_COLS + col;
}

WorldPartition* GameState::get_partition(WorldState& ws, int idx) {
    if (idx < 0 || idx >= (int)ws.partitions.size()) return nullptr;
    return &ws.partitions[idx];
}

void GameState::get_partitions_in_range(const WorldState& ws, int center_idx,
                                         std::vector<int>& out_indices, int range) const {
    out_indices.clear();
    if (center_idx < 0 || center_idx >= (int)ws.partitions.size()) return;
    int center_row = center_idx / PARTITION_COLS;
    int center_col = center_idx % PARTITION_COLS;
    for (int r = center_row - range; r <= center_row + range; r++) {
        for (int c = center_col - range; c <= center_col + range; c++) {
            if (r >= 0 && r < PARTITION_ROWS && c >= 0 && c < PARTITION_COLS) {
                out_indices.push_back(r * PARTITION_COLS + c);
            }
        }
    }
}

WorldState* GameState::get_world(int idx) {
    if (idx < 0 || idx >= (int)m_worlds.size()) return nullptr;
    return &m_worlds[idx];
}

// ---------------------------------------------------------------------------
// NPC AI tick
// ---------------------------------------------------------------------------
void GameState::tick_npcs(WorldState& ws, float dt) {
    auto process_npc = [&](ServerNPC& npc, int part_idx) {
        if (!npc.active) return;

        // Find nearest player in same or adjacent partitions
        ServerPlayer* nearest_player = nullptr;
        float nearest_dist = std::numeric_limits<float>::max();
        std::vector<int> nearby_parts;
        if (part_idx >= 0) {
            get_partitions_in_range(ws, part_idx, nearby_parts, 1);
            nearby_parts.push_back(part_idx);
        }

        for (auto& pp : m_players) {
            if (!pp.connected) continue;
            if (part_idx >= 0) {
                // Check if the player is in one of the nearby partitions
                int pp_part = get_partition_index(ws, pp.position.x, pp.position.z);
                if (pp_part < 0) continue;
                bool found = false;
                for (int np : nearby_parts) {
                    if (np == pp_part) { found = true; break; }
                }
                if (!found) continue;
            }
            float dx = pp.position.x - npc.position.x;
            float dy = pp.position.y - npc.position.y;
            float dz = pp.position.z - npc.position.z;
            float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < nearest_dist) {
                nearest_dist = dist;
                nearest_player = &pp;
            }
        }

        switch (npc.state) {
            case NpcState::IDLE:
                npc.state_timer += dt;
                if (npc.state_timer >= 3.0f) {
                    npc.state = NpcState::PATROL;
                    npc.state_timer = 0;
                }
                break;

            case NpcState::PATROL:
                if (nearest_player && nearest_dist < npc.aggro_range) {
                    npc.state = NpcState::CHASE;
                    npc.state_timer = 0;
                    break;
                }
                // Circular patrol around spawn
                {
                    float dx = npc.position.x - npc.spawn_pos.x;
                    float dz = npc.position.z - npc.spawn_pos.z;
                    float dist_to_spawn = std::sqrt(dx * dx + dz * dz);
                    if (dist_to_spawn > npc.patrol_radius) {
                        // Move back toward spawn point
                        float speed_factor = npc.speed * dt;
                        npc.position.x -= dx / dist_to_spawn * speed_factor;
                        npc.position.z -= dz / dist_to_spawn * speed_factor;
                    } else {
                        // Wander randomly (simulate patrol by moving in yaw direction)
                        npc.yaw += 10.0f * dt; // slowly turn
                        float rad = npc.yaw * 3.14159265f / 180.0f;
                        npc.position.x += npc.speed * dt * std::cos(rad);
                        npc.position.z += npc.speed * dt * std::sin(rad);
                    }
                }
                break;

            case NpcState::CHASE:
                if (!nearest_player) {
                    npc.state = NpcState::RETURN;
                    break;
                }
                // Check if player is too far from spawn
                {
                    float dx = npc.spawn_pos.x - nearest_player->position.x;
                    float dz = npc.spawn_pos.z - nearest_player->position.z;
                    float dist_player_spawn = std::sqrt(dx * dx + dz * dz);
                    if (dist_player_spawn > npc.give_up_range) {
                        npc.state = NpcState::RETURN;
                        break;
                    }
                }
                // Move toward player
                {
                    float dx = nearest_player->position.x - npc.position.x;
                    float dy = nearest_player->position.y - npc.position.y;
                    float dz = nearest_player->position.z - npc.position.z;
                    float dist_to_player = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (dist_to_player <= npc.attack_range) {
                        // Attack the player
                        if (npc.attack_cooldown <= 0) {
                            damage_player(*nearest_player, (int)npc.damage);
                            npc.attack_cooldown = npc.attack_cooldown_max;
                        }
                    } else {
                        float speed_factor = npc.speed * 1.5f * dt;
                        npc.position.x += (dx / dist_to_player) * speed_factor;
                        npc.position.y += (dy / dist_to_player) * speed_factor;
                        npc.position.z += (dz / dist_to_player) * speed_factor;
                    // Face player
                    npc.yaw = std::atan2(dz, dx) * 180.0f / 3.14159265f;
                    }
                }
                break;

            case NpcState::RETURN:
                {
                    float dx = npc.spawn_pos.x - npc.position.x;
                    float dz = npc.spawn_pos.z - npc.position.z;
                    float dist_to_spawn = std::sqrt(dx * dx + dz * dz);
                    if (dist_to_spawn <= 1.0f) {
                        npc.state = NpcState::IDLE;
                        npc.state_timer = 0;
                        npc.health = npc.max_health; // heal on return
                    } else {
                        float speed_factor = npc.speed * dt;
                        npc.position.x += (dx / dist_to_spawn) * speed_factor;
                        npc.position.z += (dz / dist_to_spawn) * speed_factor;
                    }
                }
                break;
        }

        // Decrement attack cooldown
        if (npc.attack_cooldown > 0) npc.attack_cooldown--;
    };

    // Process global NPCs
    for (auto &npc : ws.global_npcs) {
        process_npc(npc, -1);
    }

    // Process partition NPCs
    for (auto &part : ws.partitions) {
        for (auto &npc : part.npcs) {
            process_npc(npc, part.id);
        }
    }
}

// ---------------------------------------------------------------------------
// Pickup tick
// ---------------------------------------------------------------------------
void GameState::tick_pickups(WorldState& ws, float dt) {
    auto process_pickup = [&](ServerPickup& p) {
        if (!p.active && p.respawnable) {
            p.respawn_timer += dt;
            if (p.respawn_timer >= p.respawn_time) {
                respawn_pickup(ws, p);
            }
        }
    };

    for (auto &part : ws.partitions) {
        for (auto &p : part.pickups) {
            process_pickup(p);
        }
    }

    for (auto &p : ws.global_pickups) {
        process_pickup(p);
    }
}

bool GameState::collect_pickup(uint32_t player_id, int pickup_id, int world_index,
                               PickupType* out_type, int* out_value) {
    ServerPlayer* player = get_player(player_id);
    if (!player) return false;

    WorldState* ws = get_world(world_index);
    if (!ws) return false;

    // Find the pickup in partitions or global
    ServerPickup* pickup = nullptr;
    for (auto &part : ws->partitions) {
        for (auto &p : part.pickups) {
            if (p.id == pickup_id && p.active) {
                pickup = &p;
                break;
            }
        }
        if (pickup) break;
    }
    if (!pickup) {
        for (auto &p : ws->global_pickups) {
            if (p.id == pickup_id && p.active) {
                pickup = &p;
                break;
            }
        }
    }
    if (!pickup) return false;

    // Write out-params before marking inactive
    if (out_type)  *out_type  = pickup->type;
    if (out_value) *out_value = pickup->value;

    // Apply pickup effect
    switch (pickup->type) {
        case PickupType::HEALTH:
            player->health = std::min(player->max_health, player->health + pickup->value);
            break;
        case PickupType::MANA:
            player->mana = std::min(player->max_mana, player->mana + pickup->value);
            break;
        case PickupType::PSYCHIC:
            player->psychic_energy = std::min(player->max_psychic_energy, player->psychic_energy + pickup->value);
            break;
        case PickupType::ARMOR:
            player->health = std::min(player->max_health + 50, player->health + pickup->value);
            break;
        case PickupType::WEAPON: // Unlocks Object1-5
            for (int i = 0; i < 5; i++) {
                if (player->inventory[i] == 0) {
                    player->inventory[i] = 1;
                    break;
                }
            }
            break;
        case PickupType::AMMO:
        case PickupType::POWERUP:
        case PickupType::COIN:
        case PickupType::KEY:
            // Generic effect: add XP
            player->xp = std::min(player->xp + pickup->value, 999999);
            break;
    }

    // Mark pickup inactive
    pickup->active = false;
    pickup->respawn_timer = 0.0f;
    OZ_INFO("Player %s collected pickup %d (type %s) worth %d",
            player->name, pickup_id, pickup_type_str(pickup->type), pickup->value);
    return true;
}

void GameState::respawn_pickup(WorldState& ws, ServerPickup& pickup) {
    pickup.active = true;
    pickup.respawn_timer = 0.0f;
    OZ_INFO("Pickup %d (type %s) respawned", pickup.id, pickup_type_str(pickup.type));
}

// ---------------------------------------------------------------------------
// XP system
// ---------------------------------------------------------------------------
void GameState::add_xp(uint32_t player_id, int amount) {
    ServerPlayer* p = get_player(player_id);
    if (!p) return;
    p->xp += amount;
    int new_level = get_player_xp_level(p->xp);
    while (new_level > p->level) {
        p->level++;
        p->max_health += 10;  // Gain 10 HP per level
        p->max_mana += 5;     // Gain 5 MP per level
        p->health = p->max_health; // Full heal on level up
        p->mana = p->max_mana;
        p->xp_to_next = xp_needed_for_level(p->level);
        OZ_INFO("Player %s leveled up to %d!", p->name, p->level);
    }
    p->xp_to_next = xp_needed_for_level(p->level);
}

// ---------------------------------------------------------------------------
// Damage system
// ---------------------------------------------------------------------------
void GameState::damage_npc(ServerNPC& npc, int amount) {
    npc.health -= amount;
    if (npc.health <= 0) {
        npc.active = false;
        npc.health = 0;
        OZ_INFO("NPC killed at (%.2f, %.2f, %.2f)", npc.position.x, npc.position.y, npc.position.z);
    }
}

void GameState::damage_player(ServerPlayer& player, int amount) {
    // Simple armor reduction: 20% of damage is absorbed
    int absorbed = amount * 0.2f;
    int net_damage = amount - absorbed;
    player.health -= net_damage;
    if (player.health < 0) player.health = 0;
}

// ---------------------------------------------------------------------------
// Tick all worlds
// ---------------------------------------------------------------------------
void GameState::tick(float dt) {
    for (auto &world : m_worlds) {
        tick_npcs(world, dt);
        tick_pickups(world, dt);
    }

    // Player health/magic/penergy regeneration every tick
    for (auto &p : m_players) {
        if (!p.connected) continue;
        p.health_ticks++;
        p.mana_ticks++;
        p.penergy_ticks++;
        // Health regen: 1 point per 10 ticks (1 second at 10 ticks/s)
        if (p.health > 0 && p.health < p.max_health && p.health_ticks >= 10) {
            p.health++;
            p.health_ticks = 0;
        }
        // Mana regen
        if (p.mana < p.max_mana && p.mana_ticks >= 8) {
            p.mana++;
            p.mana_ticks = 0;
        }
        // Psychic energy regen
        if (p.psychic_energy < p.max_psychic_energy && p.penergy_ticks >= 12) {
            p.psychic_energy++;
            p.penergy_ticks = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Save / Load world state
// ---------------------------------------------------------------------------
void GameState::save_world_state(const WorldState& ws, const std::string& gamedata_dir) {
    // Save partition data
    for (size_t i = 0; i < ws.partitions.size(); i++) {
        // Build path
        char path[512];
        snprintf(path, sizeof(path), "%s/Saves/World%s/Partition%zu.dat",
                 gamedata_dir.c_str(), ws.name.c_str(), i);
        // Create directory if it doesn't exist
        std::string dir = gamedata_dir + "/Saves/World" + ws.name;
        #ifdef _WIN32
        mkdir(dir.c_str());
#else
        mkdir(dir.c_str(), 0755);
#endif
        std::ofstream out(path);
        if (!out) continue;
        // Serialize partition pickups and npcs in a simple CSV-like format
        for (auto &p : ws.partitions[i].pickups) {
            out << "PICKUP," << p.id << "," << (int)p.type << "," << p.position.x << "," << p.position.y << "," << p.position.z << ","
                << p.respawn_time << "," << p.respawn_timer << "," << p.value << ","
                << p.active << "," << p.respawnable << "\n";
        }
        for (auto &n : ws.partitions[i].npcs) {
            out << "NPC," << n.active << "," << (int)n.state << "," << n.position.x << "," << n.position.y << ","
                << n.position.z << "," << n.health << "," << n.state_timer << "\n";
        }
    }

    // Save global NPCs
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/Saves/World%s/GlobalNPCs.dat",
                 gamedata_dir.c_str(), ws.name.c_str());
        std::string dir = gamedata_dir + "/Saves/World" + ws.name;
        #ifdef _WIN32
        mkdir(dir.c_str());
#else
        mkdir(dir.c_str(), 0755);
#endif
        std::ofstream out(path);
        if (!out) return;
        for (auto &n : ws.global_npcs) {
            out << n.active << "," << (int)n.state << "," << n.position.x << "," << n.position.y << ","
                << n.position.z << "," << n.health << "," << n.state_timer << "," << n.speed << ","
                << n.patrol_radius << "," << n.spawn_pos.x << "," << n.spawn_pos.y << "," << n.spawn_pos.z << "\n";
        }
    }
}

void GameState::load_world_state(WorldState& ws, const std::string& gamedata_dir) {
    // Attempt to load global NPCs
    char path[512];
    snprintf(path, sizeof(path), "%s/Saves/World%s/GlobalNPCs.dat",
             gamedata_dir.c_str(), ws.name.c_str());
    std::ifstream infile(path);
    if (!infile.good()) {
        // No saved data, that's fine
        return;
    }
    OZ_INFO("Loading saved world state for %s", ws.name.c_str());
    
    // Read global NPCs
    std::string line;
    int npc_i = 0;
    while (std::getline(infile, line)) {
        if (npc_i >= (int)ws.global_npcs.size()) break;
        // Parse: active,state,px,py,pz,health,state_timer,speed,patrol_radius,spx,spy,spz
        int active, state;
        sscanf(line.c_str(), "%d,%d,%f,%f,%f,%d,%f,%f,%f,%f,%f,%f",
               &active, &state,
               &ws.global_npcs[npc_i].position.x,
               &ws.global_npcs[npc_i].position.y,
               &ws.global_npcs[npc_i].position.z,
               &ws.global_npcs[npc_i].health,
               &ws.global_npcs[npc_i].state_timer,
               &ws.global_npcs[npc_i].speed,
               &ws.global_npcs[npc_i].patrol_radius,
               &ws.global_npcs[npc_i].spawn_pos.x,
               &ws.global_npcs[npc_i].spawn_pos.y,
               &ws.global_npcs[npc_i].spawn_pos.z);
        ws.global_npcs[npc_i].active = (active != 0);
        ws.global_npcs[npc_i].state = static_cast<NpcState>(state);
        ws.global_npcs[npc_i].max_health = ws.global_npcs[npc_i].health;
        npc_i++;
    }
}
