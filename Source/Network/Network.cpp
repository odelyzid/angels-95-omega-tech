#include "Network.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <thread>
#include <chrono>
#ifndef _WIN32
#include <fcntl.h>
#endif

namespace net {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
static int winsock_init_counted = 0;

static bool winsock_init() {
#ifdef _WIN32
    if (winsock_init_counted++ > 0) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return false;
    }
#endif
    return true;
}

static void winsock_cleanup() {
#ifdef _WIN32
    if (winsock_init_counted > 0 && --winsock_init_counted == 0)
        WSACleanup();
#endif
}

static double now_seconds() {
    return (double)time(nullptr);
}

static void fd_set_nonblocking(int fd) {
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(TO_SOCK(fd), FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
const char* message_type_string(MessageType type) {
    switch (type) {
        case MessageType::PING:  return "PING";
        case MessageType::PONG:  return "PONG";
        case MessageType::PLAYER_JOIN:   return "PLAYER_JOIN";
        case MessageType::PLAYER_LEAVE:  return "PLAYER_LEAVE";
        case MessageType::PLAYER_UPDATE: return "PLAYER_UPDATE";
        case MessageType::GAME_STATE:    return "GAME_STATE";
        case MessageType::CHAT:          return "CHAT";
        case MessageType::COMMAND:       return "COMMAND";
        case MessageType::FILE_TRANSFER: return "FILE_TRANSFER";
        case MessageType::SCENE_UPDATE:  return "SCENE_UPDATE";
        default: return "UNKNOWN";
    }
}

bool is_valid_ip(const char* ip) {
    if (!ip) return false;
    struct sockaddr_in sa;
    return inet_pton(AF_INET, ip, &sa.sin_addr) == 1;
}

uint16_t find_free_port() {
    int fd = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;

    if (bind(TO_SOCK(fd), (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close_sock(fd);
        return 0;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(TO_SOCK(fd), (struct sockaddr*)&addr, &len) < 0) {
        close_sock(fd);
        return 0;
    }

    uint16_t port = ntohs(addr.sin_port);
    close_sock(fd);
    return port;
}

// ---------------------------------------------------------------------------
// NetworkServer
// ---------------------------------------------------------------------------
NetworkServer::NetworkServer() {}
NetworkServer::~NetworkServer() { stop(); }

bool NetworkServer::init(uint16_t port, uint32_t max_players) {
    if (!winsock_init()) return false;

    static_assert(sizeof(NetworkPlayer) <= MAX_MESSAGE_SIZE);

    m_port = port;
    m_players.resize(max_players);
    m_player_count = 0;
    m_message_sequence = 0;
    m_running = false;

    m_socket_fd = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd_bad(m_socket_fd)) {
        fprintf(stderr, "Server: socket creation failed\n");
        return false;
    }

    memset(&m_address, 0, sizeof(m_address));
    m_address.sin_family = AF_INET;
    m_address.sin_addr.s_addr = INADDR_ANY;
    m_address.sin_port = htons(port);

    int opt = 1;
    setsockopt(TO_SOCK(m_socket_fd), SOL_SOCKET, SO_REUSEADDR,
               sock_set_opt(&opt, sizeof(opt)));

    return true;
}

bool NetworkServer::start() {
    if (m_running || sock_fd_bad(m_socket_fd)) return false;

    if (bind(TO_SOCK(m_socket_fd), (struct sockaddr*)&m_address,
             sizeof(m_address)) < 0) {
        fprintf(stderr, "Server: bind failed on port %u\n", m_port);
        return false;
    }

    fd_set_nonblocking(m_socket_fd);
    m_running = true;
    m_last_heartbeat = now_seconds();

    printf("Server listening on UDP %u\n", m_port);
    return true;
}

void NetworkServer::stop() {
    if (!m_running) return;
    m_running = false;
    if (sock_fd_good(m_socket_fd)) {
        close_sock(m_socket_fd);
        m_socket_fd = -1;
    }
    for (auto& p : m_players) {
        if (p.connected) {
            p.connected = false;
            if (m_callbacks.on_player_leave) m_callbacks.on_player_leave(p);
        }
    }
    m_player_count = 0;
    winsock_cleanup();
}

void NetworkServer::update() {
    if (!m_running) return;

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    NetworkMessage msg;

    while (true) {
        ssize_t bytes = recvfrom(TO_SOCK(m_socket_fd),
                                 sock_recvfrom_buf(&msg, sizeof(msg)),
                                 0,
                                 (struct sockaddr*)&client_addr, &addr_len);
        if (bytes <= 0) break;

        if (static_cast<size_t>(bytes) < sizeof(NetworkMessage)) continue;
        if (msg.magic != MAGIC) continue;

        char client_ip[IP_STRING_MAX];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        uint16_t client_port = ntohs(client_addr.sin_port);

        NetworkPlayer* player = nullptr;
        for (auto& p : m_players) {
            if (p.connected &&
                std::strcmp(p.ip_address, client_ip) == 0 &&
                p.port == client_port) {
                player = &p;
                break;
            }
        }

        if (!player && m_player_count < m_players.size()) {
            for (auto& p : m_players) {
                if (!p.connected) {
                    player = &p;
                    player->id = static_cast<uint32_t>(&p - m_players.data());
                    std::snprintf(player->ip_address, sizeof(player->ip_address),
                                  "%s", client_ip);
                    player->port = client_port;
                    player->connected = true;
                    player->last_ping = now_seconds();
                    player->last_seen = now_seconds();
                    m_player_count++;
                    if (m_callbacks.on_player_join)
                        m_callbacks.on_player_join(p);
                    break;
                }
            }
        }

        if (player) {
            player->last_seen = now_seconds();

            switch (static_cast<MessageType>(msg.type)) {
                case MessageType::PING: {
                    NetworkMessage pong;
                    pong.magic = MAGIC;
                    pong.type = static_cast<uint32_t>(MessageType::PONG);
                    pong.size = 0;
                    pong.sequence = msg.sequence;
                    pong.timestamp = static_cast<uint32_t>(now_seconds());
                    send_message(*player, pong);
                    break;
                }
                case MessageType::PLAYER_JOIN:
                    printf("Player %s joined from %s:%u\n",
                           player->name, client_ip, client_port);
                    break;
                case MessageType::PLAYER_LEAVE:
                    printf("Player %s left\n", player->name);
                    player->connected = false;
                    m_player_count--;
                    if (m_callbacks.on_player_leave)
                        m_callbacks.on_player_leave(*player);
                    break;
                default:
                    if (m_callbacks.on_message_received)
                        m_callbacks.on_message_received(msg, *player);
                    break;
            }
        }
    }

    double now = now_seconds();

    // Timeouts
    for (auto& p : m_players) {
        if (p.connected && now - p.last_seen > TIMEOUT_INTERVAL) {
            printf("Player %s timed out\n", p.name);
            p.connected = false;
            m_player_count--;
            if (m_callbacks.on_player_leave)
                m_callbacks.on_player_leave(p);
        }
    }

    // Heartbeat
    if (now - m_last_heartbeat > HEARTBEAT_INTERVAL) {
        NetworkMessage hb;
        hb.magic = MAGIC;
        hb.type = static_cast<uint32_t>(MessageType::PING);
        hb.size = 0;
        hb.sequence = m_message_sequence++;
        hb.timestamp = static_cast<uint32_t>(now);
        broadcast_message(hb);
        m_last_heartbeat = now;
    }
}

bool NetworkServer::send_message(const NetworkPlayer& player,
                                 const NetworkMessage& msg) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(player.port);
    inet_pton(AF_INET, player.ip_address, &addr.sin_addr);

    ssize_t bytes = sendto(TO_SOCK(m_socket_fd),
                           sock_sendto_buf(&msg, sizeof(msg)), 0,
                           (struct sockaddr*)&addr, sizeof(addr));
    return bytes == static_cast<ssize_t>(sizeof(msg));
}

bool NetworkServer::broadcast_message(const NetworkMessage& msg) {
    bool ok = true;
    for (const auto& p : m_players) {
        if (p.connected && !send_message(p, msg)) ok = false;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// NetworkClient
// ---------------------------------------------------------------------------
NetworkClient::NetworkClient() {}
NetworkClient::~NetworkClient() { disconnect(); }

bool NetworkClient::connect(const char* server_ip, uint16_t port) {
    if (m_connected) return true;
    if (!winsock_init()) return false;

    m_socket_fd = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd_bad(m_socket_fd)) {
        fprintf(stderr, "Client: socket creation failed\n");
        return false;
    }

    fd_set_nonblocking(m_socket_fd);

    memset(&m_server_address, 0, sizeof(m_server_address));
    m_server_address.sin_family = AF_INET;
    m_server_address.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &m_server_address.sin_addr) <= 0) {
        fprintf(stderr, "Client: invalid IP %s\n", server_ip);
        close_sock(m_socket_fd);
        m_socket_fd = -1;
        return false;
    }

    m_server_ip = server_ip;
    m_server_port = port;
    m_connecting = true;
    m_last_ping_time = now_seconds();
    m_last_pong_time = now_seconds();

    NetworkMessage join;
    join.magic = MAGIC;
    join.type = static_cast<uint32_t>(MessageType::PLAYER_JOIN);
    join.size = 0;
    join.sequence = m_message_sequence++;
    join.timestamp = static_cast<uint32_t>(now_seconds());

    ssize_t bytes = sendto(TO_SOCK(m_socket_fd),
                           sock_sendto_buf(&join, sizeof(join)), 0,
                           (struct sockaddr*)&m_server_address,
                           sizeof(m_server_address));
    if (bytes == sizeof(join)) {
        m_connected = true;
        m_connecting = false;
        if (m_callbacks.on_connected) m_callbacks.on_connected();

        printf("Client: connected to %s:%u\n", server_ip, port);
        return true;
    }

    fprintf(stderr, "Client: connect failed to %s:%u\n", server_ip, port);
    close_sock(m_socket_fd);
    m_socket_fd = -1;
    m_connecting = false;
    return false;
}

void NetworkClient::disconnect() {
    if (!m_connected) return;

    NetworkMessage leave;
    leave.magic = MAGIC;
    leave.type = static_cast<uint32_t>(MessageType::PLAYER_LEAVE);
    leave.size = 0;
    leave.sequence = m_message_sequence++;
    leave.timestamp = static_cast<uint32_t>(now_seconds());

    sendto(TO_SOCK(m_socket_fd),
           sock_sendto_buf(&leave, sizeof(leave)), 0,
           (struct sockaddr*)&m_server_address, sizeof(m_server_address));

    m_connected = false;
    if (m_callbacks.on_disconnected) m_callbacks.on_disconnected();

    close_sock(m_socket_fd);
    m_socket_fd = -1;
    winsock_cleanup();
}

void NetworkClient::update() {
    if (!m_connected && !m_connecting) return;

    if (!m_connected) return;

    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    NetworkMessage msg;

    while (true) {
        ssize_t bytes = recvfrom(TO_SOCK(m_socket_fd),
                                 sock_recvfrom_buf(&msg, sizeof(msg)),
                                 0,
                                 (struct sockaddr*)&addr, &addr_len);
        if (bytes <= 0) break;
        if (static_cast<size_t>(bytes) < sizeof(NetworkMessage)) continue;
        if (msg.magic != MAGIC) continue;

        switch (static_cast<MessageType>(msg.type)) {
            case MessageType::PONG:
                m_last_pong_time = now_seconds();
                break;
            case MessageType::PING: {
                NetworkMessage pong;
                pong.magic = MAGIC;
                pong.type = static_cast<uint32_t>(MessageType::PONG);
                pong.size = 0;
                pong.sequence = msg.sequence;
                pong.timestamp = msg.timestamp;
                send_message(pong);
                break;
            }
            default:
                if (m_callbacks.on_message_received)
                    m_callbacks.on_message_received(msg);
                break;
        }
    }

    double now = now_seconds();
    if (now - m_last_pong_time > TIMEOUT_INTERVAL) {
        fprintf(stderr, "Server timeout, disconnecting\n");
        disconnect();
    }
}

bool NetworkClient::send_message(const NetworkMessage& msg) {
    if (!m_connected || sock_fd_bad(m_socket_fd)) return false;
    ssize_t bytes = sendto(TO_SOCK(m_socket_fd),
                           sock_sendto_buf(&msg, sizeof(msg)), 0,
                           (struct sockaddr*)&m_server_address,
                           sizeof(m_server_address));
    return bytes == static_cast<ssize_t>(sizeof(msg));
}

double NetworkClient::get_rtt_s() const {
    double diff = m_last_pong_time - m_last_ping_time;
    return diff < 0 ? 0 : diff;
}

int NetworkClient::get_ping_ms() const {
    return static_cast<int>(std::round(get_rtt_s() * 1000.0));
}

// ---------------------------------------------------------------------------
// NetworkDiscovery
// ---------------------------------------------------------------------------
NetworkDiscovery::NetworkDiscovery() {}
NetworkDiscovery::~NetworkDiscovery() { stop(); }

bool NetworkDiscovery::init(const char* game_name, const char* game_version,
                            uint16_t port) {
    if (!winsock_init()) return false;

    m_game_name = game_name ? game_name : "OmegaTech";
    m_game_version = game_version ? game_version : "0.1.0";
    m_game_port = port;
    return true;
}

bool NetworkDiscovery::start() {
    if (m_running) return true;
    if (m_game_port == 0) return false;

    m_socket_fd = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd_bad(m_socket_fd)) {
        fprintf(stderr, "Discovery: socket creation failed\n");
        return false;
    }

    int broadcast = 1;
    setsockopt(TO_SOCK(m_socket_fd), SOL_SOCKET, SO_BROADCAST,
               sock_set_opt(&broadcast, sizeof(broadcast)));

    int reuse = 1;
    setsockopt(TO_SOCK(m_socket_fd), SOL_SOCKET, SO_REUSEADDR,
               sock_set_opt(&reuse, sizeof(reuse)));

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(DISCOVERY_PORT);
    bind(TO_SOCK(m_socket_fd), (struct sockaddr*)&bind_addr, sizeof(bind_addr));

    memset(&m_broadcast_address, 0, sizeof(m_broadcast_address));
    m_broadcast_address.sin_family = AF_INET;
    m_broadcast_address.sin_addr.s_addr = INADDR_BROADCAST;
    m_broadcast_address.sin_port = htons(DISCOVERY_PORT);

    fd_set_nonblocking(m_socket_fd);
    m_running = true;
    return true;
}

void NetworkDiscovery::stop() {
    if (!m_running) return;
    m_running = false;
    if (sock_fd_good(m_socket_fd)) {
        close_sock(m_socket_fd);
        m_socket_fd = -1;
    }
    winsock_cleanup();
}

bool NetworkDiscovery::send_request() {
    if (sock_fd_bad(m_socket_fd)) {
        start();
    }

    const char* magic = "OZDISCOVER";
    ssize_t sent = sendto(TO_SOCK(m_socket_fd),
                          sock_sendto_buf(magic, strlen(magic)), 0,
                          (struct sockaddr*)&m_broadcast_address,
                          sizeof(m_broadcast_address));
    return sent == static_cast<ssize_t>(strlen(magic));
}

void NetworkDiscovery::update() {
    if (!m_running && sock_fd_bad(m_socket_fd)) return;

    if (!m_running) {
        start();
        if (!m_running) return;
    }

    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    char buf[256];

    ssize_t received;
    while ((received = recvfrom(TO_SOCK(m_socket_fd),
                                sock_recvfrom_buf(buf, sizeof(buf)-1),
                                0,
                                (struct sockaddr*)&sender, &sender_len)) > 0) {
        buf[received] = '\0';
    }

    static double last_announce = 0;
    double now = now_seconds();
    if (now - last_announce > 3.0) {
        char announce[128];
        int len = snprintf(announce, sizeof(announce),
                           "OZDISCOVER:%s:%s:%u",
                           m_game_name.c_str(), m_game_version.c_str(),
                           m_game_port);
        sendto(TO_SOCK(m_socket_fd),
               sock_sendto_buf(announce, (size_t)len), 0,
               (struct sockaddr*)&m_broadcast_address,
               sizeof(m_broadcast_address));
        last_announce = now;
    }
}

bool NetworkDiscovery::parse_response(const char* response,
                                      std::string& out_name,
                                      std::string& out_version,
                                      uint32_t& out_cur_players,
                                      uint32_t* out_max_players) {
    if (!response) return false;

    const char* prefix = "OZRESPONSE";
    size_t plen = strlen(prefix);
    if (std::strncmp(response, prefix, plen) != 0) return false;

    const char* p = response + plen + 1;
    const char* end = std::strchr(p, ':');
    if (!end) return false;
    out_name.assign(p, end - p);
    p = end + 1;

    end = std::strchr(p, ':');
    if (!end) return false;
    out_version.assign(p, end - p);
    p = end + 1;

    end = std::strchr(p, ':');
    if (!end) return false;
    std::string sport(p, end - p);
    uint16_t port = static_cast<uint16_t>(std::stoul(sport));
    (void)port;
    p = end + 1;

    uint32_t cur = 0, max = 0;
    std::sscanf(p, "%u/%u", &cur, &max);
    out_cur_players = cur;
    if (out_max_players) *out_max_players = max;

    return true;
}

} // namespace net