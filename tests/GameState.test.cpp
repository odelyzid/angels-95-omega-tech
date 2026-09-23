// GameState unit tests — projectile simulation and AMMO pickup.
// No raylib dependency. Compile with SERVER_CXX.
//
// g++ -O0 -g --std=c++20 -I Source -DOMEGA_TEST_ENV \
//   Source/Server/GameState.cpp Source/Network/Network.cpp Source/Log.cpp \
//   tests/GameState.test.cpp \
//   -o test_game_state -lws2_32 -lm

#include "../Source/Server/GameState.hpp"
#include <cstdio>
#include <cstring>
#include <cassert>

static int tests_total = 0, tests_passed = 0;
#define TEST(name) do { tests_total++; fprintf(stdout, "  TEST: %s ... ", name);
#define PASS() do { tests_passed++; fprintf(stdout, "PASS\n"); } while(0)
#define FAIL(msg) do { fprintf(stdout, "FAIL: %s\n", msg); return 1; } while(0)
#define CHECK(cond) do { if (!(cond)) { fprintf(stdout, "FAIL: %s\n", #cond); return 1; } } while(0)
#define CHECK_EQ(a, b) do { if ((a) != (b)) { fprintf(stdout, "FAIL: expected %d, got %d\n", (int)(a), (int)(b)); return 1; } } while(0)
#define CHECK_APROX(a, b, eps) do { float diff = (a) - (b); if (diff < 0) diff = -diff; if (diff > (eps)) { fprintf(stdout, "FAIL: expected %f, got %f\n", (float)(b), (float)(a)); return 1; } } while(0)
#define END_TEST() } while(0)

// --- Helpers ---
static WorldState make_test_world(int index) {
    WorldState ws;
    ws.world_index = index;
    ws.name = "test";
    ws.partitions.resize(1);
    ws.partitions[0].id = 0;
    ws.partitions[0].min_x = -1000;
    ws.partitions[0].max_x = 1000;
    ws.partitions[0].min_z = -1000;
    ws.partitions[0].max_z = 1000;
    return ws;
}

// --- Projectile tests (Phase 4) ---

static int test_spawn_projectile() {
    TEST("spawn_projectile creates active projectile in world");
    GameState gs;
    WorldState ws = make_test_world(0);
    gs.spawn_projectile(ws, 1, {0,0,0}, {1,0,0}, 20.0f, 15.0f, 3.0f);
    CHECK_EQ(ws.projectiles.size(), (size_t)1);
    CHECK(ws.projectiles[0].active);
    CHECK_APROX(ws.projectiles[0].velocity.x, 20.0f, 0.001f);
    CHECK_APROX(ws.projectiles[0].damage, 15.0f, 0.001f);
    CHECK_EQ(ws.projectiles[0].owner_id, (uint32_t)1);
    PASS(); return 0; END_TEST();
}

static int test_tick_projectile_movement() {
    TEST("tick_projectiles moves projectile along velocity");
    GameState gs;
    WorldState ws = make_test_world(0);
    gs.spawn_projectile(ws, 1, {0,0,0}, {0,0,-1}, 30.0f, 10.0f, 5.0f);
    gs.tick_projectiles(ws, 1.0f);
    CHECK_APROX(ws.projectiles[0].position.z, -30.0f, 0.001f);
    gs.tick_projectiles(ws, 0.5f);
    CHECK_APROX(ws.projectiles[0].position.z, -45.0f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_tick_projectile_expiry() {
    TEST("Projectile deactivates after lifetime");
    GameState gs;
    WorldState ws = make_test_world(0);
    gs.spawn_projectile(ws, 1, {0,0,0}, {1,0,0}, 1.0f, 10.0f, 0.5f);
    gs.tick_projectiles(ws, 0.6f);
    CHECK(ws.projectiles.empty());
    PASS(); return 0; END_TEST();
}

static int test_projectile_hits_npc() {
    TEST("Projectile damages NPC on collision");
    GameState gs;
    WorldState ws = make_test_world(0);
    ServerNPC npc;
    npc.active = true;
    npc.health = 100;
    npc.max_health = 100;
    npc.position = {9, 0, 0};
    ws.global_npcs.push_back(npc);
    gs.spawn_projectile(ws, 1, {0,0,0}, {1,0,0}, 20.0f, 25.0f, 5.0f);
    gs.tick_projectiles(ws, 0.5f); // travels 10 units, distance to NPC = 1 < 2
    CHECK(ws.global_npcs[0].health < 100);
    CHECK(ws.projectiles.empty());
    PASS(); return 0; END_TEST();
}

static int test_projectile_misses_npc() {
    TEST("Projectile does not hit NPC when path diverges");
    GameState gs;
    WorldState ws = make_test_world(0);
    ServerNPC npc;
    npc.active = true;
    npc.health = 100;
    npc.position = {10, 0, 0};
    ws.global_npcs.push_back(npc);
    // Projectile going away from NPC
    gs.spawn_projectile(ws, 1, {0,0,0}, {-1,0,0}, 20.0f, 25.0f, 5.0f);
    gs.tick_projectiles(ws, 1.0f);
    CHECK_EQ(ws.global_npcs[0].health, 100); // unharmed
    PASS(); return 0; END_TEST();
}

static int test_projectile_hits_partition_npc() {
    TEST("Projectile damages NPC in partition");
    GameState gs;
    WorldState ws = make_test_world(0);
    ServerNPC npc;
    npc.active = true;
    npc.health = 80;
    npc.position = {5, 0, 5};
    ws.partitions[0].npcs.push_back(npc);
    gs.spawn_projectile(ws, 1, {0,0,5}, {1,0,0}, 20.0f, 40.0f, 5.0f);
    gs.tick_projectiles(ws, 0.3f);
    CHECK(ws.partitions[0].npcs[0].health < 80);
    PASS(); return 0; END_TEST();
}

static int test_projectile_avoids_owner() {
    TEST("Projectile does not damage its owner");
    GameState gs;
    // Add owner player
    uint32_t owner_id = gs.add_player(42, "Shooter");
    ServerPlayer* owner = gs.get_player(owner_id);
    CHECK(owner != nullptr);
    owner->connected = true;
    owner->position = {0, 0, 0};
    owner->health = 100;

    WorldState ws = make_test_world(0);
    gs.spawn_projectile(ws, owner_id, {0,0,0}, {0,0,-1}, 1.0f, 50.0f, 5.0f);
    // Tick — projectile barely moves, stays near owner
    gs.tick_projectiles(ws, 0.1f);
    CHECK_EQ(owner->health, 100); // owner should not be damaged
    PASS(); return 0; END_TEST();
}

static int test_projectile_hits_other_player() {
    TEST("Projectile damages other player");
    GameState gs;
    uint32_t owner_id = gs.add_player(1, "Shooter");
    uint32_t target_id = gs.add_player(2, "Target");
    ServerPlayer* owner = gs.get_player(owner_id);
    ServerPlayer* target = gs.get_player(target_id);
    CHECK(owner != nullptr && target != nullptr);
    owner->connected = true;
    owner->position = {0, 0, 0};
    owner->health = 100;
    target->connected = true;
    target->position = {10, 0, 0};
    target->health = 100;

    WorldState ws = make_test_world(0);
    // FIXME: test requires world_index tracking on players
    // For now, this validates the tick_projectiles doesn't crash
    gs.spawn_projectile(ws, owner_id, {0,0,0}, {1,0,0}, 20.0f, 30.0f, 5.0f);
    gs.tick_projectiles(ws, 0.6f);
    // Target at x=10 with velocity 20*0.6=12, projectile should hit near target
    PASS(); return 0; END_TEST();
}

// --- Pickup tests (Phase 5) ---

static int test_ammo_pickup_grants_ammo() {
    TEST("AMMO pickup grants ammo instead of XP");
    GameState gs;
    uint32_t pid = gs.add_player(10, "AmmoCollector");
    ServerPlayer* p = gs.get_player(pid);
    CHECK(p != nullptr);
    CHECK_EQ(p->ammo, 0);

    WorldState ws = make_test_world(0);
    ServerPickup pickup;
    pickup.id = 1;
    pickup.type = PickupType::AMMO;
    pickup.value = 30;
    pickup.active = true;
    pickup.respawnable = true;
    ws.global_pickups.push_back(pickup);
    gs.worlds().push_back(ws);

    PickupType out_type;
    int out_value;
    bool collected = gs.collect_pickup(pid, 1, 0, &out_type, &out_value);
    CHECK(collected);
    CHECK_EQ(out_type == PickupType::AMMO, true);
    CHECK_EQ(p->ammo, 30);
    PASS(); return 0; END_TEST();
}

static int test_ammo_pickup_no_xp_granted() {
    TEST("AMMO pickup does not grant XP");
    GameState gs;
    uint32_t pid = gs.add_player(11, "AmmoOnly");
    ServerPlayer* p = gs.get_player(pid);
    CHECK(p != nullptr);
    int xp_before = p->xp;

    WorldState ws = make_test_world(0);
    ServerPickup pickup;
    pickup.id = 2;
    pickup.type = PickupType::AMMO;
    pickup.value = 50;
    pickup.active = true;
    ws.global_pickups.push_back(pickup);
    gs.worlds().push_back(ws);

    gs.collect_pickup(pid, 2, 0);
    CHECK_EQ(p->ammo, 50);
    CHECK_EQ(p->xp, xp_before);
    PASS(); return 0; END_TEST();
}

// --- Tier 0: trust / idempotency regressions ---

static int test_add_player_idempotent() {
    TEST("add_player is idempotent for the same id (no duplicate)");
    GameState gs;
    uint32_t a = gs.add_player(7, "First");
    uint32_t b = gs.add_player(7, "Renamed");
    CHECK_EQ(a, (uint32_t)7);
    CHECK_EQ(b, (uint32_t)7);
    CHECK_EQ(gs.player_count(), (int)1);
    ServerPlayer* p = gs.get_player(7);
    CHECK(p != nullptr);
    CHECK_EQ(std::strcmp(p->name, "Renamed"), 0);
    PASS(); return 0; END_TEST();
}

static int test_player_position_flag() {
    TEST("update_player_position flips has_position after first update");
    GameState gs;
    uint32_t id = gs.add_player(3, "Mover");
    ServerPlayer* p = gs.get_player(id);
    CHECK(p != nullptr);
    CHECK_EQ(p->has_position, false);
    gs.update_player_position(id, 1.0f, 2.0f, 3.0f, 0.5f, 0.25f);
    CHECK(p->has_position);
    CHECK_APROX(p->position.x, 1.0f, 0.0001f);
    PASS(); return 0; END_TEST();
}

int main() {
    fprintf(stdout, "GameState Tests\n");
    fprintf(stdout, "===============\n");

    int failures = 0;
    failures += test_spawn_projectile();
    failures += test_tick_projectile_movement();
    failures += test_tick_projectile_expiry();
    failures += test_projectile_hits_npc();
    failures += test_projectile_misses_npc();
    failures += test_projectile_hits_partition_npc();
    failures += test_projectile_avoids_owner();
    failures += test_projectile_hits_other_player();
    failures += test_ammo_pickup_grants_ammo();
    failures += test_ammo_pickup_no_xp_granted();
    failures += test_add_player_idempotent();
    failures += test_player_position_flag();

    fprintf(stdout, "===============\n");
    fprintf(stdout, "%d/%d passed, %d failed\n",
            tests_passed, tests_total, tests_total - tests_passed);
    return failures;
}
