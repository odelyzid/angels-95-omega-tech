#ifndef OMEGA_INTERNET_BROWSER_HPP
#define OMEGA_INTERNET_BROWSER_HPP

// Client-side internet server browser. Fetches the live list from one or more
// master servers (HTTP/JSON) on a background thread, then queries each game
// server's own /status endpoint for live player counts and real RTT.
//
// All network work happens off the render thread; the menu calls Refresh() to
// start and Update() each frame to pick up results.

#include "../Network/MasterProtocol.hpp"
#include "../Network/MasterHttp.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstdio>

namespace master {

struct OnlineServer {
    ServerEntry entry;
    bool live = false;    // /status query succeeded
    int  ping_ms = -1;    // RTT of the /status request (real round trip)
};

class InternetBrowser {
public:
    InternetBrowser() = default;
    ~InternetBrowser() { Cancel(); }
    InternetBrowser(const InternetBrowser&) = delete;
    InternetBrowser& operator=(const InternetBrowser&) = delete;

    void SetMasters(std::vector<std::string> urls) { m_urls = std::move(urls); }

    bool IsScanning() const { return m_running.load(); }
    bool HasResult() const { return m_have_result; }
    double LastScanSeconds() const { return m_last_seconds; }
    const std::vector<OnlineServer>& Servers() const { return m_servers; }
    const std::string& Error() const { return m_error; }

    void Refresh() {
        if (m_running.load()) return;
        Cancel();
        m_cancel.store(false);
        m_finished.store(false);
        m_running.store(true);
        m_last_seconds = 0.0;
        m_thread = std::thread([this] { Worker(); });
    }

    // Non-blocking; joins the worker and publishes its results when done.
    void Update() {
        if (!m_running.load()) return;
        if (!m_finished.load()) return;
        m_running.store(false);
        if (m_thread.joinable()) m_thread.join();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_servers = std::move(m_staged);
        m_error = m_staged_error;
        m_have_result = true;
    }

    void Cancel() {
        m_cancel.store(true);
        if (m_thread.joinable()) m_thread.join();
        m_running.store(false);
        m_finished.store(false);
    }

private:
    static std::string ServersUrl(const std::string& base) {
        // Explicit endpoint supplied (already points at /api/servers)
        if (base.find("/api/servers") != std::string::npos) return base;

        // Otherwise append the endpoint to the base, handling trailing slashes
        // and path prefixes such as "https://host/master".
        std::string b = base;
        while (!b.empty() && b.back() == '/') b.pop_back();
        return b + "/api/servers?gamename=" + std::string(GAMENAME);
    }

    void Worker() {
        long long t0 = mhttp_now_ms();
        std::vector<OnlineServer> found;
        std::string err;

        for (const auto& base : m_urls) {
            if (m_cancel.load()) break;
            std::string body;
            int status = 0;
            if (!HttpRequest("GET", ServersUrl(base), "", "", 4000, body, &status) ||
                status != 200) {
                if (err.empty()) err = "master unreachable: " + base;
                continue;
            }
            std::vector<ServerEntry> list;
            ParseServerListJson(body, list);
            for (auto& e : list) {
                bool dup = false;
                for (auto& f : found) {
                    if (f.entry.ip == e.ip && f.entry.port == e.port) { f.entry = e; dup = true; break; }
                }
                if (!dup) found.push_back(OnlineServer{e, false, -1});
            }
        }

        // Direct status queries (live players + RTT), capped for scan latency.
        const size_t kMaxStatus = 48;
        size_t cap = std::min(found.size(), kMaxStatus);
        for (size_t i = 0; i < cap && !m_cancel.load(); ++i) {
            OnlineServer& os = found[i];
            if (os.entry.http_port == 0) continue;
            std::string url = "http://" + os.entry.ip + ":" +
                              std::to_string(os.entry.http_port) + "/status";
            long long q0 = mhttp_now_ms();
            std::string body;
            int status = 0;
            if (HttpRequest("GET", url, "", "", 1500, body, &status) && status == 200) {
                os.ping_ms = (int)(mhttp_now_ms() - q0);
                os.live = true;
                uint32_t v = 0;
                if (JsonGetUInt(body, "players", v)) os.entry.players = v;
                if (JsonGetUInt(body, "max_players", v)) os.entry.maxplayers = v;
                std::string sname;
                if (JsonGetString(body, "server_name", sname) && !sname.empty())
                    os.entry.name = sname;
                std::string smap;
                if (JsonGetString(body, "map", smap) && !smap.empty())
                    os.entry.map = smap;
            }
        }

        m_last_seconds = (mhttp_now_ms() - t0) / 1000.0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_staged = std::move(found);
            m_staged_error = err;
        }
        m_finished.store(true);
    }

    std::vector<std::string> m_urls;
    std::vector<OnlineServer> m_servers;
    std::string m_error;
    bool m_have_result = false;
    double m_last_seconds = 0.0;

    std::vector<OnlineServer> m_staged;
    std::string m_staged_error;
    std::mutex m_mutex;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_cancel{false};
    std::thread m_thread;
};

} // namespace master

#endif // OMEGA_INTERNET_BROWSER_HPP