// oz_server – Dedicated server for OmegaTech / OzWorld
// Provides UDP game server + HTTP map API + LAN discovery.
// Build: g++ -O3 --std=c++20 -fPIC -lpthread -lm
// Usage: oz_server [--port P] [--http-port P] [--dir GameData]

#include "../Network/Network.hpp"
#include "WDLParser.hpp"
#include "OzoneParser.hpp"
#include "GameState.hpp"
#include "../Log.hpp"

#include <csignal>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>
#include <sys/stat.h>
#include <dirent.h>
#include <thread>
#include <chrono>
#include <mutex>

// ---------------------------------------------------------------------------
// Globals / Config
// ---------------------------------------------------------------------------
static bool g_running = true;
static net::NetworkServer* g_game_server = nullptr;
static std::string g_gamedata_dir = "GameData";
static int g_http_port = 8080;
static std::mutex g_print_mutex;
static std::vector<std::string> g_world_list;

static GameState g_game_state;

static void signal_handler(int) { g_running = false; }

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------
static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char ch : s) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            default:   out += ch;
        }
    }
    out += '"';
    return out;
}

static void append_elem_json(std::string& out, const WDLElement& e, bool first) {
    if (!first) out += ',';
    out += "{\n";

    auto add_field = [&](const char* key, const std::string& val) {
        out += "\"" + std::string(key) + "\": " + val;
    };

    auto add_num = [&](const char* key, float f) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.3f", f);
        out += "\"" + std::string(key) + "\": " + buf;
    };

    switch (e.type) {
        case WDLElementType::HEIGHTMAP:
            add_field("type", json_escape("heightmap")); out += ',';
            if (e.args.size() >= 5) {
                add_num("x", e.args[0]); out += ',';
                add_num("y", e.args[1]); out += ',';
                add_num("z", e.args[2]); out += ',';
                add_num("scale", e.args[3]);  out += ',';
                add_num("rotation", e.args[4]);
            }
            break;
        case WDLElementType::MODEL:
            add_field("type", json_escape("model")); out += ',';
            add_num("id", static_cast<float>(e.int_id)); out += ',';
            if (e.args.size() >= 5) {
                add_num("x", e.args[0]); out += ',';
                add_num("y", e.args[1]); out += ',';
                add_num("z", e.args[2]); out += ',';
                add_num("scale", e.args[3]); out += ',';
                add_num("rotation", e.args[4]);
            }
            break;
        case WDLElementType::COLLISION:
            add_field("type", json_escape("collision")); out += ',';
            if (e.args.size() >= 5) {
                add_num("x", e.args[0]); out += ',';
                add_num("y", e.args[1]); out += ',';
                add_num("z", e.args[2]); out += ',';
                add_num("scale", e.args[3]); out += ',';
                add_num("rotation", e.args[4]);
            }
            break;
        case WDLElementType::ADV_COLLISION:
            add_field("type", json_escape("adv_collision")); out += ',';
            if (e.args.size() >= 8) {
                add_num("x", e.args[0]); out += ',';
                add_num("y", e.args[1]); out += ',';
                add_num("z", e.args[2]); out += ',';
                add_num("scale", e.args[3]); out += ',';
                add_num("rotation", e.args[4]); out += ',';
                add_num("w", e.args[5]); out += ',';
                add_num("h", e.args[6]); out += ',';
                add_num("l", e.args[7]);
            }
            break;
        case WDLElementType::CLIP_BOX:
            add_field("type", json_escape("clip_box")); out += ',';
            if (e.args.size() >= 8) {
                add_num("x", e.args[0]); out += ',';
                add_num("y", e.args[1]); out += ',';
                add_num("z", e.args[2]); out += ',';
                add_num("scale", e.args[3]); out += ',';
                add_num("rotation", e.args[4]); out += ',';
                add_num("w", e.args[5]);  out += ',';
                add_num("h", e.args[6]);  out += ',';
                add_num("l", e.args[7]);
            }
            break;
        case WDLElementType::LIGHT:
            add_field("type", json_escape("light")); out += ',';
            if (e.args.size() >= 3) {
                add_num("x", e.args[0]); out += ',';
                add_num("y", e.args[1]); out += ',';
                add_num("z", e.args[2]);
            }
            break;
        case WDLElementType::SCRIPT:
            add_field("type", json_escape("script")); out += ',';
            add_num("id", static_cast<float>(e.int_id)); out += ',';
            if (e.args.size() >= 5) {
                add_num("x", e.args[0]); out += ',';
                add_num("y", e.args[1]); out += ',';
                add_num("z", e.args[2]); out += ',';
                add_num("scale", e.args[3]); out += ',';
                add_num("rotation", e.args[4]);
            }
            break;
        case WDLElementType::OBJECT:
            add_field("type", json_escape("object")); out += ',';
            add_num("id", static_cast<float>(e.int_id)); out += ',';
            if (e.args.size() >= 5) {
                add_num("x", e.args[0]); out += ',';
                add_num("y", e.args[1]); out += ',';
                add_num("rotation", e.args[4]);
            }
            break;
        case WDLElementType::NOISE_EMITTER:
            add_field("type", json_escape("noise_emitter")); out += ',';
            add_num("id", static_cast<float>(e.int_id)); out += ',';
            if (e.args.size() >= 5) {
                add_num("x", e.args[0]); out += ',';
                add_num("y", e.args[1]); out += ',';
                add_num("z", e.args[2]); out += ',';
                add_num("scale", e.args[3]); out += ',';
                add_num("rotation", e.args[4]);
            }
            break;
        case WDLElementType::PICKUP:
            add_field("type", json_escape("pickup")); out += ',';
            add_field("pickupType", json_escape(e.pickupType)); out += ',';
            if (e.args.size() >= 3) {
                add_num("x", e.args[0]); out += ',';
                add_num("y", e.args[1]); out += ',';
                add_num("z", e.args[2]);
            }
            break;
        case WDLElementType::SPAWN:
            add_field("type", json_escape("spawn"));
            if (e.args.size() >= 3) {
                out += ','; add_num("x", e.args[0]); out += ',';
                add_num("y", e.args[1]); out += ',';
                add_num("z", e.args[2]); out += ',';
            }
            else out += ',';
            add_num("yaw", e.yaw);
            break;
        case WDLElementType::NPC:
            add_field("type", json_escape("npc")); out += ',';
            add_field("npcType", json_escape(e.entityType)); out += ',';
            if (e.args.size() >= 3) {
                add_num("x", e.args[0]); out += ',';
                add_num("y", e.args[1]); out += ',';
                add_num("z", e.args[2]);
            }
            break;
        case WDLElementType::LIGHT_TYPE:
            add_field("type", json_escape("light_type")); out += ',';
            if (e.args.size() >= 3) {
                add_num("x", e.args[0]); out += ',';
                add_num("y", e.args[1]); out += ',';
                add_num("z", e.args[2]);
            }
            break;
        case WDLElementType::AMBIENT_TYPE:
            add_field("type", json_escape("ambient")); out += ',';
            if (e.args.size() >= 4) {
                add_num("r", e.args[0]); out += ',';
                add_num("g", e.args[1]); out += ',';
                add_num("b", e.args[2]); out += ',';
                add_num("intensity", e.args[3]);
            }
            break;
        case WDLElementType::FOG:
            add_field("type", json_escape("fog")); out += ',';
            if (e.args.size() >= 4) {
                add_num("r", e.args[0]); out += ',';
                add_num("g", e.args[1]); out += ',';
                add_num("b", e.args[2]); out += ',';
                add_num("density", e.args[3]);
            }
            break;
        case WDLElementType::ZONE_INFO:
            add_field("type", json_escape("zone_info")); out += ',';
            add_field("zoneType", json_escape(e.zoneType));
            if (e.args.size() >= 6) {
                out += ','; add_num("minX", e.args[0]); out += ',';
                add_num("minY", e.args[1]); out += ',';
                add_num("minZ", e.args[2]); out += ',';
                add_num("maxX", e.args[3]); out += ',';
                add_num("maxY", e.args[4]); out += ',';
                add_num("maxZ", e.args[5]); out += ',';
            }
            else out += ',';
            add_num("intensity", e.intensity);
            break;
        case WDLElementType::ENTITY_WALKER:
            add_field("type", json_escape("entity")); out += ',';
            add_field("subtype", json_escape("walker")); out += ',';
            if (e.args.size() >= 3) {
                add_num("x", e.args[0]);  out += ',';
                add_num("y", e.args[1]);  out += ',';
                add_num("z", e.args[2]);
            }
            break;
        case WDLElementType::COL_FLAG:
            add_field("type", json_escape("col_flag"));
            break;
        default:
            add_field("type", json_escape("unknown"));
            break;
    }

    out += "\n}";
}

static void append_ozone_json(std::string& out, const OzonePrimitive& p, bool first) {
    if (!first) out += ',';
    out += R"({"type":)";

    auto esc = [](const std::string& s) { return json_escape(s); };

    switch (p.type) {
        case OzonePrimitiveType::BOX: {
            out += esc("box");
            if (p.args.size() >= 7) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                    R"(,"center":[%.3f,%.3f,%.3f],"size":[%.3f,%.3f,%.3f],"rot":%.3f)",
                    p.args[0], p.args[1], p.args[2],
                    p.args[3], p.args[4], p.args[5],
                    p.args[6]);
                out += buf;
            }
            break;
        }
        case OzonePrimitiveType::CYLINDER: {
            out += esc("cyl");
            if (p.args.size() >= 8) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                    R"(,"center":[%.3f,%.3f,%.3f],"rt":%.3f,"rb":%.3f,"h":%.3f,"slices":%d,"rot":%.3f)",
                    p.args[0], p.args[1], p.args[2],
                    p.args[3], p.args[4],
                    p.args[5],
                    static_cast<int>(p.args[6]),
                    p.args[7]);
                out += buf;
            }
            break;
        }
        case OzonePrimitiveType::SPHERE: {
            out += esc("sph");
            if (p.args.size() >= 4) {
                char buf[128];
                snprintf(buf, sizeof(buf),
                    R"(,"center":[%.3f,%.3f,%.3f],"radius":%.3f,"segments":%d)",
                    p.args[0], p.args[1], p.args[2],
                    p.args[3],
                    p.args.size() >= 5 ? static_cast<int>(p.args[4]) : 16);
                out += buf;
            }
            break;
        }
        case OzonePrimitiveType::PYRAMID: {
            out += esc("pyr");
            if (p.args.size() >= 6) {
                char buf[128];
                snprintf(buf, sizeof(buf),
                    R"(,"center":[%.3f,%.3f,%.3f],"w":%.3f,"d":%.3f,"h":%.3f)",
                    p.args[0], p.args[1], p.args[2],
                    p.args[3], p.args[4], p.args[5]);
                out += buf;
            }
            break;
        }
        case OzonePrimitiveType::PLANE: {
            out += esc("pln");
            if (p.args.size() >= 7) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                    R"(,"center":[%.3f,%.3f,%.3f],"normal":[%.3f,%.3f,%.3f],"dist":%.3f)",
                    p.args[0], p.args[1], p.args[2],
                    p.args[3], p.args[4], p.args[5],
                    p.args[6]);
                out += buf;
            }
            break;
        }
        case OzonePrimitiveType::ENTITY_PLAYERSTART: {
            out += esc("playerstart");
            if (p.args.size() >= 4) {
                char buf[160];
                snprintf(buf, sizeof(buf), R"(,"position":[%.3f,%.3f,%.3f],"yaw":%.3f)",
                         p.args[0], p.args[1], p.args[2], p.args[3]);
                out += buf;
            }
            break;
        }
        case OzonePrimitiveType::ENTITY_PICKUP:
        case OzonePrimitiveType::ENTITY_NPC: {
            out += esc(p.type == OzonePrimitiveType::ENTITY_PICKUP ? "pickup" : "npc");
            out += R"(,"subtype":)" + esc(p.entityType);
            if (p.args.size() >= 3) {
                char buf[128];
                snprintf(buf, sizeof(buf), R"(,"position":[%.3f,%.3f,%.3f])",
                         p.args[0], p.args[1], p.args[2]);
                out += buf;
            }
            if (p.type == OzonePrimitiveType::ENTITY_PICKUP && p.args.size() >= 4) {
                char buf[64];
                snprintf(buf, sizeof(buf), R"(,"respawnTime":%.3f)", p.args[3]);
                out += buf;
            }
            break;
        }
        case OzonePrimitiveType::ENTITY_ZONE: {
            out += esc("zone");
            out += R"(,"subtype":)" + esc(p.entitySubType);
            if (p.args.size() >= 6) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         R"(,"min":[%.3f,%.3f,%.3f],"max":[%.3f,%.3f,%.3f])",
                         p.args[0], p.args[1], p.args[2], p.args[3], p.args[4], p.args[5]);
                out += buf;
            }
            if (p.args.size() >= 7) {
                char buf[64];
                snprintf(buf, sizeof(buf), R"(,"intensity":%.3f)", p.args[6]);
                out += buf;
            }
            break;
        }
        default:
            out += esc("unknown");
            break;
    }
    out += '}';
}

// ---------------------------------------------------------------------------
// HTTP server
// ---------------------------------------------------------------------------
static int http_listen(int port) {
    int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, sock_set_opt(&yes, sizeof(yes)));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7F000001); // 127.0.0.1
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    if (listen(fd, 8) < 0) { close(fd); return -1; }
    return fd;
}

static void http_write_all(int fd, const char* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w <= 0) break;
        off += (size_t)w;
    }
}

static void http_response(int fd, int code, const char* status,
                          const char* body, const char* type) {
    char hdr[512];
    int hl = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        code, status, type, strlen(body));
    http_write_all(fd, hdr, (size_t)hl);
    http_write_all(fd, body, strlen(body));
}

static void http_respond_json(int fd, const char* body) {
    http_response(fd, 200, "OK", body, "application/json");
}

static void http_respond_404(int fd) {
    http_response(fd, 404, "Not Found", "{\"ok\":false,\"error\":\"not found\"}",
                  "application/json");
}

static const char* get_query_param(const char* path, const char* key,
                                    char* out, size_t out_sz) {
    const char* q = strchr(path, '?');
    if (!q) return nullptr;
    ++q;
    size_t klen = strlen(key);
    while (*q) {
        if (strncmp(q, key, klen) == 0 && q[klen] == '=') {
            q += klen + 1;
            size_t i = 0;
            while (*q && *q != '&' && i + 1 < out_sz) out[i++] = *q++;
            out[i] = '\0';
            return out;
        }
        while (*q && *q != '&') ++q;
        if (*q == '&') ++q;
    }
    return nullptr;
}

static void handle_http_client(int cfd) {
    char req[8192];
    ssize_t n = read(cfd, req, sizeof(req) - 1);
    if (n <= 0) { close(cfd); return; }
    req[n] = '\0';

    char method[16] = {0}, path[1024] = {0};
    sscanf(req, "%15s %1023s", method, path);

    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/map") == 0 || strncmp(path, "/map?", 5) == 0) {
            char name[256] = {0};
            get_query_param(path, "name", name, sizeof(name));
            if (name[0] == '\0') {
                std::string json = "{\"ok\":true,\"worlds\":[";
                for (size_t i = 0; i < g_world_list.size(); ++i) {
                    if (i) json += ',';
                    json += json_escape(g_world_list[i]);
                }
                json += "]}";
                http_respond_json(cfd, json.c_str());
                close(cfd);
                return;
            }
            // Try WDL
            std::string wdl_path = g_gamedata_dir + "/Worlds/" + name + "/World.wdl";
            auto wdl_elems = WDLParser::parse_file(wdl_path);
            if (!wdl_elems.empty()) {
                std::string json = "{\"ok\":true,\"format\":\"wdl\",\"world\":";
                json += json_escape(name);
                json += ",\"elements\":[";
                bool first = true;
                for (const auto& e : wdl_elems) {
                    append_elem_json(json, e, first);
                    first = false;
                }
                json += "]}";
                http_respond_json(cfd, json.c_str());
                close(cfd);
                return;
            }

            // Try OZONE
            std::string ozone_path = g_gamedata_dir + "/Worlds/" + name + "/World.ozone";
            auto ozone_prims = OzoneParser::parse_file(ozone_path);
            if (!ozone_prims.empty()) {
                std::string json = "{\"ok\":true,\"format\":\"ozone\",\"world\":";
                json += json_escape(name);
                json += ",\"brushes\":[";
                bool first = true;
                for (const auto& p : ozone_prims) {
                    append_ozone_json(json, p, first);
                    first = false;
                }
                json += "]}";
                http_respond_json(cfd, json.c_str());
                close(cfd);
                return;
            }

            // Try testozones/sample.ozone (fallback)
            std::string sample_path = "testozones/" + std::string(name);
            auto sample_prims = OzoneParser::parse_file(sample_path);
            if (!sample_prims.empty()) {
                std::string json = "{\"ok\":true,\"format\":\"ozone\",\"world\":";
                json += json_escape(name);
                json += ",\"brushes\":[";
                bool first = true;
                for (const auto& p : sample_prims) {
                    append_ozone_json(json, p, first);
                    first = false;
                }
                json += "]}";
                http_respond_json(cfd, json.c_str());
                close(cfd);
                return;
            }

            http_respond_404(cfd);
            close(cfd);
            return;
        }
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/auth/login") == 0) {
        http_respond_json(cfd, "{\"ok\":true,\"token\":\"dev\"}");
        close(cfd);
        return;
    }

    http_respond_404(cfd);
    close(cfd);
}

static void http_server_thread(int port) {
    int lfd = http_listen(port);
    if (lfd < 0) {
        fprintf(stderr, "HTTP: failed to listen on port %d\n", port);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_print_mutex);
        printf("HTTP: map server on http://127.0.0.1:%d\n", port);
    }

    fd_set master_fds;
    FD_ZERO(&master_fds);
    FD_SET(lfd, &master_fds);
    int max_fd = lfd;

    while (g_running) {
        fd_set read_fds = master_fds;
        struct timeval tv = {1, 0};
        if (select(max_fd + 1, &read_fds, nullptr, nullptr, &tv) < 0) {
            if (g_running) break;
            continue;
        }

        if (FD_ISSET(lfd, &read_fds)) {
            struct sockaddr_in addr;
            socklen_t addrlen = sizeof(addr);
            int cfd = accept(lfd, (struct sockaddr*)&addr, &addrlen);
            if (cfd >= 0) {
                handle_http_client(cfd);
            }
        }
    }

    close(lfd);
}

// ---------------------------------------------------------------------------
// Server state management
// ---------------------------------------------------------------------------
static void send_pickup_respawn_msg(const net::NetworkPlayer& player,
                                    int world_index, const ServerPickup& p) {
    net::PickupRespawnData prd;
    prd.pickup_id = p.id;
    prd.world_index = world_index;
    prd.position = {p.position.x, p.position.y, p.position.z};
    prd.type = static_cast<int>(p.type);
    prd.value = p.value;
    net::NetworkMessage msg{};
    msg.magic = net::MAGIC;
    msg.type = static_cast<uint32_t>(net::MessageType::PICKUP_RESPAWN);
    msg.size = sizeof(prd);
    msg.sequence = 0;
    msg.timestamp = static_cast<uint32_t>(time(nullptr));
    memcpy(msg.payload, &prd, sizeof(prd));
    g_game_server->send_message(const_cast<net::NetworkPlayer&>(player), msg);
}

static void on_player_join(net::NetworkPlayer& player) {
    printf("Player %s (id=%u) joined from %s:%u\n",
           player.name, player.id, player.ip_address, player.port);
    g_game_state.add_player(player.id, player.name);

    std::string world_list = "{\"type\":\"world_list\",\"worlds\":[";
    for (size_t i = 0; i < g_world_list.size(); ++i) {
        if (i) world_list += ',';
        world_list += json_escape(g_world_list[i]);
    }
    world_list += "]}";
    auto msg = net::NetworkMessage{};
    msg.magic = net::MAGIC;
    msg.type = static_cast<uint32_t>(net::MessageType::SCENE_UPDATE);
    msg.size = static_cast<uint32_t>(world_list.size());
    msg.sequence = 0;
    msg.timestamp = static_cast<uint32_t>(time(nullptr));
    memcpy(msg.payload, world_list.data(), world_list.size());
    g_game_server->send_message(player, msg);

    // Sync active pickups (near-spawn first worlds)
    int synced = 0;
    for (int wi = 0; wi < g_game_state.world_count(); wi++) {
        g_game_state.for_each_active_pickup(wi, [&](WorldState& ws, ServerPickup& p) {
            (void)ws;
            send_pickup_respawn_msg(player, wi, p);
            synced++;
        });
    }
    printf("Synced %d pickups to player %s\n", synced, player.name);
}

static void on_player_leave(net::NetworkPlayer& player) {
    printf("Player %s left\n", player.name);
    g_game_state.remove_player(player.id);
}

static void on_server_message(const net::NetworkMessage& msg,
                               const net::NetworkPlayer& sender) {
    auto type = static_cast<net::MessageType>(msg.type);
    switch (type) {
        case net::MessageType::PLAYER_UPDATE: {
            if (msg.size < sizeof(net::PlayerUpdateData)) return;
            net::PlayerUpdateData pud;
            memcpy(&pud, msg.payload, sizeof(pud));
            g_game_state.update_player_position(
                sender.id, pud.position.x, pud.position.y, pud.position.z, pud.yaw, pud.pitch);

            // Relay to all other players
            pud.player_id = sender.id;
            net::NetworkMessage relay;
            relay.magic = net::MAGIC;
            relay.type = static_cast<uint32_t>(net::MessageType::PLAYER_UPDATE);
            relay.size = sizeof(pud);
            relay.sequence = 0;
            relay.timestamp = static_cast<uint32_t>(time(nullptr));
            memcpy(relay.payload, &pud, sizeof(pud));
            for (const auto& p : g_game_server->players()) {
                if (p.id != sender.id && p.connected) {
                    g_game_server->send_message(p, relay);
                }
            }
            break;
        }
        case net::MessageType::PICKUP_COLLECT: {
            if (msg.size < sizeof(net::PickupCollectData)) return;
            net::PickupCollectData pcd;
            memcpy(&pcd, msg.payload, sizeof(pcd));
            PickupType ptype;
            int pvalue;
            if (g_game_state.collect_pickup(sender.id, pcd.pickup_id, pcd.world_index, &ptype, &pvalue)) {
                ServerPlayer* pl = g_game_state.get_player(sender.id);
                if (pl) {
                    net::XpUpdateData xud;
                    xud.player_id = sender.id;
                    xud.xp = pl->xp;
                    xud.level = pl->level;
                    xud.xp_to_next = pl->xp_to_next;
                    net::NetworkMessage xmsg;
                    xmsg.magic = net::MAGIC;
                    xmsg.type = static_cast<uint32_t>(net::MessageType::XP_UPDATE);
                    xmsg.size = sizeof(xud);
                    xmsg.sequence = 0;
                    xmsg.timestamp = static_cast<uint32_t>(time(nullptr));
                    memcpy(xmsg.payload, &xud, sizeof(xud));
                    net::NetworkPlayer tmp = sender;
                    g_game_server->send_message(tmp, xmsg);
                }
                // Map pickup type/value to inventory item_id
                int item_id = -1;
                int quantity = 1;
                switch (ptype) {
                    case PickupType::HEALTH:   item_id = 1; break;
                    case PickupType::MANA:     item_id = 2; break;
                    case PickupType::PSYCHIC:
                        item_id = 2 + (pvalue / 111); // 3..11 for 111..999
                        if (item_id < 3) item_id = 3;
                        if (item_id > 11) item_id = 11;
                        break;
                    case PickupType::KEY:      item_id = 12; break;
                    case PickupType::COIN:     item_id = 13; quantity = pvalue > 0 ? pvalue : 1; break;
                    case PickupType::POWERUP:  item_id = 14; break;
                    default: break;
                }
                net::PickupCollectedData pcd_out;
                pcd_out.player_id = sender.id;
                pcd_out.pickup_id = pcd.pickup_id;
                pcd_out.item_id = item_id;
                pcd_out.quantity = quantity;
                net::NetworkMessage imsg;
                imsg.magic = net::MAGIC;
                imsg.type = static_cast<uint32_t>(net::MessageType::PICKUP_COLLECTED);
                imsg.size = sizeof(pcd_out);
                imsg.sequence = 0;
                imsg.timestamp = static_cast<uint32_t>(time(nullptr));
                memcpy(imsg.payload, &pcd_out, sizeof(pcd_out));
                // Collector gets item; everyone gets deactivate via same message
                g_game_server->broadcast_message(imsg);
            }
            break;
        }
        case net::MessageType::CHAT: {
            g_game_server->broadcast_message(msg);
            break;
        }
        case net::MessageType::PLAYER_ACTION: {
            if (msg.size < sizeof(net::WeaponFireData)) return;
            net::WeaponFireData wfd;
            memcpy(&wfd, msg.payload, sizeof(wfd));
            wfd.player_id = sender.id;
            net::NetworkMessage relay;
            relay.magic = net::MAGIC;
            relay.type = static_cast<uint32_t>(net::MessageType::PLAYER_ACTION);
            relay.size = sizeof(wfd);
            relay.sequence = 0;
            relay.timestamp = static_cast<uint32_t>(time(nullptr));
            memcpy(relay.payload, &wfd, sizeof(wfd));
            // Broadcast to all other players
            for (const auto& p : g_game_server->players()) {
                if (p.id != sender.id && p.connected) {
                    g_game_server->send_message(p, relay);
                }
            }
            break;
        }
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Environment setup
// ---------------------------------------------------------------------------
static void scan_worlds(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) {
        fprintf(stderr, "scan: couldnt open %s\n", dir.c_str());
        return;
    }
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (entry->d_name[0] == '.') continue;
        std::string path = dir + "/" + entry->d_name;
        struct stat s;
        if (stat(path.c_str(), &s) == 0 && S_ISDIR(s.st_mode)) {
            std::string wdl = path + "/World.wdl";
            std::string ozone = path + "/World.ozone";
            struct stat ss;
            if (stat(wdl.c_str(), &ss) == 0 || stat(ozone.c_str(), &ss) == 0) {
                g_world_list.push_back(entry->d_name);
                printf("Found world: %s\n", entry->d_name);
            }
        }
    }
    closedir(d);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    int game_port = 27015;
    int http_port = 8080;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            game_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--http-port") == 0 && i + 1 < argc)
            http_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc)
            g_gamedata_dir = argv[++i];
        else if (strcmp(argv[i], "--help") == 0) {
            printf("oz_server -- OzWorld/OmegaTech dedicated server\n");
            printf("Usage: oz_server [--port P] [--http-port P] [--dir GameData]\n");
            return 0;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("oz_server v0.2.0 starting\n");
    printf("Game port: UDP %d\n",  game_port);
    printf("HTTP port: %d\n",     http_port);
    printf("Data dir:  %s\n",     g_gamedata_dir.c_str());

    // Scan worlds
    scan_worlds(g_gamedata_dir + "/Worlds");

    // Start UDP game server
    net::NetworkServer game_server;
    g_game_server = &game_server;

    if (!game_server.init(static_cast<uint16_t>(game_port))) {
        fprintf(stderr, "Failed to create game server\n");
        return 1;
    }

    game_server.set_callbacks({
        .on_player_join = on_player_join,
        .on_player_leave = on_player_leave,
        .on_message_received = on_server_message
    });

    if (!game_server.start()) {
        fprintf(stderr, "Failed to start game server\n");
        return 1;
    }

    // Start HTTP server thread
    std::thread http_thread(http_server_thread, http_port);

    // LAN discovery
    net::NetworkDiscovery discovery;
    discovery.init("Angels95", "0.2.0", static_cast<uint16_t>(game_port));
    discovery.start();

    // Initialize game state with worlds
    g_game_state.init_worlds(g_gamedata_dir, g_world_list);
    {
        std::lock_guard<std::mutex> lock(g_print_mutex);
        printf("oz_server ready\n");
    }

    // Main loop
    int tick = 0;
    while (g_running) {
        game_server.update();
        discovery.update();

        g_game_state.tick(0.1f);

        // Broadcast NPC state to all players every 4 ticks (2.5/sec)
        if (tick % 4 == 0) {
            for (const auto& world : g_game_state.worlds()) {
                for (size_t i = 0; i < world.global_npcs.size(); i++) {
                    const auto& n = world.global_npcs[i];
                    if (!n.active) continue;
                    net::NpcStateUpdateData nsud;
                    nsud.world_index = world.world_index;
                    nsud.npc_index = static_cast<int>(i);
                    nsud.partition_index = -1;
                    nsud.position = n.position;
                    nsud.yaw = n.yaw;
                    nsud.state = static_cast<int>(n.state);
                    nsud.health = n.health;
                    nsud.active = n.active;
                    net::NetworkMessage bmsg;
                    bmsg.magic = net::MAGIC;
                    bmsg.type = static_cast<uint32_t>(net::MessageType::NPC_STATE_UPDATE);
                    bmsg.size = sizeof(nsud);
                    bmsg.sequence = 0;
                    bmsg.timestamp = static_cast<uint32_t>(time(nullptr));
                    memcpy(bmsg.payload, &nsud, sizeof(nsud));
                    g_game_server->broadcast_message(bmsg);
                }
            }
        }

        if (++tick % 30 == 0 && g_game_server->player_count() > 0) {
            auto& players = g_game_server->players();
            printf("Players online: %u\n", g_game_server->player_count());
            for (const auto& p : players) {
                if (p.connected)
                    printf("  %s [%s:%d]\n", p.name, p.ip_address, p.port);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    printf("Shutting down...\n");
    discovery.stop();
    g_game_server->stop();
    http_thread.join();

    return 0;
}
