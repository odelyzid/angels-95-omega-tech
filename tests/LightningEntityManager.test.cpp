// LightningEntityManager unit test — lifecycle and hotbar operations.
// Requires raylib for resource loading. Compile with client compiler.
//
// g++ -O0 -g --std=c++20 -I Source \
//   Source/Script/LightningEntityManager.cpp \
//   Source/Script/LightningEntityRegistry.cpp \
//   Source/Script/LightningScriptContext.cpp \
//   Source/Script/LightningScriptParser.cpp \
//   Source/Log.cpp \
//   tests/LightningEntityManager.test.cpp \
//   -o test_entity_manager -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 -lm

#include "../Source/Script/LightningEntityManager.hpp"
#include "../Source/Script/LightningEntityRegistry.hpp"
#include "../Source/Script/LightningScriptParser.hpp"
#include <cstdio>
#include <cassert>

static int tests_total = 0, tests_passed = 0;
#define TEST(name) do { tests_total++; fprintf(stdout, "  TEST: %s ... ", name);
#define PASS() do { tests_passed++; fprintf(stdout, "PASS\n"); } while(0)
#define FAIL(msg) do { fprintf(stdout, "FAIL: %s\n", msg); return 1; } while(0)
#define CHECK(cond) do { if (!(cond)) { fprintf(stdout, "FAIL: %s\n", #cond); return 1; } } while(0)
#define CHECK_EQ(a, b) do { if ((a) != (b)) { fprintf(stdout, "FAIL: expected %d, got %d\n", (int)(a), (int)(b)); return 1; } } while(0)
#define END_TEST() } while(0)

static int test_init() {
    TEST("Init resets state");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    CHECK_EQ(em.Count(), 0);
    CHECK_EQ(em.SelectedSlot(), 0);
    PASS(); return 0; END_TEST();
}

static int test_hotbar() {
    TEST("Hotbar assign and select");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    em.HotbarAssign(0, 5);
    CHECK_EQ(em.HotbarAt(0), 5);
    em.HotbarSwap(0, 1);
    CHECK_EQ(em.HotbarAt(0), -1);
    CHECK_EQ(em.HotbarAt(1), 5);
    em.SelectSlot(3);
    CHECK_EQ(em.SelectedSlot(), 3);
    PASS(); return 0; END_TEST();
}

static int test_spawn_unknown() {
    TEST("Spawn unknown def returns -1");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    int idx = em.Spawn("NonExistentDef");
    CHECK_EQ(idx, -1);
    CHECK_EQ(em.Count(), 0);
    PASS(); return 0; END_TEST();
}

int main() {
    fprintf(stdout, "LightningEntityManager Tests\n");
    fprintf(stdout, "============================\n");

    int failures = 0;
    failures += test_init();
    failures += test_hotbar();
    failures += test_spawn_unknown();

    fprintf(stdout, "============================\n");
    fprintf(stdout, "%d/%d passed, %d failed\n",
            tests_passed, tests_total, tests_total - tests_passed);
    return failures;
}
