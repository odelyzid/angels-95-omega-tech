// OzoneParser test — standalone, no raylib dependency
#include "../Source/Server/OzoneParser.hpp"
#include <cstdio>
#include <string>

int test_count = 0, pass_count = 0;

static void test_portal() {
    test_count++;
    printf("  TEST portal entity (full)... ");
    auto e = OzoneParser::parse_string(
        "portal EngineTest2 10 20 30 40 50 60 15 25 35 bidir\n");
    bool ok = e.size() == 1 && e[0].type == OzonePrimitiveType::ENTITY_PORTAL
        && e[0].entityType == "EngineTest2"
        && e[0].args.size() == 10
        && e[0].args[0] == 10.0f && e[0].args[5] == 60.0f   // bounds min/max
        && e[0].args[6] == 15.0f && e[0].args[8] == 35.0f;  // spawn xyz
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu type=%d world=%s\n",
                e.size(), e.size() ? e[0].args.size() : 0,
                e.size() ? (int)e[0].type : -1,
                e.size() ? e[0].entityType.c_str() : "");
}

static void test_portal_minimal() {
    test_count++;
    printf("  TEST portal entity (bounds only)... ");
    auto e = OzoneParser::parse_string("portal OtherLevel 0 0 0 8 12 8\n");
    bool ok = e.size() == 1 && e[0].type == OzonePrimitiveType::ENTITY_PORTAL
        && e[0].args.size() == 6;
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu\n", e.size(), e.size() ? e[0].args.size() : 0);
}

static void test_levelinfo() {
    test_count++;
    printf("  TEST levelinfo entity... ");
    auto e = OzoneParser::parse_string(
        "levelinfo DEATHMATCH 12 7 1 15 50 1 Textures/Sky.dds\n");
    bool ok = e.size() == 1 && e[0].type == OzonePrimitiveType::ENTITY_LEVELINFO
        && e[0].args.size() >= 7;
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu\n", e.size(), e.size() ? e[0].args.size() : 0);
}

static void test_particles() {
    test_count++;
    printf("  TEST particles entity (numeric)... ");
    auto e = OzoneParser::parse_string("particles 2 120 14 0.5 0.6 0.9 -3 1\n");
    bool ok = e.size() == 1 && e[0].type == OzonePrimitiveType::ENTITY_PARTICLES
        && e[0].args.size() == 8
        && e[0].args[0] == 2.0f   // RAIN
        && e[0].args[1] == 120.0f;
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu\n", e.size(), e.size() ? e[0].args.size() : 0);
}

static void test_particles_named() {
    test_count++;
    printf("  TEST particles entity (named type)... ");
    auto e = OzoneParser::parse_string("particles SNOW 80 5 1 1 1 0 2\n");
    bool ok = e.size() == 1 && e[0].type == OzonePrimitiveType::ENTITY_PARTICLES
        && e[0].args.size() == 8
        && e[0].args[0] == 1.0f;  // SNOW -> 1
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu\n", e.size(), e.size() ? e[0].args.size() : 0);
}

static void test_mixed_document() {
    test_count++;
    printf("  TEST mixed document... ");
    std::string doc =
        "# comment\n"
        "box 0 0 0 10 10 10 solid\n"
        "playerstart 5 1 5 90\n"
        "portal EngineTest2 10 20 30 40 50 60 15 25 35 bidir\n"
        "levelinfo DEATHMATCH 12 7 1 15 50 1\n"
        "particles SNOW 80 5 1 1 1 0 2\n";    auto e = OzoneParser::parse_string(doc);
    bool ok = e.size() == 5
        && e[0].type == OzonePrimitiveType::BOX
        && e[1].type == OzonePrimitiveType::ENTITY_PLAYERSTART
        && e[2].type == OzonePrimitiveType::ENTITY_PORTAL
        && e[3].type == OzonePrimitiveType::ENTITY_LEVELINFO
        && e[4].type == OzonePrimitiveType::ENTITY_PARTICLES;
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu\n", e.size());
}

int main() {
    printf("OzoneParser tests:\n");
    test_portal();
    test_portal_minimal();
    test_levelinfo();
    test_particles();
    test_particles_named();
    test_mixed_document();

    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
