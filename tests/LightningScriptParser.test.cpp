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

static int test_parse_projectile() {
    TEST("parse projectile entity def");
    std::string ozls = R"(
        entity "energy_bolt" : projectile {
            mesh = "bolt.obj"
            texture = "bolt_blue.png"
            stats {
                damage = 25
                speed = 30.0
                lifetime = 3.0
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_projectile.ozls");
    CHECK(def.name == "energy_bolt");
    CHECK(def.type == EntityType::PROJECTILE);
    CHECK_APROX(def.stats.floats["damage"], 25.0f, 0.001f);
    CHECK_APROX(def.stats.floats["speed"], 30.0f, 0.001f);
    CHECK_APROX(def.stats.floats["lifetime"], 3.0f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_parse_pawn() {
    TEST("parse pawn entity def");
    std::string ozls = R"(
        entity "Skaarj" : pawn {
            mesh = "skaarj.obj"
            texture = "skaarj_tex.png"
            stats {
                speed = 3.0
                aggro_range = 12.0
                attack_range = 2.0
                damage = 20
                max_health = 150
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_pawn.ozls");
    CHECK(def.name == "Skaarj");
    // EntityType::PAWN removed — parser now warns but continues parsing body
    CHECK(def.type == EntityType::UNKNOWN);
    CHECK_APROX(def.stats.floats["speed"], 3.0f, 0.001f);
    CHECK_APROX(def.stats.floats["aggro_range"], 12.0f, 0.001f);
    CHECK_APROX(def.stats.floats["max_health"], 150.0f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_parse_upgrade() {
    TEST("parse upgrade entity def");
    std::string ozls = R"(
        entity "SwiftBoots" : upgrade {
            icon = "SwiftBoots.png"
            stats {
                move_speed_bonus = 1.5
                equip_slot = "boots"
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_upgrade.ozls");
    CHECK(def.name == "SwiftBoots");
    CHECK(def.type == EntityType::UPGRADE);
    CHECK_APROX(def.stats.floats["move_speed_bonus"], 1.5f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_parse_music_field() {
    TEST("parse entity with music field");
    std::string ozls = R"(
        entity "boss_arena" : skyzone {
            skybox = "boss_sky"
            music = "boss_theme.ogg"
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_music.ozls");
    CHECK(def.name == "boss_arena");
    CHECK(def.type == EntityType::SKYZONE);
    CHECK(def.skybox == "boss_sky");
    CHECK(def.music == "boss_theme.ogg");
    PASS(); return 0; END_TEST();
}

static int test_parse_play_sound_action() {
    TEST("parse entity with play_sound action");
    std::string ozls = R"(
        entity "gun_test" : weapon {
            actions {
                on_fire {
                    play_sound "bang.wav"
                    set_cooldown 0.3
                }
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_sound.ozls");
    CHECK(def.actions.size() >= 1);
    CHECK(def.actions[0].name == "on_fire");
    bool hasPlaySound = false;
    for (auto& line : def.actions[0].scriptLines) {
        if (line.find("play_sound") != std::string::npos) hasPlaySound = true;
    }
    CHECK(hasPlaySound);
    PASS(); return 0; END_TEST();
}

static int test_parse_goto_action() {
    TEST("parse entity with goto in action");
    std::string ozls = R"(
        entity "loop_test" : weapon {
            actions {
                on_fire {
                    say "pew"
                    goto fire_loop
                }
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_goto.ozls");
    CHECK(def.actions.size() >= 1);
    CHECK(def.actions[0].name == "on_fire");
    bool hasGoto = false;
    for (auto& line : def.actions[0].scriptLines) {
        if (line.find("goto") != std::string::npos) hasGoto = true;
    }
    CHECK(hasGoto);
    PASS(); return 0; END_TEST();
}

static int test_parse_multiple_actions() {
    TEST("parse entity with multiple actions");
    std::string ozls = R"(
        entity "multi_test" : weapon {
            stats { damage = 10 }
            actions {
                on_fire { say "bang" set_cooldown 0.4 }
                on_reload { say "reloading..." }
                on_equip { say "ready" }
                on_unequip { say "holstered" }
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_multi.ozls");
    CHECK(def.actions.size() >= 4);
    CHECK(def.actions[0].name == "on_fire");
    CHECK(def.actions[1].name == "on_reload");
    CHECK(def.actions[2].name == "on_equip");
    CHECK(def.actions[3].name == "on_unequip");
    PASS(); return 0; END_TEST();
}

static int test_parse_variant_texture_override() {
    TEST("parse variant with texture_override");
    std::string ozls = R"(
        entity "armor" : armor {
            variants {
                "rusty" { texture_override = "rusty_armor.png" }
                "shiny" { texture_override = "shiny_armor.png" }
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_variant_tex.ozls");
    CHECK(def.variants.size() >= 2);
    CHECK(def.variants[0].textureOverride == "rusty_armor.png");
    CHECK(def.variants[1].textureOverride == "shiny_armor.png");
    PASS(); return 0; END_TEST();
}

static int test_parse_empty_actions() {
    TEST("parse entity with empty actions block");
    std::string ozls = R"(
        entity "silent" : weapon {
            actions { }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_empty_actions.ozls");
    CHECK(def.actions.empty());
    PASS(); return 0; END_TEST();
}

static int test_parse_all_fields() {
    TEST("parse entity with all fields");
    std::string ozls = R"(
        entity "giga_cannon" : weapon {
            mesh = "cannon.obj"
            texture = "cannon_tex.png"
            icon = "cannon_icon.png"
            stats {
                damage = 99
                fire_rate = 1.5
                projectile_speed = 50.0
                projectile_count = 3
                spread = 2.0
                magazine = 6
                reload_time = 3.0
            }
            actions {
                on_fire {
                    set_cooldown 1.5
                    play_sound "cannon_boom.wav"
                    say "BOOM!"
                }
            }
            variants {
                "mk1" { mesh_override = "cannon_mk1" texture_override = "cannon_mk1_tex.png" }
                "mk2" { mesh_override = "cannon_mk2" texture_override = "cannon_mk2_tex.png" }
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_all.ozls");
    CHECK(def.name == "giga_cannon");
    CHECK(def.type == EntityType::WEAPON);
    CHECK(def.mesh == "cannon.obj");
    CHECK(def.texture == "cannon_tex.png");
    CHECK(def.icon == "cannon_icon.png");
    CHECK_APROX(def.stats.floats["damage"], 99.0f, 0.001f);
    CHECK_APROX(def.stats.floats["projectile_count"], 3.0f, 0.001f);
    CHECK(def.actions.size() >= 1);
    CHECK(def.variants.size() >= 2);
    CHECK(def.variants[0].meshOverride == "cannon_mk1");
    PASS(); return 0; END_TEST();
}

static int test_parse_name_with_spaces() {
    TEST("parse entity name with spaces");
    std::string ozls = R"(
        entity "Heavy Plasma Rifle" : weapon {
            stats { damage = 50 }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_spaces.ozls");
    CHECK(def.name == "Heavy Plasma Rifle");
    CHECK(def.type == EntityType::WEAPON);
    PASS(); return 0; END_TEST();
}

static int test_parse_large_stats() {
    TEST("parse very large stat values");
    std::string ozls = R"(
        entity "overlord" : pawn {
            stats {
                max_health = 99999
                damage = 5000.5
                speed = 0.001
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_large.ozls");
    CHECK_APROX(def.stats.floats["max_health"], 99999.0f, 0.001f);
    CHECK_APROX(def.stats.floats["damage"], 5000.5f, 0.001f);
    CHECK_APROX(def.stats.floats["speed"], 0.001f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_parse_empty_string() {
    TEST("parse empty string returns UNKNOWN");
    EntityDef def = LightningScriptParser::Parse("", "empty.ozls");
    CHECK(def.type == EntityType::UNKNOWN);
    PASS(); return 0; END_TEST();
}

static int test_parse_set_skybox() {
    TEST("parse entity with set_skybox action");
    std::string ozls = R"(
        entity "sky_changer" : skyzone {
            actions {
                on_enter {
                    set_skybox "sunset_sky"
                    set_fog 0.8 0.6 0.5 0.003
                }
                on_exit { restore_skybox restore_fog }
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_skybox_action.ozls");
    CHECK(def.actions.size() >= 2);
    CHECK(def.actions[0].name == "on_enter");
    CHECK(def.actions[1].name == "on_exit");
    bool hasSetSkybox = false;
    for (auto& line : def.actions[0].scriptLines) {
        if (line.find("set_skybox") != std::string::npos) hasSetSkybox = true;
    }
    CHECK(hasSetSkybox);
    PASS(); return 0; END_TEST();
}

static int test_parse_both_stats_types() {
    TEST("parse entity with float and string stats");
    std::string ozls = R"(
        entity "loot_box" : pickup {
            stats {
                item_id = 42
                respawn_time = 15.0
                pickup_category = "treasure"
            }
        }
    )";
    EntityDef def = LightningScriptParser::Parse(ozls, "test_both_stats.ozls");
    CHECK_APROX(def.stats.floats["item_id"], 42.0f, 0.001f);
    CHECK_APROX(def.stats.floats["respawn_time"], 15.0f, 0.001f);
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
    failures += test_parse_projectile();
    failures += test_parse_pawn();
    failures += test_parse_upgrade();
    failures += test_parse_music_field();
    failures += test_parse_play_sound_action();
    failures += test_parse_goto_action();
    failures += test_parse_multiple_actions();
    failures += test_parse_variant_texture_override();
    failures += test_parse_empty_actions();
    failures += test_parse_all_fields();
    failures += test_parse_name_with_spaces();
    failures += test_parse_large_stats();
    failures += test_parse_empty_string();
    failures += test_parse_set_skybox();
    failures += test_parse_both_stats_types();
    fprintf(stdout, "\n%d/%d tests passed.\n", tests_passed, tests_total);
    return failures;
}
