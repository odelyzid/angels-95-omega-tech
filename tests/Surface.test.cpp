// Surface flag registry + per-face surface properties — standalone, no raylib.
#include "../Source/World/SurfaceFlags.hpp"
#include <cstdio>
#include <cstring>
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
    // SURF_SPECIAL_LIT and SURF_FAKEBACKDROP were missing from this array even
    // though BOTH are reachable from the editor's Flags tab - a collision on an
    // untested bit is exactly the bug this test exists to catch, so the list must
    // cover every flag the dialog can set.
    const uint32_t all[] = {SURF_INVISIBLE, SURF_MASKED, SURF_TRANSLUCENT, SURF_ALPHABLEND,
        SURF_MODULATED, SURF_TWO_SIDED, SURF_UNLIT, SURF_FAKE_LIT, SURF_GLOW, SURF_PORTAL,
        SURF_MIRROR, SURF_ENVIRONMENT, SURF_PAN_U, SURF_PAN_V, SURF_SMALL_WAVY,
        SURF_BRIGHT_CORNERS, SURF_DIRTY_SHADOWS, SURF_SHADOW_HI, SURF_SHADOW_LO,
        SURF_NO_SMOOTH, SURF_NO_FOG, SURF_NO_BOUNDS_REJECT, SURF_NO_BSP_CUTS,
        SURF_ZONE_HACK, SURF_INVISIBLE_OCCLUDER, SURF_FORCE_VIEW_ZONE,
        SURF_SPECIAL_LIT, SURF_FAKEBACKDROP};
    bool unique = true;
    for (size_t i = 0; i < sizeof(all)/sizeof(all[0]); i++)
        for (size_t j = i + 1; j < sizeof(all)/sizeof(all[0]); j++)
            if (all[i] == all[j]) unique = false;
    check(unique, "all 28 surface flag bits are distinct");

    // No bit is a multi-bit alias: every flag is a single bit, because Surface.fs
    // tests them with `uSurfaceFlags & bit` and the exporter writes one integer.
    bool singleBit = true;
    for (uint32_t b : all) if (b == 0 || (b & (b - 1)) != 0) singleBit = false;
    check(singleBit, "every surface flag is exactly one bit wide");
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

// ---------------------------------------------------------------------------
// Legacy renderable mirror
// ---------------------------------------------------------------------------
// OzoneRenderable::texScaleU/V, texOffsetU/V and texPath are VIEWS onto
// surface.def. ExportToOzone emits the brush-wide UV fields from the mirrors and
// suppresses `surfTex=` whenever the mirror's texPath is non-empty - so a stale
// mirror is a silently discarded edit. SetRenderableFace used to re-derive
// surfaceFlags alone, and that was latent only because nothing could write
// surface.def; the editor's brush-wide scope is what made it live.
static void test_derive_legacy_surface_mirror() {
    SurfaceProps def;
    def.flags      = SURF_FAKEBACKDROP | SURF_GLOW;
    def.uvScaleU   = 2.0f;
    def.uvScaleV   = 3.0f;
    def.uvOffsetU  = 0.25f;
    def.uvOffsetV  = 0.5f;
    def.texPath    = "oztex/wall.png";

    const LegacySurfaceMirror m = DeriveLegacySurfaceMirror(def);
    check(m.surfaceFlags == (int)SURF_FAKEBACKDROP,
          "mirror carries ONLY the legacy pipeline bits, not GLOW");
    check(m.texScaleU == 2.0f && m.texScaleV == 3.0f,
          "mirror carries brush-wide U/V scale - the field the exporter reads");
    check(m.texOffsetU == 0.25f && m.texOffsetV == 0.5f,
          "mirror carries brush-wide U/V offset");
    check(m.texPath == "oztex/wall.png",
          "mirror carries the brush-wide texture path");

    // Clearing the surface texture must fall back to the tileset rather than
    // latching the previous path - otherwise you cannot undo a texture assign.
    def.texPath.clear();
    check(DeriveLegacySurfaceMirror(def).texPath.empty(),
          "clearing surface.texPath clears the mirror (falls back to the tileset)");

    // Pristine defaults must not invent values.
    const LegacySurfaceMirror pristine = DeriveLegacySurfaceMirror(SurfaceProps{});
    check(pristine.surfaceFlags == 0 && !NeedsFlagsKwarg(pristine.surfaceFlags),
          "a pristine surface derives a zero mirror and needs no flags= kwarg");
    check(pristine.texScaleU == 1.0f && pristine.texScaleV == 1.0f &&
          pristine.texOffsetU == 0.0f && pristine.texOffsetV == 0.0f &&
          pristine.texPath.empty(),
          "a pristine surface derives identity UVs and no texture");
}

// ---------------------------------------------------------------------------
// Flag/value conjunctions
// ---------------------------------------------------------------------------
// Four flags are inert without a companion value. Surface.fs gates on the flag
// (so a zero speed is a hard no-op rather than a per-frame divide), which means a
// ticked checkbox does nothing until the value is also set. These pin the rule so
// the editor's warning and the shader cannot drift apart.
static void test_inert_flag_predicates() {
    SurfaceProps p;

    p.Set(SURF_MASKED, true);
    check(!MaskedIsActive(p), "Masked with no cutoff is inert");
    p.alphaCutoff = 0.5f;
    check(MaskedIsActive(p), "Masked with a cutoff is active");

    p = SurfaceProps{};
    p.Set(SURF_GLOW, true);
    check(!GlowIsActive(p), "Glow with no colour is inert");
    p.glowB = 0.2f;   // only the blue channel set is enough
    check(GlowIsActive(p), "Glow with any non-zero channel is active");

    p = SurfaceProps{};
    p.Set(SURF_PAN_U, true);
    check(!PanIsActive(p), "U-Pan at zero speed is inert");
    p.panU = -0.05f;
    check(PanIsActive(p), "U-Pan at non-zero speed is active (negative is legal)");

    p = SurfaceProps{};
    p.Set(SURF_PAN_V, true);
    p.panV = 0.05f;
    check(PanIsActive(p), "V-Pan at non-zero speed is active");
    p.panU = 0.05f;
    check(PanIsActive(p), "PanIsActive covers either axis independently");

    // The warning must fire for each inert combination and stay quiet once fixed.
    const char* which = nullptr;
    p = SurfaceProps{};
    p.Set(SURF_MASKED, true);
    check(SurfaceHasInertFlags(p, &which) && which != nullptr,
          "SurfaceHasInertFlags reports a masked face with no cutoff");
    p.alphaCutoff = 0.5f;
    check(!SurfaceHasInertFlags(p, &which), "...and is quiet once the cutoff is set");

    p = SurfaceProps{};
    p.Set(SURF_GLOW, true);
    check(SurfaceHasInertFlags(p, &which), "reports a glow face with no colour");
    p.glowG = 1.0f;
    check(!SurfaceHasInertFlags(p, &which), "...and is quiet once a colour is set");

    // A face with no flags at all is never inert, whatever its values.
    p = SurfaceProps{};
    p.panU = 0.0f;
    check(!SurfaceHasInertFlags(p, &which),
          "a face with no pan flag is not inert just because its speed is zero");
}

// Seeding must make every flagged combination actually do something.
static void test_seed_inert_values() {
    SurfaceProps p;
    p.Set(SURF_MASKED, true);
    p.Set(SURF_GLOW, true);
    p.Set(SURF_PAN_U, true);
    p.Set(SURF_PAN_V, true);
    check(SurfaceHasInertFlags(p), "four flagged-but-inert companions to seed");

    check(SeedInertSurfaceValues(p) == 4, "seeds all four");
    check(MaskedIsActive(p) && GlowIsActive(p) && PanIsActive(p),
          "every previously inert flag is now active");
    check(p.alphaCutoff == 0.5f, "seeded cutoff is the conventional binary cut");
    check(p.glowR == 1.0f && p.glowG == 1.0f && p.glowB == 1.0f, "seeded glow is white");

    // Idempotent: a second pass must not overwrite authored values or report work.
    p.panU = 0.42f;
    check(SeedInertSurfaceValues(p) == 0, "a second seed pass finds nothing to do");
    check(p.panU == 0.42f, "an authored speed survives a later seed pass");

    // Unflagged values are never touched.
    SurfaceProps q;
    q.panU = 0.0f; q.alphaCutoff = 0.0f;
    check(SeedInertSurfaceValues(q) == 0, "an unflagged face is left alone");
    check(q.panU == 0.0f && q.alphaCutoff == 0.0f,
          "an unflagged face's zeros stay zero");
}

// ---------------------------------------------------------------------------
// Shader / header bit parity
// ---------------------------------------------------------------------------
// GameData/Shaders/Surface.fs duplicates the flag VALUES as `#define SF_*`, and
// AGENTS.md is explicit that renumbering a bit in the header means editing the
// shader too "or the flag silently does nothing". Nothing enforced that until now,
// so a renumber would have compiled cleanly, passed every other test here, and
// shipped a flag that did nothing.
//
// This reads the shader and compares each #define against the header constant.
// It is a file read rather than a compile-time check because the two values
// genuinely live in two languages.
static void test_shader_bit_parity() {
    struct Pair { const char* sfName; uint32_t bit; };
    // Every flag the shader gives a #define to. The shader does not need all 28:
    // bits with no GLSL (shadow hints, PORTAL/MIRROR/ENVIRONMENT) are CPU or
    // future-only, so their absence here is expected, not a failure.
    const Pair pairs[] = {
        { "SF_FAKEBACKDROP", SURF_FAKEBACKDROP },
        { "SF_INVISIBLE",   SURF_INVISIBLE   },
        { "SF_MASKED",      SURF_MASKED      },
        { "SF_TRANSLUCENT", SURF_TRANSLUCENT },
        { "SF_ALPHABLEND",  SURF_ALPHABLEND  },
        { "SF_MODULATED",   SURF_MODULATED   },
        { "SF_TWO_SIDED",   SURF_TWO_SIDED   },
        { "SF_UNLIT",       SURF_UNLIT       },
        { "SF_FAKE_LIT",    SURF_FAKE_LIT    },
        { "SF_GLOW",        SURF_GLOW        },
        { "SF_PAN_U",       SURF_PAN_U       },
        { "SF_PAN_V",       SURF_PAN_V       },
        { "SF_NO_SMOOTH",   SURF_NO_SMOOTH   },
        { "SF_NO_FOG",      SURF_NO_FOG      },
    };

    FILE* f = fopen("GameData/Shaders/Surface.fs", "rb");
    if (!f) {
        // Not a failure: the suite must still run from a build directory or a
        // checkout without GameData. Say so loudly so a silent skip is not
        // mistaken for a passing parity check.
        printf("  SKIP  shader bit parity (GameData/Shaders/Surface.fs not found)\n");
        return;
    }
    char line[512];
    int compared = 0, mismatched = 0;
    char firstBad[256] = {0};
    while (fgets(line, sizeof(line), f)) {
        for (const Pair& p : pairs) {
            char want[64];
            snprintf(want, sizeof(want), "#define %s", p.sfName);
            if (strncmp(line, want, strlen(want)) != 0) continue;
            // Form is `(1 << N)` - evaluate it rather than string-matching, so a
            // reformatted-but-correct shader still passes.
            const char* lp = strchr(line, '(');
            if (!lp) { ++mismatched; continue; }
            unsigned shift = 0;
            if (sscanf(lp, "( 1 << %u )", &shift) != 1) { ++mismatched; continue; }
            ++compared;
            if ((1u << shift) != p.bit) {
                ++mismatched;
                if (!firstBad[0])
                    snprintf(firstBad, sizeof(firstBad),
                             "%s is (1<<%u) in Surface.fs but (1<<%u) in SurfaceFlags.hpp",
                             p.sfName, shift,
                             (unsigned)(p.bit == 1 ? 0 : (p.bit & (p.bit - 1)) == 0 ? 31 - __builtin_clz(p.bit) : 0));
            }
            break;
        }
    }
    fclose(f);

    check(compared == (int)(sizeof(pairs) / sizeof(pairs[0])),
          "every SF_* the shader defines was found and parsed by the parity check");
    check(mismatched == 0,
          firstBad[0] ? firstBad : "every SF_* bit matches oz::surface::SURF_* exactly");
}

// ---------------------------------------------------------------------------
// Brush-wide default round trip (parse -> export shape)
// ---------------------------------------------------------------------------
// The exporter emits a brush-wide UV field from the legacy mirror and writes a
// per-face field only when it DIFFERS from the default, because the parser
// re-seeds a face from the default. That contract only holds if the mirrors and
// the default agree, which is what DeriveLegacySurfaceMirror exists to guarantee.
static void test_brush_wide_default_contract() {
    BrushSurface bs;
    // Brush-wide: the whole point of the brush-wide editor scope.
    bs.def.flags     = SURF_FAKEBACKDROP;
    bs.def.uvScaleU  = 2.0f;
    bs.def.panU      = 0.05f;

    const LegacySurfaceMirror m = DeriveLegacySurfaceMirror(bs.def);
    check(m.texScaleU == bs.def.uvScaleU,
          "the exported brush-wide U scale reads the same value the default holds");
    check((int)bs.def.flags == m.surfaceFlags,
          "the exported flags= equals the default's flags (this equality is what "
          "silently dropped every flags= before NeedsFlagsKwarg existed)");

    // An un-overridden face resolves to the default, so the exporter must write
    // NOTHING for it (p.flags == d.flags and so on).
    check(bs.Resolve(FACE_NX).flags == bs.def.flags &&
          bs.Resolve(FACE_NX).uvScaleU == bs.def.uvScaleU,
          "an untouched face resolves to the brush default, so the per-face diff is empty");

    // A face override that only changes one field must still inherit the rest,
    // so the exporter emits exactly one kwarg for it.
    SurfaceProps over = bs.Resolve(FACE_PY);
    over.alpha = 0.5f;
    bs.SetFace(FACE_PY, over);
    const SurfaceProps& r = bs.Resolve(FACE_PY);
    check(r.alpha == 0.5f && r.flags == bs.def.flags && r.uvScaleU == bs.def.uvScaleU,
          "a face override inherits every field it does not change");
    check(r.flags != bs.def.flags || r.alpha != bs.def.alpha,
          "so only the changed field differs, which is what the exporter keys on");

    // ResetToDefault KEEPS def - the editor's brush-wide Reset therefore cannot
    // reuse it, which is why OzoneLoader::ResetRenderableSurfaceDefault exists.
    bs.ClearFace(FACE_PY);
    bs.ResetToDefault();
    check(bs.def.flags == SURF_FAKEBACKDROP && bs.def.uvScaleU == 2.0f,
          "ResetToDefault keeps the brush default (so it is NOT the brush-wide reset)");
}

int main() {
    printf("Surface tests:\n");
    test_legacy_bits_stable();
    test_legacy_flag_derivation();
    test_shader_bit_parity();
    test_derive_legacy_surface_mirror();
    test_inert_flag_predicates();
    test_seed_inert_values();
    test_brush_wide_default_contract();
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
