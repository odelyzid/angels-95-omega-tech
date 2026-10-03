// AngelEd editor event bus — the replacement for EditorPanelState's 58 loose
// `action*` fields.
//
// Raylib-free AND Win32-free on purpose. That is what makes the central invariant
// testable from the headless suites: the 58 fields it replaces are only reachable
// from inside a WM_COMMAND handler and a raylib frame loop, so today none of
// them can be verified without clicking. Same discipline as ZoneTypes.hpp,
// GameType.hpp and SurfaceFlags.hpp.
//
// See Wiki/Editor-Architecture-Refactor.md for the design and the phased plan.
#ifndef ANGELS95_EDITOR_EVENT_BUS_HPP
#define ANGELS95_EDITOR_EVENT_BUS_HPP

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace ed {

// Entity kinds the editor can select. Deliberately a *separate* enum from
// AngelEd's SelType: this header must not depend on the editor's headers, and
// duplicating a 14-value enum is far cheaper than a layering violation. The
// conversion is asserted in tests/EditorEventBus.test.cpp by size.
enum class SelKind : uint8_t {
    None = 0,
    Brush, Model, Npc, Pickup, Light, Zone, Spawn, Portal,
    Mesh, Particle, PathNode, WindZone, Map,
};

// What to place at the camera. One payload for all ten `actionSpawn*` fields,
// which is the point: those ten fields are ten spellings of "place something",
// and nothing stopped the wrong one being read.
struct SpawnDesc {
    std::string key;        // def name for pawn/mesh/pickup, "" when unused
    int         kind = 0;   // ZoneType index for a zone, else unused
    bool        solid = false;   // CSG op: SOLID when placed directly
};

// Position / size / rotation. Grouped so a resize cannot be posted with a
// rotation for some other entity's transform.
struct Transform {
    float x = 0, y = 0, z = 0;
    float w = 8, h = 8, d = 8;
    float yaw = 0;
};

// A selection, captured BY VALUE at post time.
//
// This is the whole reason the bus exists. The `action*` fields carried bare
// indices, so if the selection moved between the click and the frame that polled
// it, the operation landed on a different entity. Portal delete already had to be
// defended by hand for exactly this (see the b88 notes). Capturing the target
// with the event makes it impossible by construction.
struct SelRef {
    SelKind kind = SelKind::None;
    int     index = -1;
    bool valid() const { return kind != SelKind::None && index >= 0; }
};

// Level state lives in Subsystems/LevelState and only the Map row edits it, so
// it needs no selection reference.
struct LevelStateEdit {
    std::string worldName;
    int gameType = 0;
};

// A complete selection, captured by value.
//
// This replaces FIVE separate action fields — actionSelectFromGraph plus its
// Type/Name/Pos[3] satellites — which were written together and read together,
// but were not coupled by anything: a handler could read the new index with the
// previous frame's type and position and never notice.
//
// It also fixes the index-space confusion the WorldGraph had: its Properties
// entry stored a LIST ROW index while Delete/Duplicate stored an ENTITY index, in
// fields with near-identical names. One type, one index space.
struct Selection {
    SelRef    ref;
    std::string name;
    float x = 0, y = 0, z = 0;

    bool valid() const { return ref.valid(); }
};

enum class Ev : uint8_t {
    None = 0,

    // Placement. Payload: SpawnDesc.
    SpawnPawn, SpawnMesh, SpawnPickup, SpawnEmitter, SpawnZone,
    SpawnParticleEmitter, SpawnPathNode, SpawnWindZone, SpawnPlayerStart,

    // CSG. Payload: SpawnDesc (kind = primitive, solid) then Transform.
    CsgPlace, CsgCommit,

    // World graph / level list. Payload: SelRef.
    DeleteEntity, DuplicateEntity, SelectEntity, ApplyProperties,

    // Payload: std::string (world folder name).
    OpenWorld, LinkWorld,

    // Surface. Payload: SelRef, then int faceMask + uint32_t flags via SpawnDesc.kind
    ApplySurface, ResetSurface,

    // Assets.
    ApplyTextureToSelection,   // Payload: std::string (asset path)
    ReloadMesh,                // Payload: SelRef
    ConvertToAnimated,         // Payload: SelRef
    PlaySound,                 // Payload: std::string (path); volume/loop in LevelStateEdit
    StopSoundPreview,          // Payload: none

    // Heightmap.
    GenerateHeightmap,         // Payload: Transform

    // Level state. Payload: LevelStateEdit.
    ApplyLevelState,

    // Animation editing. Payload: std::string (clip name); index in Transform.y
    AnimNewClip, AnimDeleteClip, AnimSave, AnimRefresh, AnimScrub,
    AnimAddKey, AnimDeleteKey, AnimApplyClipMeta, AnimToggleEdit,
    AnimUndo, AnimRedo, AnimSelectAll, AnimClearSelection,

    // Sentinel, not an event. tests/EditorEventBus.test.cpp asserts its value
    // against the number of events it names, so adding an Ev without adding it to
    // that list fails the suite instead of silently going untested — the same
    // drift that let ID_PP_PORTALBROWSE and ID_BTN_GT_PREVIEW sit in the editor
    // declared, created and never handled.
    Count,
};

// The payload variant. One type per shape of data, so the post site is checked by
// the compiler against the event kind and a SpawnZone cannot be posted carrying a
// filename. A generic string-topic bus would have traded that compile error for a
// runtime typo.
using Payload = std::variant<std::monostate,
                             int,
                             float,
                             bool,
                             std::string,
                             SelRef,
                             Selection,
                             SpawnDesc,
                             Transform,
                             LevelStateEdit>;

struct Event {
    Ev      kind = Ev::None;
    Payload data{};

    template <class T>
    static Event make(Ev k, T v) { return Event{k, Payload(std::move(v))}; }

    // Typed accessors. Each asserts the shape it expects, because a mismatch here
    // means a call site and a handler disagree about the event — exactly the
    // class of bug the loose fields could not even represent.
    const SpawnDesc& spawn() const;
    const SelRef&    sel()   const;
    const Selection& selection() const;
    const Transform& xform() const;
    const std::string& str() const;
    const LevelStateEdit& level() const;

    // The target this event names, or None when it is not selection-scoped.
    // Used by the dispatcher to reject a stale event against the live selection
    // rather than silently acting on whatever happens to be selected.
    // Handles BOTH SelRef and Selection payloads.
    SelRef target() const;
};

class EventBus {
public:
    // Public on purpose. A bus you cannot instantiate is a bus you cannot test,
    // and the whole reason this type is raylib-free is so the queue can be
    // exercised directly instead of through a window procedure. `instance()` is
    // the convenience singleton for production, not the only way in.
    EventBus() = default;

    // Bounded on purpose. A panel that posts in a loop (a drag handler, say)
    // must not be able to grow the queue without limit; overflow is counted and
    // the newest event is dropped, so the failure is visible instead of silent.
    static constexpr size_t kMaxQueued = 256;

    void post(Event e) {
        m_posted++;
        if (m_queue.size() >= kMaxQueued) { m_dropped++; return; }
        m_queue.push_back(std::move(e));
    }

    template <class T>
    void post(Ev k, T v) { post(Event::make(k, std::move(v))); }

    // Move the queue out and empty it. Called once per frame, at the single point
    // where dispatching is safe — a panel is mid-layout while it is posting.
    void drain(std::vector<Event>& out) {
        out.clear();
        out.swap(m_queue);
    }

    size_t pending() const { return m_queue.size(); }
    size_t posted()   const { return m_posted; }
    size_t dropped()  const { return m_dropped; }
    void   clear()    { m_queue.clear(); m_dropped = 0; }

    // The bus is a process-wide singleton by design: the UI posts from a window
    // procedure and the dispatcher drains from the frame loop, with no other way
    // to hand a reference across.
    static EventBus& instance() { static EventBus b; return b; }

private:
    std::vector<Event> m_queue;
    // m_posted counts every request including drops, so `posted() - dropped()` is
    // everything that actually reached a handler and `pending()` is what is
    // waiting. An event that was posted but never drained is therefore a
    // visible, countable condition instead of a mystery.
    size_t m_posted = 0, m_dropped = 0;
};

} // namespace ed

#endif // ANGELS95_EDITOR_EVENT_BUS_HPP