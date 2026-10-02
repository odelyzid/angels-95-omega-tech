#pragma once
// ---------------------------------------------------------------------------
// SelType — what the editor currently has selected.
//
// This lives in a header rather than in Main.cpp because the value crosses the
// file boundary: Win32Dialogs.cpp stores it as a raw `int` in
// `EditorPanelState::propsTargetType` and every WorldGraph row carries it as a
// plain integer. Both sides used to compare magic numbers (`selType == 5`,
// `propsTargetType == 6`), which is why a routing table documented in a wiki page
// can silently lose a row when the enum is reordered.
//
// Two values are load-bearing and must not be reused:
//   * 0 (NONE) is what the Sound/Music emitter WorldGraph rows carry, on purpose.
//     `OpenPropertiesForSelection` bails on it, which is how emitters stay
//     non-selectable without a separate flag.
//   * -1 is the Script Manager's "def only, no world instance" sentinel, set by
//     `ShowDefPropertiesFor`. Out-of-range values are therefore already valid and
//     render the def section alone.
//
// Adding a type means adding BOTH the routing branch in `ShowPropertiesPanel`
// (seed) and one in the apply dispatch (write) — plus auditing the guards in
// PopulatePropertiesPanel, DeleteSelectedEntity and DuplicateSelectedEntity,
// which are `else if` chains with no `else` and so fail silently rather than
// loudly. `Wiki/Editor-PropertyPanel-Refactor.md` has the audit table.
// ---------------------------------------------------------------------------

enum class SelType {
    NONE = 0,
    BRUSH,
    MODEL,
    NPC,
    PICKUP,
    LIGHT,
    ZONE,
    SPAWN,
    PORTAL,
    MESH,
    PARTICLE,
    PATHNODE,
    WINDZONE,
    // The level itself: gameType rules, weather particles and the level skybox.
    // Lives in the WorldGraph list as a synthetic first row, so it can reach the
    // Entity Properties panel through the normal right-click path. A level has no
    // position, rotation or geometry — the panels must not emit those rows for it,
    // and Delete/Duplicate must not be offered.
    MAP,
};

// Named constants for the `int` mirrors (`WorldGraphEntry::selType`,
// `EditorPanelState::propsTargetType`, `actionSelectFromGraphType`). Spelling the
// value at both ends is the whole point: a bare `13` in a comparison is exactly
// the drift this header removes.
namespace sel {
constexpr int NONE     = static_cast<int>(SelType::NONE);
constexpr int BRUSH    = static_cast<int>(SelType::BRUSH);
constexpr int MODEL    = static_cast<int>(SelType::MODEL);
constexpr int NPC      = static_cast<int>(SelType::NPC);
constexpr int PICKUP   = static_cast<int>(SelType::PICKUP);
constexpr int LIGHT    = static_cast<int>(SelType::LIGHT);
constexpr int ZONE     = static_cast<int>(SelType::ZONE);
constexpr int SPAWN    = static_cast<int>(SelType::SPAWN);
constexpr int PORTAL   = static_cast<int>(SelType::PORTAL);
constexpr int MESH     = static_cast<int>(SelType::MESH);
constexpr int PARTICLE = static_cast<int>(SelType::PARTICLE);
constexpr int PATHNODE = static_cast<int>(SelType::PATHNODE);
constexpr int WINDZONE = static_cast<int>(SelType::WINDZONE);
constexpr int MAP      = static_cast<int>(SelType::MAP);

// The Script Manager's def-only sentinel: render the .ozls stats with no
// per-instance rows at all.
constexpr int DEF_ONLY = -1;
} // namespace sel