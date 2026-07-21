#ifndef OMEGA_NETWORK_HPP
#define OMEGA_NETWORK_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <ctime>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifdef _MSC_VER
        #pragma comment(lib, "ws2_32.lib")
    #endif
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
#endif

namespace net {

constexpr uint32_t MAGIC = 0x4F5A574F;
constexpr int PROTOCOL_VERSION = 1;
constexpr size_t MAX_MESSAGE_SIZE = 1024;
constexpr int HEARTBEAT_INTERVAL = 5;
constexpr int TIMEOUT_INTERVAL = 30;
constexpr int DISCOVERY_PORT = 27100;
constexpr int MAX_PLAYERS = 32;
constexpr int PLAYER_NAME_MAX = 64;
constexpr int IP_STRING_MAX = 48;

struct NetVec3 {
    float x, y, z;
};

enum class MessageType : uint32_t {
    PING = 1,
    PONG = 2,
    PLAYER_JOIN = 3,
    PLAYER_LEAVE = 4,
    PLAYER_UPDATE = 5,
    GAME_STATE = 6,
    CHAT = 7,
    COMMAND = 8,
    FILE_TRANSFER = 9,
    SCENE_UPDATE = 10,
    // Phase 2: extended game state types
    PICKUP_COLLECT = 11,
    PICKUP_RESPAWN = 12,
    NPC_STATE_UPDATE = 13,
    XP_UPDATE = 14,
    PLAYER_HURT = 15,
    PLAYER_KILL = 16,
    PLAYER_ACTION = 17,
    PICKUP_COLLECTED = 18
};

struct NetworkPlayer {
    uint32_t id;
    char name[PLAYER_NAME_MAX];
    char ip_address[IP_STRING_MAX];
    uint16_t port;
    bool connected;
    double last_ping;
    double last_seen;
};

#pragma pack(push, 1)
struct NetworkMessage {
    uint32_t magic;
    uint32_t type;
    uint32_t size;
    uint32_t sequence;
    uint32_t timestamp;
    uint8_t payload[MAX_MESSAGE_SIZE];
};
#pragma pack(pop)

struct PlayerUpdateData {
    uint32_t player_id;  // 0 for client→server; server fills when relaying
    NetVec3 position;
    float yaw;
    float pitch;
    float health;
    float mana;
    float psychic_energy;
    int level;
    int xp;
    int inventory[5]; // Objects 1-5 ownership flags
};

struct PickupCollectData {
    uint32_t player_id;
    int pickup_id;
    int world_index;
};

struct PickupRespawnData {
    int pickup_id;
    int world_index;
    NetVec3 position;
    int type;       // PickupType as int
    int value;
};

struct NpcStateUpdateData {
    int world_index;
    int npc_index;       // index in global_npcs or partition_npcs
    int partition_index; // -1 for global NPCs
    NetVec3 position;
    float yaw;
    int state;          // NpcState as int
    int health;
    bool active;
};

struct XpUpdateData {
    uint32_t player_id;
    int xp;
    int level;
    int xp_to_next;
};

struct PlayerHurtData {
    uint32_t player_id; // victim
    int damage;
    float remaining_health;
};

struct PlayerKillData {
    uint32_t killer_id;
    uint32_t victim_id;
};

struct PickupCollectedData {
    uint32_t player_id;
    int pickup_id;
    int item_id;
    int quantity;
};

struct WeaponFireData {
    uint32_t player_id;
    float origin_x, origin_y, origin_z;
    float dir_x, dir_y, dir_z;
    int weapon_type; // 1 = wand/energy bolt
    int power;       // damage multiplier
};

struct ChatData {
    char text[256];
};

struct CommandData {
    char cmd[64];
    char args[256];
};

struct SceneUpdateData {
    uint32_t data_size;
    char data[MAX_MESSAGE_SIZE - sizeof(uint32_t)];
};

struct ServerCallbacks {
    std::function<void(NetworkPlayer& player)> on_player_join;
    std::function<void(NetworkPlayer& player)> on_player_leave;
    std::function<void(const NetworkMessage& msg, const NetworkPlayer& sender)> on_message_received;
};

struct ClientCallbacks {
    std::function<void()> on_connected;
    std::function<void()> on_disconnected;
    std::function<void(const NetworkMessage& msg)> on_message_received;
};

// ---------------------------------------------------------------------------
// Platform socket helpers (internal)
// ---------------------------------------------------------------------------
#ifdef _WIN32
    #define TO_SOCK(fd)  ((SOCKET)(intptr_t)(fd))
    #define sock_fd_good(fd) ((int)(fd) >= 0)
    #define sock_fd_bad(fd)  ((int)(fd) < 0)
    #define close_sock(fd)  closesocket(TO_SOCK(fd))
    #define sock_set_opt(opt,optlen) (const char*)(opt),(optlen)
    #define sock_sendto_buf(buf,len) (const char*)(buf),(int)(len)
    #define sock_recvfrom_buf(buf,len) (char*)(buf),(int)(len)
#else
    #define TO_SOCK(fd)  (fd)
    #define sock_fd_good(fd) ((fd) >= 0)
    #define sock_fd_bad(fd)  ((fd) < 0)
    #define close_sock(fd)  close(fd)
    #define sock_set_opt(opt,optlen) (opt),(optlen)
    #define sock_sendto_buf(buf,len) (buf),(len)
    #define sock_recvfrom_buf(buf,len) (buf),(len)
#endif

// ---------------------------------------------------------------------------
// NetworkServer
// ---------------------------------------------------------------------------
class NetworkServer {
public:
    NetworkServer();
    ~NetworkServer();
    NetworkServer(const NetworkServer&) = delete;
    NetworkServer& operator=(const NetworkServer&) = delete;
    NetworkServer(NetworkServer&&) = delete;
    NetworkServer& operator=(NetworkServer&&) = delete;

    bool init(uint16_t port, uint32_t max_players = MAX_PLAYERS);
    bool start();
    void stop();
    void update();

    void set_callbacks(ServerCallbacks cb) { m_callbacks = std::move(cb); }

    bool send_message(const NetworkPlayer& player, const NetworkMessage& msg);
    bool broadcast_message(const NetworkMessage& msg);

    uint32_t player_count() const { return m_player_count; }
    const std::vector<NetworkPlayer>& players() const { return m_players; }
    bool is_running() const { return m_running; }

private:
    bool        m_winsock_initialized = false;
    int         m_socket_fd = -1;
    struct sockaddr_in m_address;
    uint16_t    m_port = 0;
    bool        m_running = false;
    std::vector<NetworkPlayer> m_players;
    uint32_t    m_player_count = 0;
    uint32_t    m_message_sequence = 0;
    double      m_last_heartbeat = 0;
    ServerCallbacks m_callbacks;
};

// ---------------------------------------------------------------------------
// NetworkClient
// ---------------------------------------------------------------------------
class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();
    NetworkClient(const NetworkClient&) = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;

    bool connect(const char* server_ip, uint16_t port);
    void disconnect();
    void update();
    bool send_message(const NetworkMessage& msg);
    void set_callbacks(ClientCallbacks cb) { m_callbacks = std::move(cb); }

    bool is_connected() const { return m_connected; }
    bool is_connecting() const { return m_connecting; }
    const std::string& get_server_ip() const { return m_server_ip; }
    uint16_t server_port() const { return m_server_port; }
    double get_last_ping_time() const { return m_last_ping_time; }
    double get_last_pong_time() const { return m_last_pong_time; }
    double get_rtt_s() const;
    int get_ping_ms() const;

private:
    bool        m_winsock_initialized = false;
    int         m_socket_fd = -1;
    struct sockaddr_in m_server_address;
    bool        m_connected = false;
    bool        m_connecting = false;
    std::string m_server_ip;
    uint16_t    m_server_port = 0;
    uint32_t    m_message_sequence = 0;
    double      m_last_ping_time = 0;
    double      m_last_pong_time = 0;
    ClientCallbacks m_callbacks;
};

// ---------------------------------------------------------------------------
// NetworkDiscovery
// ---------------------------------------------------------------------------
class NetworkDiscovery {
public:
    NetworkDiscovery();
    ~NetworkDiscovery();
    NetworkDiscovery(const NetworkDiscovery&) = delete;
    NetworkDiscovery& operator=(const NetworkDiscovery&) = delete;

    bool init(const char* game_name, const char* game_version, uint16_t port);
    bool start();
    void stop();
    void update();
    bool send_request();
    bool parse_response(const char* response,
                        std::string& out_name,
                        std::string& out_version,
                        uint32_t& out_cur_players,
                        uint32_t* out_max_players);

    bool is_running() const { return m_running; }

private:
    bool        m_winsock_initialized = false;
    int         m_socket_fd = -1;
    struct sockaddr_in m_broadcast_address;
    bool        m_running = false;
    std::string m_game_name;
    std::string m_game_version;
    uint16_t    m_game_port = 0;
    uint32_t    m_max_players = 32;
    uint32_t    m_current_players = 0;
};

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
const char* message_type_string(MessageType type);
bool is_valid_ip(const char* ip);
uint16_t find_free_port();

} // namespace net

#endif // OMEGA_NETWORK_HPP