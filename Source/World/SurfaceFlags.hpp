#pragma once
// ---------------------------------------------------------------------------
// World/SurfaceFlags.hpp — per-face surface properties for OZONE brushes
//
// Inspired by the UT99 Surface properties dialog (Flags / Alignment / Stats
// tabs, one entry per brush FACE rather than per brush). The editor edits these
// by right-clicking a face in the viewport; each face stores its own flags,
// texture slot, UV transform, U/V pan, alpha and glow.
//
// RAYLIB-FREE ON PURPOSE. Plain floats, no Color/Vector2/Vector3, so the
// server's `worldcheck` auditor, the OZONE parser and the headless test harness
// can all link this (same discipline as World/ZoneTypes.hpp and
// World/GameType.hpp). The renderer maps the float triples onto raylib types at
// the point of use.
//
// ---------------------------------------------------------------------------
// BIT ALLOCATION
//
// Bits 0-2 are unallocated. Bits 3 and 4 are the pre-existing, already-shipped
// meanings and MUST NOT be renumbered:
//
//   1<<3 SURF_FAKEBACKDROP      painted 2D backdrop (Ocarina of Time trick)
//   1<<4 SURF_COLLISION_PROXY   generated AutoConvex box, never drawn in-game
//
// Only `flags=8` appears in the shipped worlds (299 times), so nothing written
// today can collide with 1<<5 and above. Surface flags therefore start at bit 5
// and run to bit 29, which is exactly enough for the 25 UT99 checkboxes.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>

namespace oz {
namespace surface {

// ---------------------------------------------------------------------------
// Flag bits
//
// These are constants, not an enum and not macros. They were `#define`s in
// OzOzoneLoader.hpp before, which meant the names could not also be declared in
// a scoped type, and every call site used them unqualified. Keeping the same
// unqualified spellings (via the using-declarations at the bottom of this
// header) means no existing call site has to change, while the values now have
// a real type and a single home.
//
// Bits 0-2 are unallocated. Bits 3 and 4 are the pre-existing, already-shipped
// meanings and MUST NOT be renumbered:
//
//   1<<3 SURF_FAKEBACKDROP      painted 2D backdrop (Ocarina of Time trick)
//   1<<4 SURF_COLLISION_PROXY   generated AutoConvex box, never drawn in-game
//
// Only `flags=8` appears in the shipped worlds (299 times), so nothing written
// today can collide with 1<<5 and above. Surface flags therefore start at bit 5
// and run to bit 31, which covers the 25 UT99 checkboxes.
// ---------------------------------------------------------------------------

// --- reserved / pre-existing -----------------------------------------------
inline constexpr uint32_t SURF_FAKEBACKDROP      = (1u << 3);
inline constexpr uint32_t SURF_COLLISION_PROXY   = (1u << 4);

// --- rendering behaviour ---------------------------------------------------
inline constexpr uint32_t SURF_INVISIBLE       = (1u << 5);   // not drawn (still collides)
inline constexpr uint32_t SURF_MASKED          = (1u << 6);   // alpha test vs cutoff
inline constexpr uint32_t SURF_TRANSLUCENT     = (1u << 7);   // alpha blend, no depth write
inline constexpr uint32_t SURF_ALPHABLEND      = (1u << 8);   // explicit blend, no fog
inline constexpr uint32_t SURF_MODULATED       = (1u << 9);   // multiply blend
inline constexpr uint32_t SURF_TWO_SIDED       = (1u << 10);  // draw back faces too
inline constexpr uint32_t SURF_UNLIT           = (1u << 11);  // full-bright
inline constexpr uint32_t SURF_FAKE_LIT        = (1u << 12);  // lit, no light contribution
inline constexpr uint32_t SURF_SPECIAL_LIT     = (1u << 13);  // reserved: custom model
inline constexpr uint32_t SURF_GLOW            = (1u << 14);  // self-illumination
inline constexpr uint32_t SURF_PORTAL          = (1u << 15);  // portal surface
inline constexpr uint32_t SURF_MIRROR          = (1u << 16);  // mirror surface
inline constexpr uint32_t SURF_ENVIRONMENT     = (1u << 17);  // environment-mapped

// --- texture animation -----------------------------------------------------
inline constexpr uint32_t SURF_PAN_U           = (1u << 18);  // scroll U
inline constexpr uint32_t SURF_PAN_V           = (1u << 19);  // scroll V
inline constexpr uint32_t SURF_SMALL_WAVY      = (1u << 20);
inline constexpr uint32_t SURF_BRIGHT_CORNERS  = (1u << 21);

// --- lighting / shadow hints -----------------------------------------------
inline constexpr uint32_t SURF_DIRTY_SHADOWS   = (1u << 22);
inline constexpr uint32_t SURF_SHADOW_HI       = (1u << 23);  // high shadow detail
inline constexpr uint32_t SURF_SHADOW_LO       = (1u << 24);  // low shadow detail
inline constexpr uint32_t SURF_NO_SMOOTH       = (1u << 25);  // flat shading

// --- fog / bounds / BSP hints ----------------------------------------------
inline constexpr uint32_t SURF_NO_FOG          = (1u << 26);
inline constexpr uint32_t SURF_NO_BOUNDS_REJECT = (1u << 27); // always draw
inline constexpr uint32_t SURF_NO_BSP_CUTS     = (1u << 28); // cosmetic only
inline constexpr uint32_t SURF_ZONE_HACK       = (1u << 29); // zone visual override
inline constexpr uint32_t SURF_INVISIBLE_OCCLUDER = (1u << 30); // occludes, not drawn
inline constexpr uint32_t SURF_FORCE_VIEW_ZONE   = (1u << 31); // forces sky-zone view

// ---------------------------------------------------------------------------
// Faces
//
// Six buckets, chosen by the dominant axis of a face's normal. Deriving the
// face geometrically (rather than assuming the primitive generator's vertex
// order) is what lets the same six faces work for a box, a cylinder, a sphere
// and a pyramid.
// ---------------------------------------------------------------------------
enum SurfaceFace : int {
    FACE_PX = 0,   // +X
    FACE_NX = 1,   // -X
    FACE_PY = 2,   // +Y  (floor in OZONE terms; engine Y-up)
    FACE_NY = 3,   // -Y
    FACE_PZ = 4,   // +Z
    FACE_NZ = 5,   // -Z
    FACE_COUNT = 6,
    FACE_NONE = -1
};

// Editor / export spelling of each face. Note this is ENGINE Y-up naming, not
// OZONE Z-up: the OZONE text swaps y/z on load and load, so a face authored as
// `face_pz` is the brush's +Z side in engine space.
const char* FaceName(SurfaceFace f);
SurfaceFace  FaceFromName(const char* name);      // FACE_NONE when unknown
SurfaceFace  FaceFromNormal(float nx, float ny, float nz);

// ---------------------------------------------------------------------------
// SurfaceProps — one face's worth of surface state. Plain data, no raylib.
// ---------------------------------------------------------------------------
struct SurfaceProps {
    uint32_t flags = 0;

    // Texture. texSlot is 1-based into the world's <world>/oztex/tileset/ list;
    // 0 means "whatever the brush mesh already uses". texPath, when non-empty,
    // wins over texSlot (a free-placement asset rather than a tileset entry).
    int         texSlot  = 0;
    std::string texPath;

    // UV transform, applied on top of the mesh's generated UVs. Same units as
    // the existing texScaleU/V + texOffsetU/V brush kwargs.
    float uvScaleU  = 1.0f;
    float uvScaleV  = 1.0f;
    float uvOffsetU = 0.0f;
    float uvOffsetV = 0.0f;

    // U/V pan, in texture units per second. Only animated when the matching
    // SURF_PAN_U / SURF_PAN_V bit is set, so a zero speed is a hard no-op
    // rather than a per-frame division.
    float panU = 0.0f;
    float panV = 0.0f;

    // Transparency. `alpha` multiplies the sampled colour; `alphaCutoff > 0`
    // switches the surface to masked (alpha-test) instead of blended.
    float alpha      = 1.0f;
    float alphaCutoff = 0.0f;

    // Self-illumination. `glowR/G/B` in 0..1, scaled by glowScale.
    float glowR = 0.0f, glowG = 0.0f, glowB = 0.0f;
    float glowScale = 1.0f;

    bool Has(uint32_t bit) const { return (flags & bit) != 0; }
    void Set(uint32_t bit, bool on) {
        if (on) flags |= bit; else flags &= ~bit;
    }
    // True when this face needs anything the default DrawModel path cannot do.
    // Used to keep undecorated brushes on the original single-draw fast path.
    bool IsNonDefault() const;
};

// ---------------------------------------------------------------------------
// BrushSurface — a brush's six faces plus the brush-wide default they inherit.
//
// Precedence when resolving face f:
//     face[f] if overridden  >  def  >  engine defaults
//
// This mirrors the rule GameData/Global/Lights/Light.ozls documents: a value
// the author set explicitly always wins over the shared default.
// ---------------------------------------------------------------------------
struct BrushSurface {
    SurfaceProps def;                 // brush-wide default
    SurfaceProps face[FACE_COUNT];    // per-face, valid only where overridden
    bool         overridden[FACE_COUNT] = {false, false, false, false, false, false};

    // Bitmask of faces with at least one override. Lets the editor track a
    // multi-face selection ("Surface Properties (3 Selected)") as a single int.
    uint32_t selectionMask = 0;

    bool IsFaceOverridden(SurfaceFace f) const {
        return f >= 0 && f < FACE_COUNT && overridden[(int)f];
    }
    void SetFace(SurfaceFace f, const SurfaceProps& p);
    void ClearFace(SurfaceFace f);
    void ClearAllFaces();
    // Push the brush-wide default onto every face and drop all overrides.
    void ResetToDefault();

    // Effective properties for a face (override or default).
    const SurfaceProps& Resolve(SurfaceFace f) const {
        return IsFaceOverridden(f) ? face[(int)f] : def;
    }

    // True when the brush needs the per-face draw path.
    bool NeedsPerFaceDraw() const;
    // True when the brush-wide default alone already forces the surface shader.
    bool DefIsNonDefault() const { return def.IsNonDefault(); }

    // Selection helpers (editor).
    void SelectOnly(SurfaceFace f);
    void AddToSelection(SurfaceFace f);
    void ToggleSelection(SurfaceFace f);
    void ClearSelection() { selectionMask = 0; }
    int  SelectedCount() const;
    bool IsSelected(SurfaceFace f) const {
        return f >= 0 && (selectionMask & (1u << (int)f)) != 0;
    }
};

// Apply one face's props to every face in the selection mask (no-op when the
// mask is empty). Used by the Surface Properties dialog's Apply.
void ApplyToSelection(BrushSurface& s, const SurfaceProps& p);

} // namespace surface
} // namespace oz

// ---------------------------------------------------------------------------
// Unqualified re-exports.
//
// SURF_FAKEBACKDROP and SURF_COLLISION_PROXY were `#define`s, and every call
// site (DrawWorldGeometry, Core.hpp, the editor, the AutoConvex pass) uses the
// bare name. These using-declarations keep those spellings working without
// reintroducing macros - a macro of the same name would break any scoped
// declaration that mentions it.
// ---------------------------------------------------------------------------
using oz::surface::SURF_FAKEBACKDROP;
using oz::surface::SURF_COLLISION_PROXY;
using oz::surface::SURF_INVISIBLE;
using oz::surface::SURF_MASKED;
using oz::surface::SURF_TRANSLUCENT;
using oz::surface::SURF_ALPHABLEND;
using oz::surface::SURF_MODULATED;
using oz::surface::SURF_TWO_SIDED;
using oz::surface::SURF_UNLIT;
using oz::surface::SURF_FAKE_LIT;
using oz::surface::SURF_SPECIAL_LIT;
using oz::surface::SURF_GLOW;
using oz::surface::SURF_PORTAL;
using oz::surface::SURF_MIRROR;
using oz::surface::SURF_ENVIRONMENT;
using oz::surface::SURF_PAN_U;
using oz::surface::SURF_PAN_V;
using oz::surface::SURF_SMALL_WAVY;
using oz::surface::SURF_BRIGHT_CORNERS;
using oz::surface::SURF_DIRTY_SHADOWS;
using oz::surface::SURF_SHADOW_HI;
using oz::surface::SURF_SHADOW_LO;
using oz::surface::SURF_NO_SMOOTH;
using oz::surface::SURF_NO_FOG;
using oz::surface::SURF_NO_BOUNDS_REJECT;
using oz::surface::SURF_NO_BSP_CUTS;
using oz::surface::SURF_ZONE_HACK;
using oz::surface::SURF_INVISIBLE_OCCLUDER;
using oz::surface::SURF_FORCE_VIEW_ZONE;
