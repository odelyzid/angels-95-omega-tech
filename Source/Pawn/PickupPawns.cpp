#include "../WindowsCompat.hpp"   // must precede raylib + winsock2 includes
#include "PickupPawns.hpp"
#include "../Client/Client.hpp"
#include "../Log.hpp"
#include "../Pawn/OzPawnSystem.hpp"
#include <cmath>

void PickupPawns::Update(bool pressedE, const Vector3& playerPos, int local_world, double now) {
    if (!m_client || !m_client->is_connected())
        return;

    // Update the throttle timestamp even when nothing is in range, otherwise the
    // full distance scan runs every frame when standing in an empty area.
    if (now - m_lastCollectTry <= kAutoCollectInterval && !pressedE)
        return;
    m_lastCollectTry = now;

    // Self-heal: we are connected, this world has pickups drawn from World.ozone,
    // and the server has issued us none. That is the "pickups do not work in MP"
    // state, and it used to be indistinguishable from "nothing is nearby" — so
    // ask for the list instead of silently skipping the scan forever.
    if (local_world < 0) {
        // An unknown world index makes every server collect fail its world check.
        // Warn once, then keep going: leaving it -1 would mean never asking.
        if (!m_warnedNoWorld) {
            m_warnedNoWorld = true;
            OZ_WARN("Pickups: server world index is unknown; collects will be "
                    "refused until it resolves");
        }
    } else {
        m_warnedNoWorld = false;
        if (!m_client->has_pickup_world(local_world)) {
            const auto& local = PawnSystem::Instance().GetPickups();
            bool haveLocal = false;
            for (const auto& n : local)
                if (n.netId >= 0) { haveLocal = true; break; }
            if (haveLocal && now - m_lastResyncTry >= kResyncInterval) {
                m_lastResyncTry = now;
                OZ_INFO("Pickups: no server state for world %d — requesting re-sync", local_world);
                m_client->request_pickup_resync();
            }
            return;
        }
    }

    // pickups() returns a snapshot; only this world's pickups are collectable.
    // Without the filter a pickup in a different loaded world could win the
    // nearest-pickup contest and be requested with the wrong world index.
    const std::vector<ClientPickup> pickups = m_client->pickups();
    float nearest_dist = pressedE ? kInteractRange : kWalkoverRange;
    int nearest_pickup = -1;
    int nearest_world = local_world;
    for (const auto& p : pickups) {
        if (!p.active) continue;
        if (p.world_index != local_world) continue;
        float dx = p.position.x - playerPos.x;
        float dy = p.position.y - playerPos.y;
        float dz = p.position.z - playerPos.z;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist < nearest_dist) {
            nearest_dist = dist;
            nearest_pickup = p.id;
            nearest_world = p.world_index;
        }
    }
    if (nearest_pickup >= 0) {
        m_client->send_pickup_collect(nearest_pickup, nearest_world, nullptr);
    }
}
