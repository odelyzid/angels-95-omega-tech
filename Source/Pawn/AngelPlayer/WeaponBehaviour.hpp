#pragma once
#include "raylib.h"
#include <string>

class OmegaClient;

// WeaponBehaviour — client weapon firing, recoil and ADS/crosshair state for
// the AngelPlayer. Part of the AngelPlayer player stack.
//
// The network client + its "network enabled" flag are injected by the host so
// this stays decoupled from Main.cpp's engine globals.
class WeaponBehaviour {
public:
    static WeaponBehaviour& Instance() {
        static WeaponBehaviour instance;
        return instance;
    }

    void SetClient(OmegaClient* client, bool* networkEnabled) {
        m_client = client;
        m_networkEnabled = networkEnabled;
    }

    // Read a float stat from the currently selected weapon entity.
    float SelectedWeaponStat(const std::string& key, float defVal) const;

    // Fire the selected weapon (view-model FX, recoil, network damage).
    void FireWeapon(Camera3D& cam);

    // Player input: fire while LMB held (ignored while a UI overlay is open).
    void HandleInput(bool uiBlocking, Camera3D& cam);

    // ADS state (right mouse). Set before Update().
    void SetAdsActive(bool v) { m_adsActive = v; }
    bool AdsActive() const { return m_adsActive; }

    // Decay recoil / bloom and apply the remaining recoil to the camera target.
    void Update(Camera3D& cam);

    float CrosshairBloom() const { return m_crosshairBloom; }

private:
    WeaponBehaviour() = default;

    OmegaClient* m_client = nullptr;
    bool* m_networkEnabled = nullptr;

    float m_recoilPitch = 0.0f;
    float m_recoilYaw = 0.0f;
    float m_crosshairBloom = 0.0f;
    bool m_adsActive = false;
};
