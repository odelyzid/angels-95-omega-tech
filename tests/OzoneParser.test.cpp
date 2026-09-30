// OzoneParser test — standalone, no raylib dependency
#include "../Source/World/OzoneParser.hpp"
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

static void test_mesh_static() {
    test_count++;
    printf("  TEST GameEngine.Mesh.Static entity... ");
    auto e = OzoneParser::parse_string(
        "Mesh.Static GameData/Models/Crate.glb 1 2 3 90 scale=2 tex=GameData/Tex/crate.png\n");
    bool ok = e.size() == 1 && e[0].type == OzonePrimitiveType::ENTITY_MESH_STATIC
        && e[0].meshPath == "GameData/Models/Crate.glb"
        && e[0].texPath == "GameData/Tex/crate.png"
        && e[0].args.size() == 5
        && e[0].args[0] == 1.0f && e[0].args[3] == 90.0f && e[0].args[4] == 2.0f;
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu mesh=%s\n",
                e.size(), e.size() ? e[0].args.size() : 0,
                e.size() ? e[0].meshPath.c_str() : "");
}

static void test_mesh_skeletal() {
    test_count++;
    printf("  TEST GameEngine.Mesh.Skeletal entity... ");
    auto e = OzoneParser::parse_string(
        "Mesh.Skeletal GameData/Pawns/Walker.glb 0 0 0 45 anim=Walk speed=1.5\n");
    bool ok = e.size() == 1 && e[0].type == OzonePrimitiveType::ENTITY_MESH_SKELETAL
        && e[0].meshPath == "GameData/Pawns/Walker.glb"
        && e[0].animClip == "Walk"
        && e[0].animSpeed == 1.5f
        && e[0].args.size() == 4;
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu clip=%s speed=%.2f\n",
                e.size(), e.size() ? e[0].args.size() : 0,
                e.size() ? e[0].animClip.c_str() : "", e.size() ? e[0].animSpeed : 0.0f);
}

static void test_particle_emitter() {
    test_count++;
    printf("  TEST GameEngine.ParticleEmitter entity... ");
    auto e = OzoneParser::parse_string(
        "ParticleEmitter fire 1 2 3 30 0.8 2.5 0.5 0.4 0.0 "
        "255 180 80 40 10 0 0 0.2 0 1 0 0 tex=GameData/EFX/fire.png\n");
    bool ok = e.size() == 1 && e[0].type == OzonePrimitiveType::ENTITY_PARTICLE_EMITTER
        && e[0].entityType == "fire"
        && e[0].texPath == "GameData/EFX/fire.png"
        && e[0].args.size() == 21
        && e[0].args[0] == 1.0f && e[0].args[2] == 3.0f   // position
        && e[0].args[3] == 30.0f;                          // rate
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu type=%s\n",
                e.size(), e.size() ? e[0].args.size() : 0,
                e.size() ? e[0].entityType.c_str() : "");
}

static void test_mesh_animfile() {
    test_count++;
    printf("  TEST Mesh.Skeletal animfile=/speed=... ");
    auto e = OzoneParser::parse_string(
        "Mesh.Skeletal GameData/M.glb 0 0 0 0 animfile=Global/Anims/m.ozanim speed=2.5\n");
    bool ok = e.size() == 1 && e[0].type == OzonePrimitiveType::ENTITY_MESH_SKELETAL
        && e[0].animFile == "Global/Anims/m.ozanim"
        && e[0].animSpeed == 2.5f
        && e[0].args.size() == 4;
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu animfile=%s speed=%.2f\n",
                e.size(), e.size() ? e[0].animFile.c_str() : "",
                e.size() ? e[0].animSpeed : 0.0f);
}

static void test_path_node() {
    test_count++;
    printf("  TEST GameEngine.PathNode entity... ");
    auto e = OzoneParser::parse_string(
        "PathNode A 0 0 0 radius=1.5 next=B,C loop\n");
    bool ok = e.size() == 1 && e[0].type == OzonePrimitiveType::ENTITY_PATH_NODE
        && e[0].entityType == "A"
        && e[0].entitySubType == "B,C"
        && e[0].pathLoop
        && e[0].args.size() == 4
        && e[0].args[0] == 0.0f && e[0].args[3] == 1.5f;
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu name=%s next=%s loop=%d\n",
                e.size(), e.size() ? e[0].args.size() : 0,
                e.size() ? e[0].entityType.c_str() : "",
                e.size() ? e[0].entitySubType.c_str() : "",
                e.size() ? (int)e[0].pathLoop : -1);
}

static void test_wind_zone() {
    test_count++;
    printf("  TEST WindZone entity... ");
    auto e = OzoneParser::parse_string(
        "WindZone -10 -5 -10 10 25 10 1 0 0 2.5 0.8\n");
    bool ok = e.size() == 1 && e[0].type == OzonePrimitiveType::ENTITY_WIND_ZONE
        && e[0].args.size() == 11
        && e[0].args[0] == -10.0f && e[0].args[9] == 2.5f && e[0].args[10] == 0.8f;
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu args=%zu\n", e.size(), e.size() ? e[0].args.size() : 0);
}

static void test_mesh_wind_flag() {
    test_count++;
    printf("  TEST GameEngine.Mesh wind flag... ");
    auto e = OzoneParser::parse_string(
        "Mesh.Static GameData/Models/Tree.glb 0 0 0 0 wind=1 tex=x.png\n");
    bool ok = e.size() == 1 && e[0].type == OzonePrimitiveType::ENTITY_MESH_STATIC
        && e[0].meshWind
        && e[0].meshPath == "GameData/Models/Tree.glb";
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: size=%zu wind=%d\n", e.size(), e.size() ? (int)e[0].meshWind : -1);
}

static void test_zone_physics_kwargs() {
    test_count++;
    printf("  TEST zone physics kwargs... ");
    auto e = OzoneParser::parse_string(
        "zone water -8 -8 0 8 8 2 1 gravity=14 jump=6.5 terminal=45 "
        "water_gravity=5 water_drag=0.9 swim_up=4 ladder_speed=7 fly_mult=2 "
        "name=zone_water_0\n");
    auto& p = e[0];
    bool ok = e.size() == 1 && p.type == OzonePrimitiveType::ENTITY_ZONE
        && p.entitySubType == "water"
        && p.name == "zone_water_0"
        && p.args.size() == 7                     // bounds + intensity
        && p.hasPhysics
        && p.physics.gravity == 14.0f
        && p.physics.jumpSpeed == 6.5f
        && p.physics.terminalVelocity == 45.0f
        && p.physics.waterGravity == 5.0f
        && p.physics.waterDrag == 0.9f
        && p.physics.swimUpSpeed == 4.0f
        && p.physics.ladderSpeed == 7.0f
        && p.physics.flySpeedMult == 2.0f;
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: type=%d args=%zu hasPhysics=%d g=%f\n",
                e.size() ? (int)p.type : -1, e.size() ? p.args.size() : 0,
                e.size() ? (int)p.hasPhysics : -1,
                e.size() ? p.physics.gravity : -1.0f);
}

static void test_zone_default_physics() {
    test_count++;
    printf("  TEST zone without physics kwargs keeps defaults... ");
    auto e = OzoneParser::parse_string(
        "zone water -3 30 -8 3 36 -2 1 name=acidpool\n");
    auto& p = e[0];
    bool ok = e.size() == 1 && p.type == OzonePrimitiveType::ENTITY_ZONE
        && !p.hasPhysics
        && p.physics.gravity == 20.0f
        && p.physics.jumpSpeed == 8.0f
        && p.physics.ladderSpeed == 6.0f;
    if (ok) { pass_count++; printf("PASS\n"); }
    else printf("FAIL: hasPhysics=%d g=%f\n",
                e.size() ? (int)p.hasPhysics : -1,
                e.size() ? p.physics.gravity : -1.0f);
}

int main() {
    printf("OzoneParser tests:\n");
    test_portal();
    test_portal_minimal();
    test_levelinfo();
    test_particles();
    test_particles_named();
    test_mesh_static();
    test_mesh_skeletal();
    test_particle_emitter();
    test_path_node();
    test_wind_zone();
    test_mesh_wind_flag();
    test_mesh_animfile();
    test_mixed_document();
    test_zone_physics_kwargs();
    test_zone_default_physics();

    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
