#include "oz_sound_loader.h"
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
SoundLoader& SoundLoader::Instance() {
    static SoundLoader instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Find a registered SoundRef by name (case-insensitive)
// ---------------------------------------------------------------------------
SoundRef* SoundLoader::FindRef(const char* name) {
    for (auto& r : m_registry) {
        const char* a = name;
        const char* b = r.name;
        while (*a && *b) {
            if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
                break;
            a++; b++;
        }
        if (*a == *b) return &r;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// RegisterDefaults — all sounds the engine uses
// ---------------------------------------------------------------------------
void SoundLoader::RegisterDefaults() {
    // Global SFX (loaded once)
    m_registry.push_back({"CollisionSound", OZSoundType::SFX, "GameData/Global/Sounds/CollisionSound.mp3", true});
    m_registry.push_back({"WalkingSound",   OZSoundType::SFX, "GameData/Global/Sounds/WalkingSound.mp3", true});
    m_registry.push_back({"ChasingSound",   OZSoundType::SFX, "GameData/Global/Sounds/ChasingSound.mp3", true});
    m_registry.push_back({"UIClick",        OZSoundType::SFX, "GameData/Global/Title/Click.mp3", true});
    m_registry.push_back({"Death",          OZSoundType::SFX, "GameData/Global/Sounds/Hurt.mp3", true});
    m_registry.push_back({"TextNoise",      OZSoundType::SFX, "GameData/Global/Sounds/TalkingNoise.mp3", true});

    // Per-world music
    m_registry.push_back({"BackgroundMusic", OZSoundType::MUSIC_STREAM, "GameData/Worlds/World%i/Music/Main.mp3", false});

    // Per-world noise emitters (spatial music)
    m_registry.push_back({"NESound1", OZSoundType::MUSIC_STREAM, "GameData/Worlds/World%i/NoiseEmitter/NE1.mp3", false});
    m_registry.push_back({"NESound2", OZSoundType::MUSIC_STREAM, "GameData/Worlds/World%i/NoiseEmitter/NE2.mp3", false});
    m_registry.push_back({"NESound3", OZSoundType::MUSIC_STREAM, "GameData/Worlds/World%i/NoiseEmitter/NE3.mp3", false});

    // Per-world NPC scream (loaded per world in LoadWorld)
    m_registry.push_back({"Scream", OZSoundType::SFX, "GameData/Worlds/World%i/Entities/Walker/Scream.mp3", false});
}

// ---------------------------------------------------------------------------
// Load a single SoundRef (with optional worldIndex substitution)
// ---------------------------------------------------------------------------
void SoundLoader::LoadSoundRef(const SoundRef& ref, int worldIndex) {
    // Build final path
    char path[512];
    if (ref.isGlobal) {
        snprintf(path, sizeof(path), "%s", ref.pathPattern);
    } else {
        snprintf(path, sizeof(path), ref.pathPattern, worldIndex);
    }

    if (!IsPathFile(path)) {
        // File missing — skip silently (normal for per-world assets)
        return;
    }

    if (ref.type == OZSoundType::SFX) {
        Sound s = LoadSound(path);
        if (s.frameCount > 0) {
            m_sfx[ref.name] = s;
        }
    } else {
        Music m = LoadMusicStream(path);
        if (m.ctxType != 0 || m.ctxData != nullptr) {
            m_music[ref.name] = m;
        }
    }
}

// ---------------------------------------------------------------------------
// Unload a single SoundRef
// ---------------------------------------------------------------------------
void SoundLoader::UnloadSoundRef(const SoundRef& ref) {
    if (ref.type == OZSoundType::SFX) {
        auto it = m_sfx.find(ref.name);
        if (it != m_sfx.end()) {
            UnloadSound(it->second);
            m_sfx.erase(it);
        }
    } else {
        auto it = m_music.find(ref.name);
        if (it != m_music.end()) {
            UnloadMusicStream(it->second);
            m_music.erase(it);
        }
    }
}

// ---------------------------------------------------------------------------
// PreloadGlobal — load all global sounds
// ---------------------------------------------------------------------------
void SoundLoader::PreloadGlobal() {
    for (auto& ref : m_registry) {
        if (!ref.isGlobal) continue;
        LoadSoundRef(ref, 1);
    }
}

// ---------------------------------------------------------------------------
// PreloadWorld — load world-specific sounds for the given world index
// ---------------------------------------------------------------------------
void SoundLoader::PreloadWorld(int worldIndex) {
    if (m_worldLoaded.test(worldIndex)) return;
    m_worldLoaded.set(worldIndex);

    for (auto& ref : m_registry) {
        if (ref.isGlobal) continue;
        LoadSoundRef(ref, worldIndex);
    }
}

// ---------------------------------------------------------------------------
// UnloadWorld — unload sounds for a specific world
// ---------------------------------------------------------------------------
void SoundLoader::UnloadWorld(int worldIndex) {
    if (!m_worldLoaded.test(worldIndex)) return;
    m_worldLoaded.reset(worldIndex);

    for (auto& ref : m_registry) {
        if (ref.isGlobal) continue;
        UnloadSoundRef(ref);
    }
}

// ---------------------------------------------------------------------------
// UnloadAll
// ---------------------------------------------------------------------------
void SoundLoader::UnloadAll() {
    for (auto& kv : m_sfx) UnloadSound(kv.second);
    for (auto& kv : m_music) UnloadMusicStream(kv.second);
    m_sfx.clear();
    m_music.clear();
    m_worldLoaded.reset();
}

// ---------------------------------------------------------------------------
// GetSFX / GetMusic
// ---------------------------------------------------------------------------
Sound* SoundLoader::GetSFX(const char* name) {
    auto it = m_sfx.find(name);
    if (it != m_sfx.end()) return &it->second;
    return nullptr;
}

Music* SoundLoader::GetMusic(const char* name) {
    auto it = m_music.find(name);
    if (it != m_music.end()) return &it->second;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Master volume
// ---------------------------------------------------------------------------
void SoundLoader::SetMasterVolume(float vol) {
    m_masterVol = (vol < 0.0f) ? 0.0f : (vol > 1.0f ? 1.0f : vol);
    ::SetMasterVolume(m_masterVol);
}

// ---------------------------------------------------------------------------
// StopAll
// ---------------------------------------------------------------------------
void SoundLoader::StopAll() {
    for (auto& kv : m_sfx) StopSound(kv.second);
    for (auto& kv : m_music) StopMusicStream(kv.second);
}
