#ifndef OMEGA_CLIENT_HPP
#define OMEGA_CLIENT_HPP

#include "../Network/Network.hpp"
#include <string>
#include <vector>
#include <functional>
#include <mutex>

// Client-side NPC representation for rendering
struct ClientNPC {
    int world_index;
    int npc_index;
    int partition_index;
    net::NetVec3 position{0,0,0};
    float yaw = 0;
    int state = 0;     // NpcState as int
    int health = 100;
    bool active = true;
};

// Client-side pickup representation
struct ClientPickup {
    int id;
    int world_index;
    net::NetVec3 position{0,0,0};
    int type = 0;
    int value = 0;
    bool active = true;
};

// Remote player representation (other players connected)
struct RemotePlayer {
    uint32_t player_id;
    net::NetVec3 position{0,0,0};
    float yaw = 0;
    float pitch = 0;
    float health = 100;
    bool active = false;
    uint32_t color_packed = 0xFFFFFFFF; // RGBA
};

// Projectile from weapon fire for rendering
struct ClientProjectile {
    net::NetVec3 origin{0,0,0};
    net::NetVec3 direction{0,0,0};
    float spawn_time = 0;
    int weapon_type = 1;
    uint32_t owner_id = 0;
    uint32_t color_packed = 0xFFFFFFFF; // RGBA
};

// Game client networking module.
// Connects to oz_server and syncs player position / receives updates.

class OmegaClient {
public:
    OmegaClient() = default;
    ~OmegaClient() { disconnect(); }
    OmegaClient(const OmegaClient&) = delete;
    OmegaClient& operator=(const OmegaClient&) = delete;

    // Connect to a server. Returns true on success.
    bool connect(const char* ip, uint16_t port);

    // Disconnect from server
    void disconnect();

    // Call every frame: sends player position, processes incoming messages
    void update(float cam_x, float cam_y, float cam_z,
                float cam_yaw, float cam_pitch);

    // Send a chat message
    void send_chat(const char* text);

    // Send pickup collect request
    void send_pickup_collect(int pickup_id, int world_index);

    // Send weapon fire action
    void send_weapon_fire(float ox, float oy, float oz,
                          float dx, float dy, float dz,
                          int weapon_type, int power = 10);

    // Status
    bool is_connected() const { return m_client.is_connected(); }
    int get_ping_ms() const { return m_client.get_ping_ms(); }
    const std::string& get_server_ip() const { return m_client.get_server_ip(); }

    // Incoming scene data — call from game loop to apply dynamic WDL
    std::string consume_pending_scene_data();

    // Incoming chat messages
    std::string consume_pending_chat_message();

    // Access received NPC / pickup state
    const std::vector<ClientNPC>& npcs() const { return m_npcs; }
    const std::vector<ClientPickup>& pickups() const { return m_pickups; }
    const std::vector<RemotePlayer>& remote_players() const { return m_remote_players; }
    const std::vector<ClientProjectile>& projectiles() const { return m_projectiles; }
    int get_xp() const { return m_xp; }
    int get_level() const { return m_level; }
    int get_xp_to_next() const { return m_xp_to_next; }

    // Callbacks for game integration
    void set_on_scene_received(std::function<void(const std::string&)> cb) {
        m_on_scene_received = std::move(cb);
    }
    void set_on_chat_received(std::function<void(const std::string&)> cb) {
        m_on_chat_received = std::move(cb);
    }

private:
    net::NetworkClient m_client;
    std::string m_pending_scene;
    std::string m_chat_msg;
    std::mutex m_msg_mutex;

    std::vector<ClientNPC> m_npcs;
    std::vector<ClientPickup> m_pickups;
    std::vector<RemotePlayer> m_remote_players;
    std::vector<ClientProjectile> m_projectiles;
    int m_xp = 0;
    int m_level = 1;
    int m_xp_to_next = 100;

    std::function<void(const std::string&)> m_on_scene_received;
    std::function<void(const std::string&)> m_on_chat_received;

    void handle_message(const net::NetworkMessage& msg);
    void on_connected();
    void on_disconnected();
};

#endif // OMEGA_CLIENT_HPP