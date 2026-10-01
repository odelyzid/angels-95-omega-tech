// AutoConvex test - standalone, no raylib dependency.
//   make test_autoconvex
//   g++ -std=c++20 tests/AutoConvex.test.cpp Source/Physics/AutoConvex.cpp
//
#include "../Source/Physics/AutoConvex.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

// Axis-aligned unit cube centred on the origin, as a triangle soup.
static std::vector<float> UnitBoxVerts(float sx, float sy, float sz) {
    float hx = sx * 0.5f, hy = sy * 0.5f, hz = sz * 0.5f;
    float v[8][3] = {
        {-hx,-hy,-hz}, { hx,-hy,-hz}, { hx, hy,-hz}, {-hx, hy,-hz},
        {-hx,-hy, hz}, { hx,-hy, hz}, { hx, hy, hz}, {-hx, hy, hz},
    };
    const int faces[12][3] = {
        {0,1,2},{0,2,3},   // -Z
        {4,6,5},{4,7,6},   // +Z
        {0,4,5},{0,5,1},   // -Y
        {3,2,6},{3,6,7},   // +Y
        {0,3,7},{0,7,4},   // -X
        {1,5,6},{1,6,2},   // +X
    };
    std::vector<float> out;
    out.reserve(36 * 3);
    for (const auto& f : faces) {
        for (int k = 0; k < 3; k++) {
            out.push_back(v[f[k]][0]);
            out.push_back(v[f[k]][1]);
            out.push_back(v[f[k]][2]);
        }
    }
    return out;
}

// Union bounds of a box list.
static void BoundsOf(const std::vector<ConvexBox>& b, float out[6]) {
    out[0] = out[1] = out[2] = 1e30f;
    out[3] = out[4] = out[5] = -1e30f;
    for (const auto& c : b) {
        out[0] = std::min(out[0], c.minX); out[3] = std::max(out[3], c.maxX);
        out[1] = std::min(out[1], c.minY); out[4] = std::max(out[4], c.maxY);
        out[2] = std::min(out[2], c.minZ); out[5] = std::max(out[5], c.maxZ);
    }
}

static void TestRejectsBadInput() {
    printf("AutoConvex: input validation\n");
    std::vector<float> box = UnitBoxVerts(2, 2, 2);
    std::vector<ConvexBox> out;

    CHECK(AutoConvex_Build(nullptr, 0, AutoConvexParams{}, out) == 0);
    CHECK(AutoConvex_Build(box.data(), 0, AutoConvexParams{}, out) == 0);
    CHECK(AutoConvex_Build(box.data(), 2, AutoConvexParams{}, out) == 0);
    CHECK(out.empty());

    AutoConvexParams bad;
    bad.cellSize = 0.0f;
    CHECK(AutoConvex_Build(box.data(), (int)box.size(), bad, out) == 0);
    CHECK(out.empty());

    bad.cellSize = -1.0f;
    CHECK(AutoConvex_Build(box.data(), (int)box.size(), bad, out) == 0);
    CHECK(out.empty());
}

// A 2x2x2 box on a 0.5 grid is 4x4x4 = 64 cells, but only the 56 SHELL cells
// touch a triangle: the 2x2x2 interior is enclosed by the mesh and no surface
// passes through it. That is correct for collision - a hollow shell you cannot
// enter beats a filled box that swallows the room behind the prop.
static void TestFillsSolidBox() {
    printf("AutoConvex: box produces a closed shell\n");
    std::vector<float> box = UnitBoxVerts(2, 2, 2);
    AutoConvexParams p;
    p.cellSize = 0.5f;
    p.grow = 0.0f;  // grow is applied symmetrically; check it separately

    std::vector<ConvexBox> out;
    int n = AutoConvex_Build(box.data(), (int)box.size(), p, out);
    CHECK(n == 56);
    CHECK((int)out.size() == n);

    float b[6];
    BoundsOf(out, b);
    CHECK(std::fabs(b[0] + 1.0f) < 1e-4f);
    CHECK(std::fabs(b[3] - 1.0f) < 1e-4f);
    CHECK(std::fabs(b[1] + 1.0f) < 1e-4f);
    CHECK(std::fabs(b[4] - 1.0f) < 1e-4f);
    CHECK(std::fabs(b[2] + 1.0f) < 1e-4f);
    CHECK(std::fabs(b[5] - 1.0f) < 1e-4f);

    // No cell may sit strictly inside the source volume - that would mean the
    // proxy set is fatter than the prop it is standing in for.
    for (const auto& c : out) {
        bool inside = c.minX > -0.9f && c.maxX < 0.9f &&
                      c.minY > -0.9f && c.maxY < 0.9f &&
                      c.minZ > -0.9f && c.maxZ < 0.9f;
        CHECK(!inside);
    }
}

// Cell snapping is outward, so an odd-sized source still lands on the grid.
static void TestSnapsOutwardToGrid() {
    printf("AutoConvex: grid snaps outward\n");
    // 1.5-unit box on a 1.0 grid spans two cells per axis.
    std::vector<float> box = UnitBoxVerts(1.5f, 1.5f, 1.5f);
    AutoConvexParams p;
    p.cellSize = 1.0f;
    p.grow = 0.0f;

    std::vector<ConvexBox> out;
    CHECK(AutoConvex_Build(box.data(), (int)box.size(), p, out) == 8);

    float b[6];
    BoundsOf(out, b);
    CHECK(std::fabs(b[0] + 1.0f) < 1e-4f);
    CHECK(std::fabs(b[3] - 1.0f) < 1e-4f);
}

// grow widens every box, so the union is strictly larger than the source AABB.
static void TestGrowWidensBoxes() {
    printf("AutoConvex: grow inflates\n");
    std::vector<float> box = UnitBoxVerts(2, 2, 2);
    AutoConvexParams p;
    p.cellSize = 1.0f;
    p.grow = 0.25f;

    std::vector<ConvexBox> out;
    CHECK(AutoConvex_Build(box.data(), (int)box.size(), p, out) == 8);
    float b[6];
    BoundsOf(out, b);
    CHECK(std::fabs(b[0] - (-1.25f)) < 1e-4f);
    CHECK(std::fabs(b[3] - 1.25f) < 1e-4f);
}

// Two parallel quads 2 units apart with nothing between them. A cheap
// triangle-AABB test would fill the gap and make it solid; the real
// triangle/AABB test must leave it empty, or the player would be walled off
// from the room behind the fence.
static void TestDoesNotFillHollows() {
    printf("AutoConvex: hollow interior stays empty\n");
    // Two full 2x2 quads at z = -1 and z = +1, facing each other with a 2-unit
    // gap between them. A cheap triangle-AABB test would fill that gap and wall
    // the player off from the far side; the real triangle test must leave it
    // empty. Each quad is two triangles.
    const float v[12][3] = {
        {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},   // z = -1
        {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1,-1, 1}, { 1, 1, 1}, {-1, 1, 1},   // z = +1
    };
    std::vector<float> verts;
    for (int i = 0; i < 12; i++) {
        verts.push_back(v[i][0]);
        verts.push_back(v[i][1]);
        verts.push_back(v[i][2]);
    }

    AutoConvexParams p;
    p.cellSize = 0.25f;
    p.grow = 0.0f;

    std::vector<ConvexBox> out;
    AutoConvex_Build(verts.data(), (int)verts.size(), p, out);

    // 8x8 cells across each quad and only the single cell layer touching each
    // plane: 2 * 64 = 128. The whole middle of the box must stay empty.
    CHECK((int)out.size() == 128);
    int middle = 0;
    for (const auto& c : out)
        if (c.maxZ < 0.5f && c.minZ > -0.5f) middle++;
    CHECK(middle == 0);
}

// maxBoxes must abort the whole build, never emit a partial collision hull.
static void TestMaxBoxesRefuses() {
    printf("AutoConvex: maxBoxes refuses instead of truncating\n");
    std::vector<float> box = UnitBoxVerts(2, 2, 2);
    AutoConvexParams p;
    p.cellSize = 0.25f;   // 8^3 = 512 cells
    p.grow = 0.0f;
    p.maxBoxes = 10;

    std::vector<ConvexBox> out;
    CHECK(AutoConvex_Build(box.data(), (int)box.size(), p, out) == 0);
    CHECK(out.empty());
}

// CountCells must agree with what Build would actually emit, otherwise the
// editor's pre-flight number lies.
static void TestCountMatchesBuild() {
    printf("AutoConvex: count matches build\n");
    std::vector<float> box = UnitBoxVerts(3, 1, 2);
    for (float cell : {0.25f, 0.5f, 1.0f, 2.0f}) {
        AutoConvexParams p;
        p.cellSize = cell;
        p.grow = 0.0f;
        p.maxBoxes = 100000;
        std::vector<ConvexBox> out;
        int built = AutoConvex_Build(box.data(), (int)box.size(), p, out);
        int counted = AutoConvex_CountCells(box.data(), (int)box.size(), p);
        CHECK(built == counted);
    }
}

// An absurd extent must be refused outright rather than hanging the editor.
static void TestRefusesAbsurdGrid() {
    printf("AutoConvex: absurd grid refused\n");
    std::vector<float> huge = UnitBoxVerts(100000, 100000, 100000);
    AutoConvexParams p;
    p.cellSize = 0.01f;
    p.grow = 0.0f;
    std::vector<ConvexBox> out;
    CHECK(AutoConvex_Build(huge.data(), (int)huge.size(), p, out) == 0);
    CHECK(out.empty());
}

// Flat (zero-thickness) geometry is normal for planes, card meshes and flat
// brushes; it must still produce collision or the player falls through a floor.
// A zero-thickness axis gets ONE cell centred on the geometry (floor/ceil of an
// equal min and max is zero cells, which silently returned nothing).
static void TestFlatGeometryProducesCollision() {
    printf("AutoConvex: flat geometry still collides\n");
    std::vector<float> quad = {
        -1,0,-1,  1,0,-1,  1,0,1,
        -1,0, 1,  1,0, 1, -1,0,-1,
    };
    AutoConvexParams p;
    p.cellSize = 1.0f;
    p.grow = 0.0f;
    std::vector<ConvexBox> out;
    CHECK(AutoConvex_Build(quad.data(), (int)quad.size(), p, out) == 4);

    // The quad lies in the XZ plane, so Y is the thin axis: it must get exactly
    // one cell STRADDLING the plane (2 x-cells x 2 z-cells), not collapse to
    // zero height.
    for (const auto& c : out) CHECK(c.maxY > c.minY);
    float yMin = 1e30f, yMax = -1e30f;
    for (const auto& c : out) { yMin = std::min(yMin, c.minY); yMax = std::max(yMax, c.maxY); }
    CHECK(std::fabs(yMin + 0.5f) < 1e-4f);
    CHECK(std::fabs(yMax - 0.5f) < 1e-4f);
}

int main() {
    TestRejectsBadInput();
    TestFillsSolidBox();
    TestSnapsOutwardToGrid();
    TestGrowWidensBoxes();
    TestDoesNotFillHollows();
    TestMaxBoxesRefuses();
    TestCountMatchesBuild();
    TestRefusesAbsurdGrid();
    TestFlatGeometryProducesCollision();

    printf("\nAutoConvex: %d/%d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail == 0 ? 0 : 1;
}