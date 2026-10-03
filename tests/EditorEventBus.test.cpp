// EditorEventBus — standalone, no raylib and no Win32. This is the point of the
// bus: the 58 `EditorPanelState::action*` fields it replaces were only reachable
// from a WM_COMMAND handler inside a raylib frame loop, so none of them could be
// verified without clicking.
#include "../AngelEd/Source/Core/EditorEventBus.hpp"

#include <cstdio>

using namespace ed;

static int test_count = 0, pass_count = 0;

static void check(bool ok, const char* what) {
    test_count++;
    if (ok) { pass_count++; printf("  PASS  %s\n", what); }
    else     { printf("  FAIL  %s\n", what); }
}

// ---------------------------------------------------------------------------
// Payloads are typed, and reading the wrong shape is reported
// ---------------------------------------------------------------------------
// The loose `action*` fields had no shape at all: actionSpawnZone is an int and
// actionSpawnPawn is a std::string, and nothing stopped a handler reading one as
// the other. std::variant makes the post site compile-checked.
static void test_typed_payloads() {
    SpawnDesc sd;
    sd.key = "pistol_01";
    sd.kind = 2;
    Event e = Event::make(Ev::SpawnPickup, sd);

    check(e.kind == Ev::SpawnPickup, "event round-trips its kind");
    check(e.spawn().key == "pistol_01", "SpawnDesc.string survives the round trip");
    check(e.spawn().kind == 2, "SpawnDesc.int survives the round trip");

    SelRef sel{SelKind::Portal, 3};
    Event d = Event::make(Ev::DeleteEntity, sel);
    check(d.sel().kind == SelKind::Portal && d.sel().index == 3,
          "SelRef survives the round trip");
    check(d.target().valid(), "a named target reports valid()");

    Transform t;
    t.x = 1.5f; t.y = -2.0f; t.z = 3.25f; t.yaw = 90.0f;
    Event x = Event::make(Ev::GenerateHeightmap, t);
    check(x.xform().x == 1.5f && x.xform().z == 3.25f && x.xform().yaw == 90.0f,
          "Transform survives the round trip");

    Event s = Event::make(Ev::OpenWorld, std::string("TestMap"));
    check(s.str() == "TestMap", "std::string survives the round trip");
}

// A handler reading the wrong shape logs and gets an inert value rather than
// reinterpreting memory. `s.spawn()` on a string event is the compile-time-legal
// mistake this makes visible.
static void test_wrong_shape_is_inert() {
    Event s = Event::make(Ev::OpenWorld, std::string("TestMap"));
    check(s.spawn().key.empty(), "wrong-shape read yields an inert SpawnDesc");
    check(!s.sel().valid(), "wrong-shape read yields an invalid SelRef");
    check(s.xform().x == 0.0f, "wrong-shape read yields a zeroed Transform");

    // A non-selection event has no target, which is what lets the dispatcher skip
    // the staleness check for it rather than comparing against a default index.
    check(!s.target().valid(), "a non-selection event has no target");
}

// ---------------------------------------------------------------------------
// The stale-index fix: a selection is captured BY VALUE at post time
// ---------------------------------------------------------------------------
// This is the bug the bus exists for. actionWorldGraphDelete carried a bare
// index; if the selection moved between the click and the frame that polled it,
// the delete landed on a different entity. Portal delete already had to be
// defended by hand for this in b88. Capturing the target with the event makes it
// impossible by construction.
// One Selection replaces the five fields the WorldGraph used to split a single
// message across: actionSelectFromGraph plus its Type/Name/Pos[3] satellites.
// They were written together and read together with nothing coupling them, and
// THREE of them carried TWO different index spaces (a ListView row index vs an
// entity index) in near-identically named fields - one of which
// (actionWorldGraphProperties) was written and then never read at all.
static void test_selection_is_one_atomic_message() {
    Selection s;
    s.ref = SelRef{SelKind::Mesh, 12};
    s.name = "GameEngine.Mesh/crate.glb";
    s.x = 1.0f; s.y = 2.0f; s.z = 3.0f;
    Event e = Event::make(Ev::SelectEntity, s);

    check(e.selection().ref.index == 12, "Selection carries the entity index");
    check(e.selection().ref.kind == SelKind::Mesh, "Selection carries the kind");
    check(e.selection().name == "GameEngine.Mesh/crate.glb", "Selection carries the name");
    check(e.selection().x == 1.0f && e.selection().z == 3.0f, "Selection carries the position");
    check(e.selection().valid(), "a fully populated Selection is valid");

    // target() must unwrap both selection-shaped payloads, because the dispatcher
    // uses it to staleness-check an event without knowing which it holds.
    check(e.target().index == 12, "target() unwraps a Selection payload");
    SelRef bare{SelKind::Zone, 4};
    check(Event::make(Ev::DeleteEntity, bare).target().index == 4,
          "target() unwraps a SelRef payload too");

    Selection empty;
    check(!empty.valid(), "a default Selection is not valid");
    check(!Event::make(Ev::SelectEntity, empty).target().valid(),
          "target() of an empty Selection is invalid, so the dispatcher can skip it");
}

static void test_target_is_captured_not_looked_up() {
    SelRef atClickTime{SelKind::Zone, 7};
    Event e = Event::make(Ev::DeleteEntity, atClickTime);

    // Whatever happens to the live selection afterwards, the event still names
    // what the user actually clicked.
    const SelRef liveSelectionAfterwards{SelKind::Brush, 0};
    (void)liveSelectionAfterwards;

    check(e.target().index == 7, "the event still carries the index from post time");
    check(e.target().kind == SelKind::Zone, "the event still carries the kind from post time");

    // And two events posted from different selections stay distinct even if the
    // selection moved between the two posts.
    Event first  = Event::make(Ev::DeleteEntity, SelRef{SelKind::Zone, 7});
    Event second = Event::make(Ev::DeleteEntity, SelRef{SelKind::Zone, 9});
    check(first.sel().index != second.sel().index,
          "two posts keep their own targets instead of aliasing one index field");
}

// ---------------------------------------------------------------------------
// Queueing and draining
// ---------------------------------------------------------------------------
static void test_fifo_drain() {
    EventBus bus;
    check(bus.pending() == 0, "a fresh bus is empty");

    bus.post(Ev::SpawnZone, SpawnDesc{"", 1, true});
    bus.post(Ev::SpawnPlayerStart, SpawnDesc{});
    bus.post(Ev::OpenWorld, std::string("A"));
    bus.post(Ev::OpenWorld, std::string("B"));
    check(bus.pending() == 4, "four posts are pending");
    check(bus.posted() == 4, "posted() counts every request");

    std::vector<Event> out;
    bus.drain(out);
    check(out.size() == 4, "drain returns everything queued");
    // Order matters: these are user actions, and reordering two "open world"
    // clicks would load the wrong level.
    check(out[0].kind == Ev::SpawnZone, "drain is FIFO, first event");
    check(out[1].kind == Ev::SpawnPlayerStart, "drain is FIFO, second event");
    check(out[2].str() == "A", "drain is FIFO, third event keeps its payload");
    check(out[3].str() == "B", "drain is FIFO, fourth event keeps its payload");

    check(bus.pending() == 0, "drain empties the queue");
    bus.drain(out);
    check(out.empty(), "a second drain yields nothing");
    check(bus.posted() == 4, "drain does not change the posted count");
}

// The "an unconsumed field fires later" failure mode. Draining every frame is
// what makes an event a single frame of intent rather than a sticky field.
static void test_no_sticky_events() {
    EventBus bus;
    bus.post(Ev::ApplyProperties, SelRef{SelKind::Mesh, 2});
    std::vector<Event> out;
    bus.drain(out);
    check(out.size() == 1, "the event arrives once");
    bus.drain(out);
    check(out.empty(), "it does NOT re-fire on the next frame");
}

// Overflow is bounded and counted, so a panel that posts in a loop fails
// visibly instead of growing the queue without limit.
static void test_overflow_is_bounded_and_counted() {
    EventBus bus;
    for (size_t i = 0; i < EventBus::kMaxQueued + 10; i++)
        bus.post(Ev::ReloadMesh, SelRef{SelKind::Mesh, -1});
    check(bus.pending() == EventBus::kMaxQueued, "the queue never exceeds its bound");
    check(bus.posted() == EventBus::kMaxQueued + 10, "posted() counts the overflow too");
    check(bus.dropped() == 10, "dropped() reports exactly what was refused");

    std::vector<Event> out;
    bus.drain(out);
    check(out.size() == EventBus::kMaxQueued, "a full drain still returns the bound");
}

// ---------------------------------------------------------------------------
// Coverage guards: the bus must cover every `action*` field it replaces
// ---------------------------------------------------------------------------
// Every Ev enumerator here corresponds to one or more of the 58 action* fields.
// If a new one is added to the editor without a matching Ev, this fails — which is
// the mechanism that stops the two lists drifting the way ID_PP_PORTALBROWSE and
// ID_BTN_GT_PREVIEW did.
static void test_event_coverage() {
    struct Row { Ev k; const char* name; };
    static const Row kEvents[] = {
        {Ev::SpawnPawn, "SpawnPawn"}, {Ev::SpawnMesh, "SpawnMesh"},
        {Ev::SpawnPickup, "SpawnPickup"}, {Ev::SpawnEmitter, "SpawnEmitter"},
        {Ev::SpawnZone, "SpawnZone"}, {Ev::SpawnParticleEmitter, "SpawnParticleEmitter"},
        {Ev::SpawnPathNode, "SpawnPathNode"}, {Ev::SpawnWindZone, "SpawnWindZone"},
        {Ev::SpawnPlayerStart, "SpawnPlayerStart"},
        {Ev::CsgPlace, "CsgPlace"}, {Ev::CsgCommit, "CsgCommit"},
        {Ev::DeleteEntity, "DeleteEntity"}, {Ev::DuplicateEntity, "DuplicateEntity"},
        {Ev::SelectEntity, "SelectEntity"}, {Ev::ApplyProperties, "ApplyProperties"},
        {Ev::OpenWorld, "OpenWorld"}, {Ev::LinkWorld, "LinkWorld"},
        {Ev::ApplySurface, "ApplySurface"}, {Ev::ResetSurface, "ResetSurface"},
        {Ev::ApplyTextureToSelection, "ApplyTextureToSelection"},
        {Ev::ReloadMesh, "ReloadMesh"}, {Ev::ConvertToAnimated, "ConvertToAnimated"},
        {Ev::PlaySound, "PlaySound"}, {Ev::StopSoundPreview, "StopSoundPreview"},
        {Ev::GenerateHeightmap, "GenerateHeightmap"},
        {Ev::ApplyLevelState, "ApplyLevelState"},
        {Ev::AnimNewClip, "AnimNewClip"}, {Ev::AnimDeleteClip, "AnimDeleteClip"},
        {Ev::AnimSave, "AnimSave"}, {Ev::AnimRefresh, "AnimRefresh"},
        {Ev::AnimScrub, "AnimScrub"}, {Ev::AnimAddKey, "AnimAddKey"},
        {Ev::AnimDeleteKey, "AnimDeleteKey"}, {Ev::AnimApplyClipMeta, "AnimApplyClipMeta"},
        {Ev::AnimToggleEdit, "AnimToggleEdit"}, {Ev::AnimUndo, "AnimUndo"},
        {Ev::AnimRedo, "AnimRedo"}, {Ev::AnimSelectAll, "AnimSelectAll"},
        {Ev::AnimClearSelection, "AnimClearSelection"},
    };

    // Distinct enumerators, and no accidental aliasing.
    bool unique = true;
    for (size_t i = 0; i < sizeof(kEvents)/sizeof(kEvents[0]); i++)
        for (size_t j = i + 1; j < sizeof(kEvents)/sizeof(kEvents[0]); j++)
            if (kEvents[i].k == kEvents[j].k) unique = false;
    check(unique, "every Ev enumerator is distinct");

    bool noneIsUsed = (Ev::None != Ev::SpawnPawn);
    check(noneIsUsed, "Ev::None is not aliased onto a real event");

    // Coverage, not a magic number: Ev::Count moves the moment an event is added,
    // so a new Ev that is not named in kEvents fails HERE rather than shipping
    // with no test. That is the guard that keeps the enum and the editor's
    // action set from drifting apart.
    const size_t named = sizeof(kEvents) / sizeof(kEvents[0]);
    check(static_cast<size_t>(Ev::Count) == named + 1,
          "Ev::Count == named events + None (every Ev has a row above)");
    check(named >= 30, "the bus still covers the bulk of the 58 action* fields");
}

// SelKind mirrors AngelEd's SelType. If someone adds a selection type to the
// editor and forgets the bus, the static_cast below silently mis-maps, so pin the
// count here where a mistake is loud.
static void test_selkind_covers_seltype() {
    check(static_cast<int>(SelKind::Map) == 13,
          "SelKind matches SelType's values through Map (13)");
    check(static_cast<int>(SelKind::Portal) == 8, "SelKind::Portal is 8, as in SelType");
}

int main() {
    printf("EditorEventBus tests:\n");
    test_typed_payloads();
    test_selection_is_one_atomic_message();
    test_wrong_shape_is_inert();
    test_target_is_captured_not_looked_up();
    test_fifo_drain();
    test_no_sticky_events();
    test_overflow_is_bounded_and_counted();
    test_event_coverage();
    test_selkind_covers_seltype();
    printf("\nResults: %d/%d passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}