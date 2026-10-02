#pragma once
// ---------------------------------------------------------------------------
// World/LevelSettings.hpp — per-world level metadata
//
// Extracted from Source/Pawn/OzPawnSystem.hpp. LevelSettings is populated by
// the OZONE `levelinfo` and `particles` primitives and is applied by the world
// orchestrator (Core.hpp LoadWorld / AngelEd open) after the loader has handed
// the parsed primitives over. Keeping it out of the pawn header means world
// metadata parsing never has to link against the entity system.
//
// Plain data, no raylib dependency.
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

#include "GameType.hpp"   // ParseLevelInfo resolves gametype=<name>
#include "../Log.hpp"     // OZ_WARN for the unknown-gametype fallback
#include "../Log.hpp"
#include "GameType.hpp"

// Level metadata — per-world game rules + environment defaults (LevelInfo/Particles)
struct LevelSettings {
    int gameType = 0;              // matches editor GameType enum order
    int maxPlayers = 8;
    float respawnTime = 5.0f;
    bool timeLimitEnabled = false;
    float timeLimitMinutes = 10.0f;
    int scoreLimit = 50;
    bool friendlyFire = false;
    std::string skyboxPath;        // empty = world default (Models/Skybox.png)
    std::string skyboxSidePath;    // optional horizon/side sky texture (cap = skyboxPath)
    // Ambient particle weather
    int particleType = 0;          // 0=none, 1=snow, 2=rain, 3=void, 4=psychic
    float particleDensity = 50.0f;
    float particleSpeed = 1.0f;
    int particleR = 200, particleG = 200, particleB = 200;
    float particleWindX = 0.0f, particleWindZ = 0.0f;

    bool WeatherEnabled() const { return particleType != 0; }
};

// OZONE `levelinfo gameType maxPlayers respawnTime timeLimitEnabled
// timeLimitMinutes scoreLimit friendlyFire skyboxPath [skyboxSidePath] [gametype=<name>]`
//
// `gametypeKey` is the optional trailing `gametype=<name>` kwarg. When it
// resolves to a known mode it overrides the positional arg(0); when it does
// NOT resolve it warns and falls back to arg(0) rather than silently
// becoming 0 (the failure mode GameData/Worlds/EngineTest/World.ozone:282
// documents for the old bare-token parse).
inline void ParseLevelInfo(LevelSettings& s, const std::vector<float>& args,
                           const std::string& skyboxPath,
                           const std::string& skyboxSidePath,
                           const std::string& gametypeKey = "") {
    auto arg = [&](int i) -> float {
        return (i >= 0 && i < (int)args.size()) ? args[i] : 0.0f;
    };
    s.gameType = (int)arg(0);
    if (!gametypeKey.empty()) {
        oz::gametype::GameType named = oz::gametype::GameTypeFromName(gametypeKey);
        if (oz::gametype::IsGameTypeKey(gametypeKey)) {
            s.gameType = (int)named;
        } else {
            OZ_WARN("levelinfo: unknown gametype='%s', using positional id %d",
                    gametypeKey.c_str(), (int)s.gameType);
        }
    }
    s.maxPlayers = (int)arg(1);
    s.respawnTime = arg(2);
    s.timeLimitEnabled = arg(3) != 0.0f;
    s.timeLimitMinutes = arg(4);
    s.scoreLimit = (int)arg(5);
    s.friendlyFire = arg(6) != 0.0f;
    s.skyboxPath = skyboxPath;
    s.skyboxSidePath = skyboxSidePath;
}

// OZONE `particles type density speed r g b windX windZ`
inline void ParseLevelParticles(LevelSettings& s, const std::vector<float>& args) {
    auto arg = [&](int i) -> float {
        return (i >= 0 && i < (int)args.size()) ? args[i] : 0.0f;
    };
    s.particleType = (int)arg(0);
    s.particleDensity = arg(1);
    s.particleSpeed = arg(2);
    s.particleR = (int)arg(3);
    s.particleG = (int)arg(4);
    s.particleB = (int)arg(5);
    s.particleWindX = arg(6);
    s.particleWindZ = arg(7);
}