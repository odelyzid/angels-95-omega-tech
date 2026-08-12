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

static int test_hotbar_out_of_range() {
    TEST("HotbarAssign with invalid slot is ignored");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    em.HotbarAssign(-1, 99);
    em.HotbarAssign(8, 99);
    // All hotbar slots should still be -1 since init
    for (int s = 0; s < 8; s++) CHECK_EQ(em.HotbarAt(s), -1);
    PASS(); return 0; END_TEST();
}

static int test_select_slot_out_of_range() {
    TEST("SelectSlot with invalid slot is ignored");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    em.SelectSlot(-1);
    CHECK_EQ(em.SelectedSlot(), 0);
    em.SelectSlot(99);
    CHECK_EQ(em.SelectedSlot(), 0);
    PASS(); return 0; END_TEST();
}

static int test_selected_entity_empty() {
    TEST("SelectedEntity returns null when hotbar empty");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    CHECK(em.SelectedEntity() == nullptr);
    PASS(); return 0; END_TEST();
}

static int test_equipment_assign() {
    TEST("Equipment assign and query");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    // Assign a dummy index (no real entity, just slot tracking)
    em.EquipmentAssign(0, 42);
    CHECK_EQ(em.EquipmentAt(0), 42);
    PASS(); return 0; END_TEST();
}

static int test_equipment_find_free() {
    TEST("EquipmentFindFreeSlot returns first empty");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    CHECK_EQ(em.EquipmentFindFreeSlot(), 0);
    em.EquipmentAssign(0, 1);
    CHECK_EQ(em.EquipmentFindFreeSlot(), 1);
    for (int s = 0; s < 8; s++) em.EquipmentAssign(s, s);
    CHECK_EQ(em.EquipmentFindFreeSlot(), -1); // all full
    PASS(); return 0; END_TEST();
}

static int test_equipment_clear() {
    TEST("EquipmentClear empties all slots");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    for (int s = 0; s < 8; s++) em.EquipmentAssign(s, s);
    em.EquipmentClear();
    for (int s = 0; s < 8; s++) CHECK_EQ(em.EquipmentAt(s), -1);
    PASS(); return 0; END_TEST();
}

static int test_equipment_out_of_range() {
    TEST("Equipment access with invalid slot returns -1");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    CHECK_EQ(em.EquipmentAt(-1), -1);
    CHECK_EQ(em.EquipmentAt(8), -1);
    em.EquipmentAssign(-1, 99); // should be ignored
    em.EquipmentAssign(8, 99); // should be ignored
    PASS(); return 0; END_TEST();
}

static int test_serialize_format() {
    TEST("SerializeState returns valid format");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    std::string s = em.SerializeState();
    // Format: "hotbar:...|equip:...|"
    CHECK(s.find("hotbar:") == 0 || s.find("hotbar:") != std::string::npos);
    CHECK(s.find("|equip:") != std::string::npos);
    PASS(); return 0; END_TEST();
}

static int test_deserialize_empty() {
    TEST("DeserializeState with empty string resets slots");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    em.HotbarAssign(2, 5);
    CHECK(em.DeserializeState(""));
    CHECK_EQ(em.HotbarAt(2), -1); // should be reset
    PASS(); return 0; END_TEST();
}

static int test_deserialize_roundtrip() {
    TEST("Serialize then Deserialize restores state");
    auto& reg = LightningEntityRegistry::Instance();
    EntityDef defA, defB, defC;
    defA.name = "ser_a"; defA.type = EntityType::PICKUP;
    defB.name = "ser_b"; defB.type = EntityType::PICKUP;
    defC.name = "ser_c"; defC.type = EntityType::PICKUP;
    reg.Register(defA);
    reg.Register(defB);
    reg.Register(defC);

    auto& em = LightningEntityManager::Instance();
    em.Init();
    int idxA = em.Spawn("ser_a");
    int idxB = em.Spawn("ser_b");
    int idxC = em.Spawn("ser_c");
    CHECK(idxA >= 0);
    CHECK(idxB >= 0);
    CHECK(idxC >= 0);
    em.HotbarAssign(0, idxA);
    em.HotbarAssign(3, idxB);
    em.SelectSlot(3);
    em.EquipmentAssign(1, idxC);

    std::string saved = em.SerializeState();
    em.Init();
    reg.Register(defA);
    reg.Register(defB);
    reg.Register(defC);
    CHECK(em.DeserializeState(saved));
    // No Player entity in test env, so ser_a→idx0, ser_b→idx1, ser_c→idx2
    CHECK_EQ(em.HotbarAt(0), 0);
    CHECK_EQ(em.HotbarAt(3), 1);
    CHECK_EQ(em.EquipmentAt(1), 2);
    CHECK_EQ(em.SelectedSlot(), 0);
    PASS(); return 0; END_TEST();
}

static int test_run_action_no_crash() {
    TEST("RunAction on null entity does not crash");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    em.RunAction(nullptr, "on_fire");
    em.RunAction(nullptr, "");
    PASS(); return 0; END_TEST();
}

static int test_player_stats_default() {
    TEST("GetPlayerStat returns default when no player");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    CHECK_EQ((int)em.GetPlayerHealth(), 100);
    CHECK_EQ((int)em.GetPlayerMaxHealth(), 100);
    CHECK_EQ((int)em.GetPlayerLevel(), 1);
    PASS(); return 0; END_TEST();
}

// --- Ammo / Reload tests (Phase 2) ---

static int test_fire_no_weapon() {
    TEST("FireSelectedWeapon returns -1 when no weapon selected");
    auto& em = LightningEntityManager::Instance();
    em.Init();
    // No entity in hotbar
    Vector3 origin = {0,0,0}, dir = {0,0,-1};
    int result = em.FireSelectedWeapon(origin, dir);
    CHECK_EQ(result, -1);
    PASS(); return 0; END_TEST();
}

static int test_fire_not_a_weapon() {
    TEST("FireSelectedWeapon returns -1 for non-weapon entity");
    auto& reg = LightningEntityRegistry::Instance();
    EntityDef def;
    def.name = "NotWeapon";
    def.type = EntityType::PICKUP;
    reg.Register(def);
    auto& em = LightningEntityManager::Instance();
    em.Init();
    int idx = em.Spawn("NotWeapon");
    CHECK(idx >= 0);
    em.HotbarAssign(0, idx);
    Vector3 origin = {0,0,0}, dir = {0,0,-1};
    int result = em.FireSelectedWeapon(origin, dir);
    CHECK_EQ(result, -1);
    PASS(); return 0; END_TEST();
}

static int test_fire_cooldown() {
    TEST("FireSelectedWeapon returns -1 on cooldown");
    auto& reg = LightningEntityRegistry::Instance();
    EntityDef def;
    def.name = "CooldownGun";
    def.type = EntityType::WEAPON;
    def.stats.floats["fire_rate"] = 0.5f;
    reg.Register(def);
    auto& em = LightningEntityManager::Instance();
    em.Init();
    int idx = em.Spawn("CooldownGun");
    CHECK(idx >= 0);
    em.HotbarAssign(0, idx);
    em.SelectSlot(0);
    Vector3 origin = {0,0,0}, dir = {0,0,-1};
    // First fire should succeed (ranged)
    int r1 = em.FireSelectedWeapon(origin, dir);
    CHECK(r1 > 0); // projectile count
    // Second fire immediately should fail due to cooldown
    int r2 = em.FireSelectedWeapon(origin, dir);
    CHECK_EQ(r2, -1);
    PASS(); return 0; END_TEST();
}

static int test_fire_ranged_weapon() {
    TEST("FireSelectedWeapon returns projectile count for ranged");
    auto& reg = LightningEntityRegistry::Instance();
    EntityDef def;
    def.name = "RangedGun";
    def.type = EntityType::WEAPON;
    def.stats.floats["projectile_count"] = 3;
    def.stats.floats["fire_rate"] = 0.25f;
    reg.Register(def);
    auto& em = LightningEntityManager::Instance();
    em.Init();
    int idx = em.Spawn("RangedGun");
    CHECK(idx >= 0);
    em.HotbarAssign(0, idx);
    em.SelectSlot(0);
    Vector3 origin = {0,0,0}, dir = {0,0,-1};
    int result = em.FireSelectedWeapon(origin, dir);
    CHECK_EQ(result, 3);
    PASS(); return 0; END_TEST();
}

static int test_fire_melee_weapon() {
    TEST("FireSelectedWeapon returns 0 for melee swing");
    auto& reg = LightningEntityRegistry::Instance();
    EntityDef def;
    def.name = "MeleeBlade";
    def.type = EntityType::WEAPON;
    def.stats.floats["reach"] = 3.0f;
    def.stats.floats["swing_speed"] = 0.8f;
    reg.Register(def);
    auto& em = LightningEntityManager::Instance();
    em.Init();
    int idx = em.Spawn("MeleeBlade");
    CHECK(idx >= 0);
    em.HotbarAssign(0, idx);
    em.SelectSlot(0);
    Vector3 origin = {0,0,0}, dir = {0,0,-1};
    int result = em.FireSelectedWeapon(origin, dir);
    CHECK_EQ(result, 0); // 0 = melee swing
    PASS(); return 0; END_TEST();
}

static int test_ammo_init_and_decrement() {
    TEST("Ammo initializes from magazine and decrements per shot");
    auto& reg = LightningEntityRegistry::Instance();
    EntityDef def;
    def.name = "AmmoGun";
    def.type = EntityType::WEAPON;
    def.stats.floats["magazine"] = 5;
    def.stats.floats["fire_rate"] = 0.1f;
    reg.Register(def);
    auto& em = LightningEntityManager::Instance();
    em.Init();
    int idx = em.Spawn("AmmoGun");
    CHECK(idx >= 0);
    em.HotbarAssign(0, idx);
    em.SelectSlot(0);
    EntityInstance* ent = em.SelectedEntity();
    CHECK(ent != nullptr);
    Vector3 origin = {0,0,0}, dir = {0,0,-1};
    // Fire 3 shots
    for (int i = 0; i < 3; i++) {
        int r = em.FireSelectedWeapon(origin, dir);
        CHECK(r > 0);
        // Wait for cooldown by setting it to 0
        ent->cooldownRemaining = 0.0f;
    }
    auto it = ent->runtimeStats.find("ammo");
    CHECK(it != ent->runtimeStats.end());
    CHECK_EQ((int)it->second, 2); // 5 - 3 = 2
    PASS(); return 0; END_TEST();
}

static int test_auto_reload_on_empty() {
    TEST("Auto-reload triggers when ammo reaches 0");
    auto& reg = LightningEntityRegistry::Instance();
    EntityDef def;
    def.name = "AutoReloadGun";
    def.type = EntityType::WEAPON;
    def.stats.floats["magazine"] = 2;
    def.stats.floats["fire_rate"] = 0.1f;
    def.stats.floats["reload_time"] = 1.5f;
    reg.Register(def);
    auto& em = LightningEntityManager::Instance();
    em.Init();
    int idx = em.Spawn("AutoReloadGun");
    CHECK(idx >= 0);
    em.HotbarAssign(0, idx);
    em.SelectSlot(0);
    EntityInstance* ent = em.SelectedEntity();
    CHECK(ent != nullptr);
    Vector3 origin = {0,0,0}, dir = {0,0,-1};
    // Fire until empty (2 shots)
    for (int i = 0; i < 2; i++) {
        int r = em.FireSelectedWeapon(origin, dir);
        CHECK(r > 0);
        ent->cooldownRemaining = 0.0f;
    }
    // Next fire should auto-reload (return -1) and reset ammo
    int r = em.FireSelectedWeapon(origin, dir);
    CHECK_EQ(r, -1);
    // Ammo should be back to magazine (2)
    auto it = ent->runtimeStats.find("ammo");
    CHECK(it != ent->runtimeStats.end());
    CHECK_EQ((int)it->second, 2);
    PASS(); return 0; END_TEST();
}

static int test_reload_selected() {
    TEST("ReloadSelectedWeapon refills ammo");
    auto& reg = LightningEntityRegistry::Instance();
    EntityDef def;
    def.name = "ReloadGun";
    def.type = EntityType::WEAPON;
    def.stats.floats["magazine"] = 10;
    def.stats.floats["fire_rate"] = 0.1f;
    def.stats.floats["reload_time"] = 2.0f;
    reg.Register(def);
    auto& em = LightningEntityManager::Instance();
    em.Init();
    int idx = em.Spawn("ReloadGun");
    CHECK(idx >= 0);
    em.HotbarAssign(0, idx);
    em.SelectSlot(0);
    EntityInstance* ent = em.SelectedEntity();
    CHECK(ent != nullptr);
    Vector3 origin = {0,0,0}, dir = {0,0,-1};
    // Fire 4 shots to deplete some ammo
    for (int i = 0; i < 4; i++) {
        em.FireSelectedWeapon(origin, dir);
        ent->cooldownRemaining = 0.0f;
    }
    auto it = ent->runtimeStats.find("ammo");
    CHECK(it != ent->runtimeStats.end());
    CHECK_EQ((int)it->second, 6); // 10-4=6
    // Reload
    bool reloaded = em.ReloadSelectedWeapon();
    CHECK(reloaded);
    CHECK_EQ((int)ent->runtimeStats["ammo"], 10); // back to full
    PASS(); return 0; END_TEST();
}

static int test_reload_no_magazine() {
    TEST("ReloadSelectedWeapon returns false when no magazine stat");
    auto& reg = LightningEntityRegistry::Instance();
    EntityDef def;
    def.name = "NoMagWeapon";
    def.type = EntityType::WEAPON;
    reg.Register(def);
    auto& em = LightningEntityManager::Instance();
    em.Init();
    int idx = em.Spawn("NoMagWeapon");
    CHECK(idx >= 0);
    em.HotbarAssign(0, idx);
    em.SelectSlot(0);
    bool reloaded = em.ReloadSelectedWeapon();
    CHECK(!reloaded);
    PASS(); return 0; END_TEST();
}

static int test_reload_full_ammo() {
    TEST("ReloadSelectedWeapon returns false when ammo is full");
    auto& reg = LightningEntityRegistry::Instance();
    EntityDef def;
    def.name = "FullAmmoGun";
    def.type = EntityType::WEAPON;
    def.stats.floats["magazine"] = 8;
    reg.Register(def);
    auto& em = LightningEntityManager::Instance();
    em.Init();
    int idx = em.Spawn("FullAmmoGun");
    CHECK(idx >= 0);
    em.HotbarAssign(0, idx);
    em.SelectSlot(0);
    // Ammo should be full (not initialized until first fire)
    // Fire once to init ammo, then reload
    Vector3 origin = {0,0,0}, dir = {0,0,-1};
    em.FireSelectedWeapon(origin, dir);
    EntityInstance* ent = em.SelectedEntity();
    CHECK(ent != nullptr);
    ent->cooldownRemaining = 0.0f;
    // Now ammo = 7 (one shot fired)
    em.ReloadSelectedWeapon();
    CHECK_EQ((int)ent->runtimeStats["ammo"], 8);
    // Try reloading again when full
    bool reloaded = em.ReloadSelectedWeapon();
    CHECK(!reloaded);
    PASS(); return 0; END_TEST();
}

static int test_multi_despawn_cycles() {
    TEST("Multiple init cycles do not crash");
    auto& em = LightningEntityManager::Instance();
    for (int i = 0; i < 5; i++) {
        em.Init();
        CHECK_EQ(em.Count(), 0);
        em.HotbarAssign(0, 1);
        em.EquipmentAssign(0, 2);
        CHECK_EQ(em.HotbarAt(0), 1);
        CHECK_EQ(em.EquipmentAt(0), 2);
    }
    PASS(); return 0; END_TEST();
}

int main() {
    fprintf(stdout, "LightningEntityManager Tests\n");
    fprintf(stdout, "============================\n");

    int failures = 0;
    failures += test_init();
    failures += test_hotbar();
    failures += test_spawn_unknown();
    failures += test_hotbar_out_of_range();
    failures += test_select_slot_out_of_range();
    failures += test_selected_entity_empty();
    failures += test_equipment_assign();
    failures += test_equipment_find_free();
    failures += test_equipment_clear();
    failures += test_equipment_out_of_range();
    failures += test_serialize_format();
    failures += test_deserialize_empty();
    failures += test_deserialize_roundtrip();
    failures += test_run_action_no_crash();
    failures += test_player_stats_default();
    failures += test_multi_despawn_cycles();
    failures += test_fire_no_weapon();
    failures += test_fire_not_a_weapon();
    failures += test_fire_cooldown();
    failures += test_fire_ranged_weapon();
    failures += test_fire_melee_weapon();
    failures += test_ammo_init_and_decrement();
    failures += test_auto_reload_on_empty();
    failures += test_reload_selected();
    failures += test_reload_no_magazine();
    failures += test_reload_full_ammo();

    fprintf(stdout, "============================\n");
    fprintf(stdout, "%d/%d passed, %d failed\n",
            tests_passed, tests_total, tests_total - tests_passed);
    return failures;
}
