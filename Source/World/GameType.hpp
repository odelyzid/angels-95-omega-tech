#pragma once
// ---------------------------------------------------------------------------
// World/GameType.hpp — runtime game-mode taxonomy + ruleset data
//
// Extracted from the editor's GameType enum and the levelinfo positional int
// so that world/level code (the OZONE parser/loader, the server rules engine,
// the editor and the headless test harness) can reason about game modes
// without pulling in the entity system or raylib.
//
// Nothing in this header depends on raylib or on gameplay code: it is plain
// data so it can be included from tools and tests.
//
// Numeric IDs 0-6 are the legacy editor enum order and MUST NOT be renumbered:
// every shipped World.ozone stores `levelinfo <id> ...` and a renumber would
// silently re-mode every existing world. New modes are appended at 7+.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

// FORWARD DECLARATION, not an include. ResolveGameTypeInfo() below only takes
// LevelSettings by const reference, and LevelSettings.hpp itself calls into
// oz::gametype from ParseLevelInfo() - so including it here is a circular
// dependency that leaves oz::gametype undeclared when LevelSettings.hpp is the
// header reached first.
struct LevelSettings;

namespace oz {
namespace gametype {

// Game mode identifiers. Values 0-6 are the legacy editor enum order and are
// the on-disk contract; 7+ are new canonical names.
enum class GameType : uint8_t {
    SINGLEPLAYER    = 0,   // legacy id - every shipped world
    COOP            = 1,
    ETHERAL_MATCH   = 2,   // == Deathmatch (free-for-all)
    ANGEL_TEAM_GAME = 3,   // == TDM
    ANGEL_RUN       = 4,
    CAPTURE_THE_ORB = 5,   // == CTF / object
    TIME_SHIFT      = 6,
    DEATHMATCH      = 7,
    CTF             = 8,
    CAMPAIGN        = 9,
    COOPERATIVE     = 10,
};

// Source tag for damage attribution. Numeric so a future LavaZone/AcidZone/
// fire/fall call site can name its own source without an enum churn at every
// call site. WORLD is the default for damage_player/damage_npc.
enum class DamageSource : uint8_t {
    WORLD    = 0,   // environmental / fall / lava / acid / fire (future)
    NPC      = 1,
    PLAYER   = 2,   // projectile PvP (friendlyFire path)
    SCRIPT   = 3,   // .ozls `damage` opcode (forward-compatible)
};

// Per-mode ruleset descriptor. The built-in table is the default; a .ozls
// EntityType::GAMETYPE def may patch individual fields (see
// GameTypeInfoFromOverride), and the world's own levelinfo numbers override
// last because those are what the designer authored in the editor.
struct GameTypeInfo {
    GameType id = GameType::SINGLEPLAYER;
    const char* key = "singleplayer";     // stable machine name (OZONE kwarg, HTTP JSON)
    const char* label = "Single Player";  // UI text
    bool  teamBased = false;
    int   teamCount = 1;                  // authoritative ceiling for AssignTeam
    bool  friendlyFire = false;
    bool  respawnEnabled = true;
    float respawnTime = 5.0f;

    // Scoring. Only killScore is enforced in phase 1; the rest are declared so
    // a mode can be expressed now and enforced later without a second registry
    // edit.
    int   killScore = 1;
    int   suicidePenalty = 0;
    int   flagCaptureScore = 1;
    int   objectiveScore = 1;

    // Win condition
    bool  scoreLimitEnabled = true;
    int   scoreLimit = 50;
    bool  timeLimitEnabled = false;
    float timeLimitMinutes = 10.0f;

    // Pawn & rule binding - resolved against GameData/PawnDefs/*.cfg names and
    // .ozls weapon def names. EMPTY = "everything the world defines is allowed".
    std::vector<std::string> allowedSpawnPawns;
    std::vector<std::string> allowedWeapons;
};

// Per-match runtime state. Owned by GameState; advanced once per tick.
struct MatchState {
    GameType type = GameType::SINGLEPLAYER;
    int      teamScores[8] = {};   // indexed by SERVER-assigned team
    float    elapsedSeconds = 0.0f;

    enum class WinReason : uint8_t { NONE, SCORE_LIMIT, TIME_LIMIT };
    WinReason winner = WinReason::NONE;
    int       winningTeam = -1;   // -1 = draw / none
};

// ---------------------------------------------------------------------------
// Lookup + name table (mirrors ZoneTypeFromString / ZoneTypeName)
// ---------------------------------------------------------------------------
const GameTypeInfo& GameTypeInfoFor(GameType t);
GameType            GameTypeFromId(int raw);          // clamp, never throw
GameType            GameTypeFromName(const std::string&);
const char*         GameTypeName(GameType);           // "Single Player"
const char*         GameTypeKey(GameType);            // "singleplayer"
bool                IsGameTypeKey(const std::string&);
const std::vector<GameType>& AllGameTypes();

// ---------------------------------------------------------------------------
// Match rules
// ---------------------------------------------------------------------------

// Award a scoring event to `team`. Returns true when this event ENDED the
// match (score limit reached or, via CheckWinConditions, time limit).
bool ApplyScoreEvent(const GameTypeInfo& info, MatchState& st,
                     int team, int delta, MatchState::WinReason* out_reason);

// Evaluate win conditions without awarding anything. Split from
// ApplyScoreEvent so the per-tick clock check is a pure read.
struct WinCheck { MatchState::WinReason reason; int winningTeam; };
WinCheck CheckWinConditions(const GameTypeInfo& info, const MatchState& st);

// Server-owned team assignment. `requestedTeam` is a HINT ONLY: honoured only
// when non-zero, in range [0, min(MAX_TEAMS, info.teamCount)], and would not
// unbalance the roster by more than one player. Out-of-range becomes "no
// preference" rather than clamping onto a real team, so a hostile value
// cannot steer team assignment by sitting near a boundary.
int AssignTeam(const GameTypeInfo& info, const std::vector<int>& teamSizes,
               int requestedTeam);

// ---------------------------------------------------------------------------
// Layered resolution. Priority, lowest -> highest:
//   1. Built-in table for `t`
//   2. .ozls override if `overrideDef` is non-empty and resolvable
//   3. The world's own LevelSettings numeric overrides (levelinfo)
// ---------------------------------------------------------------------------
GameTypeInfo ResolveGameTypeInfo(GameType t, const LevelSettings& s,
                                 const std::string& overrideDef);

// Patch a built-in GameTypeInfo from an EntityType::GAMETYPE .ozls def.
// Returns the built-in unchanged when the def is missing or is not a
// gametype def, so an authoring typo cannot break the server.
GameTypeInfo GameTypeInfoFromOverride(const std::string& defName);

} // namespace gametype
} // namespace oz
