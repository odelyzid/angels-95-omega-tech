// OzPawnSystem unit tests — data management logic, no rendering.
// Requires raylib for types (Vector3, BoundingBox).
// g++ -O0 -g --std=c++20 -I Source -DOMEGA_TEST_ENV \
//   tests/OzPawnSystem.test.cpp \
//   Source/Pawn/OzPawnSystem.cpp \
//   Source/Physics/OzBsp.cpp Source/Physics/WorldChunk.cpp Source/Log.cpp \
//   -o test_pawn_system -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 -lm

#include "../Source/Pawn/OzPawnSystem.hpp"
#include <cstdio>
#include <cassert>

static int tests_total = 0, tests_passed = 0;
#define TEST(name) do { tests_total++; fprintf(stdout, "  TEST: %s ... ", name);
#define PASS() do { tests_passed++; fprintf(stdout, "PASS\n"); } while(0)
#define FAIL(msg) do { fprintf(stdout, "FAIL: %s\n", msg); return 1; } while(0)
#define CHECK(cond) do { if (!(cond)) { fprintf(stdout, "FAIL: %s\n", #cond); return 1; } } while(0)
#define CHECK_EQ(a, b) do { if ((a) != (b)) { fprintf(stdout, "FAIL: expected %d, got %d\n", (int)(a), (int)(b)); return 1; } } while(0)
#define CHECK_APROX(a, b, eps) do { float diff = (a) - (b); if (diff < 0) diff = -diff; if (diff > (eps)) { fprintf(stdout, "FAIL: expected %f, got %f\n", (float)(b), (float)(a)); return 1; } } while(0)
#define END_TEST() } while(0)

static void reset_pawn_system() {
    auto& ps = PawnSystem::Instance();
    ps.DespawnAll();
    ps.ClearPlayerStarts();
    ps.ClearPickups();
    ps.ClearZones();
    ps.ClearEmitters();
    ps.ClearLights();
    ps.ClearSkyZones();
}

static int test_register_def() {
    TEST("RegisterDef and Spawn from template");
    reset_pawn_system();
    auto& ps = PawnSystem::Instance();
    PawnDef def;
    def.name = "TestBot";
    def.speed = 2.5f;
    def.aggroRange = 8.0f;
    def.damage = 15.0f;
    def.maxHealth = 50;
    ps.RegisterDef(def);
    int id = ps.Spawn({10, 0, 10}, "TestBot");
    CHECK(id >= 0);
    Pawn* p = ps.Get(id);
    CHECK(p != nullptr);
    CHECK(p->active);
    CHECK_APROX(p->position.x, 10.0f, 0.001f);
    CHECK_APROX(p->speed, 2.5f, 0.001f);
    CHECK_EQ(p->health, 50);
    PASS(); return 0; END_TEST();
}

static int test_spawn_unknown_def() {
    TEST("Spawn with unknown def name returns -1");
    reset_pawn_system();
    auto& ps = PawnSystem::Instance();
    int id = ps.Spawn({0, 0, 0}, "NonExistentPawnType");
    CHECK_EQ(id, -1);
    PASS(); return 0; END_TEST();
}

static int test_despawn() {
    TEST("Despawn marks pawn inactive and Get returns null");
    reset_pawn_system();
    auto& ps = PawnSystem::Instance();
    PawnDef def;
    def.name = "DespawnTest";
    def.maxHealth = 10;
    ps.RegisterDef(def);
    int id = ps.Spawn({0, 0, 0}, "DespawnTest");
    CHECK(id >= 0);
    CHECK(ps.Get(id) != nullptr);
    ps.Despawn(id);
    CHECK(ps.Get(id) == nullptr);
    PASS(); return 0; END_TEST();
}

static int test_despawn_all() {
    TEST("DespawnAll marks all pawns inactive");
    reset_pawn_system();
    auto& ps = PawnSystem::Instance();
    PawnDef def;
    def.name = "MultiSpawn";
    ps.RegisterDef(def);
    int id1 = ps.Spawn({0,0,0}, "MultiSpawn");
    int id2 = ps.Spawn({5,0,5}, "MultiSpawn");
    CHECK(id1 >= 0 && id2 >= 0);
    ps.DespawnAll();
    CHECK(ps.Get(id1) == nullptr);
    CHECK(ps.Get(id2) == nullptr);
    PASS(); return 0; END_TEST();
}

static int test_player_starts() {
    TEST("AddPlayerStart and GetFirstPlayerStart");
    auto& ps = PawnSystem::Instance();
    ps.ClearPlayerStarts();
    PlayerStartNode node1, node2;
    node1.position = {10, 0, 20};
    node1.yaw = 90.0f;
    node2.position = {30, 0, 40};
    node2.yaw = 180.0f;
    ps.AddPlayerStart(node1);
    ps.AddPlayerStart(node2);
    PlayerStartNode* first = ps.GetFirstPlayerStart();
    CHECK(first != nullptr);
    CHECK_APROX(first->position.x, 10.0f, 0.001f);
    CHECK_EQ(ps.GetPlayerStarts().size(), (size_t)2);
    PASS(); return 0; END_TEST();
}

static int test_pickup_crud() {
    TEST("AddPickup, GetPickup, RemovePickup");
    auto& ps = PawnSystem::Instance();
    ps.ClearPickups();
    PickupNode node;
    node.typeName = "HealthVial";
    node.position = {1, 2, 3};
    node.respawnTime = 30.0f;
    int id = ps.AddPickup(node);
    CHECK(id >= 0);
    PickupNode* found = ps.GetPickup(id);
    CHECK(found != nullptr);
    CHECK(found->typeName == "HealthVial");
    ps.RemovePickup(id);
    CHECK(ps.GetPickup(id) == nullptr);
    PASS(); return 0; END_TEST();
}

static int test_zone_crud() {
    TEST("AddZone, GetZone, RemoveZone");
    auto& ps = PawnSystem::Instance();
    ps.ClearZones();
    ZoneVolumeNode zone;
    zone.bounds = {{-5, -5, -5}, {5, 5, 5}};
    zone.zoneType = ZoneType::ZONE_WATER;
    zone.name = "test_water";
    int id = ps.AddZone(zone);
    CHECK(id >= 0);
    ZoneVolumeNode* found = ps.GetZone(id);
    CHECK(found != nullptr);
    CHECK(found->zoneType == ZoneType::ZONE_WATER);
    CHECK(found->name == "test_water");
    ps.RemoveZone(id);
    CHECK(ps.GetZone(id) == nullptr);
    PASS(); return 0; END_TEST();
}

static int test_skyzone_crud() {
    TEST("AddSkyZone, GetSkyZone, active state");
    reset_pawn_system();
    auto& ps = PawnSystem::Instance();
    SkyZoneNode sky;
    sky.id = 100;
    sky.position = {0, 10, 0};
    sky.bounds = {{-10, 8, -10}, {10, 12, 10}};
    sky.name = "test_sky";
    sky.fov = 75.0f;
    int idx = ps.AddSkyZone(sky);
    CHECK(idx >= 0);
    SkyZoneNode* found = ps.GetSkyZone(100);
    CHECK(found != nullptr);
    CHECK_APROX(found->fov, 75.0f, 0.001f);
    CHECK(found->name == "test_sky");
    CHECK(ps.GetSkyZones().size() >= 1);
    PASS(); return 0; END_TEST();
}

static int test_skyzone_active() {
    TEST("UpdateSkyZone detects player in bounds");
    auto& ps = PawnSystem::Instance();
    ps.ClearSkyZones();
    SkyZoneNode sky;
    sky.bounds = {{-10, -10, -10}, {10, 10, 10}};
    sky.name = "active_test";
    ps.AddSkyZone(sky);
    // Player inside
    BoundingBox playerBounds = {{-0.5f, -1, -0.5f}, {0.5f, 1, 0.5f}};
    ps.UpdateSkyZone({0, 0, 0}, playerBounds);
    CHECK(ps.IsInSkyZone());
    CHECK(ps.GetActiveSkyZone() != nullptr);
    CHECK(ps.GetActiveSkyZone()->name == "active_test");
    // Player outside
    ps.UpdateSkyZone({20, 0, 0}, playerBounds);
    CHECK(!ps.IsInSkyZone());
    PASS(); return 0; END_TEST();
}

static int test_emitter_crud() {
    TEST("AddEmitter and RemoveEmitter");
    auto& ps = PawnSystem::Instance();
    ps.ClearEmitters();
    EmitterNode e;
    e.position = {100, 0, 200};
    e.type = EmitterType::MUSIC;
    int id = ps.AddEmitter(e);
    CHECK(id >= 0);
    CHECK(ps.GetEmitters().size() >= 1);
    ps.RemoveEmitter(id);
    CHECK(ps.GetEmitters().size() == 0);
    PASS(); return 0; END_TEST();
}

static int test_light_crud() {
    TEST("AddLight and ClearLights");
    auto& ps = PawnSystem::Instance();
    ps.ClearLights();
    LightNode light;
    light.position = {0, 5, 0};
    light.radius = 10.0f;
    ps.AddLight(light);
    CHECK(ps.GetLights().size() >= 1);
    ps.ClearLights();
    CHECK(ps.GetLights().size() == 0);
    PASS(); return 0; END_TEST();
}

int main() {
    fprintf(stdout, "OzPawnSystem Tests\n");
    fprintf(stdout, "==================\n");

    int failures = 0;
    failures += test_register_def();
    failures += test_spawn_unknown_def();
    failures += test_despawn();
    failures += test_despawn_all();
    failures += test_player_starts();
    failures += test_pickup_crud();
    failures += test_zone_crud();
    failures += test_skyzone_crud();
    failures += test_skyzone_active();
    failures += test_emitter_crud();
    failures += test_light_crud();

    fprintf(stdout, "==================\n");
    fprintf(stdout, "%d/%d passed, %d failed\n",
            tests_passed, tests_total, tests_total - tests_passed);
    return failures;
}
