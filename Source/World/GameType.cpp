#include "GameType.hpp"

#include "LevelSettings.hpp"   // full definition: ResolveGameTypeInfo reads it
#include "../Log.hpp"
#include "../Script/LightningEntityRegistry.hpp"

#include <algorithm>
#include <cstring>

namespace oz {
namespace gametype {

namespace {

// Built-in ruleset table. Indexed by GameType value; the array is sized to
// the highest legacy id + 1 and extended for new modes. Every shipped world
// stores `levelinfo <id> ...` so ids 0-6 are the on-disk contract and must
// never be renumbered.
const GameTypeInfo* BuiltinTable() {
    static const GameTypeInfo kTable[] = {
        /* 0 SINGLEPLAYER */    { GameType::SINGLEPLAYER,    "singleplayer",    "Single Player",       false, 1, false, true,  5.0f, 1, 0, 1, 1, true,  50, false, 10.0f, {}, {} },
        /* 1 COOP */            { GameType::COOP,            "coop",            "Coop",                false, 1, false, true,  5.0f, 1, 0, 1, 1, true,  50, false, 10.0f, {}, {} },
        /* 2 ETHERAL_MATCH */   { GameType::ETHERAL_MATCH,   "etheral_match",   "Etheral Match (DM)",   false, 1, true,  true,  5.0f, 1, 0, 1, 1, true,  25, false, 10.0f, {}, {} },
        /* 3 ANGEL_TEAM_GAME */ { GameType::ANGEL_TEAM_GAME, "angel_team_game", "Angel Team Game (TDM)", true,  2, false, true,  5.0f, 1, 0, 1, 1, true,  75, false, 10.0f, {}, {} },
        /* 4 ANGEL_RUN */       { GameType::ANGEL_RUN,       "angel_run",       "Angel Run",           false, 1, false, false, 5.0f, 1, 0, 1, 1, false, 0,  true,   5.0f, {}, {} },
        /* 5 CAPTURE_THE_ORB */ { GameType::CAPTURE_THE_ORB, "capture_the_orb", "Capture The Orb",      true,  2, false, true,  5.0f, 1, 0, 1, 1, false, 0,  true,  10.0f, {}, {} },
        /* 6 TIME_SHIFT */      { GameType::TIME_SHIFT,      "time_shift",      "Time Shift",          false, 1, false, true,  5.0f, 1, 0, 1, 1, false, 0,  true,   8.0f, {}, {} },
        /* 7 DEATHMATCH */      { GameType::DEATHMATCH,      "deathmatch",      "Deathmatch",          false, 1, true,  true,  5.0f, 1, 0, 1, 1, true,  25, false, 10.0f, {}, {} },
        /* 8 CTF */             { GameType::CTF,             "ctf",             "Capture The Flag",     true,  2, false, true,  5.0f, 1, 0, 1, 1, false, 0,  true,  10.0f, {}, {} },
        /* 9 CAMPAIGN */        { GameType::CAMPAIGN,        "campaign",        "Campaign",            false, 1, false, true,  5.0f, 1, 0, 1, 1, false, 0,  false, 10.0f, {}, {} },
        /* 10 COOPERATIVE */    { GameType::COOPERATIVE,    "cooperative",    "Cooperative",         false, 1, false, true,  5.0f, 1, 0, 1, 1, false, 0,  false, 10.0f, {}, {} },
    };
    return kTable;
}

constexpr int kTableSize = 11;

const GameTypeInfo& BuiltinFor(GameType t) {
    int idx = static_cast<int>(t);
    if (idx < 0 || idx >= kTableSize) idx = 0;
    return BuiltinTable()[idx];
}

// Split a comma-separated list, dropping empty tokens. The .ozls stats parser
// stores a string verbatim and stops at the first space, so authored values
// must be quote-free and space-free; this splitter tolerates both anyway.
std::vector<std::string> SplitCommaList(const std::string& raw) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= raw.size()) {
        size_t comma = raw.find(',', start);
        std::string tok = (comma == std::string::npos)
            ? raw.substr(start)
            : raw.substr(start, comma - start);
        while (!tok.empty() && (tok.front() == ' ' || tok.front() == '\t')) tok.erase(0, 1);
        while (!tok.empty() && (tok.back() == ' ' || tok.back() == '\t')) tok.pop_back();
        if (!tok.empty()) out.push_back(tok);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Lookup + name table
// ---------------------------------------------------------------------------
const GameTypeInfo& GameTypeInfoFor(GameType t) {
    return BuiltinFor(t);
}

GameType GameTypeFromId(int raw) {
    if (raw < 0 || raw >= kTableSize) return GameType::SINGLEPLAYER;
    return static_cast<GameType>(raw);
}

GameType GameTypeFromName(const std::string& name) {
    std::string lower;
    lower.reserve(name.size());
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        lower.push_back(c);
    }
    for (int i = 0; i < kTableSize; i++) {
        const GameTypeInfo& info = BuiltinTable()[i];
        if (lower == info.key) return info.id;
    }
    return GameType::SINGLEPLAYER;
}

const char* GameTypeName(GameType t) { return BuiltinFor(t).label; }
const char* GameTypeKey(GameType t)  { return BuiltinFor(t).key; }

bool IsGameTypeKey(const std::string& name) {
    std::string lower;
    lower.reserve(name.size());
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        lower.push_back(c);
    }
    for (int i = 0; i < kTableSize; i++) {
        if (lower == BuiltinTable()[i].key) return true;
    }
    return false;
}

const std::vector<GameType>& AllGameTypes() {
    static const std::vector<GameType> kAll = [] {
        std::vector<GameType> v;
        for (int i = 0; i < kTableSize; i++) v.push_back(static_cast<GameType>(i));
        return v;
    }();
    return kAll;
}

// ---------------------------------------------------------------------------
// Match rules
// ---------------------------------------------------------------------------
bool ApplyScoreEvent(const GameTypeInfo& info, MatchState& st,
                     int team, int delta, MatchState::WinReason* out_reason) {
    if (team >= 0 && team < 8) st.teamScores[team] += delta;
    if (info.scoreLimitEnabled) {
        for (int i = 0; i < 8; i++) {
            if (st.teamScores[i] >= info.scoreLimit) {
                st.winner = MatchState::WinReason::SCORE_LIMIT;
                st.winningTeam = i;
                if (out_reason) *out_reason = st.winner;
                return true;
            }
        }
    }
    if (out_reason) *out_reason = MatchState::WinReason::NONE;
    return false;
}

WinCheck CheckWinConditions(const GameTypeInfo& info, const MatchState& st) {
    WinCheck out{ MatchState::WinReason::NONE, -1 };
    if (info.scoreLimitEnabled) {
        for (int i = 0; i < 8; i++) {
            if (st.teamScores[i] >= info.scoreLimit) {
                out.reason = MatchState::WinReason::SCORE_LIMIT;
                out.winningTeam = i;
                return out;
            }
        }
    }
    if (info.timeLimitEnabled && st.elapsedSeconds >= info.timeLimitMinutes * 60.0f) {
        out.reason = MatchState::WinReason::TIME_LIMIT;
        int best = 0;
        for (int i = 1; i < 8; i++) {
            if (st.teamScores[i] > st.teamScores[best]) best = i;
        }
        bool tie = false;
        for (int i = 0; i < 8; i++) {
            if (i != best && st.teamScores[i] == st.teamScores[best]) { tie = true; break; }
        }
        out.winningTeam = tie ? -1 : best;
    }
    return out;
}

int AssignTeam(const GameTypeInfo& info, const std::vector<int>& teamSizes,
               int requestedTeam) {
    const int teamCap = std::max(1, info.teamCount);
    // requestedTeam is a HINT ONLY. Honour it only when non-zero, in range,
    // and not unbalancing the roster by more than one player.
    if (requestedTeam > 0 && requestedTeam < teamCap) {
        int smallest = 0;
        for (int i = 1; i < teamCap; i++) {
            int sz = (i < (int)teamSizes.size()) ? teamSizes[i] : 0;
            int szSmallest = (smallest < (int)teamSizes.size()) ? teamSizes[smallest] : 0;
            if (sz < szSmallest) smallest = i;
        }
        int reqSz = (requestedTeam < (int)teamSizes.size()) ? teamSizes[requestedTeam] : 0;
        int smallSz = (smallest < (int)teamSizes.size()) ? teamSizes[smallest] : 0;
        if (reqSz <= smallSz + 1) return requestedTeam;
    }
    // Default: smallest team wins; ties go to the lowest index.
    int smallest = 0;
    for (int i = 1; i < teamCap; i++) {
        int sz = (i < (int)teamSizes.size()) ? teamSizes[i] : 0;
        int szSmallest = (smallest < (int)teamSizes.size()) ? teamSizes[smallest] : 0;
        if (sz < szSmallest) smallest = i;
    }
    return smallest;
}

// ---------------------------------------------------------------------------
// Layered resolution
// ---------------------------------------------------------------------------
GameTypeInfo ResolveGameTypeInfo(GameType t, const LevelSettings& s,
                                 const std::string& overrideDef) {
    GameTypeInfo info = GameTypeInfoFor(t);
    if (!overrideDef.empty()) {
        GameTypeInfo patched = GameTypeInfoFromOverride(overrideDef);
        if (patched.id == t) info = patched;
    }
    // The world's own levelinfo numbers win last: they are what the designer
    // authored in the editor.
    if (s.maxPlayers > 0) info.teamCount = std::max(1, info.teamCount); // maxPlayers is not teamCount; keep as-is
    if (s.scoreLimit > 0) { info.scoreLimit = s.scoreLimit; info.scoreLimitEnabled = true; }
    if (s.respawnTime > 0.0f) info.respawnTime = s.respawnTime;
    if (s.timeLimitEnabled) { info.timeLimitEnabled = true; info.timeLimitMinutes = s.timeLimitMinutes; }
    if (s.friendlyFire) info.friendlyFire = true;
    return info;
}

GameTypeInfo GameTypeInfoFromOverride(const std::string& defName) {
    GameTypeInfo info = GameTypeInfoFor(GameType::SINGLEPLAYER);
    const EntityDef* def = LightningEntityRegistry::Instance().Find(defName);
    if (!def || def->type != EntityType::GAMETYPE) return info;

    // The def header names the mode: `entity "deathmatch" : gametype { ... }`.
    info = GameTypeInfoFor(GameTypeFromName(def->name));

    auto applyFloat = [&](const char* key, float GameTypeInfo::*field) {
        auto it = def->stats.floats.find(key);
        if (it != def->stats.floats.end()) info.*field = it->second;
    };
    auto applyInt = [&](const char* key, int GameTypeInfo::*field) {
        auto it = def->stats.floats.find(key);
        if (it != def->stats.floats.end()) info.*field = (int)it->second;
    };
    auto applyBool = [&](const char* key, bool GameTypeInfo::*field) {
        auto it = def->stats.floats.find(key);
        if (it != def->stats.floats.end()) info.*field = it->second != 0.0f;
    };
    auto applyStrings = [&](const char* key, std::vector<std::string> GameTypeInfo::*field) {
        auto it = def->stats.strings.find(key);
        if (it != def->stats.strings.end()) info.*field = SplitCommaList(it->second);
    };

    applyInt("kill_score", &GameTypeInfo::killScore);
    applyInt("suicide_penalty", &GameTypeInfo::suicidePenalty);
    applyInt("flag_capture_score", &GameTypeInfo::flagCaptureScore);
    applyInt("objective_score", &GameTypeInfo::objectiveScore);
    applyInt("score_limit", &GameTypeInfo::scoreLimit);
    applyInt("team_count", &GameTypeInfo::teamCount);
    applyFloat("respawn_time", &GameTypeInfo::respawnTime);
    applyFloat("time_limit_minutes", &GameTypeInfo::timeLimitMinutes);
    applyBool("friendly_fire", &GameTypeInfo::friendlyFire);
    applyBool("respawn_enabled", &GameTypeInfo::respawnEnabled);
    applyBool("score_limit_enabled", &GameTypeInfo::scoreLimitEnabled);
    applyBool("time_limit_enabled", &GameTypeInfo::timeLimitEnabled);
    applyStrings("allowed_spawn_pawns", &GameTypeInfo::allowedSpawnPawns);
    applyStrings("allowed_weapons", &GameTypeInfo::allowedWeapons);

    return info;
}

} // namespace gametype
} // namespace oz
