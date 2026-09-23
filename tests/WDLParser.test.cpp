// WDLParser test — standalone, no raylib dependency
#include "../Source/Server/WDLParser.hpp"
#include <cstdio>
#include <cassert>
#include <string>

int test_count = 0, pass_count = 0;

static void test_classify() {
    test_count++;
    printf("  TEST classify HeightMap... ");
    auto e = WDLParser::parse_string("HeightMap:0:0:0:100:100:\n");
    if (e.size() == 1 && e[0].type == WDLElementType::HEIGHTMAP) {
        pass_count++; printf("PASS\n");
    } else printf("FAIL: expected HEIGHTMAP type\n");
}

static void test_classify_model() {
    test_count++;
    printf("  TEST classify Model1... ");
    auto e = WDLParser::parse_string("Model1:10:20:30:1.5:45:\n");
    if (e.size() == 1 && e[0].type == WDLElementType::MODEL && e[0].int_id == 1) {
        pass_count++; printf("PASS\n");
    } else printf("FAIL: expected MODEL type with id=1\n");
}

static void test_classify_npc() {
    test_count++;
    printf("  TEST classify Walker NPC... ");
    auto e = WDLParser::parse_string("Walker:100:0:200:\n");
    if (e.size() == 1 && e[0].type == WDLElementType::NPC && e[0].entityType == "Walker") {
        pass_count++; printf("PASS\n");
    } else printf("FAIL: expected NPC type with entityType=Walker\n");
}

static void test_classify_npc_custom() {
    test_count++;
    printf("  TEST classify custom NPC name... ");
    auto e = WDLParser::parse_string("Dragon:50:0:100:\n");
    if (e.size() == 1 && e[0].type == WDLElementType::UNKNOWN) {
        pass_count++; printf("PASS\n");
    } else printf("FAIL: expected UNKNOWN for unrecognized NPC type\n");
}

static void test_multiple_lines() {
    test_count++;
    printf("  TEST multiple WDL lines... ");
    std::string wdl = "Model1:0:0:0:1:0:\nCollision:0:0:0:100:10:100:\nSpawn:10:0:10:90:\n";
    auto e = WDLParser::parse_string(wdl);
    if (e.size() == 3) {
        pass_count++; printf("PASS\n");
    } else printf("FAIL: expected 3 elements, got %zu\n", e.size());
}

static void test_pickup() {
    test_count++;
    printf("  TEST Pickup line... ");
    auto e = WDLParser::parse_string("Pickup:HealthVial:5:0:5:\n");
    if (e.size() == 1 && e[0].type == WDLElementType::PICKUP) {
        pass_count++; printf("PASS\n");
    } else printf("FAIL: expected PICKUP type\n");
}

static void test_comment() {
    test_count++;
    printf("  TEST comment line... ");
    auto e = WDLParser::parse_string("# This is a comment\nModel1:0:0:0:1:0:\n");
    if (e.size() == 1 && e[0].type == WDLElementType::MODEL) {
        pass_count++; printf("PASS\n");
    } else printf("FAIL: expected MODEL type after comment\n");
}

static void test_light() {
    test_count++;
    printf("  TEST Light line... ");
    auto e = WDLParser::parse_string("Light:10:20:30:\n");
    if (e.size() == 1 && e[0].type == WDLElementType::LIGHT) {
        pass_count++; printf("PASS\n");
    } else printf("FAIL: expected LIGHT type\n");
}

static void test_portal_full() {
    test_count++;
    printf("  TEST Portal line (full)... ");
    auto e = WDLParser::parse_string("Portal:EngineTest2:10:0:20:30:40:50:15:25:35:1:\n");
    bool ok = e.size() == 1 && e[0].type == WDLElementType::PORTAL
        && e[0].entityType == "EngineTest2"
        && e[0].args.size() == 10
        && e[0].args[0] == 10.0f && e[0].args[5] == 50.0f   // bounds min/max
        && e[0].args[6] == 15.0f && e[0].args[8] == 35.0f   // spawn xyz
        && e[0].args[9] == 1.0f;                             // bidirectional
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu type=%d world=%s\n",
                e.size(), e[0].args.size(), (int)e[0].type, e[0].entityType.c_str());
}

static void test_portal_bounds_only() {
    test_count++;
    printf("  TEST Portal line (bounds only)... ");
    auto e = WDLParser::parse_string("Portal:OtherLevel:0:0:0:8:12:8:\n");
    bool ok = e.size() == 1 && e[0].type == WDLElementType::PORTAL
        && e[0].entityType == "OtherLevel"
        && e[0].args.size() == 6;
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu\n", e.size(), e[0].args.size());
}

static void test_level_info() {
    test_count++;
    printf("  TEST LevelInfo line... ");
    auto e = WDLParser::parse_string("LevelInfo:DEATHMATCH:12:7:1:15:50:1:Textures/Sky.dds:\n");
    bool ok = e.size() == 1 && e[0].type == WDLElementType::LEVEL_INFO
        && e[0].args.size() == 7
        && e[0].args[0] == 0.0f   // gameType enum index (string -> 0 in args)
        && e[0].args[1] == 12.0f
        && e[0].args[3] == 1.0f
        && e[0].entityType == "Textures/Sky.dds";  // skybox path
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu skybox=%s\n",
                e.size(), e[0].args.size(), e[0].entityType.c_str());
}

static void test_particles() {
    test_count++;
    printf("  TEST Particles line (numeric)... ");
    auto e = WDLParser::parse_string("Particles:2:120:14:0.5:0.6:0.9:-3:1:\n");
    bool ok = e.size() == 1 && e[0].type == WDLElementType::PARTICLES
        && e[0].args.size() == 8
        && e[0].args[0] == 2.0f   // RAIN
        && e[0].args[1] == 120.0f
        && e[0].args[6] == -3.0f;
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu\n", e.size(), e.size() ? e[0].args.size() : 0);
}

static void test_particles_named() {
    test_count++;
    printf("  TEST Particles line (named type)... ");
    auto e = WDLParser::parse_string("Particles:RAIN:120:14:0.5:0.6:0.9:-3:1:\n");
    bool ok = e.size() == 1 && e[0].type == WDLElementType::PARTICLES
        && e[0].args.size() == 8
        && e[0].args[0] == 2.0f;  // RAIN -> 2
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu\n", e.size(), e.size() ? e[0].args.size() : 0);
}

int main() {
    printf("WDLParser tests:\n");
    test_classify();
    test_classify_model();
    test_classify_npc();
    test_classify_npc_custom();
    test_multiple_lines();
    test_pickup();
    test_comment();
    test_light();
    test_portal_full();
    test_portal_bounds_only();
    test_level_info();
    test_particles();
    test_particles_named();

    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
