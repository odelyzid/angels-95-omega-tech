// GameType unit tests — game-mode taxonomy, ruleset data, match rules.
// No raylib dependency. Compile with SERVER_CXX.
//
// g++ -O0 -g --std=c++20 -I Source -DOMEGA_TEST_ENV \
//   Source/World/GameType.cpp \
//   Source/Script/LightningEntityRegistry.cpp \
//   Source/Script/LightningScriptParser.cpp \
//   Source/Script/LightningScriptContext.cpp \
//   Source/Log.cpp \
//   tests/GameType.test.cpp \
//   -o test_gametype -lm

#include "../Source/World/GameType.hpp"
// GameType.hpp only FORWARD-declARES LevelSettings (LevelSettings.hpp itself
// calls back into oz::gametype), so a test that builds a LevelSettings needs
// the full definition here.
#include "../Source/World/LevelSettings.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int tests_total = 0, tests_passed = 0;
#define TEST(name) do { tests_total++; fprintf(stdout, "  TEST: %s ... ", name);
#define PASS() do { tests_passed++; fprintf(stdout, "PASS\n"); } while(0)
#define FAIL(msg) do { fprintf(stdout, "FAIL: %s\n", msg); return 1; } while(0)
#define CHECK(cond) do { if (!(cond)) { fprintf(stdout, "FAIL: %s\n", #cond); return 1; } } while(0)
#define CHECK_EQ(a, b) do { if ((a) != (b)) { fprintf(stdout, "FAIL: expected %d, got %d\n", (int)(a), (int)(b)); return 1; } } while(0)
#define CHECK_ENUM_EQ(a, b) do { if ((int)(a) != (int)(b)) { fprintf(stdout, "FAIL: expected %d, got %d\n", (int)(a), (int)(b)); return 1; } } while(0)
#define CHECK_APROX(a, b, eps) do { float diff = (float)(a) - (float)(b); if (diff < 0) diff = -diff; if (diff > (eps)) { fprintf(stdout, "FAIL: expected %f, got %f\n", (float)(b), (float)(a)); return 1; } } while(0)
#define END_TEST() } while(0)

// ---------------------------------------------------------------------------
// Literal-id contract: ids 0-6 are the on-disk format. A renumber breaks
// every shipped World.ozone.
// ---------------------------------------------------------------------------
static int test_literal_ids() {
    TEST("literal legacy ids 0-6 preserved");
    using namespace oz::gametype;
    CHECK_EQ((int)GameType::SINGLEPLAYER,    0);
    CHECK_EQ((int)GameType::COOP,            1);
    CHECK_EQ((int)GameType::ETHERAL_MATCH,   2);
    CHECK_EQ((int)GameType::ANGEL_TEAM_GAME, 3);
    CHECK_EQ((int)GameType::ANGEL_RUN,       4);
    CHECK_EQ((int)GameType::CAPTURE_THE_ORB, 5);
    CHECK_EQ((int)GameType::TIME_SHIFT,      6);
    PASS(); return 0; END_TEST();
}

// ---------------------------------------------------------------------------
// Name round-trip
// ---------------------------------------------------------------------------
static int test_name_roundtrip() {
    TEST("GameTypeFromName/GameTypeName round-trip");
    using namespace oz::gametype;
    for (GameType t : AllGameTypes()) {
        const char* key = GameTypeKey(t);
        GameType back = GameTypeFromName(std::string(key));
        CHECK(back == t);
    }
    PASS(); return 0; END_TEST();
}

static int test_unknown_name_falls_back() {
    TEST("unknown name falls back to SINGLEPLAYER");
    using namespace oz::gametype;
    CHECK(GameTypeFromName("nonexistent_mode") == GameType::SINGLEPLAYER);
    CHECK(GameTypeFromName("") == GameType::SINGLEPLAYER);
    PASS(); return 0; END_TEST();
}

static int test_is_game_type_key() {
    TEST("IsGameTypeKey recognises known keys only");
    using namespace oz::gametype;
    CHECK(IsGameTypeKey("deathmatch"));
    CHECK(IsGameTypeKey("singleplayer"));
    CHECK(!IsGameTypeKey("not_a_mode"));
    CHECK(!IsGameTypeKey(""));
    PASS(); return 0; END_TEST();
}

// ---------------------------------------------------------------------------
// Invariants
// ---------------------------------------------------------------------------
static int test_invariants() {
    TEST("teamBased implies teamCount>=2; !teamBased implies teamCount==1");
    using namespace oz::gametype;
    for (GameType t : AllGameTypes()) {
        const GameTypeInfo& info = GameTypeInfoFor(t);
        if (info.teamBased) {
            CHECK(info.teamCount >= 2);
        } else {
            CHECK_EQ(info.teamCount, 1);
        }
    }
    PASS(); return 0; END_TEST();
}

static int test_score_limit_positive_when_enabled() {
    TEST("scoreLimit > 0 for score-limited modes");
    using namespace oz::gametype;
    for (GameType t : AllGameTypes()) {
        const GameTypeInfo& info = GameTypeInfoFor(t);
        if (info.scoreLimitEnabled) {
            CHECK(info.scoreLimit > 0);
        }
    }
    PASS(); return 0; END_TEST();
}

// ---------------------------------------------------------------------------
// ApplyScoreEvent
// ---------------------------------------------------------------------------
static int test_apply_score_event_increments() {
    TEST("ApplyScoreEvent increments the right team");
    using namespace oz::gametype;
    const GameTypeInfo& info = GameTypeInfoFor(GameType::DEATHMATCH);
    MatchState st;
    st.type = GameType::DEATHMATCH;
    MatchState::WinReason r = MatchState::WinReason::NONE;
    bool ended = ApplyScoreEvent(info, st, 0, 5, &r);
    CHECK(!ended);
    CHECK_EQ(st.teamScores[0], 5);
    CHECK_ENUM_EQ(r, MatchState::WinReason::NONE);
    PASS(); return 0; END_TEST();
}

static int test_apply_score_event_reaches_limit() {
    TEST("ApplyScoreEvent returns true at score limit");
    using namespace oz::gametype;
    const GameTypeInfo& info = GameTypeInfoFor(GameType::DEATHMATCH);
    MatchState st;
    st.type = GameType::DEATHMATCH;
    MatchState::WinReason r = MatchState::WinReason::NONE;
    ApplyScoreEvent(info, st, 0, 24, &r);
    bool ended = ApplyScoreEvent(info, st, 0, 1, &r);
    CHECK(ended);
    CHECK_ENUM_EQ(r, MatchState::WinReason::SCORE_LIMIT);
    CHECK_EQ(st.winningTeam, 0);
    PASS(); return 0; END_TEST();
}

// ---------------------------------------------------------------------------
// CheckWinConditions (time limit)
// ---------------------------------------------------------------------------
static int test_time_limit_ends_match() {
    TEST("CheckWinConditions fires TIME_LIMIT past the clock");
    using namespace oz::gametype;
    const GameTypeInfo& info = GameTypeInfoFor(GameType::ANGEL_RUN);
    MatchState st;
    st.type = GameType::ANGEL_RUN;
    st.teamScores[0] = 5;
    st.elapsedSeconds = info.timeLimitMinutes * 60.0f + 1.0f;
    WinCheck wc = CheckWinConditions(info, st);
    CHECK_ENUM_EQ(wc.reason, MatchState::WinReason::TIME_LIMIT);
    CHECK_EQ(wc.winningTeam, 0); // only team 0 has score
    PASS(); return 0; END_TEST();
}

static int test_time_limit_picks_highest_team() {
    TEST("TIME_LIMIT winner is the highest-scoring team");
    using namespace oz::gametype;
    const GameTypeInfo& info = GameTypeInfoFor(GameType::ANGEL_RUN);
    MatchState st;
    st.type = GameType::ANGEL_RUN;
    st.teamScores[0] = 3;
    st.teamScores[1] = 7;
    st.elapsedSeconds = info.timeLimitMinutes * 60.0f + 1.0f;
    WinCheck wc = CheckWinConditions(info, st);
    CHECK_ENUM_EQ(wc.reason, MatchState::WinReason::TIME_LIMIT);
    CHECK_EQ(wc.winningTeam, 1);
    PASS(); return 0; END_TEST();
}

static int test_time_limit_tie_is_draw() {
    TEST("TIME_LIMIT tie yields winningTeam == -1");
    using namespace oz::gametype;
    const GameTypeInfo& info = GameTypeInfoFor(GameType::ANGEL_RUN);
    MatchState st;
    st.type = GameType::ANGEL_RUN;
    st.teamScores[0] = 5;
    st.teamScores[1] = 5;
    st.elapsedSeconds = info.timeLimitMinutes * 60.0f + 1.0f;
    WinCheck wc = CheckWinConditions(info, st);
    CHECK_ENUM_EQ(wc.reason, MatchState::WinReason::TIME_LIMIT);
    CHECK_EQ(wc.winningTeam, -1);
    PASS(); return 0; END_TEST();
}

// ---------------------------------------------------------------------------
// AssignTeam
// ---------------------------------------------------------------------------
static int test_assign_team_balances() {
    TEST("AssignTeam picks the smaller team");
    using namespace oz::gametype;
    const GameTypeInfo& info = GameTypeInfoFor(GameType::ANGEL_TEAM_GAME);
    std::vector<int> sizes = {3, 1};
    CHECK_EQ(AssignTeam(info, sizes, 0), 1);
    PASS(); return 0; END_TEST();
}

static int test_assign_team_honours_valid_request() {
    TEST("AssignTeam honours a valid non-zero request");
    using namespace oz::gametype;
    const GameTypeInfo& info = GameTypeInfoFor(GameType::ANGEL_TEAM_GAME);
    std::vector<int> sizes = {2, 2};
    CHECK_EQ(AssignTeam(info, sizes, 1), 1);
    PASS(); return 0; END_TEST();
}

static int test_assign_team_refuses_out_of_range() {
    TEST("AssignTeam refuses out-of-range request (never clamps onto a real team)");
    using namespace oz::gametype;
    const GameTypeInfo& info = GameTypeInfoFor(GameType::ANGEL_TEAM_GAME);
    std::vector<int> sizes = {2, 2};
    // 99 is out of range; result must be the smaller team (0), not clamped to 1.
    CHECK_EQ(AssignTeam(info, sizes, 99), 0);
    PASS(); return 0; END_TEST();
}

static int test_assign_team_zero_request_uses_balance() {
    TEST("AssignTeam with request 0 falls back to balance");
    using namespace oz::gametype;
    const GameTypeInfo& info = GameTypeInfoFor(GameType::ANGEL_TEAM_GAME);
    std::vector<int> sizes = {4, 2};
    CHECK_EQ(AssignTeam(info, sizes, 0), 1);
    PASS(); return 0; END_TEST();
}

static int test_assign_team_respects_team_count_cap() {
    TEST("AssignTeam never returns a team >= info.teamCount");
    using namespace oz::gametype;
    const GameTypeInfo& info = GameTypeInfoFor(GameType::SINGLEPLAYER); // teamCount == 1
    std::vector<int> sizes = {5, 3, 1};
    int team = AssignTeam(info, sizes, 2);
    CHECK(team >= 0 && team < 1);
    PASS(); return 0; END_TEST();
}

static int test_assign_team_idempotent() {
    TEST("AssignTeam is idempotent for identical inputs");
    using namespace oz::gametype;
    const GameTypeInfo& info = GameTypeInfoFor(GameType::ANGEL_TEAM_GAME);
    std::vector<int> sizes = {3, 3, 1};
    int a = AssignTeam(info, sizes, 0);
    int b = AssignTeam(info, sizes, 0);
    CHECK_EQ(a, b);
    PASS(); return 0; END_TEST();
}

// ---------------------------------------------------------------------------
// ResolveGameTypeInfo layering
// ---------------------------------------------------------------------------
static int test_resolve_applies_levelinfo_overrides() {
    TEST("ResolveGameTypeInfo applies levelinfo numeric overrides last");
    using namespace oz::gametype;
    LevelSettings s;
    s.gameType = 2; // ETHERAL_MATCH
    s.scoreLimit = 99;
    s.respawnTime = 2.5f;
    s.timeLimitEnabled = true;
    s.timeLimitMinutes = 7.0f;
    s.friendlyFire = false;
    GameTypeInfo info = ResolveGameTypeInfo(GameType::ETHERAL_MATCH, s, "");
    CHECK_EQ(info.scoreLimit, 99);
    CHECK_APROX(info.respawnTime, 2.5f, 0.001f);
    CHECK(info.timeLimitEnabled);
    CHECK_APROX(info.timeLimitMinutes, 7.0f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_resolve_builtin_defaults() {
    TEST("ResolveGameTypeInfo returns built-in defaults with empty override");
    using namespace oz::gametype;
    LevelSettings s;
    s.gameType = 7; // DEATHMATCH
    s.scoreLimit = 0;          // 0 = "not authored", built-in wins
    s.respawnTime = 0.0f;
    s.timeLimitEnabled = false;
    s.friendlyFire = false;
    GameTypeInfo info = ResolveGameTypeInfo(GameType::DEATHMATCH, s, "");
    CHECK_ENUM_EQ(info.id, GameType::DEATHMATCH);
    CHECK_EQ(info.scoreLimit, 25);
    CHECK(info.friendlyFire);
    PASS(); return 0; END_TEST();
}

// ---------------------------------------------------------------------------
// GameTypeInfoFromOverride
// ---------------------------------------------------------------------------
static int test_override_missing_def_returns_builtin() {
    TEST("GameTypeInfoFromOverride returns built-in for missing def");
    using namespace oz::gametype;
    GameTypeInfo info = GameTypeInfoFromOverride("no_such_def");
    CHECK_ENUM_EQ(info.id, GameType::SINGLEPLAYER);
    PASS(); return 0; END_TEST();
}

// ---------------------------------------------------------------------------
// Shipped-world preservation: every World.ozone must resolve to a known mode.
// ---------------------------------------------------------------------------
static int test_shipped_worlds_resolve() {
    TEST("every shipped World.ozone levelinfo resolves to a known GameType");
    using namespace oz::gametype;
    const char* worlds[] = {
        "CitadelRuins", "Dessert_Dreams", "Dust_Ravine", "EngineTest",
        "TestMap", "World_endless_snow",
    };
    for (const char* w : worlds) {
        char path[512];
        snprintf(path, sizeof(path), "GameData/Worlds/%s/World.ozone", w);
        FILE* f = fopen(path, "r");
        if (!f) { fprintf(stdout, "FAIL: cannot open %s\n", path); return 1; }
        char line[1024];
        bool found = false;
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "levelinfo", 9) == 0) {
                int id = 0;
                sscanf(line, "levelinfo %d", &id);
                GameType t = GameTypeFromId(id);
                const GameTypeInfo& info = GameTypeInfoFor(t);
                CHECK_ENUM_EQ(info.id, t);
                found = true;
                break;
            }
        }
        fclose(f);
        CHECK(found);
    }
    PASS(); return 0; END_TEST();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    fprintf(stdout, "=== GameType Tests ===\n");

    int failures = 0;
    failures += test_literal_ids();
    failures += test_name_roundtrip();
    failures += test_unknown_name_falls_back();
    failures += test_is_game_type_key();
    failures += test_invariants();
    failures += test_score_limit_positive_when_enabled();
    failures += test_apply_score_event_increments();
    failures += test_apply_score_event_reaches_limit();
    failures += test_time_limit_ends_match();
    failures += test_time_limit_picks_highest_team();
    failures += test_time_limit_tie_is_draw();
    failures += test_assign_team_balances();
    failures += test_assign_team_honours_valid_request();
    failures += test_assign_team_refuses_out_of_range();
    failures += test_assign_team_zero_request_uses_balance();
    failures += test_assign_team_respects_team_count_cap();
    failures += test_assign_team_idempotent();
    failures += test_resolve_applies_levelinfo_overrides();
    failures += test_resolve_builtin_defaults();
    failures += test_override_missing_def_returns_builtin();
    failures += test_shipped_worlds_resolve();

    fprintf(stdout, "===============\n");
    fprintf(stdout, "%d/%d passed, %d failed\n",
            tests_passed, tests_total, tests_total - tests_passed);
    return failures;
}
