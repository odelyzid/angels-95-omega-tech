// LightningScriptParser unit tests
// Compile: g++ -O0 -g --std=c++20 -I ../Source ../Source/Script/LightningScriptParser.cpp LightningScriptParser.test.cpp -o LightningScriptParser.test

#include "../Source/Script/LightningScriptParser.hpp"
#include <cstdio>
#include <cstring>
#include <cassert>

static int tests_total = 0, tests_passed = 0;
#define TEST(name) do { tests_total++; fprintf(stdout, "  TEST: %s ... ", name);
#define PASS() do { tests_passed++; fprintf(stdout, "PASS\n"); } while(0)
#define FAIL(msg) do { fprintf(stdout, "FAIL: %s\n", msg); return 1; } while(0)
#define CHECK(cond) do { if (!(cond)) { fprintf(stdout, "FAIL: %s\n", #cond); return 1; } } while(0)
#define CHECK_EQ(a, b) do { if ((a) != (b)) { fprintf(stdout, "FAIL: expected '%s', got '%s'\n", std::string(b).c_str(), std::string(a).c_str()); return 1; } } while(0)
#define CHECK_APROX(a, b, eps) do { float diff = (a) - (b); if (diff < 0) diff = -diff; if (diff > (eps)) { fprintf(stdout, "FAIL: expected %f, got %f\n", (float)(b), (float)(a)); return 1; } } while(0)
#define END_TEST() } while(0)

static int test_parse_weapon() {
    TEST("parse weapon entity def");
    std::string ozls = R"(
        entity "automag" : weapon {
            mesh = "automag_lvl1.obj"
            texture = "automag_lvl1_texture.png"
            icon = "automag_lvl1_icon.png"
            stats {
                damage = 15
                fire_rate = 0.4
                range = 50.0
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_weapon.ozls");
    CHECK(def.name == "automag");
    CHECK(def.type == EntityType::WEAPON);
    CHECK(def.mesh == "automag_lvl1.obj");
    CHECK(def.texture == "automag_lvl1_texture.png");
    CHECK(def.icon == "automag_lvl1_icon.png");
    CHECK_APROX(def.stats.floats["damage"], 15.0f, 0.001f);
    CHECK_APROX(def.stats.floats["fire_rate"], 0.4f, 0.001f);
    CHECK_APROX(def.stats.floats["range"], 50.0f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_parse_skyzone() {
    TEST("parse skyzone entity def");
    std::string ozls = R"(
        entity "snowy_zone" : skyzone {
            fog_color = (0.8, 0.85, 0.9)
            fog_density = 0.015
            ambient_light = (0.6, 0.6, 0.7)
            skybox = "skybox_snowy"
            actions {
                on_enter {
                    set_fog 0.8 0.85 0.9 0.015
                    set_skybox "skybox_snowy"
                }
                on_exit {
                    set_fog 0.5 0.5 0.5 0.002
                    restore_skybox
                }
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_skyzone.ozls");
    CHECK(def.name == "snowy_zone");
    CHECK(def.type == EntityType::SKYZONE);
    CHECK(def.skybox == "skybox_snowy");
    CHECK_APROX(def.stats.vec3s["fog_color"][0], 0.8f, 0.001f);
    CHECK_APROX(def.stats.vec3s["fog_color"][1], 0.85f, 0.001f);
    CHECK_APROX(def.stats.vec3s["fog_color"][2], 0.9f, 0.001f);
    CHECK_APROX(def.stats.floats["fog_density"], 0.015f, 0.001f);
    CHECK(def.actions.size() >= 2);
    CHECK(def.actions[0].name == "on_enter");
    CHECK(def.actions[0].scriptLines.size() >= 2);
    PASS(); return 0; END_TEST();
}

static int test_parse_armor() {
    TEST("parse armor entity def");
    std::string ozls = R"(
        entity "iron_helmet" : armor {
            mesh = "Helmet.obj"
            texture = "HelmetTexture.png"
            icon = "HelmetIcon.png"
            stats {
                defense = 5
                weight = 2.0
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_armor.ozls");
    CHECK(def.name == "iron_helmet");
    CHECK(def.type == EntityType::ARMOR);
    CHECK_APROX(def.stats.floats["defense"], 5.0f, 0.001f);
    CHECK_APROX(def.stats.floats["weight"], 2.0f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_parse_consumable() {
    TEST("parse consumable entity def");
    std::string ozls = R"(
        entity "mana_vial" : consumable {
            icon = "ManaVial.png"
            stats {
                restore = 25.0
                max_stack = 10
            }
            actions {
                on_use {
                    say "Used mana vial"
                }
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_consumable.ozls");
    CHECK(def.name == "mana_vial");
    CHECK(def.type == EntityType::CONSUMABLE);
    CHECK(def.icon == "ManaVial.png");
    CHECK_APROX(def.stats.floats["restore"], 25.0f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_parse_with_variants() {
    TEST("parse variants block");
    std::string ozls = R"(
        entity "automag" : weapon {
            mesh = "base.obj"
            variants {
                "lvl1" { mesh_override = "automag_lvl1" }
                "lvl2" { mesh_override = "automag_lvl2" }
                "lvl3" { mesh_override = "automag_heavy_rifle_lvl3" }
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_variants.ozls");
    CHECK(def.name == "automag");
    CHECK(def.variants.size() == 3);
    CHECK(def.variants[0].name == "lvl1");
    CHECK(def.variants[0].meshOverride == "automag_lvl1");
    PASS(); return 0; END_TEST();
}

static int test_parse_error_recovery() {
    TEST("parse error recovery (malformed input)");
    std::string ozls = "this is not valid ozls";
    EntityDef def = LightningScriptParser::Parse(ozls, "bad.ozls");
    // Should return empty/unknown entity def, not crash
    CHECK(def.type == EntityType::UNKNOWN);
    PASS(); return 0; END_TEST();
}

static int test_action_block_content() {
    TEST("action block extracts real script lines");
    std::string ozls = R"(
        entity "test" : weapon {
            actions {
                on_fire {
                    set_cooldown 0.4
                    say "Fired!"
                }
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_action.ozls");
    CHECK(def.actions.size() >= 1);
    CHECK(def.actions[0].name == "on_fire");
    CHECK(def.actions[0].scriptLines.size() >= 2);
    bool hasCooldown = false, hasSay = false;
    for (auto& line : def.actions[0].scriptLines) {
        if (line.find("set_cooldown") != std::string::npos) hasCooldown = true;
        if (line.find("say") != std::string::npos) hasSay = true;
    }
    CHECK(hasCooldown);
    CHECK(hasSay);
    PASS(); return 0; END_TEST();
}

int main() {
    fprintf(stdout, "LightningScriptParser Tests:\n");
    int failures = 0;
    failures += test_parse_weapon();
    failures += test_parse_skyzone();
    failures += test_parse_armor();
    failures += test_parse_consumable();
    failures += test_parse_with_variants();
    failures += test_parse_error_recovery();
    failures += test_action_block_content();
    fprintf(stdout, "\n%d/%d tests passed.\n", tests_passed, tests_total);
    return failures;
}
