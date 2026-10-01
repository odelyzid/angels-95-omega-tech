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
    //
    // runtimeStats is consulted before the def, matching the readStat lambda in
    // FireSelectedWeapon. It used to read the def only, so a value set by a
    // script or by a spawned instance override was invisible here even though
    // the projectile path saw the same stat's overridden value -- recoil and
    // damage therefore disagreed with what actually got spawned.
    float SelectedWeaponStat(const std::string& key, float defVal) const;

    // Read a string stat (asset paths, sound paths) from the selected weapon.
    // Returns `def` when the weapon, the def, or the key is absent.
    std::string SelectedWeaponString(const std::string& key,
                                     const std::string& def = "") const;

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

    // Play an authored sound stat (fire_sound / swing_sound), falling back to
    // `fallbackPath` when the weapon def does not declare the key.
    void PlayWeaponSound(const char* pathKey, const char* volKey,
                         const char* pitchKey, const std::string& fallbackPath);

    OmegaClient* m_client = nullptr;
    bool* m_networkEnabled = nullptr;

    float m_recoilPitch = 0.0f;
    float m_recoilYaw = 0.0f;
    float m_crosshairBloom = 0.0f;
    bool m_adsActive = false;
};
