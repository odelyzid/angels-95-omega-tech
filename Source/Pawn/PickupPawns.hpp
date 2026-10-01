#pragma once
#include "raylib.h"

class OmegaClient;

// PickupPawns — client-side handler for **networked** pickups only.
//
// Owns the throttled auto-collect loop (E key / walk-over) that queries the
// server-replicated pickup list and sends collect requests. Local/offline
// pickups remain owned by PawnSystem::UpdatePickups (Authoritative pickups are
// granted server-side; this handler only asks the server to collect).
class PickupPawns {
public:
    static PickupPawns& Instance() {
        static PickupPawns instance;
        return instance;
    }

    void SetClient(OmegaClient* client) { m_client = client; }

    // Scan the server pickup list for a nearby active pickup and request a
    // collect. Called inside the network block of the game loop (throttled
    // internally; `pressedE` widens the range to the interact radius).
    // `local_world` restricts the scan to the world the player is actually in.
    void Update(bool pressedE, const Vector3& playerPos, int local_world, double now);

private:
    PickupPawns() = default;

    static constexpr double kAutoCollectInterval = 0.35;
    static constexpr float kInteractRange = 5.0f;
    static constexpr float kWalkoverRange = 2.0f;

    OmegaClient* m_client = nullptr;
    double m_lastCollectTry = 0.0;
};

