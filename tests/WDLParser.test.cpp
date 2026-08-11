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

    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
