// OzAnim vertex-keyframe format tests — standalone, no raylib dependency.
#include "../Source/Anim/OzAnimFormat.hpp"
#include <cstdio>
#include <cmath>

static int tests_total = 0, tests_passed = 0;
#define TEST(name) do { tests_total++; fprintf(stdout, "  TEST: %s ... ", name);
#define PASS() do { tests_passed++; fprintf(stdout, "PASS\n"); } while(0)
#define FAIL(msg) do { fprintf(stdout, "FAIL: %s\n", msg); return 1; } while(0)
#define CHECK(cond) do { if (!(cond)) { fprintf(stdout, "FAIL: %s\n", #cond); return 1; } } while(0)
#define CHECK_APROX(a, b, eps) do { float d = (a) - (b); if (d < 0) d = -d; if (d > (eps)) { \
    fprintf(stdout, "FAIL: expected %f, got %f\n", (float)(b), (float)(a)); return 1; } } while(0)
#define END_TEST() } while(0)

static const char* kSample =
    "# comment\n"
    "ozanim 1\n"
    "clip \"Idle\" fps 30 loop 1\n"
    "key 0.0\n"
    "  v 2 0.0 0.0 0.0\n"
    "  v 5 1.0 0.0 0.0\n"
    "key 1.0\n"
    "  v 5 3.0 0.0 0.0   // move +x\n";

static int test_parse() {
    TEST("parse clip/keys/offsets");
    ozanim::Animation a = ozanim::Parse(kSample);
    CHECK(a.clips.size() == 1);
    const ozanim::Clip& c = a.clips[0];
    CHECK(c.name == "Idle");
    CHECK_APROX(c.fps, 30.0f, 0.001f);
    CHECK(c.loop);
    CHECK(c.keys.size() == 2);
    CHECK_APROX(c.Duration(), 1.0f, 0.001f);
    CHECK(c.keys[0].offsets.size() == 2);
    CHECK(c.keys[1].offsets.size() == 1);
    CHECK(c.keys[1].offsets[0].index == 5);
    CHECK_APROX(c.keys[1].offsets[0].dx, 3.0f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_sample() {
    TEST("SampleOffsets lerp + clamp");
    ozanim::Animation a = ozanim::Parse(kSample);
    const ozanim::Clip& c = a.clips[0];
    std::vector<float> out;

    c.SampleOffsets(0.0f, 8, out);
    CHECK(out.size() == 24);
    CHECK_APROX(out[5 * 3 + 0], 1.0f, 0.001f);   // v5 at t0
    CHECK_APROX(out[2 * 3 + 0], 0.0f, 0.001f);   // v2 at t0

    c.SampleOffsets(0.5f, 8, out);
    CHECK_APROX(out[5 * 3 + 0], 2.0f, 0.001f);   // halfway 1 -> 3

    // Looping: t == duration wraps back to t == 0.
    c.SampleOffsets(1.0f, 8, out);
    CHECK_APROX(out[5 * 3 + 0], 1.0f, 0.001f);

    // loop wraps 1.5 -> 0.5 (duration 1.0)
    c.SampleOffsets(1.5f, 8, out);
    CHECK_APROX(out[5 * 3 + 0], 2.0f, 0.001f);

    // Non-looping clamps to the last key.
    ozanim::Animation a2 = ozanim::Parse(kSample);
    a2.clips[0].loop = false;
    a2.clips[0].SampleOffsets(1.0f, 8, out);
    CHECK_APROX(out[5 * 3 + 0], 3.0f, 0.001f);
    a2.clips[0].SampleOffsets(5.0f, 8, out);
    CHECK_APROX(out[5 * 3 + 0], 3.0f, 0.001f);
    PASS(); return 0; END_TEST();
}

static int test_roundtrip() {
    TEST("Serialize -> Parse round-trip");
    ozanim::Animation a = ozanim::Parse(kSample);
    std::string text = ozanim::Serialize(a);
    ozanim::Animation b = ozanim::Parse(text);
    CHECK(b.clips.size() == 1);
    CHECK(b.clips[0].keys.size() == 2);
    CHECK(b.clips[0].keys[1].offsets.size() == 1);
    CHECK_APROX(b.clips[0].keys[1].offsets[0].dx, 3.0f, 0.001f);
    CHECK(b.clips[0].name == "Idle");
    PASS(); return 0; END_TEST();
}

static int test_empty() {
    TEST("empty / no-clip input is safe");
    ozanim::Animation a = ozanim::Parse("");
    CHECK(a.clips.empty());
    std::vector<float> out;
    ozanim::Clip c;
    CHECK(c.SampleOffsets(0.0f, 0, out) == false);
    CHECK(c.SampleOffsets(0.0f, 4, out) == true);
    CHECK(out.size() == 12);
    PASS(); return 0; END_TEST();
}

int main() {
    fprintf(stdout, "OzAnim Tests\n");
    int failures = 0;
    failures += test_parse();
    failures += test_sample();
    failures += test_roundtrip();
    failures += test_empty();
    fprintf(stdout, "%d/%d passed, %d failed\n", tests_passed, tests_total, tests_total - tests_passed);
    return failures;
}
