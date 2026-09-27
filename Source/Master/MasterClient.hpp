#ifndef OMEGA_MASTER_CLIENT_HPP
#define OMEGA_MASTER_CLIENT_HPP

// Game-server -> master uplink used by AngelServ. Runs on a background thread
// so the dedicated server's 10 Hz tick is never blocked by name resolution or
// HTTP. Sends the heartbeat over UDP (`--master host:port`) and/or HTTP
// (`--master-http http://host:port`).

#include "MasterProtocol.hpp"
#include "MasterHttp.hpp"
#include "../Log.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <cstring>
#include <cstdio>

namespace master {

inline long long uplink_now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

inline std::string AppendHeartbeatPath(const std::string& url) {
    size_t scheme = url.find("://");
    size_t slash = (scheme == std::string::npos) ? url.find('/') : url.find('/', scheme + 3);
    if (slash == std::string::npos) return url + "/api/heartbeat";
    if (slash + 1 >= url.size()) return url + "api/heartbeat";
    // A non-root path is treated as an explicit heartbeat endpoint.
    return url;
}

class MasterUplink {
public:
    MasterUplink() = default;
    ~MasterUplink() { Stop(); }
    MasterUplink(const MasterUplink&) = delete;
    MasterUplink& operator=(const MasterUplink&) = delete;

    void SetMasters(const std::vector<std::string>& udp_masters,
                    const std::vector<std::string>& http_masters,
                    const std::string& public_ip) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_udp_requests = udp_masters;
        m_http_requests.clear();
        for (const auto& u : http_masters) m_http_requests.push_back(AppendHeartbeatPath(u));
        m_status.public_ip = public_ip;
    }

    void SetStatus(const std::string& name, uint16_t game_port, uint16_t http_port,
                   const std::string& map, uint32_t players, uint32_t maxplayers,
                   bool password) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status.host = name;
        m_status.port = game_port;
        m_status.http_port = http_port;
        m_status.map = map;
        m_status.players = players;
        m_status.maxplayers = maxplayers;
        m_status.password = password;
        m_status.gamever = GAMEVER_DEFAULT;
        m_status.gamename = GAMENAME;
    }

    bool HasMasters() const {
        return !m_udp_requests.empty() || !m_http_requests.empty();
    }

    void Start() {
        if (m_running) return;
        if (!HasMasters()) return;
        m_running = true;
        m_thread = std::thread([this] { Loop(); });
    }

    void Stop() {
        if (!m_running) return;
        m_running = false;
        if (m_thread.joinable()) m_thread.join();
    }

private:
    void Loop() {
        long long last_send = 0; // 0 => send immediately on first pass
        while (m_running) {
            long long now = uplink_now_ms();
            if (last_send == 0 || now - last_send >= (long long)HEARTBEAT_INTERVAL_S * 1000) {
                SendAll();
                last_send = uplink_now_ms();
            }
            for (int i = 0; i < 10 && m_running; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void SendAll() {
        Heartbeat hb;
        std::vector<std::string> udp_targets;
        std::vector<std::string> http_targets;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            hb = m_status;
            udp_targets = m_udp_requests;
            http_targets = m_http_requests;
        }
        std::string line = EncodeHeartbeat(hb);
        SendUdp(udp_targets, line);
        SendHttp(http_targets, line);
    }

    void SendUdp(const std::vector<std::string>& targets, const std::string& line) {
        if (targets.empty()) return;
#ifdef _WIN32
        static bool wsa = false;
        if (!wsa) { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); wsa = true; }
#endif
        int fd = (int)socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return;
        for (const auto& t : targets) {
            std::string host = t;
            uint16_t port = (uint16_t)DEFAULT_UDP_PORT;
            size_t colon = t.rfind(':');
            if (colon != std::string::npos) {
                host = t.substr(0, colon);
                long p = strtol(t.substr(colon + 1).c_str(), nullptr, 10);
                if (p > 0 && p <= 65535) port = (uint16_t)p;
            }
            struct addrinfo hints;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            struct addrinfo* res = nullptr;
            char ps[16];
            snprintf(ps, sizeof(ps), "%u", (unsigned)port);
            if (getaddrinfo(host.c_str(), ps, &hints, &res) != 0 || !res) continue;
            for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
                if (sendto((SOCKET)(intptr_t)fd, line.c_str(), (int)line.size(), 0,
                           ai->ai_addr, (socklen_t)ai->ai_addrlen) > 0) {
                    OZ_INFO("Master uplink: UDP heartbeat -> %s:%u", host.c_str(), (unsigned)port);
                    break;
                }
            }
            freeaddrinfo(res);
        }
        MHTTP_CLOSE(fd);
    }

    void SendHttp(const std::vector<std::string>& targets, const std::string& line) {
        for (const auto& url : targets) {
            std::string resp;
            int status = 0;
            if (HttpRequest("POST", url, line, "text/plain", 4000, resp, &status))
                OZ_INFO("Master uplink: HTTP heartbeat -> %s (status %d)", url.c_str(), status);
            else
                OZ_WARN("Master uplink: HTTP heartbeat failed -> %s", url.c_str());
        }
    }

    mutable std::mutex m_mutex;
    std::vector<std::string> m_udp_requests;
    std::vector<std::string> m_http_requests;
    Heartbeat m_status;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

} // namespace master

#endif // OMEGA_MASTER_CLIENT_HPP