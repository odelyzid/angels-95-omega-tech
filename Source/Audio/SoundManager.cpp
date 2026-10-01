#include "SoundManager.hpp"

#include "../Log.hpp"
#include "../Package/PackageAssetLoader.hpp"
#include "DspReverb.hpp"

#include <raymath.h>

// How long a one-shot handle stays resident after playing before it is freed.
static constexpr float kOneShotLifetime = 2.0f;
// How long a script sound cache entry stays idle before it is unloaded.
static constexpr float kScriptSoundIdleLimit = 5.0f;

SoundManager& SoundManager::Instance() {
    static SoundManager instance;
    return instance;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void SoundManager::AttachReverbProcessor() {
    DspReverb::Reset();
    AttachAudioMixedProcessor(DspReverb::AudioCallback);
}

void SoundManager::LoadCoreSounds() {
    GameSounds& s = m_sounds;
    s.CollisionSound = LoadSoundWithFallback("GameData/Global/Sounds/CollisionSound.mp3");
    s.WalkingSound    = LoadSoundWithFallback("GameData/Global/Sounds/WalkingSound.mp3");
    s.ChasingSound    = LoadSoundWithFallback("GameData/Global/Sounds/ChasingSound.mp3");
    s.UIClick         = LoadSoundWithFallback("GameData/Global/Sounds/UI/MenuSelect_Click.wav");
    s.Death           = LoadSoundWithFallback("GameData/Global/Sounds/Hurt.mp3");
    s.JumpSound       = LoadSoundWithFallback("GameData/Global/Sounds/Player/jump1.wav");
    s.WeaponLoadSound = LoadSoundWithFallback("GameData/Global/Sounds/Player/weapload.wav");
    s.MenuSelectSound = LoadSoundWithFallback("GameData/Global/Sounds/UI/MenuSelect_Click.wav");
    s.MatchStartSound = LoadSoundWithFallback("GameData/Global/Sounds/UI/MenuRoundBeginn.wav");
    m_textNoise       = LoadSoundWithFallback("GameData/Global/Sounds/TalkingNoise.mp3");
}

void SoundManager::Shutdown() {
    ResetWorldAudio();

    GameSounds& s = m_sounds;
    for (Sound* snd : {&s.CollisionSound, &s.WalkingSound, &s.UIClick,
                       &s.ChasingSound, &s.Death, &s.JumpSound, &s.WeaponLoadSound,
                       &s.MenuSelectSound, &s.MatchStartSound, &m_textNoise}) {
        if (snd->frameCount > 0) {
            StopSound(*snd);
            UnloadSound(*snd);
        }
        *snd = Sound{0};
    }
    DspReverb::SetMix(0.0f);
    DspReverb::SetDecay(0.5f);
}

void SoundManager::ResetWorldAudio() {
    StopAmbience();
    m_prevSoundZone.clear();
    m_wasInReverb = false;

    if (m_defaultWorldMusic.ctxData != nullptr)
        m_defaultWorldMusic = Music{0};

    StopWorldMusic();
    DspReverb::SetMix(0.0f);
    DspReverb::SetDecay(0.5f);

    for (auto& kv : m_scriptSounds) {
        if (kv.second.sound.frameCount > 0)
            UnloadSound(kv.second.sound);
    }
    m_scriptSounds.clear();
    PruneOneShot(kOneShotLifetime);   // force-release the one-shot handle
}

// ---------------------------------------------------------------------------
// One-shot effects
// ---------------------------------------------------------------------------
void SoundManager::PlayUIClick() {
    if (m_sounds.UIClick.frameCount > 0)
        PlaySound(m_sounds.UIClick);
}

void SoundManager::PlayChasing() {
    if (m_sounds.ChasingSound.frameCount == 0) return;
    // Throttle so a pack of NPCs cannot stack the stinger on one frame.
    if (!IsSoundPlaying(m_sounds.ChasingSound))
        PlaySound(m_sounds.ChasingSound);
}

void SoundManager::PlayDeath() {
    if (m_sounds.Death.frameCount > 0 && !IsSoundPlaying(m_sounds.Death))
        PlaySound(m_sounds.Death);
}

void SoundManager::PlayCollision() {
    if (m_sounds.CollisionSound.frameCount > 0)
        PlaySound(m_sounds.CollisionSound);
}

void SoundManager::PlayTextNoise() {
    if (m_textNoise.frameCount > 0)
        PlaySound(m_textNoise);
}

void SoundManager::StopTextNoise() {
    if (m_textNoise.frameCount > 0)
        StopSound(m_textNoise);
}

void SoundManager::PlayJump() {
    if (m_sounds.JumpSound.frameCount > 0)
        PlaySound(m_sounds.JumpSound);
}

void SoundManager::PlayWeaponLoad() {
    if (m_sounds.WeaponLoadSound.frameCount > 0)
        PlaySound(m_sounds.WeaponLoadSound);
}

void SoundManager::PlayMenuSelect() {
    if (m_sounds.MenuSelectSound.frameCount > 0)
        PlaySound(m_sounds.MenuSelectSound);
}

void SoundManager::PlayMatchStart() {
    if (m_sounds.MatchStartSound.frameCount > 0)
        PlaySound(m_sounds.MatchStartSound);
}

void SoundManager::PlayScream(const Sound& scream) {
    if (scream.frameCount > 0)
        PlaySound(scream);
}

Sound SoundManager::LoadPawnScream(const std::string& defName,
                                  const std::string& screamPath) {
    if (!screamPath.empty()) {
        Sound snd = LoadSoundWithFallback(screamPath.c_str());
        if (snd.frameCount > 0)
            return snd;
    }
    // Convention fallbacks shipped next to the pawn sprites.
    const std::string base = "GameData/Global/Pawn/" + defName;
    for (const char* ext : {".wav", ".mp3"}) {
        Sound snd = LoadSoundWithFallback((base + ext).c_str());
        if (snd.frameCount > 0)
            return snd;
    }
    OZ_WARN("Pawn '%s': no scream sound found (path='%s')", defName.c_str(), screamPath.c_str());
    return Sound{0};
}

void SoundManager::StartWalkLoop() {
    if (m_sounds.WalkingSound.frameCount == 0) return;
    if (!IsSoundPlaying(m_sounds.WalkingSound))
        PlaySound(m_sounds.WalkingSound);
}

void SoundManager::StopWalkLoop() {
    if (m_sounds.WalkingSound.frameCount > 0 && IsSoundPlaying(m_sounds.WalkingSound))
        StopSound(m_sounds.WalkingSound);
}

// ---------------------------------------------------------------------------
// Music
// ---------------------------------------------------------------------------
void SoundManager::PlayStream(Music music) {
    if (music.ctxData != nullptr)
        PlayMusicStream(music);
}

void SoundManager::StopStream(Music music) {
    if (music.ctxData != nullptr)
        StopMusicStream(music);
}

void SoundManager::UpdateStream(Music music) {
    if (music.ctxData != nullptr)
        UpdateMusicStream(music);
}

void SoundManager::PlayWorldMusic(const std::string& assetPrefix) {
    StopWorldMusic();

    const std::string worldTrack = assetPrefix + "Music/Main.mp3";
    // Fallback so worlds without their own track still get atmosphere.
    static const char* kGlobalAmbience = "GameData/Global/Sounds/Ambience/Music_Atmo_1.wav";

    const bool hasWorldTrack = IsPathFile(worldTrack.c_str());
    const char* path = hasWorldTrack ? worldTrack.c_str() : kGlobalAmbience;
    if (!hasWorldTrack && !IsPathFile(kGlobalAmbience)) {
        OZ_WARN("PlayWorldMusic: no music for prefix '%s' and no global ambience", assetPrefix.c_str());
        return;
    }

    Music m = LoadMusicWithFallback(path);
    if (m.ctxData == nullptr) {
        OZ_WARN("PlayWorldMusic: failed to load '%s'", path);
        return;
    }
    m_sounds.BackgroundMusic = m;
    m_sounds.MusicFound = true;
    PlayMusicStream(m_sounds.BackgroundMusic);
    OZ_INFO("PlayWorldMusic: %s", path);
}

void SoundManager::StopWorldMusic() {
    if (!m_sounds.MusicFound) return;
    StopMusicStream(m_sounds.BackgroundMusic);
    UnloadMusicStream(m_sounds.BackgroundMusic);
    m_sounds.BackgroundMusic = Music{0};
    m_sounds.MusicFound = false;
}

void SoundManager::Update() {
    PruneScriptSoundCache(GetFrameTime());
    PruneOneShot(GetFrameTime());
    if (m_sounds.MusicFound)
        UpdateMusicStream(m_sounds.BackgroundMusic);
}

// ---------------------------------------------------------------------------
// Gameplay sound zones
// ---------------------------------------------------------------------------
void SoundManager::EnterSoundZone(const ZoneVolumeNode& zone) {
    const GameplaySoundProfile& sp = zone.soundProfile;

    if (sp.HasEnterMusic()) {
        // Capture the world track before crossfading so exit can restore it.
        if (m_prevSoundZone.empty() && m_sounds.MusicFound)
            m_defaultWorldMusic = m_sounds.BackgroundMusic;

        StopMusicStream(m_sounds.BackgroundMusic);
        Music newMusic = LoadMusicWithFallback(sp.music_on_enter.c_str());
        if (newMusic.ctxData != nullptr) {
            m_sounds.BackgroundMusic = newMusic;
            m_sounds.MusicFound = true;
            PlayMusicStream(m_sounds.BackgroundMusic);
        } else {
            OZ_WARN("Sound zone '%s': music '%s' not found",
                    zone.name.c_str(), sp.music_on_enter.c_str());
            // Keep the (stopped) world track so exit does not clobber it.
            m_sounds.BackgroundMusic = m_defaultWorldMusic;
            m_sounds.MusicFound = m_defaultWorldMusic.ctxData != nullptr;
        }
    }

    if (sp.HasAmbience()) {
        if (m_ambienceZoneName != zone.name) {
            StopAmbience();
            m_ambienceHandle = LoadSoundWithFallback(sp.ambience_loop.c_str());
            if (m_ambienceHandle.frameCount > 0)
                PlaySound(m_ambienceHandle);
            m_ambienceZoneName = zone.name;
        }
        // Keep-alive: raylib stops a sound when its stream drains.
        if (m_ambienceHandle.frameCount > 0 && !IsSoundPlaying(m_ambienceHandle))
            PlaySound(m_ambienceHandle);
    }

    if (sp.HasEnterSfx()) {
        Sound sfx = LoadSoundWithFallback(sp.sfx_on_enter.c_str());
        if (sfx.frameCount > 0) {
            if (m_oneShot.frameCount > 0)
                UnloadSound(m_oneShot);
            m_oneShot = sfx;
            m_oneShotIdle = 0.0f;
            PlaySound(m_oneShot);
        }
    }

    m_prevSoundZone = zone.name;
}

void SoundManager::StopAmbience() {
    if (m_ambienceHandle.frameCount > 0) {
        StopSound(m_ambienceHandle);
        UnloadSound(m_ambienceHandle);
    }
    m_ambienceHandle = Sound{0};
    m_ambienceZoneName.clear();
}

void SoundManager::ExitSoundZone() {
    StopAmbience();

    if (m_defaultWorldMusic.ctxData != nullptr) {
        StopMusicStream(m_sounds.BackgroundMusic);
        m_sounds.BackgroundMusic = m_defaultWorldMusic;
        m_sounds.MusicFound = true;
        PlayMusicStream(m_sounds.BackgroundMusic);
        m_defaultWorldMusic = Music{0};
    }
    m_prevSoundZone.clear();
}

void SoundManager::UpdateSoundZones(const PointRegion& region) {
    const ZoneVolumeNode* soundZone = nullptr;
    if (region.primaryZoneId >= 0) {
        const ZoneVolumeNode* z = ZoneManager::Instance().GetZone(region.primaryZoneId);
        if (z && z->zoneType == ZoneType::ZONE_GAMEPLAY_SOUND)
            soundZone = z;
    }

    if (soundZone) {
        // Already inside this zone: only the ambience keep-alive is needed.
        if (m_prevSoundZone == soundZone->name) {
            if (soundZone->soundProfile.HasAmbience() &&
                m_ambienceHandle.frameCount > 0 &&
                !IsSoundPlaying(m_ambienceHandle)) {
                PlaySound(m_ambienceHandle);
            }
            return;
        }
        EnterSoundZone(*soundZone);
        return;
    }

    if (region.primaryZoneId < 0 && !m_prevSoundZone.empty())
        ExitSoundZone();
}

// ---------------------------------------------------------------------------
// Reverb
// ---------------------------------------------------------------------------
void SoundManager::UpdateReverb(const PointRegion& region) {
    const bool inReverb = region.HasZoneType(ZoneType::ZONE_REVERB);
    if (inReverb == m_wasInReverb) return;

    if (inReverb) {
        float mix   = region.combinedEnv.reverbMix;
        float decay = region.combinedEnv.reverbDecay;
        if (mix <= 0.0f)   mix = 0.35f;
        if (decay <= 0.0f) decay = 0.5f;
        float vol = 1.0f - mix * 0.5f;
        OZ_INFO("ZONE_REVERB entered - mix=%.2f decay=%.2f vol=%.2f", mix, decay, vol);
        if (m_sounds.MusicFound)
            SetMusicVolume(m_sounds.BackgroundMusic, vol);
        DspReverb::SetMix(mix);
        DspReverb::SetDecay(decay);
    } else {
        OZ_INFO("ZONE_REVERB exited - restoring audio");
        if (m_sounds.MusicFound)
            SetMusicVolume(m_sounds.BackgroundMusic, 1.0f);
        DspReverb::SetMix(0.0f);
        DspReverb::SetDecay(0.5f);
    }
    m_wasInReverb = inReverb;
}

// ---------------------------------------------------------------------------
// LightningScript `play_sound`
// ---------------------------------------------------------------------------
bool SoundManager::PlayScriptSound(const std::string& path) {
    if (path.empty()) return false;

    auto it = m_scriptSounds.find(path);
    if (it == m_scriptSounds.end()) {
        // Package-aware: scripts referencing .ozsnd assets must resolve.
        Sound snd = LoadSoundWithFallback(path.c_str());
        if (snd.frameCount == 0) {
            OZ_WARN("play_sound: '%s' not found", path.c_str());
            return false;
        }
        it = m_scriptSounds.emplace(path, ScriptSound{snd, 0.0f}).first;
    } else {
        it->second.idle = 0.0f;   // keep alive
    }

    PlaySound(it->second.sound);
    return true;
}

void SoundManager::PruneScriptSoundCache(float dt) {
    for (auto it = m_scriptSounds.begin(); it != m_scriptSounds.end(); ) {
        it->second.idle += dt;
        if (it->second.idle > kScriptSoundIdleLimit) {
            if (it->second.sound.frameCount > 0)
                UnloadSound(it->second.sound);
            it = m_scriptSounds.erase(it);
        } else {
            ++it;
        }
    }
}

void SoundManager::PruneOneShot(float dt) {
    if (m_oneShot.frameCount == 0) return;
    m_oneShotIdle += dt;
    if (m_oneShotIdle >= kOneShotLifetime) {
        StopSound(m_oneShot);
        UnloadSound(m_oneShot);
        m_oneShot = Sound{0};
        m_oneShotIdle = 0.0f;
    }
}