// LightningScriptContext unit tests
// Compile: g++ -O0 -g --std=c++20 -I ../Source ../Source/Script/LightningScriptContext.cpp LightningScriptContext.test.cpp -o LightningScriptContext.test

#include "../Source/Script/LightningScriptContext.hpp"
#include <cstdio>
#include <cstring>
#include <cassert>

static int tests_total = 0, tests_passed = 0;
#define TEST(name) do { tests_total++; fprintf(stdout, "  TEST: %s ... ", name);
#define PASS() do { tests_passed++; fprintf(stdout, "PASS\n"); } while(0)
#define FAIL(msg) do { fprintf(stdout, "FAIL: %s\n", msg); return 1; } while(0)
#define CHECK(cond) do { if (!(cond)) { fprintf(stdout, "FAIL: %s\n", #cond); return 1; } } while(0)
#define CHECK_EQ(a, b) do { if ((a) != (b)) { fprintf(stdout, "FAIL: expected %d, got %d\n", (int)(a), (int)(b)); return 1; } } while(0)
#define CHECK_APROX(a, b, eps) do { float diff = (a) - (b); if (diff < 0) diff = -diff; if (diff > (eps)) { fprintf(stdout, "FAIL: expected %f, got %f (eps %f)\n", (float)(b), (float)(a), (float)(eps)); return 1; } } while(0)
#define CHECK_STR(a, b) do { if ((a) != (b)) { fprintf(stdout, "FAIL: expected '%s', got '%s'\n", (b).c_str(), (a).c_str()); return 1; } } while(0)
#define END_TEST() } while(0)

static int test_var_declaration() {
    TEST("var auto-detects int vs float");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_var");
    CHECK(ctx.Load("var x = 5\nvar y = 3.14\nvar z = hello"));
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetInt("x"), 5);
    ctx.ExecuteNext(); CHECK_APROX(ctx.GetFloat("y"), 3.14f, 0.001f);
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetInt("z"), 0); // unknown string → defaults to 0
    PASS(); return 0; END_TEST();
}

static int test_assignment() {
    TEST("$var = value");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_assign");
    CHECK(ctx.Load("$a = 42\n$b = $a\n$c = 3.5"));
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetInt("a"), 42);
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetInt("b"), 42);
    ctx.ExecuteNext(); CHECK_APROX(ctx.GetFloat("c"), 3.5f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_compound_assign() {
    TEST("compound += -= *= /=");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_compound");
    CHECK(ctx.Load("$x = 10\n$x += 5\n$x -= 3\n$x *= 2\n$x /= 2"));
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetInt("x"), 10);
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetInt("x"), 15);
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetInt("x"), 12);
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetInt("x"), 24);
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetInt("x"), 12);
    PASS(); return 0; END_TEST();
}

static int test_if_condition() {
    TEST("if/else condition evaluation");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_if");
    CHECK(ctx.Load("if ($x == 5) { $result = 1 } endif\nif ($x > 10) { $result = 2 } endif"));
    ctx.SetInt("x", 5);
    ctx.ExecuteNext(); // if true → executes block
    ctx.ExecuteNext(); // endif
    ctx.ExecuteNext(); // if false → skips block
    ctx.ExecuteNext(); // endif
    PASS(); return 0; END_TEST();
}

static int test_say_opcode() {
    TEST("say opcode");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_say");
    CHECK(ctx.Load("say \"hello world\""));
    ctx.ExecuteNext(); // should not crash
    PASS(); return 0; END_TEST();
}

static int test_flags() {
    TEST("wtflag / rtflag");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_flags");
    CHECK(ctx.Load("wtflag 3 1\nwtflag 7 0"));
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetFlag(3), 1);
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetFlag(7), 0);
    PASS(); return 0; END_TEST();
}

static int test_stop() {
    TEST("stop halts execution");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_stop");
    CHECK(ctx.Load("$a = 1\nstop\n$a = 2"));
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetInt("a"), 1);
    ctx.ExecuteNext(); // stop → no more instructions
    CHECK(!ctx.HasMore()); // PC at end
    PASS(); return 0; END_TEST();
}

static int test_jump_label() {
    TEST("jump labels resolved on Load()");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_label");
    CHECK(ctx.Load("start:\n$a = 1\nend:\n$b = 2"));
    int startLine = ctx.FindJumpLabel("start");
    int endLine = ctx.FindJumpLabel("end");
    CHECK(startLine >= 0);
    CHECK(endLine >= 0);
    CHECK(startLine < endLine);
    PASS(); return 0; END_TEST();
}

static int test_set_cooldown() {
    TEST("set_cooldown stores __cooldown var");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_cd");
    CHECK(ctx.Load("set_cooldown 1.5"));
    ctx.ExecuteNext();
    CHECK_APROX(ctx.GetFloat("__cooldown"), 1.5f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_goto_jump() {
    TEST("goto jumps forward and backward");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_goto");
    CHECK(ctx.Load("$x = 0\nloop:\n$x += 1\nif ($x < 3)\ngoto loop\nendif\n$x += 10"));
    // $x=0, loop:, $x+=1→1, if(1<3)=true→goto, $x+=1→2, if(2<3)=true→goto, $x+=1→3, if(3<3)=false, $x+=10→13
    int steps = 0;
    while (ctx.HasMore() && steps < 20) { ctx.ExecuteNext(); steps++; }
    CHECK_EQ(ctx.GetInt("x"), 13);
    PASS(); return 0; END_TEST();
}

static int test_else_branch() {
    TEST("else branch executes correctly");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_else");
    CHECK(ctx.Load("$flag = 1\nif ($flag == 1)\n$result = 10\nelse\n$result = 20\nendif"));
    ctx.ExecuteNext(); // $flag = 1
    ctx.ExecuteNext(); // if true — fall through
    ctx.ExecuteNext(); // $result = 10
    ctx.ExecuteNext(); // endif
    CHECK_EQ(ctx.GetInt("result"), 10);
    PASS(); return 0; END_TEST();
}

static int test_else_branch_false() {
    TEST("else branch when condition is false");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_else_false");
    CHECK(ctx.Load("$flag = 0\nif ($flag == 1)\n$result = 10\nelse\n$result = 20\nendif"));
    ctx.ExecuteNext(); // $flag = 0
    ctx.ExecuteNext(); // if false — skip past if-body to else-body
    ctx.ExecuteNext(); // $result = 20
    ctx.ExecuteNext(); // endif
    CHECK_EQ(ctx.GetInt("result"), 20);
    PASS(); return 0; END_TEST();
}

static int test_string_vars() {
    TEST("string variable set/get");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_str");
    ctx.SetStr("name", "Skaarj");
    CHECK(ctx.GetStr("name") == "Skaarj");
    CHECK(ctx.GetStr("nonexistent") == "");
    CHECK(ctx.GetStr("nonexistent", "default") == "default");
    PASS(); return 0; END_TEST();
}

static int test_pop_sound() {
    TEST("PopPendingSound returns and clears");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_pop_sound");
    CHECK(ctx.Load("play_sound \"explosion.wav\""));
    ctx.ExecuteNext();
    std::string sound = ctx.PopPendingSound();
    CHECK(sound == "explosion.wav");
    CHECK(ctx.PopPendingSound() == ""); // cleared
    PASS(); return 0; END_TEST();
}

static int test_pop_skybox() {
    TEST("PopPendingSkybox returns skybox path");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_pop_sky");
    CHECK(ctx.Load("set_skybox \"red_sun\""));
    ctx.ExecuteNext();
    std::string sky = ctx.PopPendingSkybox();
    CHECK(sky == "red_sun");
    PASS(); return 0; END_TEST();
}

static int test_restore_skybox() {
    TEST("restore_skybox clears pending skybox");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_restore_sky");
    CHECK(ctx.Load("set_skybox \"foo\"\nrestore_skybox"));
    ctx.ExecuteNext();
    CHECK(ctx.PopPendingSkybox() == "foo");
    ctx.ExecuteNext(); // restore
    CHECK(ctx.PopPendingSkybox() == ""); // should be empty
    PASS(); return 0; END_TEST();
}

static int test_rtflag_stores_result() {
    TEST("rtflag reads flag and stores in result");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_rtflag");
    CHECK(ctx.Load("wtflag 5 1\nrtflag 5"));
    ctx.ExecuteNext(); // wtflag 5 1
    CHECK_EQ(ctx.GetFlag(5), 1);
    ctx.ExecuteNext(); // rtflag 5 — stores in m_intVars["result"]
    CHECK_EQ(ctx.GetInt("result"), 1);
    PASS(); return 0; END_TEST();
}

static int test_empty_script() {
    TEST("empty script has no lines and no crash");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_empty");
    CHECK(ctx.Load(""));
    CHECK(!ctx.HasMore());
    CHECK_EQ(ctx.LineCount(), 0);
    PASS(); return 0; END_TEST();
}

static int test_reset_rewinds() {
    TEST("Reset() rewinds program counter");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_reset");
    CHECK(ctx.Load("$a = 1\n$a = 2\n$a = 3"));
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetInt("a"), 1);
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetInt("a"), 2);
    ctx.Reset();
    CHECK_EQ(ctx.ProgramCounter(), 0);
    ctx.ExecuteNext(); CHECK_EQ(ctx.GetInt("a"), 1); // back to start
    PASS(); return 0; END_TEST();
}

static int test_pc_values() {
    TEST("ProgramCounter returns correct values");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_pc");
    CHECK(ctx.Load("$a = 1\n$a = 2\n$a = 3"));
    CHECK_EQ(ctx.ProgramCounter(), 0);
    ctx.ExecuteNext(); CHECK_EQ(ctx.ProgramCounter(), 1);
    ctx.ExecuteNext(); CHECK_EQ(ctx.ProgramCounter(), 2);
    ctx.ExecuteNext(); CHECK_EQ(ctx.ProgramCounter(), 3);
    CHECK(!ctx.HasMore());
    PASS(); return 0; END_TEST();
}

static int test_find_unknown_label() {
    TEST("FindJumpLabel returns -1 for unknown");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_unknown_label");
    CHECK(ctx.Load("$a = 1\nloop:\n$a += 1"));
    CHECK_EQ(ctx.FindJumpLabel("nonexistent"), -1);
    CHECK(ctx.FindJumpLabel("loop") >= 0);
    PASS(); return 0; END_TEST();
}

static int test_set_fog() {
    TEST("set_fog stores fog vars");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_fog");
    CHECK(ctx.Load("set_fog 0.8 0.85 0.9 0.015"));
    ctx.ExecuteNext();
    CHECK_APROX(ctx.GetFloat("__fog_r"), 0.8f, 0.001f);
    CHECK_APROX(ctx.GetFloat("__fog_g"), 0.85f, 0.001f);
    CHECK_APROX(ctx.GetFloat("__fog_b"), 0.9f, 0.001f);
    CHECK_APROX(ctx.GetFloat("__fog_density"), 0.015f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_set_ambient() {
    TEST("set_ambient stores ambient vars");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_ambient");
    CHECK(ctx.Load("set_ambient 0.6 0.55 0.5"));
    ctx.ExecuteNext();
    CHECK_APROX(ctx.GetFloat("__ambient_r"), 0.6f, 0.001f);
    CHECK_APROX(ctx.GetFloat("__ambient_g"), 0.55f, 0.001f);
    CHECK_APROX(ctx.GetFloat("__ambient_b"), 0.5f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_restore_fog() {
    TEST("restore_fog clears fog vars");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_restore_fog");
    CHECK(ctx.Load("set_fog 0.8 0.85 0.9 0.015\nrestore_fog"));
    ctx.ExecuteNext();
    CHECK_APROX(ctx.GetFloat("__fog_r"), 0.8f, 0.001f);
    float r=0,g=0,b=0,d=0;
    CHECK(ctx.PopPendingFog(r,g,b,d));
    ctx.ExecuteNext(); // restore_fog
    CHECK(!ctx.PopPendingFog(r,g,b,d)); // should return false now
    PASS(); return 0; END_TEST();
}

static int test_restore_ambient() {
    TEST("restore_ambient clears ambient vars");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_restore_ambient");
    CHECK(ctx.Load("set_ambient 0.6 0.55 0.5\nrestore_ambient"));
    ctx.ExecuteNext();
    float r=0,g=0,b=0;
    CHECK(ctx.PopPendingAmbient(r,g,b));
    ctx.ExecuteNext(); // restore_ambient
    CHECK(!ctx.PopPendingAmbient(r,g,b)); // should return false now
    PASS(); return 0; END_TEST();
}

static int test_unknown_opcode_warns() {
    TEST("unknown opcode is skipped (no crash)");
    LightningScriptContext ctx;
    ctx.SetDebugTag("test_unknown");
    CHECK(ctx.Load("foobar_baz 1 2 3\n$a = 5"));
    ctx.ExecuteNext(); // unknown → skipped
    ctx.ExecuteNext(); // $a = 5
    CHECK_EQ(ctx.GetInt("a"), 5);
    PASS(); return 0; END_TEST();
}

int main() {
    fprintf(stdout, "LightningScriptContext Tests:\n");
    int failures = 0;
    failures += test_var_declaration();
    failures += test_assignment();
    failures += test_compound_assign();
    failures += test_if_condition();
    failures += test_say_opcode();
    failures += test_flags();
    failures += test_stop();
    failures += test_jump_label();
    failures += test_set_cooldown();
    failures += test_set_fog();
    failures += test_set_ambient();
    failures += test_restore_fog();
    failures += test_restore_ambient();
    failures += test_unknown_opcode_warns();
    failures += test_goto_jump();
    failures += test_else_branch();
    failures += test_else_branch_false();
    failures += test_string_vars();
    failures += test_pop_sound();
    failures += test_pop_skybox();
    failures += test_restore_skybox();
    failures += test_rtflag_stores_result();
    failures += test_empty_script();
    failures += test_reset_rewinds();
    failures += test_pc_values();
    failures += test_find_unknown_label();
    fprintf(stdout, "\n%d/%d tests passed.\n", tests_passed, tests_total);
    return failures;
}
