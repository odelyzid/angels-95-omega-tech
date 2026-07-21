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
    failures += test_unknown_opcode_warns();
    fprintf(stdout, "\n%d/%d tests passed.\n", tests_passed, tests_total);
    return failures;
}
