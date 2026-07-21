#include "Client.hpp"
#include "../Log.hpp"
#include <cstring>
#include <cstdio>
#include <mutex>
#include <cstdlib>
#include <cmath>
#include <chrono>

static double now_seconds() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(steady_clock::now().time_since_epoch()).count();
}

static uint32_t id_to_color_packed(uint32_t id) {
    uint32_t c = id * 1234567u;
    return (c & 0xFF) | ((c >> 8) & 0xFF00) | ((c >> 16) & 0xFF0000) | 0xFF000000u;
}

bool OmegaClient::connect(const char* ip, uint16_t port) {
    net::ClientCallbacks cbs;
    cbs.on_connected = [this]() { this->on_connected(); };
    cbs.on_disconnected = [this]() { this->on_disconnected(); };
    cbs.on_message_received = [this](const net::NetworkMessage& msg) {
        this->handle_message(msg);
    };
    m_client.set_callbacks(std::move(cbs));

    bool ok = m_client.connect(ip, port);
    if (!ok) {
        OZ_ERROR("OmegaClient: failed to connect to %s:%u", ip, port);
    }
    return ok;
}

void OmegaClient::disconnect() {
    m_client.disconnect();
}

void OmegaClient::update(float cam_x, float cam_y, float cam_z,
                         float cam_yaw, float cam_pitch) {
    m_client.update();

    if (!m_client.is_connected()) return;

    // Send player position update (with full state data for server sync)
    net::PlayerUpdateData pud;
    pud.player_id = 0;
    pud.position = {cam_x, cam_y, cam_z};
    pud.yaw = cam_yaw;
    pud.pitch = cam_pitch;
    pud.health = 100.0f;
    pud.mana = 0.0f;
    pud.psychic_energy = 0.0f;
    pud.level = m_level;
    pud.xp = m_xp;
    memset(pud.inventory, 0, sizeof(pud.inventory));

    net::NetworkMessage msg;
    msg.magic = net::MAGIC;
    msg.type = static_cast<uint32_t>(net::MessageType::PLAYER_UPDATE);
    msg.size = sizeof(pud);
    msg.sequence = 0;
    msg.timestamp = static_cast<uint32_t>(time(nullptr));
    std::memcpy(msg.payload, &pud, sizeof(pud));

    m_client.send_message(msg);
}

void OmegaClient::send_chat(const char* text) {
    if (!m_client.is_connected()) return;

    net::ChatData cd;
    std::strncpy(cd.text, text, sizeof(cd.text) - 1);
    cd.text[sizeof(cd.text) - 1] = '\0';

    net::NetworkMessage msg;
    msg.magic = net::MAGIC;
    msg.type = static_cast<uint32_t>(net::MessageType::CHAT);
    msg.size = static_cast<uint32_t>(std::strlen(cd.text) + 1);
    msg.sequence = 0;
    msg.timestamp = static_cast<uint32_t>(time(nullptr));
    std::memcpy(msg.payload, cd.text, msg.size);

    m_client.send_message(msg);
}

void OmegaClient::send_pickup_collect(int pickup_id, int world_index) {
    if (!m_client.is_connected()) return;

    net::PickupCollectData pcd;
    pcd.player_id = 0; // server knows player id
    pcd.pickup_id = pickup_id;
    pcd.world_index = world_index;
    m_pending_collect_id = pickup_id;

    net::NetworkMessage msg;
    msg.magic = net::MAGIC;
    msg.type = static_cast<uint32_t>(net::MessageType::PICKUP_COLLECT);
    msg.size = sizeof(pcd);
    msg.sequence = 0;
    msg.timestamp = static_cast<uint32_t>(time(nullptr));
    std::memcpy(msg.payload, &pcd, sizeof(pcd));

    m_client.send_message(msg);
}

void OmegaClient::send_weapon_fire(float ox, float oy, float oz,
                                   float dx, float dy, float dz,
                                   int weapon_type, int power) {
    if (!m_client.is_connected()) return;

    net::WeaponFireData wfd;
    wfd.player_id = 0;
    wfd.origin_x = ox; wfd.origin_y = oy; wfd.origin_z = oz;
    wfd.dir_x = dx; wfd.dir_y = dy; wfd.dir_z = dz;
    wfd.weapon_type = weapon_type;
    wfd.power = power;

    net::NetworkMessage msg;
    msg.magic = net::MAGIC;
    msg.type = static_cast<uint32_t>(net::MessageType::PLAYER_ACTION);
    msg.size = sizeof(wfd);
    msg.sequence = 0;
    msg.timestamp = static_cast<uint32_t>(time(nullptr));
    std::memcpy(msg.payload, &wfd, sizeof(wfd));

    m_client.send_message(msg);
}

void OmegaClient::handle_message(const net::NetworkMessage& msg) {
    auto type = static_cast<net::MessageType>(msg.type);

    switch (type) {
        case net::MessageType::CHAT: {
            const std::lock_guard<std::mutex> lock(m_msg_mutex);
            m_chat_msg = reinterpret_cast<const char*>(msg.payload);
            if (m_on_chat_received) m_on_chat_received(m_chat_msg);
            break;
        }
        case net::MessageType::SCENE_UPDATE: {
            const std::lock_guard<std::mutex> lock(m_msg_mutex);
            m_pending_scene.assign(reinterpret_cast<const char*>(msg.payload),
                                    msg.size);
            if (m_on_scene_received) m_on_scene_received(m_pending_scene);
            break;
        }
        case net::MessageType::NPC_STATE_UPDATE: {
            if (msg.size < sizeof(net::NpcStateUpdateData)) return;
            const std::lock_guard<std::mutex> lock(m_msg_mutex);
            net::NpcStateUpdateData nsud;
            memcpy(&nsud, msg.payload, sizeof(nsud));
            bool found = false;
            for (auto& n : m_npcs) {
                if (n.npc_index == nsud.npc_index &&
                    n.world_index == nsud.world_index &&
                    n.partition_index == nsud.partition_index) {
                    n.position = nsud.position;
                    n.yaw = nsud.yaw;
                    n.state = nsud.state;
                    n.health = nsud.health;
                    n.active = nsud.active;
                    found = true;
                    break;
                }
            }
            if (!found) {
                ClientNPC cn;
                cn.world_index = nsud.world_index;
                cn.npc_index = nsud.npc_index;
                cn.partition_index = nsud.partition_index;
                cn.position = nsud.position;
                cn.yaw = nsud.yaw;
                cn.state = nsud.state;
                cn.health = nsud.health;
                cn.active = nsud.active;
                m_npcs.push_back(cn);
            }
            break;
        }
        case net::MessageType::PICKUP_RESPAWN: {
            if (msg.size < sizeof(net::PickupRespawnData)) return;
            const std::lock_guard<std::mutex> lock(m_msg_mutex);
            net::PickupRespawnData prd;
            memcpy(&prd, msg.payload, sizeof(prd));
            bool found = false;
            for (auto& p : m_pickups) {
                if (p.id == prd.pickup_id && p.world_index == prd.world_index) {
                    p.active = true;
                    p.position = prd.position;
                    p.type = prd.type;
                    p.value = prd.value;
                    found = true;
                    break;
                }
            }
            if (!found) {
                ClientPickup cp;
                cp.id = prd.pickup_id;
                cp.world_index = prd.world_index;
                cp.position = prd.position;
                cp.type = prd.type;
                cp.value = prd.value;
                cp.active = true;
                m_pickups.push_back(cp);
            }
            break;
        }
        case net::MessageType::PICKUP_COLLECTED: {
            if (msg.size < sizeof(net::PickupCollectedData)) return;
            const std::lock_guard<std::mutex> lock(m_msg_mutex);
            net::PickupCollectedData pcd;
            memcpy(&pcd, msg.payload, sizeof(pcd));
            for (auto& p : m_pickups) {
                if (p.id == pcd.pickup_id) {
                    p.active = false;
                    break;
                }
            }
            // Grant item only if we requested this collect
            if (pcd.item_id > 0 && m_pending_collect_id == pcd.pickup_id) {
                if (m_on_item_collected) m_on_item_collected(pcd.item_id, pcd.quantity);
                m_pending_collect_id = -1;
            }
            break;
        }
        case net::MessageType::XP_UPDATE: {
            if (msg.size < sizeof(net::XpUpdateData)) return;
            const std::lock_guard<std::mutex> lock(m_msg_mutex);
            net::XpUpdateData xud;
            memcpy(&xud, msg.payload, sizeof(xud));
            m_xp = xud.xp;
            m_level = xud.level;
            m_xp_to_next = xud.xp_to_next;
            break;
        }
        case net::MessageType::PLAYER_LEAVE: {
            if (msg.size < sizeof(uint32_t)) return;
            const std::lock_guard<std::mutex> lock(m_msg_mutex);
            uint32_t leave_id;
            memcpy(&leave_id, msg.payload, sizeof(leave_id));
            m_remote_players.erase(
                std::remove_if(m_remote_players.begin(), m_remote_players.end(),
                    [leave_id](const RemotePlayer& rp) { return rp.player_id == leave_id; }),
                m_remote_players.end());
            break;
        }
        case net::MessageType::PLAYER_HURT: {
            if (msg.size < sizeof(net::PlayerHurtData)) return;
            const std::lock_guard<std::mutex> lock(m_msg_mutex);
            net::PlayerHurtData pud;
            memcpy(&pud, msg.payload, sizeof(pud));
            if (m_on_player_hurt)
                m_on_player_hurt(pud.damage, pud.remaining_health);
            break;
        }
        case net::MessageType::PLAYER_UPDATE: {
            // Relayed player update from another player
            if (msg.size < sizeof(net::PlayerUpdateData)) return;
            const std::lock_guard<std::mutex> lock(m_msg_mutex);
            net::PlayerUpdateData pud;
            memcpy(&pud, msg.payload, sizeof(pud));
            if (pud.player_id == 0) break; // skip own messages

            bool found = false;
            for (auto& rp : m_remote_players) {
                if (rp.player_id == pud.player_id) {
                    rp.position = pud.position;
                    rp.yaw = pud.yaw;
                    rp.pitch = pud.pitch;
                    rp.health = pud.health;
                    rp.active = true;
                    found = true;
                    break;
                }
            }
            if (!found) {
                RemotePlayer rp;
                rp.player_id = pud.player_id;
                rp.position = pud.position;
                rp.yaw = pud.yaw;
                rp.pitch = pud.pitch;
                rp.health = pud.health;
                rp.active = true;
                    rp.color_packed = id_to_color_packed(pud.player_id);
                m_remote_players.push_back(rp);
            }
            break;
        }
        case net::MessageType::PLAYER_ACTION: {
            if (msg.size < sizeof(net::WeaponFireData)) return;
            const std::lock_guard<std::mutex> lock(m_msg_mutex);
            net::WeaponFireData wfd;
            memcpy(&wfd, msg.payload, sizeof(wfd));
            if (wfd.player_id == 0) break; // skip own actions

            ClientProjectile proj;
            proj.origin = {wfd.origin_x, wfd.origin_y, wfd.origin_z};
            proj.direction = {wfd.dir_x, wfd.dir_y, wfd.dir_z};
            proj.spawn_time = static_cast<float>(now_seconds());
            proj.weapon_type = wfd.weapon_type;
            proj.owner_id = wfd.player_id;
            proj.color_packed = id_to_color_packed(wfd.player_id);
            m_projectiles.push_back(proj);

            // Keep last 64 projectiles
            while (m_projectiles.size() > 64) {
                m_projectiles.erase(m_projectiles.begin());
            }
            break;
        }
        default:
            break;
    }
}

void OmegaClient::on_connected() {
    OZ_INFO("OmegaClient: connected to server");
}

void OmegaClient::on_disconnected() {
    OZ_INFO("OmegaClient: disconnected from server");
    m_remote_players.clear();
    m_projectiles.clear();
}

std::string OmegaClient::consume_pending_scene_data() {
    const std::lock_guard<std::mutex> lock(m_msg_mutex);
    std::string data = std::move(m_pending_scene);
    m_pending_scene.clear();
    return data;
}

std::string OmegaClient::consume_pending_chat_message() {
    const std::lock_guard<std::mutex> lock(m_msg_mutex);
    std::string msg = std::move(m_chat_msg);
    m_chat_msg.clear();
    return msg;
}