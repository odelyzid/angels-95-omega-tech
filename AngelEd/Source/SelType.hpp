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
// `EditorPanelState::propsTargetType`). Spelling the value at both ends is the
// whole point: a bare `13` in a comparison is exactly the drift this header
// removes. `actionSelectFromGraphType` used to be a third mirror and is now an
// ed::Selection, so it is bridged by ToBusKind() below instead.
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

// ---------------------------------------------------------------------------
// SelType -> ed::SelKind
//
// The event bus carries its OWN SelKind enum because EditorEventBus.hpp must stay
// raylib-free and independent of the editor's headers. The two enums have
// identical values, so the mapping could be a `static_cast` — which is exactly
// why it is a switch instead. `static_cast<ed::SelKind>(someInt)` would keep
// compiling after someone adds a 15th SelType, and every event for that new type
// would silently arrive with the wrong kind: no diagnostic, wrong entity acted
// on. A switch has no `else` that compiles, so the omission is a compile error.
//
// `NONE` maps to `None`, and `sel::DEF_ONLY` (-1) is NOT a SelType value, so it
// falls through to `None` as well. Both mean "no live target", which is what the
// dispatcher needs in order to skip the staleness check.
// ---------------------------------------------------------------------------
#include "Core/EditorEventBus.hpp"

inline ed::SelKind ToBusKind(SelType t) {
    switch (t) {
        case SelType::NONE:     return ed::SelKind::None;
        case SelType::BRUSH:    return ed::SelKind::Brush;
        case SelType::MODEL:    return ed::SelKind::Model;
        case SelType::NPC:      return ed::SelKind::Npc;
        case SelType::PICKUP:   return ed::SelKind::Pickup;
        case SelType::LIGHT:    return ed::SelKind::Light;
        case SelType::ZONE:     return ed::SelKind::Zone;
        case SelType::SPAWN:    return ed::SelKind::Spawn;
        case SelType::PORTAL:   return ed::SelKind::Portal;
        case SelType::MESH:     return ed::SelKind::Mesh;
        case SelType::PARTICLE: return ed::SelKind::Particle;
        case SelType::PATHNODE: return ed::SelKind::PathNode;
        case SelType::WINDZONE: return ed::SelKind::WindZone;
        case SelType::MAP:      return ed::SelKind::Map;
    }
    // Unreachable while the switch is exhaustive, which is the point. -Wswitch
    // turns a future unhandled SelType into a warning here before it becomes a
    // silent mis-dispatch in the dispatcher.
    return ed::SelKind::None;
}

// ed::SelKind -> SelType. The reverse of the above, needed where an event's captured
// target has to become a SelType again to index a container: Subsystems/PropsApply
// resolves ev.sel() back to a SelType to dispatch the properties apply.
//
// Also an exhaustive switch rather than a static_cast, for the same reason. The two
// enums have identical values today, so a cast would work - and would keep compiling
// after someone adds a 15th SelType, silently applying properties to the wrong
// container.
inline SelType ToSelType(ed::SelKind k) {
    switch (k) {
        case ed::SelKind::None:       return SelType::NONE;
        case ed::SelKind::Brush:      return SelType::BRUSH;
        case ed::SelKind::Model:      return SelType::MODEL;
        case ed::SelKind::Npc:        return SelType::NPC;
        case ed::SelKind::Pickup:     return SelType::PICKUP;
        case ed::SelKind::Light:      return SelType::LIGHT;
        case ed::SelKind::Zone:       return SelType::ZONE;
        case ed::SelKind::Spawn:      return SelType::SPAWN;
        case ed::SelKind::Portal:     return SelType::PORTAL;
        case ed::SelKind::Mesh:       return SelType::MESH;
        case ed::SelKind::Particle:   return SelType::PARTICLE;
        case ed::SelKind::PathNode:   return SelType::PATHNODE;
        case ed::SelKind::WindZone:   return SelType::WINDZONE;
        case ed::SelKind::Map:        return SelType::MAP;
    }
    return SelType::NONE;
}