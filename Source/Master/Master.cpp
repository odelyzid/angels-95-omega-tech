// AngelMaster — standalone master server for Angels95 / OmegaTech.
//
// Collects heartbeats from public game servers and serves the live list to
// clients (HTTP/JSON) — a lightweight GameSpy/Unreal-style master.
//
//   UDP  heartbeat : OZHEARTBEAT:gamename=angels95;...
//   HTTP GET       : /api/servers?gamename=angels95
//   HTTP POST      : /api/heartbeat   (same payload as UDP)
//
// Raw sockets only, no raylib. Usage:
//   AngelMaster [--port 27900] [--http-port 27950] [--max-servers 4096]

#include "MasterProtocol.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <algorithm>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
    #define MSOCK(fd) ((SOCKET)(intptr_t)(fd))
    #define MCLOSE(fd) closesocket(MSOCK(fd))
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #define MSOCK(fd) (fd)
    #define MCLOSE(fd) ::close(fd)
#endif

// Log.hpp after the platform headers: on Windows, <windows.h> defines an
// ERROR macro that would otherwise mangle LogLevel::ERROR.
#include "../Log.hpp"

using namespace master;

static std::atomic<bool> g_running{true};
static std::mutex g_state_mutex;
static std::mutex g_print_mutex;
static std::unordered_map<std::string, ServerEntry> g_servers;   // key: ip:port
static std::unordered_map<std::string, long long> g_last_accept_ms; // key: source ip
static int g_max_servers = MAX_SERVERS;
static std::string g_gamename = GAMENAME;

static long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
static long long now_epoch() { return (long long)time(nullptr); }

static void log_line(LogLevel level, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Log::write(level, __FILE__, __LINE__, "%s", buf);
}

static void signal_handler(int) { g_running = false; }

static bool init_winsock() {
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA d;
        if (WSAStartup(MAKEWORD(2, 2), &d) != 0) return false;
        done = true;
    }
#endif
    return true;
}

// ---------------------------------------------------------------------------
// Registration (shared by UDP + HTTP heartbeat paths)
// ---------------------------------------------------------------------------
static std::string heartbeat_key(const std::string& ip, uint16_t port) {
    return ip + ":" + std::to_string(port);
}

// Returns false when rate-limited or invalid.
static bool apply_heartbeat(const Heartbeat& hb, const std::string& source_ip,
                            bool from_http) {
    if (hb.port == 0) return false;
    std::string ip = hb.public_ip.empty() ? source_ip : hb.public_ip;
    if (ip.empty()) return false;

    // Rate limit per source IP (1.5s). The public_ip field is client-supplied
    // and cannot be used to bypass the per-source limit.
    long long now = now_ms();
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        auto it = g_last_accept_ms.find(source_ip);
        if (it != g_last_accept_ms.end() && now - it->second < 1500) return false;
        g_last_accept_ms[source_ip] = now;
    }

    std::string key = heartbeat_key(ip, hb.port);
    std::lock_guard<std::mutex> lock(g_state_mutex);
    auto it = g_servers.find(key);
    bool is_new = (it == g_servers.end());
    if (is_new && (int)g_servers.size() >= g_max_servers) return false;

    ServerEntry& e = g_servers[key];
    e.ip = ip;
    e.port = hb.port;
    e.http_port = hb.http_port;
    e.name = hb.host.empty() ? ("Angels95 Server " + ip) : hb.host;
    e.map = hb.map;
    e.gamever = hb.gamever.empty() ? GAMEVER_DEFAULT : hb.gamever;
    e.gamename = GAMENAME;
    e.players = hb.players;
    e.maxplayers = hb.maxplayers ? hb.maxplayers : 32;
    e.password = hb.password;
    e.last_seen = (double)now_epoch();

    log_line(LogLevel::INFO, "Master: %s heartbeat %s:%u (%u/%u, map=%s)%s",
             is_new ? "new" : "update", ip.c_str(), (unsigned)e.port,
             e.players, e.maxplayers, e.map.c_str(), from_http ? " [http]" : "");
    return true;
}

static void prune_expired_locked() {
    long long now = now_epoch();
    for (auto it = g_servers.begin(); it != g_servers.end();) {
        if (IsExpired(it->second, now)) {
            log_line(LogLevel::INFO, "Master: expired %s (no heartbeat for %ds)",
                     it->first.c_str(), EXPIRE_SECONDS);
            it = g_servers.erase(it);
        } else {
            ++it;
        }
    }
}

static std::vector<ServerEntry> snapshot_servers(const std::string& gamename) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    prune_expired_locked();
    std::vector<ServerEntry> out;
    for (auto& kv : g_servers) {
        if (!gamename.empty() && kv.second.gamename != gamename) continue;
        out.push_back(kv.second);
    }
    std::sort(out.begin(), out.end(), [](const ServerEntry& a, const ServerEntry& b) {
        if (a.players != b.players) return a.players > b.players;
        return a.name < b.name;
    });
    return out;
}

// ---------------------------------------------------------------------------
// HTTP server
// ---------------------------------------------------------------------------
static int http_listen(int port) {
    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int yes = 1;
    setsockopt(MSOCK(fd), SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((u_short)port);
    if (bind(MSOCK(fd), (struct sockaddr*)&addr, sizeof(addr)) < 0) { MCLOSE(fd); return -1; }
    if (listen(MSOCK(fd), 16) < 0) { MCLOSE(fd); return -1; }
    return fd;
}

static void write_all(int fd, const char* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        int n = (int)send(MSOCK(fd), data + off, (int)(len - off), 0);
        if (n <= 0) break;
        off += (size_t)n;
    }
}

static void http_response(int fd, int code, const char* status,
                          const std::string& body, const char* type) {
    char hdr[512];
    int hl = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        code, status, type, body.size());
    write_all(fd, hdr, (size_t)hl);
    write_all(fd, body.c_str(), body.size());
}

static std::string get_query_param(const char* path, const char* key) {
    const char* q = strchr(path, '?');
    if (!q) return "";
    ++q;
    size_t klen = strlen(key);
    while (*q) {
        if (strncmp(q, key, klen) == 0 && q[klen] == '=') {
            q += klen + 1;
            std::string out;
            while (*q && *q != '&') out.push_back(*q++);
            return out;
        }
        while (*q && *q != '&') ++q;
        if (*q == '&') ++q;
    }
    return "";
}

static void handle_http_client(int cfd, const std::string& client_ip) {
    char req[8192];
    int n = (int)recv(MSOCK(cfd), req, sizeof(req) - 1, 0);
    if (n <= 0) { MCLOSE(cfd); return; }
    req[n] = '\0';

    char method[16] = {0}, path[1024] = {0};
    sscanf(req, "%15s %1023s", method, path);

    if (strcmp(method, "GET") == 0 &&
        (strcmp(path, "/api/servers") == 0 || strncmp(path, "/api/servers?", 12) == 0)) {
        std::string gn = get_query_param(path, "gamename");
        if (gn.empty()) gn = g_gamename;
        auto servers = snapshot_servers(gn);
        std::string json = BuildServerListJson(servers, gn);
        http_response(cfd, 200, "OK", json, "application/json");
        MCLOSE(cfd);
        return;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/stats") == 0) {
        auto servers = snapshot_servers(g_gamename);
        uint32_t players = 0;
        for (auto& s : servers) players += s.players;
        std::string json = "{\"ok\":true,\"gamename\":\"" + std::string(g_gamename) +
                           "\",\"servers\":" + std::to_string(servers.size()) +
                           ",\"players\":" + std::to_string(players) + "}";
        http_response(cfd, 200, "OK", json, "application/json");
        MCLOSE(cfd);
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/heartbeat") == 0) {
        // Body is the same key=value payload as the UDP heartbeat.
        const char* body = strstr(req, "\r\n\r\n");
        body = body ? body + 4 : (strstr(req, "\n\n") ? strstr(req, "\n\n") + 2 : "");
        Heartbeat hb;
        if (DecodeHeartbeat(body, hb) && apply_heartbeat(hb, client_ip, true)) {
            http_response(cfd, 200, "OK", "{\"ok\":true}", "application/json");
        } else {
            http_response(cfd, 400, "Bad Request",
                          "{\"ok\":false,\"error\":\"invalid_or_rate_limited\"}",
                          "application/json");
        }
        MCLOSE(cfd);
        return;
    }

    http_response(cfd, 404, "Not Found", "{\"ok\":false,\"error\":\"not found\"}",
                  "application/json");
    MCLOSE(cfd);
}

static void http_server_thread(int port) {
    int lfd = http_listen(port);
    if (lfd < 0) {
        log_line(LogLevel::ERROR, "Master: HTTP listen failed on port %d", port);
        return;
    }
    log_line(LogLevel::INFO, "Master: HTTP server on port %d (/api/servers, /api/heartbeat)", port);

    fd_set master_fds;
    FD_ZERO(&master_fds);
    FD_SET(MSOCK(lfd), &master_fds);
    while (g_running) {
        fd_set read_fds = master_fds;
        struct timeval tv = {1, 0};
        if (select(lfd + 1, &read_fds, nullptr, nullptr, &tv) < 0) {
            if (g_running) break;
            continue;
        }
        if (!FD_ISSET(MSOCK(lfd), &read_fds)) continue;
        struct sockaddr_in addr;
        socklen_t alen = sizeof(addr);
        int cfd = (int)accept(MSOCK(lfd), (struct sockaddr*)&addr, &alen);
        if (cfd < 0) continue;
        char ip[64] = {0};
        inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        handle_http_client(cfd, ip);
    }
    MCLOSE(lfd);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    int udp_port = DEFAULT_UDP_PORT;
    int http_port = DEFAULT_HTTP_PORT;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)           udp_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--http-port") == 0 && i + 1 < argc) http_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--max-servers") == 0 && i + 1 < argc) g_max_servers = atoi(argv[++i]);
        else if (strcmp(argv[i], "--gamename") == 0 && i + 1 < argc)  g_gamename = argv[++i];
        else if (strcmp(argv[i], "--help") == 0) {
            printf("AngelMaster -- Angels95 / OmegaTech master server\n");
            printf("Usage: AngelMaster [--port 27900] [--http-port 27950] [--max-servers 4096]\n");
            printf("  --port         UDP heartbeat listener (default 27900)\n");
            printf("  --http-port    HTTP list/API port (default 27950)\n");
            printf("  --max-servers  Maximum tracked game servers (default 4096)\n");
            return 0;
        }
    }
    if (g_max_servers <= 0) g_max_servers = MAX_SERVERS;

    if (!init_winsock()) { fprintf(stderr, "AngelMaster: winsock init failed\n"); return 1; }
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // UDP heartbeat socket (non-blocking).
    int ufd = (int)socket(AF_INET, SOCK_DGRAM, 0);
    if (ufd < 0) { fprintf(stderr, "AngelMaster: UDP socket failed\n"); return 1; }
    {
        int reuse = 1;
        setsockopt(MSOCK(ufd), SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
#ifdef _WIN32
        u_long nb = 1;
        ioctlsocket(MSOCK(ufd), FIONBIO, &nb);
#else
        int flags = fcntl(ufd, F_GETFL, 0);
        fcntl(ufd, F_SETFL, flags | O_NONBLOCK);
#endif
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons((u_short)udp_port);
        if (bind(MSOCK(ufd), (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            fprintf(stderr, "AngelMaster: UDP bind failed on %d\n", udp_port);
            return 1;
        }
    }

    log_line(LogLevel::INFO, "AngelMaster starting (gamename=%s)", g_gamename.c_str());
    printf("Master UDP:  %d\n", udp_port);
    printf("Master HTTP: %d\n", http_port);

    std::thread http_thread(http_server_thread, http_port);

    long long last_prune = 0;
    while (g_running) {
        struct sockaddr_in sender;
        socklen_t slen = sizeof(sender);
        char buf[1024];
        int n;
        while ((n = (int)recvfrom(MSOCK(ufd), buf, sizeof(buf) - 1, 0,
                                  (struct sockaddr*)&sender, &slen)) > 0) {
            buf[n] = '\0';
            Heartbeat hb;
            if (!DecodeHeartbeat(buf, hb)) continue;
            char ip[64] = {0};
            inet_ntop(AF_INET, &sender.sin_addr, ip, sizeof(ip));
            if (apply_heartbeat(hb, ip, false)) {
                const char* ack = "OZMASTEROK";
                sendto(MSOCK(ufd), ack, (int)strlen(ack), 0,
                       (struct sockaddr*)&sender, slen);
            }
        }
        long long now = now_ms();
        if (now - last_prune > 5000) {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            prune_expired_locked();
            last_prune = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    printf("Shutting down master...\n");
    MCLOSE(ufd);
    if (http_thread.joinable()) http_thread.join();
    return 0;
}
