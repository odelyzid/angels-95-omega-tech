#pragma once
#include "raylib.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <bitset>
#include <vector>

// ---------------------------------------------------------------------------
// SoundLoader — centralized sound & music stream manager
//
// Consolidates all LoadSound / LoadMusicStream calls into one place.
// Supports global sounds (loaded once) and per-world sounds (loaded on
// world switch, keyed by world index).
//
// Usage:
//   SoundLoader::Instance().PreloadGlobal();
//   SoundLoader::Instance().PreloadWorld(1);
//   Sound* s = SoundLoader::Instance().GetSFX("WalkingSound");
// ---------------------------------------------------------------------------

enum class OZSoundType : uint8_t {
    SFX,
    MUSIC_STREAM
};

struct SoundRef {
    const char* name;          // logical name, e.g. "WalkingSound"
    OZSoundType type;
    const char* pathPattern;   // may contain "%i" for world index
    bool isGlobal;             // true = loaded once, false = per-world
};

class SoundLoader {
public:
    // Register all known sound references (called once at startup)
    void RegisterDefaults();

    // Load all global SFX and music streams
    void PreloadGlobal();

    // Load world-specific sounds (noise emitters, background music, scream)
    void PreloadWorld(int worldIndex);

    // Unload sounds for one world (frees slots for another world)
    void UnloadWorld(int worldIndex);

    // Unload every sound and music stream
    void UnloadAll();

    // Retrieve a loaded sound / music stream by logical name; nullptr if not loaded
    Sound* GetSFX(const char* name);
    Music* GetMusic(const char* name);

    // Master volume (0.0 – 1.0); applied to all subsequent plays
    void SetMasterVolume(float vol);
    float MasterVolume() const { return m_masterVol; }

    // Stop every currently playing sound / music stream
    void StopAll();

    // Singleton
    static SoundLoader& Instance();

private:
    static constexpr int kMaxWorlds = 32;

    std::unordered_map<std::string, Sound> m_sfx;
    std::unordered_map<std::string, Music> m_music;
    std::bitset<kMaxWorlds> m_worldLoaded;
    std::vector<SoundRef> m_registry;
    float m_masterVol = 1.0f;

    SoundRef* FindRef(const char* name);
    void LoadSoundRef(const SoundRef& ref, int worldIndex);
    void UnloadSoundRef(const SoundRef& ref);
};
