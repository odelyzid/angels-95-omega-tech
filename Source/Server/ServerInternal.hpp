#pragma once

// Internal shared state + helpers for the AngelServ translation units
// (Server.cpp, ServerHttp.cpp). Not part of any public interface.

#include "../Network/Network.hpp"
#include "../World/OzoneParser.hpp"
#include "GameState.hpp"
#include "../Log.hpp"

#include <string>
#include <vector>
#include <mutex>
#include <chrono>

// --- Shared server state (defined in Server.cpp) ---
extern bool g_running;
extern net::NetworkServer* g_game_server;
extern std::string g_gamedata_dir;
extern int g_http_port;
extern std::mutex g_print_mutex;
extern std::vector<std::string> g_world_list;
extern std::string g_auth_token;
extern std::string g_admin_token;
extern std::string g_server_name;
extern std::chrono::steady_clock::time_point g_server_start_time;
extern GameState g_game_state;
extern net::NetworkDiscovery* g_discovery;
extern std::vector<std::string> g_master_udp;
extern std::vector<std::string> g_master_http;
extern std::string g_public_ip;
extern std::string g_bind_ip;

// --- Shared helpers ---
std::string current_map_name();
void send_syschat(const net::NetworkPlayer& player, const std::string& text);
std::string json_escape(const std::string& s);
void append_ozone_json(std::string& out, const OzonePrimitive& p, bool first);

// --- HTTP server (ServerHttp.cpp) ---
void http_server_thread(int port);
