#pragma once

#include <string>
#include <vector>

// One saved player profile.
//
// Deliberately raylib-free (no raygui/raylib types) so the same struct and the
// same file can be read by the client menu, AngelEd and the dedicated server.
// UI lives in Source/Menu/TitleMenu.hpp; networking is a separate concern and
// does NOT touch this header.
struct PlayerProfile {
    std::string displayName;   // shown in HUD / scoreboard / chat
    std::string modelPath;     // model or skin mesh reference ("" = engine default)
    std::string voiceSet;      // voice / sound set name      ("" = engine default)
    int         team = 0;      // 0 = unassigned, otherwise 1-based team index

    bool operator==(const PlayerProfile& o) const {
        return displayName == o.displayName && modelPath == o.modelPath &&
               voiceSet == o.voiceSet && team == o.team;
    }
};

// Profile slots persisted to an INI file (default System/PlayerProfiles.ini,
// alongside Angels95.ini).
//
// Layout uses ONE [Profiles] section with index-prefixed keys rather than a
// section per profile: IniConfig stores sections in an unordered_map, so
// per-profile sections would come back in an arbitrary order and profile 0
// could silently become profile 1 between saves. Index prefixes keep the
// ordering stable and round-trip lossless.
class PlayerProfileManager {
public:
    static PlayerProfileManager& Instance();

    void Init();   // load from disk; seeds one default profile if the file is absent/empty
    void Save() const;

    int  Count() const;
    const PlayerProfile& Get(int index) const;
    const PlayerProfile& Active() const;
    int  ActiveIndex() const;

    void SetActive(int index);              // out-of-range indices are ignored
    int  Add(PlayerProfile profile);       // returns the new index
    bool Update(int index, PlayerProfile profile);
    bool Remove(int index);                // refuses to remove the final profile
    void Reset();                          // back to a single default profile

    // Point the manager at a different file (tests / tools). Call before Init().
    void SetPath(const std::string& path);

    static PlayerProfile  MakeDefault();
    static const char*    DefaultPath();

    // Public so tests/tools can build an independent instance pointed at a
    // scratch file to verify save/load round-tripping.
    PlayerProfileManager() = default;

private:
    std::vector<PlayerProfile> m_profiles;
    int         m_active = 0;
    std::string m_path;

    void LoadFrom(const std::string& path);
    void SaveTo(const std::string& path) const;
    void EnsureValid();
};
