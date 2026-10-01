#include "../WindowsCompat.hpp"   // must precede raylib + winsock2 includes
#include "PickupPawns.hpp"
#include "../Client/Client.hpp"
#include <cmath>

void PickupPawns::Update(bool pressedE, const Vector3& playerPos, int local_world, double now) {
    if (!m_client || !m_client->is_connected())
        return;

    // Update the throttle timestamp even when nothing is in range, otherwise the
    // full distance scan runs every frame when standing in an empty area.
    if (now - m_lastCollectTry <= kAutoCollectInterval && !pressedE)
        return;
    m_lastCollectTry = now;

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
