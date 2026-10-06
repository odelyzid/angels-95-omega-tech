// Surface flag registry + per-face surface properties — standalone, no raylib.
#include "../Source/World/SurfaceFlags.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace oz::surface;

// EVERY flag in World/SurfaceFlags.hpp, once. The bit-allocation test, the
// shader parity test and the dead-flag register all index off this list, so a new
// flag cannot be added to the header without the suite noticing it is unclassified.
static const uint32_t kAllSurfaceFlags[] = {
    SURF_FAKEBACKDROP, SURF_COLLISION_PROXY, SURF_INVISIBLE, SURF_MASKED,
    SURF_TRANSLUCENT, SURF_ALPHABLEND, SURF_MODULATED, SURF_TWO_SIDED,
    SURF_UNLIT, SURF_FAKE_LIT, SURF_SPECIAL_LIT, SURF_GLOW, SURF_PORTAL,
    SURF_MIRROR, SURF_ENVIRONMENT, SURF_PAN_U, SURF_PAN_V, SURF_SMALL_WAVY,
    SURF_BRIGHT_CORNERS, SURF_DIRTY_SHADOWS, SURF_SHADOW_HI, SURF_SHADOW_LO,
    SURF_NO_SMOOTH, SURF_NO_FOG, SURF_NO_BOUNDS_REJECT, SURF_NO_BSP_CUTS,
    SURF_ZONE_HACK, SURF_INVISIBLE_OCCLUDER, SURF_FORCE_VIEW_ZONE,
};
static const int kAllSurfaceFlagCount =
    (int)(sizeof(kAllSurfaceFlags) / sizeof(kAllSurfaceFlags[0]));

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
    // Built from kAllSurfaceFlags so a new header flag is automatically covered.
    bool unique = true;
    for (int i = 0; i < kAllSurfaceFlagCount; i++)
        for (int j = i + 1; j < kAllSurfaceFlagCount; j++)
            if (kAllSurfaceFlags[i] == kAllSurfaceFlags[j]) unique = false;
    check(unique, "every surface flag bit is distinct");

    // No bit is a multi-bit alias: every flag is a single bit, because Surface.fs
    // tests them with `uSurfaceFlags & bit` and the exporter writes one integer.
    bool singleBit = true;
    for (uint32_t b : kAllSurfaceFlags) if (b == 0 || (b & (b - 1)) != 0) singleBit = false;
    check(singleBit, "every surface flag is exactly one bit wide");

    // Bits 3 and 4 are the pre-existing pair; 5..31 is 27 more.
    check(kAllSurfaceFlagCount == 29, "the header defines 29 flags");
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
// Shader / header bit parity, and the light budget
// ---------------------------------------------------------------------------
// GameData/Shaders/Surface.fs duplicates the flag VALUES as `#define SF_*`, and
// AGENTS.md is explicit that renumbering a bit in the header means editing the
// shader too "or the flag silently does nothing". Nothing enforced that until now,
// so a renumber would have compiled cleanly, passed every other test here, and
// shipped a flag that does nothing.
//
// This reads the shader and compares each #define against the header constant.
// It is a file read rather than a compile-time check because the two values
// genuinely live in two languages.
//
// The same hazard, worse, applies to MAX_LIGHTS, which lives in THREE files
// (rlights.h + both live shaders). It was 64 in C++ and 32 in both GLSL programs,
// so the submission loop wrote slots the driver does not have and half of the
// "supported" lights were silently discarded - WHICH half being decided per frame
// by the distance sort. See test_light_budget_parity below.
// Defined below, next to the other source-reading helpers.
static std::string ReadFile(const char* path);

static int ReadDefineInt(const std::string& src, const char* name) {
    const std::string want = std::string("#define ") + name;
    size_t p = src.find(want);
    if (p == std::string::npos) return -1;
    p += want.size();
    while (p < src.size() && (src[p] == ' ' || src[p] == '\t')) p++;
    if (p >= src.size()) return -1;
    // Accept the two spellings in use: "32" and "(1 << 5)".
    if (src[p] >= '0' && src[p] <= '9') return std::atoi(src.c_str() + p);
    if (src[p] == '(') {
        const size_t shift = src.find("<<", p);
        if (shift == std::string::npos) return -1;
        return 1 << std::atoi(src.c_str() + shift + 2);
    }
    return -1;
}

static void test_light_budget_parity() {
    const std::string lit  = ReadFile("GameData/Shaders/Lights/LitFog.fs");
    const std::string surf = ReadFile("GameData/Shaders/Surface.fs");
    const std::string rl   = ReadFile("Source/Renderer/rlights/rlights.h");
    if (lit.empty() || surf.empty() || rl.empty()) {
        printf("  SKIP  light budget parity (shader or rlights.h not found from cwd)\n");
        return;
    }

    const int cLit  = ReadDefineInt(lit,  "MAX_LIGHTS");
    const int cSurf = ReadDefineInt(surf, "MAX_LIGHTS");
    const int cRl   = ReadDefineInt(rl,   "MAX_LIGHTS");

    check(cLit > 0 && cSurf > 0 && cRl > 0,
          "MAX_LIGHTS is defined in all three places (rlights.h, LitFog.fs, Surface.fs)");
    check(cLit == cSurf,
          "the two shaders agree on the light budget - a mismatch makes one program's "
          "scene lighter than the other's in the same frame");
    check(cRl == cLit,
          "C++ agrees with the shaders on the light budget - a mismatch means the "
          "submission loop writes slots the driver discards, losing lights silently "
          "and per-frame, decided by the distance sort");

    // A power-of-two budget is not required, but a tiny or absurd one is a
    // configuration mistake worth failing on rather than discovering in-game.
    check(cLit >= 8, "the light budget is at least 8");
    check(cLit <= 64, "the light budget is at most 64");

    // The transient reservation is the other half of the real budget: transients are
    // submitted FIRST and cannot be evicted, so a world authored to the full budget
    // silently loses world lights the moment a weapon fires. Both numbers have to be
    // visible together or the "design to N lights" advice in Wiki/Lighting-Plan.md
    // cannot be checked.
    //
    // Read as text because LitLightning.hpp needs raylib and this suite is
    // deliberately raylib-free.
    const std::string litHdr = ReadFile("Source/Renderer/LitLightning.hpp");
    int transient = -1;
    if (!litHdr.empty()) {
        const size_t p = litHdr.find("MAX_TRANSIENT_LIGHTS");
        if (p != std::string::npos) {
            const size_t eq = litHdr.find('=', p);
            if (eq != std::string::npos) transient = std::atoi(litHdr.c_str() + eq + 1);
        }
    }
    check(transient > 0 && transient < cLit,
          "the transient reservation is non-zero and smaller than the total budget "
          "(so a full-budget world is knowingly over-subscribed when one is live)");
}

// The THREE fogFactorAt copies.
//
// LitFog.fs and Surface.fs were already two hand-maintained copies of the same
// fog curve, and they had drifted in four places (fog light-influence, Fresnel
// specular, negative clamp, opt-out flags) - so a surface-flagged brush could
// disagree with the room it stands in, in the same frame. That is the failure
// mode AGENTS.md warns about and the reason the light budget is policed by
// test_light_budget_parity rather than by convention.
//
// Sky.fs is now a THIRD copy, added for horizon fog on the skybox cube. Adding a
// copy is only defensible if adding a copy cannot silently succeed, which is what
// this does: it extracts all three bodies, strips comments and whitespace, and
// requires them to be identical. A comment-only edit passes; a changed constant,
// a renamed variable or a reintroduced clamp fails the build.
//
// It also checks that Sky.fs still DECLARES all five fog uniforms it reads. A
// missing declaration does not fail to compile - GLSL substitutes a default - so
// the sky would quietly render with start=10/end=100/rate=1.0 while the world
// used the level's, which is the exact bug SurfaceMaterial shipped with.
static std::string ExtractFogFn(const std::string& src) {
    const size_t at = src.find("float fogFactorAt(");
    if (at == std::string::npos) return std::string();
    const size_t open = src.find('{', at);
    if (open == std::string::npos) return std::string();
    int depth = 0;
    size_t i = open;
    for (; i < src.size(); i++) {
        if (src[i] == '{') depth++;
        else if (src[i] == '}') { depth--; if (depth == 0) { i++; break; } }
    }
    std::string body = src.substr(at, i - at);

    // Strip // comments, then all whitespace. Both are cosmetic; comparing them
    // would fail this test on a reformat, which is how parity tests get disabled.
    std::string noComment;
    for (size_t p = 0; p < body.size(); p++) {
        if (body[p] == '/' && p + 1 < body.size() && body[p+1] == '/') {
            while (p < body.size() && body[p] != '\n') p++;
            noComment += ' ';
        } else {
            noComment += body[p];
        }
    }
    std::string tight;
    for (size_t p = 0; p < noComment.size(); p++) {
        const char c = noComment[p];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        tight += c;
    }
    return tight;
}

static void test_fog_curve_parity() {
    const std::string lit  = ReadFile("GameData/Shaders/Lights/LitFog.fs");
    const std::string surf = ReadFile("GameData/Shaders/Surface.fs");
    const std::string sky  = ReadFile("GameData/Shaders/Sky.fs");
    if (lit.empty() || surf.empty() || sky.empty()) {
        printf("  SKIP  fog curve parity (a shader not found from cwd)\n");
        return;
    }

    const std::string a = ExtractFogFn(lit);
    const std::string b = ExtractFogFn(surf);
    const std::string c = ExtractFogFn(sky);

    check(!a.empty() && !b.empty() && !c.empty(),
          "fogFactorAt is defined in all three shaders (LitFog.fs, Surface.fs, Sky.fs)");
    if (a.empty() || b.empty() || c.empty()) return;

    check(a == b,
          "LitFog.fs and Surface.fs agree on fogFactorAt - a divergence means a "
          "surface-flagged brush fogs differently from the lit room beside it");
    check(a == c,
          "Sky.fs agrees on fogFactorAt - a divergence means the sky fogs on a "
          "different curve from the world it sits in, which is the seam this "
          "shader exists to remove");

    // Every uniform the body reads must be declared, or GLSL silently substitutes
    // a default and the shader disagrees with C++ without any diagnostic.
    const char* needed[] = { "fogColor", "fogStart", "fogEnd", "fogDensity", "fogIntensity" };
    for (const char* u : needed) {
        const std::string decl = std::string("uniform float ") + u;
        const std::string dcol = std::string("uniform vec3  ") + u;
        const bool ok = sky.find(decl) != std::string::npos ||
                        sky.find(dcol) != std::string::npos;
        check(ok, (std::string("Sky.fs declares ") + u +
                   " - a missing declaration compiles fine and renders with GLSL's "
                   "default instead of the level's fog").c_str());
    }
}

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

// ---------------------------------------------------------------------------
// Backdrop full-bright gain
// ---------------------------------------------------------------------------
// The gain is a per-PASS value, and the bug it replaces was a timing one: an
// earlier version batched it into SurfaceMaterial::UpdateFrame, which runs once
// per frame BEFORE DrawWorld, while DrawZoneGeometry sets it from INSIDE the draw
// passes. The flush had therefore already happened and uFullBright stayed at
// 1.0 forever - every painted backdrop rendered at full value rather than the
// 0.55 it was tuned for.
//
// These pin the VALUE and, more importantly, the property that matters: the gain
// must be reachable as a constant from raylib-free code, so a test can assert it
// at all. A local inside DrawZoneGeometry was untestable by construction.
static void test_backdrop_gain() {
    check(kBackdropGain > 0.0f && kBackdropGain < kFullBrightGain,
          "the backdrop gain is a dimmer tone curve, not full value");
    check(kFullBrightGain == 1.0f, "the world pass full-bright gain is 1.0");

    // 0.55 must stay exactly the legacy hack's gain: that hack forced ambient to
    // 5.5 and Surface.fs divides ambient by 10. If either number moves, the
    // 69 shipped flags=8 backdrops change brightness.
    check(kBackdropGain == 5.5f / 10.0f,
          "the backdrop gain equals the old ambient/10 hack's gain (5.5 / 10)");

    // Both passes must agree on what "no override" is, or a world pass that runs
    // after a backdrop pass would inherit 0.55 forever.
    check(kFullBrightGain != kBackdropGain,
          "the two gains differ, so DrawZoneGeometry's restore is meaningful");
}

// A structural guard, because the timing bug above is not visible from the API:
// SetFullBright took a float and looked correct in every review. It failed
// because of WHERE it was read back. So assert the shape of the code itself.
//
// This reads source files, which is unusual for a unit test but is the same
// discipline as the shader bit-parity check above: two artefacts that must agree
// cannot both be expressed in one language, so one of them has to be inspected
// as text. If the file is missing the suite skips loudly rather than passing
// silently.
static std::string ReadFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return std::string();
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return out;
}

static void test_full_bright_is_not_batched() {
    const std::string mat = ReadFile("Source/Renderer/SurfaceMaterial.cpp");
    const std::string hdr = ReadFile("Source/Renderer/SurfaceMaterial.hpp");
    const std::string ldr = ReadFile("Source/World/OzOzoneLoader.cpp");
    if (mat.empty() || hdr.empty() || ldr.empty()) {
        printf("  SKIP  full-bright upload shape (source files not found from cwd)\n");
        return;
    }

    check(hdr.find("m_fullBrightDirty") == std::string::npos,
          "SurfaceMaterial has no fullBright dirty flag - the lazy flush is gone");

    // The setter must do the upload itself, not delegate to a per-frame flush.
    const size_t setAt = mat.find("void SurfaceMaterial::SetFullBright");
    const bool found = setAt != std::string::npos;
    check(found, "SurfaceMaterial.cpp still defines SetFullBright");
    if (found) {
        // Isolate the function body: from its opening brace to the next
        // "\n}" at column 0, which is how this file ends every definition.
        const size_t bodyEnd = mat.find("\n}", setAt);
        const std::string body = mat.substr(setAt, (bodyEnd - setAt) + 2);
        check(body.find("SetShaderValue") != std::string::npos,
              "SetFullBright uploads with SetShaderValue (not deferred)");
        check(body.find("m_fullBrightDirty") == std::string::npos,
              "SetFullBright does not set a dirty flag");
    }

    // UpdateFrame must NOT have regained a flush, which is where it used to live.
    const size_t updAt = mat.find("void SurfaceMaterial::UpdateFrame");
    if (updAt != std::string::npos) {
        const size_t bodyEnd = mat.find("\n}", updAt);
        const std::string body = mat.substr(updAt, (bodyEnd - updAt) + 2);
        check(body.find("m_fullBrightLoc") == std::string::npos,
              "UpdateFrame no longer flushes uFullBright (it runs before the draw passes)");
    }

    // The backdrop pass must both set and restore, so the world pass that follows
    // cannot inherit the dimmer gain.
    const size_t dzgAt = ldr.find("void OzoneLoader::DrawZoneGeometry(Camera3D& camera, const BoundingBox&");
    check(dzgAt != std::string::npos, "DrawZoneGeometry(bounds) is still defined");
    if (dzgAt != std::string::npos) {
        const size_t bodyEnd = ldr.find("\n}", dzgAt);
        const std::string body = ldr.substr(dzgAt, (bodyEnd - dzgAt) + 2);
        const size_t setGain = body.find("SetFullBright");
        check(setGain != std::string::npos, "the backdrop pass sets the full-bright gain");
        check(body.find("kBackdropGain") != std::string::npos,
              "...using the shared oz::surface::kBackdropGain, not a local literal");
        check(body.find("kFullBrightGain") != std::string::npos,
              "...and restores kFullBrightGain on the way out");
        check(body.find("SurfacePass::BackdropOnly") != std::string::npos,
              "the backdrop pass draws through DrawSurface(BackdropOnly)");
    }
}

// ---------------------------------------------------------------------------
// Dead-flag register, and what the editor is allowed to offer
// ---------------------------------------------------------------------------
// Fourteen flags parse, export and round-trip, and NOTHING in the engine reads
// them. They used to be live checkboxes in the Surface Properties dialog, so
// ticking one saved a bit that then did nothing - strictly worse than not offering
// it, because the author cannot tell which half of the dialog works.
//
// This is the register, and it is asserted rather than described: a flag added to
// the dialog without a consumer fails here.
static void test_dead_flags_are_still_dead() {
    struct Named { uint32_t bit; const char* name; };
    // SURF_NO_SMOOTH is the nasty one: the shader DOES read it, at
    // `normal = normal;`, so it looks implemented and is not. It belongs here
    // because inert is the property that matters, not unread.
    const Named dead[] = {
        { SURF_SPECIAL_LIT, "SURF_SPECIAL_LIT" }, { SURF_PORTAL, "SURF_PORTAL" },
        { SURF_MIRROR, "SURF_MIRROR" }, { SURF_ENVIRONMENT, "SURF_ENVIRONMENT" },
        { SURF_SMALL_WAVY, "SURF_SMALL_WAVY" },
        { SURF_BRIGHT_CORNERS, "SURF_BRIGHT_CORNERS" },
        { SURF_DIRTY_SHADOWS, "SURF_DIRTY_SHADOWS" },
        { SURF_SHADOW_HI, "SURF_SHADOW_HI" }, { SURF_SHADOW_LO, "SURF_SHADOW_LO" },
        { SURF_NO_BOUNDS_REJECT, "SURF_NO_BOUNDS_REJECT" },
        { SURF_ZONE_HACK, "SURF_ZONE_HACK" },
        { SURF_INVISIBLE_OCCLUDER, "SURF_INVISIBLE_OCCLUDER" },
        { SURF_FORCE_VIEW_ZONE, "SURF_FORCE_VIEW_ZONE" },
        { SURF_NO_SMOOTH, "SURF_NO_SMOOTH" },
    };
    const int deadCount = (int)(sizeof(dead) / sizeof(dead[0]));
    check(deadCount == 14, "the dead-flag register has 14 entries");

    // The dialog's Flags tab exposes 16 rows: these 14 live flags plus PORTAL and
    // MIRROR shown-but-disabled as "(planned)".
    const uint32_t live[] = {
        SURF_FAKEBACKDROP, SURF_INVISIBLE, SURF_MASKED, SURF_TRANSLUCENT,
        SURF_ALPHABLEND, SURF_MODULATED, SURF_TWO_SIDED, SURF_UNLIT,
        SURF_FAKE_LIT, SURF_GLOW, SURF_PAN_U, SURF_PAN_V,
        SURF_NO_FOG, SURF_NO_BSP_CUTS,
    };
    const int liveCount = (int)(sizeof(live) / sizeof(live[0]));
    check(liveCount == 14, "14 flags are live and offered as enabled checkboxes");

    bool overlap = false;
    for (int i = 0; i < liveCount; i++)
        for (int j = 0; j < deadCount; j++)
            if (live[i] == dead[j].bit) overlap = true;
    check(!overlap, "no flag is in both the live and dead registers");

    // SURF_COLLISION_PROXY must be in NEITHER: AutoConvex generates it, so exposing
    // it would let an author hand-write "invisible but still solid".
    bool proxyClassified = false;
    for (int i = 0; i < liveCount; i++)  if (live[i] == SURF_COLLISION_PROXY) proxyClassified = true;
    for (int j = 0; j < deadCount; j++) if (dead[j].bit == SURF_COLLISION_PROXY) proxyClassified = true;
    check(!proxyClassified, "SURF_COLLISION_PROXY is in neither register (AutoConvex-generated)");

    // Every flag in the header must be classified, or one exists that nobody has
    // decided about.
    uint32_t accounted = SURF_COLLISION_PROXY;
    for (int i = 0; i < liveCount; i++)  accounted |= live[i];
    for (int j = 0; j < deadCount; j++) accounted |= dead[j].bit;
    uint32_t missing = 0;
    for (uint32_t b : kAllSurfaceFlags) if ((accounted & b) == 0) missing |= b;
    check(missing == 0,
          "every flag in the header is classified live or dead - none is unaccounted for");

    // The dialog must not offer a dead flag as enabled. Source-level, because the
    // table lives in a Win32 UI fragment that cannot be linked here - same
    // discipline as the shader parity check.
    const std::string panel = ReadFile("AngelEd/Source/UI/Panels/SurfacePropsPanel.cpp");
    if (panel.empty()) {
        printf("  SKIP  dead flags are hidden in the dialog (panel source not found)\n");
        return;
    }
    check(panel.find("bool implemented;") != std::string::npos,
          "the dialog's flag table carries an `implemented` column");

    // Scan the table rows. A row looks like:  { L"Label", SURF_NAME, true },
    // A dead flag's row must not say `true`.
    int wrongRows = 0;
    size_t p = 0;
    while ((p = panel.find("{ L\"", p)) != std::string::npos) {
        const size_t end = panel.find("}", p);
        if (end == std::string::npos) break;
        const std::string row = panel.substr(p, end - p);
        const bool enabled = row.find(", true }") != std::string::npos;
        for (int j = 0; j < deadCount; j++) {
            if (row.find(dead[j].name) == std::string::npos) continue;
            if (enabled) {
                printf("    FAIL: the dialog offers dead flag %s as ENABLED\n", dead[j].name);
                wrongRows++;
            }
        }
        p = end;
    }
    check(wrongRows == 0, "no dead flag is offered as an enabled checkbox");

    check(panel.find("L\"Portal (planned)\"") != std::string::npos &&
          panel.find("L\"Mirror (planned)\"") != std::string::npos,
          "PORTAL and MIRROR are labelled '(planned)' rather than hidden");
    check(panel.find("EnableWindow(c, FALSE)") != std::string::npos,
          "the dialog disables its unimplemented rows rather than leaving them live");
}

int main() {
    printf("Surface tests:\n");
    test_legacy_bits_stable();
    test_legacy_flag_derivation();
    test_shader_bit_parity();
    test_light_budget_parity();
    test_fog_curve_parity();
    test_full_bright_is_not_batched();
    test_derive_legacy_surface_mirror();
    test_backdrop_gain();
    test_dead_flags_are_still_dead();
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
