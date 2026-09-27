#ifndef OMEGA_MASTER_PROTOCOL_HPP
#define OMEGA_MASTER_PROTOCOL_HPP

// Master-server protocol shared by AngelMaster (daemon), the AngelServ uplink
// (MasterClient.hpp), the client browser (InternetBrowser.hpp) and the unit
// tests. Pure logic only: no sockets, raylib or engine dependencies.
//
// Wire formats
//   UDP heartbeat :  OZHEARTBEAT:gamename=angels95;gamever=..;host=..;map=..;
//                    port=27015;httpport=8080;players=3;maxplayers=16;password=0
//   HTTP list     :  GET /api/servers?gamename=angels95 -> JSON (see below)
//   HTTP heartbeat:  POST /api/heartbeat  body = the UDP heartbeat line

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <algorithm>

namespace master {

constexpr const char* GAMENAME          = "angels95";
constexpr const char* GAMEVER_DEFAULT   = "0.2.1";
constexpr int DEFAULT_UDP_PORT          = 27900;
constexpr int DEFAULT_HTTP_PORT         = 27950;
constexpr int HEARTBEAT_INTERVAL_S      = 30;   // server -> master cadence
constexpr int EXPIRE_SECONDS            = 90;   // 3x heartbeat interval
constexpr int MAX_SERVERS               = 4096;
constexpr const char* HEARTBEAT_PREFIX  = "OZHEARTBEAT:";
constexpr size_t MAX_NAME_LEN           = 64;
constexpr size_t MAX_MAP_LEN            = 64;
constexpr size_t MAX_VER_LEN            = 16;

// ---------------------------------------------------------------------------
// Small string helpers
// ---------------------------------------------------------------------------
inline std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

inline bool StartsWith(const std::string& s, const char* prefix) {
    size_t n = strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

inline bool EndsWith(const std::string& s, const char* suffix) {
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// Values must not contain the ';' separator or newlines. Anything else is
// replaced with a space so a hostile name can't inject extra fields.
inline std::string SanitizeValue(const std::string& v, size_t max_len) {
    std::string out;
    out.reserve(std::min(v.size(), max_len));
    for (char c : v) {
        if (c == ';' || c == '\r' || c == '\n' || c == ':') c = ' ';
        out.push_back(c);
        if (out.size() >= max_len) break;
    }
    return Trim(out);
}

// ---------------------------------------------------------------------------
// Heartbeat
// ---------------------------------------------------------------------------
struct Heartbeat {
    std::string gamename = GAMENAME;
    std::string gamever  = GAMEVER_DEFAULT;
    std::string host;                 // display name
    std::string map;
    uint16_t    port = 0;             // game UDP port
    uint16_t    http_port = 0;        // HTTP status port (0 = unknown)
    uint32_t    players = 0;
    uint32_t    maxplayers = 0;
    bool        password = false;
    std::string public_ip;            // optional, for NAT
};

// A live entry as tracked by the master / handed to the client.
struct ServerEntry {
    std::string ip;                   // master-observed or heartbeat public_ip
    std::string name;
    std::string map;
    std::string gamever;
    std::string gamename = GAMENAME;
    uint16_t    port = 0;
    uint16_t    http_port = 0;
    uint32_t    players = 0;
    uint32_t    maxplayers = 0;
    bool        password = false;
    double      last_seen = 0.0;      // monotonic seconds (master) or epoch (json)
};

inline std::string EncodeHeartbeat(const Heartbeat& hb) {
    Heartbeat h = hb;
    h.host = SanitizeValue(h.host, MAX_NAME_LEN);
    h.map  = SanitizeValue(h.map, MAX_MAP_LEN);
    h.gamever = SanitizeValue(h.gamever, MAX_VER_LEN);
    char buf[512];
    snprintf(buf, sizeof(buf),
             "%sgamename=%s;gamever=%s;host=%s;map=%s;port=%u;httpport=%u;"
             "players=%u;maxplayers=%u;password=%d",
             HEARTBEAT_PREFIX,
             h.gamename.c_str(), h.gamever.c_str(), h.host.c_str(), h.map.c_str(),
             (unsigned)h.port, (unsigned)h.http_port,
             (unsigned)h.players, (unsigned)h.maxplayers, h.password ? 1 : 0);
    std::string line(buf);
    if (!h.public_ip.empty()) {
        std::string ip;
        for (char c : h.public_ip) {
            if (isalnum((unsigned char)c) || c == '.' || c == ':') ip.push_back(c);
        }
        if (!ip.empty()) line += ";publicip=" + ip;
    }
    return line;
}

inline bool ParseKV(const std::string& data, const char* key, std::string& out) {
    size_t klen = strlen(key);
    size_t pos = 0;
    while (pos < data.size()) {
        size_t end = data.find_first_of(";\n", pos);
        if (end == std::string::npos) end = data.size();
        size_t eq = data.find('=', pos);
        if (eq != std::string::npos && eq < end) {
            std::string k = Trim(data.substr(pos, eq - pos));
            if (k == key) { out = Trim(data.substr(eq + 1, end - eq - 1)); return true; }
        }
        pos = end + 1;
    }
    return false;
}

// Accepts "OZHEARTBEAT:key=val;..." and bare "key=val;..." bodies.
inline bool DecodeHeartbeat(const std::string& data, Heartbeat& out) {
    std::string body = Trim(data);
    if (StartsWith(body, HEARTBEAT_PREFIX)) body = body.substr(strlen(HEARTBEAT_PREFIX));

    std::string v;
    if (!ParseKV(body, "gamename", v) || v != GAMENAME) return false;

    out.gamename = GAMENAME;
    if (ParseKV(body, "gamever", v)) out.gamever = SanitizeValue(v, MAX_VER_LEN);
    if (ParseKV(body, "host", v))    out.host = SanitizeValue(v, MAX_NAME_LEN);
    if (ParseKV(body, "map", v))     out.map = SanitizeValue(v, MAX_MAP_LEN);

    unsigned long n = 0;
    if (ParseKV(body, "port", v)) {
        n = strtoul(v.c_str(), nullptr, 10);
        if (n == 0 || n > 65535) return false;
        out.port = (uint16_t)n;
    } else {
        return false; // game port is mandatory
    }
    if (ParseKV(body, "httpport", v)) {
        n = strtoul(v.c_str(), nullptr, 10);
        out.http_port = (n > 65535) ? 0 : (uint16_t)n;
    }
    if (ParseKV(body, "players", v))    out.players = (uint32_t)strtoul(v.c_str(), nullptr, 10);
    if (ParseKV(body, "maxplayers", v)) out.maxplayers = (uint32_t)strtoul(v.c_str(), nullptr, 10);
    if (ParseKV(body, "password", v))   out.password = (v == "1" || v == "true");
    if (ParseKV(body, "publicip", v)) {
        if (v.find_first_not_of("0123456789.abcdefABCDEF:") == std::string::npos)
            out.public_ip = v;
    }
    if (out.maxplayers == 0) out.maxplayers = 32;
    return true;
}

// ---------------------------------------------------------------------------
// JSON helpers (tolerant scanner; master output is ours, but we never trust
// inputs blindly)
// ---------------------------------------------------------------------------
inline std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out.push_back(c);
        }
    }
    return out;
}

// Returns pointer just past the ':' for `"key"`, or nullptr.
inline const char* JsonFindKey(const std::string& obj, const char* key) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = obj.find(k);
    if (p == std::string::npos) return nullptr;
    p += k.size();
    while (p < obj.size() && (obj[p] == ' ' || obj[p] == '\t' || obj[p] == ':')) ++p;
    return obj.c_str() + p;
}

inline bool JsonGetString(const std::string& obj, const char* key, std::string& out) {
    const char* p = JsonFindKey(obj, key);
    if (!p || *p != '"') return false;
    ++p;
    std::string s;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            ++p;
            switch (*p) {
                case 'n': s.push_back('\n'); break;
                case 'r': s.push_back('\r'); break;
                case 't': s.push_back('\t'); break;
                default:  s.push_back(*p);
            }
        } else {
            s.push_back(*p);
        }
        ++p;
    }
    out = s;
    return true;
}

inline bool JsonGetUInt(const std::string& obj, const char* key, uint32_t& out) {
    const char* p = JsonFindKey(obj, key);
    if (!p) return false;
    if (*p == '"') ++p;
    if (*p < '0' || *p > '9') return false;
    uint32_t v = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (uint32_t)(*p - '0'); ++p; }
    out = v;
    return true;
}

inline bool JsonGetBool(const std::string& obj, const char* key, bool& out) {
    const char* p = JsonFindKey(obj, key);
    if (!p) return false;
    if (strncmp(p, "true", 4) == 0 || *p == '1') { out = true; return true; }
    if (strncmp(p, "false", 5) == 0 || *p == '0') { out = false; return true; }
    return false;
}

// Split out the objects inside the "servers": [ ... ] array. Brace/string aware.
inline std::vector<std::string> JsonExtractObjects(const std::string& json) {
    std::vector<std::string> out;
    size_t arr = json.find("\"servers\"");
    if (arr == std::string::npos) return out;
    size_t lb = json.find('[', arr);
    if (lb == std::string::npos) return out;

    int depth = 0;
    bool in_str = false;
    size_t obj_start = std::string::npos;
    for (size_t i = lb + 1; i < json.size(); ++i) {
        char c = json[i];
        if (in_str) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; continue; }
        if (c == '{') {
            if (depth == 0) obj_start = i;
            ++depth;
        } else if (c == '}') {
            if (depth > 0) {
                --depth;
                if (depth == 0 && obj_start != std::string::npos) {
                    out.push_back(json.substr(obj_start, i - obj_start + 1));
                    obj_start = std::string::npos;
                }
            }
        } else if (c == ']' && depth == 0) {
            break;
        }
    }
    return out;
}

inline std::string BuildServerListJson(const std::vector<ServerEntry>& servers,
                                       const std::string& gamename) {
    std::string json = "{\"ok\":true,\"gamename\":\"";
    json += JsonEscape(gamename) + "\",\"count\":" + std::to_string(servers.size()) + ",\"servers\":[";
    bool first = true;
    for (const auto& s : servers) {
        if (!first) json += ',';
        first = false;
        json += "{\"ip\":\"" + JsonEscape(s.ip) + "\"";
        json += ",\"port\":" + std::to_string(s.port);
        json += ",\"httpport\":" + std::to_string(s.http_port);
        json += ",\"name\":\"" + JsonEscape(s.name) + "\"";
        json += ",\"map\":\"" + JsonEscape(s.map) + "\"";
        json += ",\"gamever\":\"" + JsonEscape(s.gamever) + "\"";
        json += ",\"players\":" + std::to_string(s.players);
        json += ",\"maxplayers\":" + std::to_string(s.maxplayers);
        json += std::string(",\"password\":") + (s.password ? "true" : "false");
        json += ",\"lastseen\":" + std::to_string((long long)s.last_seen);
        json += "}";
    }
    json += "]}";
    return json;
}

// True when a tracked server has not heartbeated within EXPIRE_SECONDS.
inline bool IsExpired(const ServerEntry& e, long long now_epoch) {
    return (now_epoch - (long long)e.last_seen) > EXPIRE_SECONDS;
}

inline bool ParseServerListJson(const std::string& json,
                                std::vector<ServerEntry>& out) {
    out.clear();
    for (const auto& obj : JsonExtractObjects(json)) {
        ServerEntry e;
        if (!JsonGetString(obj, "ip", e.ip) || e.ip.empty()) continue;
        uint32_t n = 0;
        if (!JsonGetUInt(obj, "port", n) || n == 0 || n > 65535) continue;
        e.port = (uint16_t)n;
        if (JsonGetUInt(obj, "httpport", n)) e.http_port = (n > 65535) ? 0 : (uint16_t)n;
        if (JsonGetUInt(obj, "players", n)) e.players = n;
        if (JsonGetUInt(obj, "maxplayers", n)) e.maxplayers = n;
        JsonGetString(obj, "name", e.name);
        JsonGetString(obj, "map", e.map);
        JsonGetString(obj, "gamever", e.gamever);
        JsonGetBool(obj, "password", e.password);
        out.push_back(std::move(e));
    }
    return true;
}

} // namespace master

#endif // OMEGA_MASTER_PROTOCOL_HPP