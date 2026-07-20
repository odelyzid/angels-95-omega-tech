#include "Client.hpp"
#include "../Log.hpp"
#include <cstring>
#include <cstdio>
#include <mutex>

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

    net::NetworkMessage msg;
    msg.magic = net::MAGIC;
    msg.type = static_cast<uint32_t>(net::MessageType::PICKUP_COLLECT);
    msg.size = sizeof(pcd);
    msg.sequence = 0;
    msg.timestamp = static_cast<uint32_t>(time(nullptr));
    std::memcpy(msg.payload, &pcd, sizeof(pcd));

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
            // Update or add NPC in local list
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
        case net::MessageType::PLAYER_HURT: {
            // Can trigger screen shake or damage flash
            // (for now, just ignore)
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