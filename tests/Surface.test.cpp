// Surface flag registry + per-face surface properties — standalone, no raylib.
#include "../Source/World/SurfaceFlags.hpp"
#include <cstdio>
#include <string>

using namespace oz::surface;

static int test_count = 0, pass_count = 0;

static void check(bool ok, const char* what) {
    test_count++;
    if (ok) { pass_count++; printf("  PASS  %s\n", what); }
    else     { printf("  FAIL  %s\n", what); }
}

// ---------------------------------------------------------------------------
// Bit allocation
// ---------------------------------------------------------------------------
// The two pre-existing, already-shipped bits must never move: the world's 299
// `flags=8` painted backdrops and every AutoConvex proxy depend on them.
static void test_legacy_bits_stable() {
    check(SURF_FAKEBACKDROP == 8, "SURF_FAKEBACKDROP stays bit 3 (flags=8 in shipped worlds)");
    check(SURF_COLLISION_PROXY == 16, "SURF_COLLISION_PROXY stays bit 4");
    // New flags must not collide with them.
    uint32_t newBits = SURF_INVISIBLE | SURF_MASKED | SURF_TRANSLUCENT | SURF_ALPHABLEND |
                       SURF_MODULATED | SURF_TWO_SIDED | SURF_UNLIT | SURF_FAKE_LIT |
                       SURF_GLOW | SURF_PORTAL | SURF_MIRROR | SURF_ENVIRONMENT |
                       SURF_PAN_U | SURF_PAN_V | SURF_SMALL_WAVY | SURF_BRIGHT_CORNERS |
                       SURF_DIRTY_SHADOWS | SURF_SHADOW_HI | SURF_SHADOW_LO | SURF_NO_SMOOTH |
                       SURF_NO_FOG | SURF_NO_BOUNDS_REJECT | SURF_NO_BSP_CUTS |
                       SURF_ZONE_HACK | SURF_INVISIBLE_OCCLUDER | SURF_FORCE_VIEW_ZONE;
    check((newBits & (SURF_FAKEBACKDROP | SURF_COLLISION_PROXY)) == 0,
          "no new surface flag overlaps the legacy bits");
    // Every bit unique (a duplicate would silently make one checkbox untoggleable).
    const uint32_t all[] = {SURF_INVISIBLE, SURF_MASKED, SURF_TRANSLUCENT, SURF_ALPHABLEND,
        SURF_MODULATED, SURF_TWO_SIDED, SURF_UNLIT, SURF_FAKE_LIT, SURF_GLOW, SURF_PORTAL,
        SURF_MIRROR, SURF_ENVIRONMENT, SURF_PAN_U, SURF_PAN_V, SURF_SMALL_WAVY,
        SURF_BRIGHT_CORNERS, SURF_DIRTY_SHADOWS, SURF_SHADOW_HI, SURF_SHADOW_LO,
        SURF_NO_SMOOTH, SURF_NO_FOG, SURF_NO_BOUNDS_REJECT, SURF_NO_BSP_CUTS,
        SURF_ZONE_HACK, SURF_INVISIBLE_OCCLUDER, SURF_FORCE_VIEW_ZONE};
    bool unique = true;
    for (size_t i = 0; i < sizeof(all)/sizeof(all[0]); i++)
        for (size_t j = i + 1; j < sizeof(all)/sizeof(all[0]); j++)
            if (all[i] == all[j]) unique = false;
    check(unique, "all 26 surface flag bits are distinct");
}

// ---------------------------------------------------------------------------
// Face mapping
// ---------------------------------------------------------------------------
static void test_face_from_normal() {
    check(FaceFromNormal( 1, 0, 0) == FACE_PX, "normal +X -> px");
    check(FaceFromNormal(-1, 0, 0) == FACE_NX, "normal -X -> nx");
    check(FaceFromNormal( 0, 1, 0) == FACE_PY, "normal +Y -> py");
    check(FaceFromNormal( 0,-1, 0) == FACE_NY, "normal -Y -> ny");
    check(FaceFromNormal( 0, 0, 1) == FACE_PZ, "normal +Z -> pz");
    check(FaceFromNormal( 0, 0,-1) == FACE_NZ, "normal -Z -> nz");
    // Dominant axis, not first non-zero: a mostly-vertical wall with a little
    // horizontal tilt must land on its vertical face.
    check(FaceFromNormal(0.3f, 0.95f, 0.1f) == FACE_PY, "dominant axis wins over first non-zero");
    // Ties must be deterministic, not depend on rounding.
    check(FaceFromNormal(1,1,0) == FaceFromNormal(1,1,0), "45-degree tie is stable");
    check(FaceFromNormal(0,0,0) != FACE_NONE, "degenerate normal still maps to a face");
}

static void test_face_names() {
    check(FaceFromName("pz") == FACE_PZ, "FaceFromName pz");
    check(FaceFromName("+x") == FACE_PX, "FaceFromName accepts +x");
    check(FaceFromName("-Z") == FACE_NZ, "FaceFromName accepts -Z");
    check(FaceFromName("bogus") == FACE_NONE, "unknown face name -> FACE_NONE");
    check(std::string(FaceName(FACE_NY)) == "ny", "FaceName round-trip ny");
    // Round-trip every face.
    bool rt = true;
    for (int f = 0; f < FACE_COUNT; f++)
        if (FaceFromName(FaceName((SurfaceFace)f)) != (SurfaceFace)f) rt = false;
    check(rt, "FaceName/FaceFromName round-trip all six faces");
}

// ---------------------------------------------------------------------------
// SurfaceProps
// ---------------------------------------------------------------------------
static void test_props_is_non_default() {
    SurfaceProps p;
    check(!p.IsNonDefault(), "a pristine SurfaceProps is the default");
    p.flags = SURF_UNLIT;
    check(p.IsNonDefault(), "any flag makes it non-default");
    p = SurfaceProps{}; p.uvScaleU = 2.0f;
    check(p.IsNonDefault(), "uvScale makes it non-default");
    p = SurfaceProps{}; p.panU = 0.5f;
    check(p.IsNonDefault(), "pan makes it non-default");
    p = SurfaceProps{}; p.alphaCutoff = 0.5f;
    check(p.IsNonDefault(), "alphaCutoff makes it non-default");
    p = SurfaceProps{}; p.glowR = 1.0f;
    check(p.IsNonDefault(), "glow colour makes it non-default");
    p = SurfaceProps{}; p.texSlot = 2;
    check(p.IsNonDefault(), "texSlot makes it non-default");
}

static void test_props_set_clear() {
    SurfaceProps p;
    p.Set(SURF_MASKED, true);
    check(p.Has(SURF_MASKED), "Set then Has");
    p.Set(SURF_UNLIT, true);
    check(p.Has(SURF_MASKED) && p.Has(SURF_UNLIT), "two independent bits set");
    p.Set(SURF_MASKED, false);
    check(!p.Has(SURF_MASKED) && p.Has(SURF_UNLIT), "clearing one bit leaves the other");
    p.Set(SURF_UNLIT, false);
    check(p.flags == 0, "clearing everything returns to zero");
}

// ---------------------------------------------------------------------------
// BrushSurface precedence + selection
// ---------------------------------------------------------------------------
static void test_brush_default_and_override() {
    BrushSurface b;
    b.def.flags = SURF_UNLIT;
    check(b.Resolve(FACE_PX).flags == SURF_UNLIT, "un-overridden face resolves to the default");
    SurfaceProps f;
    f.flags = SURF_GLOW;
    b.SetFace(FACE_PZ, f);
    check(b.IsFaceOverridden(FACE_PZ), "face marked overridden");
    check(b.Resolve(FACE_PZ).flags == SURF_GLOW, "override wins over the default");
    check(b.Resolve(FACE_PX).flags == SURF_UNLIT, "other faces still see the default");
    // A partial face override inherits the rest of the default (this is what
    // lets the parser seed a face from the brush default).
    SurfaceProps partial;
    partial.flags = b.def.flags;      // author only meant to change the texture
    partial.texSlot = 4;
    b.SetFace(FACE_NY, partial);
    check(b.Resolve(FACE_NY).texSlot == 4, "partial override keeps the default flags");
}

static void test_brush_needs_per_face() {
    BrushSurface b;
    check(!b.NeedsPerFaceDraw(), "a bare brush stays on the fast path");
    b.def.flags = SURF_UNLIT;
    check(b.NeedsPerFaceDraw(), "a non-default brush default needs the surface path");
    BrushSurface c;
    SurfaceProps f; f.alpha = 0.5f;
    c.SetFace(FACE_PX, f);
    check(c.NeedsPerFaceDraw(), "one overridden face is enough");
    BrushSurface d;
    d.SetFace(FACE_PX, SurfaceProps{});   // override that equals the default
    check(d.NeedsPerFaceDraw(), "an explicit override is honoured even when equal");
}

static void test_clear_and_reset() {
    BrushSurface b;
    b.def.flags = SURF_UNLIT;
    SurfaceProps f; f.flags = SURF_GLOW;
    b.SetFace(FACE_PX, f);
    b.SetFace(FACE_PY, f);
    check(b.IsFaceOverridden(FACE_PX) && b.IsFaceOverridden(FACE_PY), "two faces overridden");
    b.ClearFace(FACE_PX);
    check(!b.IsFaceOverridden(FACE_PX) && b.IsFaceOverridden(FACE_PY), "ClearFace removes one");
    check(b.Resolve(FACE_PX).flags == SURF_UNLIT, "cleared face falls back to the default");
    b.ResetToDefault();
    check(!b.NeedsPerFaceDraw() || b.def.flags == SURF_UNLIT,
          "ResetToDefault keeps the brush default and drops overrides");
    check(!b.IsFaceOverridden(FACE_PY), "ResetToDefault clears overrides");
    check(b.SelectedCount() == 0, "ResetToDefault clears the selection");
}

static void test_selection() {
    BrushSurface b;
    b.SelectOnly(FACE_NZ);
    check(b.SelectedCount() == 1 && b.IsSelected(FACE_NZ), "SelectOnly");
    b.AddToSelection(FACE_PX);
    check(b.SelectedCount() == 2, "AddToSelection");
    b.AddToSelection(FACE_PX);
    check(b.SelectedCount() == 2, "AddToSelection is idempotent");
    b.ToggleSelection(FACE_PX);
    check(!b.IsSelected(FACE_PX) && b.SelectedCount() == 1, "ToggleSelection removes");
    b.ToggleSelection(FACE_NX);
    check(b.SelectedCount() == 2, "ToggleSelection adds");
    b.ClearSelection();
    check(b.SelectedCount() == 0, "ClearSelection");
}

static void test_apply_to_selection() {
    BrushSurface b;
    SurfaceProps p; p.flags = SURF_MASKED; p.alphaCutoff = 0.25f;

    // Empty selection must be a no-op: the dialog must never silently write
    // "all six faces" because the user forgot to select one.
    ApplyToSelection(b, p);
    check(b.SelectedCount() == 0 && !b.NeedsPerFaceDraw(),
          "ApplyToSelection with no selection writes nothing");

    b.SelectOnly(FACE_PX);
    ApplyToSelection(b, p);
    check(b.IsFaceOverridden(FACE_PX) && b.Resolve(FACE_PX).alphaCutoff == 0.25f,
          "ApplyToSelection writes the selected face");

    b.ClearSelection();
    b.SelectOnly(FACE_PX);
    b.AddToSelection(FACE_NX);
    b.AddToSelection(FACE_PZ);
    ApplyToSelection(b, p);
    check(b.IsFaceOverridden(FACE_PX) && b.IsFaceOverridden(FACE_NX) &&
          b.IsFaceOverridden(FACE_PZ) && !b.IsFaceOverridden(FACE_PY),
          "ApplyToSelection writes every selected face and no others");
}

// ---------------------------------------------------------------------------
// Legacy-flag derivation + the exporter's `flags=` kwarg decision
// ---------------------------------------------------------------------------
// Both halves of a real data-loss bug, pinned together because they are the same
// invariant: surface.def.flags OWNS the flags, the legacy mirror is derived from
// it, and the exporter must read the owner.
//
// The exporter used to write `flags=` only when `def.flags != legacyFlags`. Since
// legacyFlags is derived FROM def.flags, that is false for every brush that came
// through the loader — so no brush line ever carried `flags=` again, and
// re-exporting a level deleted every painted backdrop and every AutoConvex
// collision proxy in it. GameData/Worlds/TestMap lost all 230 of its proxies that
// way.
static void test_legacy_flag_derivation() {
    check(kLegacyPipelineFlags == (SURF_FAKEBACKDROP | SURF_COLLISION_PROXY |
                                   SURF_INVISIBLE),
          "kLegacyPipelineFlags is exactly the three pipeline-visible flags");

    SurfaceProps def;
    def.flags = SURF_COLLISION_PROXY;
    check(DeriveLegacyFlags(def) == (int)SURF_COLLISION_PROXY,
          "a collision proxy derives legacyFlags = 16");

    // The mirror is a SUBSET: a decorative flag must not be able to make a brush
    // invisible or carve it out of CSG by accident.
    def.flags = SURF_COLLISION_PROXY | SURF_GLOW | SURF_ENVIRONMENT;
    check(DeriveLegacyFlags(def) == (int)SURF_COLLISION_PROXY,
          "decorative flags are NOT mirrored into legacyFlags");

    // And the exporter's predicate reads the owner only.
    check(NeedsFlagsKwarg(SURF_COLLISION_PROXY),
          "NeedsFlagsKwarg(16) - a proxy needs its flags= kwarg");
    check(NeedsFlagsKwarg(SURF_FAKEBACKDROP),
          "NeedsFlagsKwarg(8) - a backdrop needs its flags= kwarg");
    check(NeedsFlagsKwarg(SURF_FAKEBACKDROP | SURF_COLLISION_PROXY),
          "NeedsFlagsKwarg(24) - a combined mask is emitted");
    check(!NeedsFlagsKwarg(0),
          "NeedsFlagsKwarg(0) - a plain brush writes no flags= kwarg");

    // The exact shape that regressed: owner set, mirror derived from it. The old
    // guard compared these two and so emitted nothing.
    def.flags = SURF_COLLISION_PROXY;
    const int legacy = DeriveLegacyFlags(def);
    check(def.flags == (uint32_t)legacy,
          "after derivation, owner == mirror (this equality is what killed the old guard)");
    check(NeedsFlagsKwarg(def.flags),
          "...and NeedsFlagsKwarg still emits, so the round trip survives");
}

int main() {
    printf("Surface tests:\n");
    test_legacy_bits_stable();
    test_legacy_flag_derivation();
    test_face_from_normal();
    test_face_names();
    test_props_is_non_default();
    test_props_set_clear();
    test_brush_default_and_override();
    test_brush_needs_per_face();
    test_clear_and_reset();
    test_selection();
    test_apply_to_selection();
    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
