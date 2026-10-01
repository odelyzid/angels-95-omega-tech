#include "PlayerProfile.hpp"
#include "IniConfig.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

const char* kSection = "Profiles";

// Key prefix for one profile's fields, e.g. "Profile2_DisplayName".
std::string Prefix(int i) { return "Profile" + std::to_string(i) + "_"; }

// Strip anything that would break the line-based INI format. Reading stays with
// IniConfig (which splits on the FIRST '=', so a value may contain one); only
// newlines would terminate the record early.
std::string Sanitize(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (c == '\n' || c == '\r') continue;
        out.push_back(c);
    }
    return out;
}

} // namespace

const char* PlayerProfileManager::DefaultPath() { return "System/PlayerProfiles.ini"; }

PlayerProfile PlayerProfileManager::MakeDefault() {
    PlayerProfile p;
    p.displayName = "Player";
    p.modelPath   = "";   // empty = engine default model
    p.voiceSet    = "";   // empty = engine default voice set
    p.team        = 0;    // unassigned
    return p;
}

PlayerProfileManager& PlayerProfileManager::Instance() {
    static PlayerProfileManager instance;
    return instance;
}

void PlayerProfileManager::SetPath(const std::string& path) {
    m_path = path.empty() ? DefaultPath() : path;
}

void PlayerProfileManager::Init() {
    if (m_path.empty()) m_path = DefaultPath();
    LoadFrom(m_path);
    // A missing/empty/corrupt file must still leave a usable profile behind:
    // every caller assumes Active() is valid.
    if (m_profiles.empty()) {
        m_profiles.push_back(MakeDefault());
        m_active = 0;
        SaveTo(m_path);
    }
    EnsureValid();
}

void PlayerProfileManager::EnsureValid() {
    if (m_profiles.empty()) {
        m_profiles.push_back(MakeDefault());
    }
    if (m_active < 0 || m_active >= static_cast<int>(m_profiles.size())) {
        m_active = 0;
    }
}

int  PlayerProfileManager::Count() const { return static_cast<int>(m_profiles.size()); }

int  PlayerProfileManager::ActiveIndex() const {
    return (m_active >= 0 && m_active < static_cast<int>(m_profiles.size())) ? m_active : 0;
}

const PlayerProfile& PlayerProfileManager::Get(int index) const {
    static const PlayerProfile fallback = MakeDefault();
    if (index < 0 || index >= static_cast<int>(m_profiles.size())) return fallback;
    return m_profiles[index];
}

const PlayerProfile& PlayerProfileManager::Active() const {
    return Get(ActiveIndex());
}

void PlayerProfileManager::SetActive(int index) {
    if (index < 0 || index >= static_cast<int>(m_profiles.size())) return;
    m_active = index;
    Save();
}

int PlayerProfileManager::Add(PlayerProfile profile) {
    // Guard against a blank slot becoming the active profile by accident.
    if (profile.displayName.empty()) profile.displayName = "Player";
    m_profiles.push_back(std::move(profile));
    const int index = static_cast<int>(m_profiles.size()) - 1;
    m_active = index;
    Save();
    return index;
}

bool PlayerProfileManager::Update(int index, PlayerProfile profile) {
    if (index < 0 || index >= static_cast<int>(m_profiles.size())) return false;
    if (profile.displayName.empty()) profile.displayName = "Player";
    m_profiles[index] = std::move(profile);
    Save();
    return true;
}

bool PlayerProfileManager::Remove(int index) {
    // Always keep at least one profile so Active() never has to be empty.
    if (m_profiles.size() <= 1) return false;
    if (index < 0 || index >= static_cast<int>(m_profiles.size())) return false;

    m_profiles.erase(m_profiles.begin() + index);
    if (m_active >= static_cast<int>(m_profiles.size())) {
        m_active = static_cast<int>(m_profiles.size()) - 1;
    } else if (m_active > index) {
        --m_active;   // removed a slot before the active one
    }
    Save();
    return true;
}

void PlayerProfileManager::Reset() {
    m_profiles.clear();
    m_profiles.push_back(MakeDefault());
    m_active = 0;
    Save();
}

void PlayerProfileManager::Save() const { SaveTo(m_path.empty() ? DefaultPath() : m_path); }

void PlayerProfileManager::LoadFrom(const std::string& path) {
    m_profiles.clear();
    m_active = 0;

    IniConfig cfg;
    if (!cfg.Load(path.c_str())) return;   // absent file -> caller seeds a default

    const int count = cfg.GetInt(kSection, "Count", 0);
    if (count <= 0) return;

    m_profiles.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::string p = Prefix(i);
        PlayerProfile profile;
        profile.displayName = cfg.Get(kSection, (p + "DisplayName").c_str(), "Player");
        profile.modelPath   = cfg.Get(kSection, (p + "ModelPath").c_str(), "");
        profile.voiceSet    = cfg.Get(kSection, (p + "VoiceSet").c_str(), "");
        profile.team        = cfg.GetInt(kSection, (p + "Team").c_str(), 0);
        if (profile.displayName.empty()) profile.displayName = "Player";
        m_profiles.push_back(std::move(profile));
    }

    m_active = cfg.GetInt(kSection, "ActiveIndex", 0);
}

void PlayerProfileManager::SaveTo(const std::string& path) const {
    // System/ may not exist yet on a fresh checkout.
    std::error_code ec;
    const fs::path fsPath(path);
    if (fsPath.has_parent_path()) fs::create_directories(fsPath.parent_path(), ec);

    // Written directly rather than through IniConfig::Save: IniConfig keeps
    // sections in an unordered_map, so the emitted key order is arbitrary and
    // the file becomes unpleasant to hand-edit. Loading still goes through
    // IniConfig, which does not care about order. Profiles are written in slot
    // order so a human can read and tweak them.
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return;

    f << "; Angels95 player profiles\n"
      << "; Profiles are listed in slot order; the active one is marked [ACTIVE].\n"
      << "; Team: 0 = unassigned, otherwise a 1-based team index.\n"
      << "[Profiles]\n";
    f << "Count=" << m_profiles.size() << "\n";
    f << "ActiveIndex=" << ActiveIndex() << "\n";

    for (size_t i = 0; i < m_profiles.size(); ++i) {
        const PlayerProfile& p = m_profiles[i];
        const std::string prefix = Prefix(static_cast<int>(i));
        f << "\n; " << (static_cast<int>(i) == ActiveIndex() ? "[ACTIVE] " : "[        ] ")
          << "slot " << static_cast<int>(i) << "\n";
        f << prefix << "DisplayName=" << Sanitize(p.displayName) << "\n";
        f << prefix << "ModelPath="   << Sanitize(p.modelPath)   << "\n";
        f << prefix << "VoiceSet="    << Sanitize(p.voiceSet)    << "\n";
        f << prefix << "Team="        << p.team                 << "\n";
    }
}
