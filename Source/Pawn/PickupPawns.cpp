#include "../WindowsCompat.hpp"   // must precede raylib + winsock2 includes
#include "PickupPawns.hpp"
#include "../Client/Client.hpp"
#include <cmath>

void PickupPawns::Update(bool pressedE, const Vector3& playerPos, double now) {
    if (!m_client || !m_client->is_connected())
        return;

    bool want = pressedE || (now - m_lastCollectTry > kAutoCollectInterval);
    if (!want)
        return;

    const auto& pickups = m_client->pickups();
    float nearest_dist = pressedE ? kInteractRange : kWalkoverRange;
    int nearest_pickup = -1;
    int nearest_world = 0;
    for (const auto& p : pickups) {
        if (!p.active) continue;
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
        m_lastCollectTry = now;
    }
}
