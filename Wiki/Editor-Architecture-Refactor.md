# AngelEd Architecture Refactor — Core / Subsystems / UI / Resources + EditorEventBus

**Status: R0–R2 DONE, R3 next.** This file is the source of truth. Update the
progress log as items land; do not work from memory of a plan held in context.

**R2 result in one line:** 58 `action*` fields → **4**, and all 4 are documented
exceptions with a reason. Nine of the 58 turned out to have no writer at all. Five
**live** bugs were found and fixed along the way — see the R2 section.

This is the *deferred* follow-up to `Wiki/Editor-PropertyPanel-Refactor.md`. That
work fixed the editor's behaviour and deliberately did **not** restructure it,
because restructuring a 10,900-line pair of god-files while also hunting behaviour
bugs is how you ship two bugs at once. The defects are fixed; this document is
about maintainability.

## Why now, and why not before

The property-panel refactor was worth doing on its own merits and was kept
separate for one reason: **it could be verified**. Every change there was either
observable in a panel or pinned by a test. A layer split changes no behaviour at
all — its only output is "the next change is easier and safer" — so bundling it
with bug fixes would have made both unverifiable.

## The actual problem: 58 hand-rolled message channels

`EditorPanelState` carries **58 `action*` fields**. Each one is a hand-rolled
message: a panel's `WM_COMMAND` writes a field, `Main.cpp` polls all of them once
per frame, consumes, and resets. There is no envelope, no payload type, and no
record of who asked for what.

Three concrete failure modes, all of which are latent today:

1. **A stale index redirects the operation.** `actionWorldGraphDelete` is an
   index. If the selection moves between the click and the poll — which it can,
   because raylib mouse picking and the Win32 message pump both run inside the
   frame — the delete lands on a *different* entity. This exact hazard had to be
   defended by hand for portal delete in b88:
   > "Deleting the *selected* portal now clears the selection — `RemovePortal`
   > shifts every later portal down, so a retained target would have silently
   > edited a different portal."
   That is a bug class, not a missing null check, and an index in a loose field
   cannot express the fix.
2. **An unconsumed field fires later.** Nothing tracks whether a handler ran. A
   field set while the handler is disabled silently executes on a subsequent
   frame against whatever is selected then.
3. **There is no way to test any of it.** The channels only exist inside a Win32
   message handler and a raylib frame loop, so none of it is reachable from the
   headless suites. Every one of the 58 is verified only by clicking.

## Design

### Layers

```
AngelEd/Source/
├── Core/            frame loop, editor state, the bus, the dispatcher
│   ├── EditorState.hpp        selection, placement mode, gizmo, viewport flags
│   ├── EditorEventBus.hpp/.cpp  ← R1, raylib-free and Win32-free
│   └── EditorDispatcher.cpp   routes bus events to subsystems
├── Subsystems/      domain logic, no Win32, no raw raylib drawing
│   ├── Selection.{hpp,cpp}    pick / snap / gizmo targeting
│   ├── History.{hpp,cpp}      undo-redo snapshots
│   ├── Placement.{hpp,cpp}    spawn pawn / mesh / pickup / zone / pathnode / ...
│   ├── SurfaceOps.{hpp,cpp}   SetRenderableFace / ResetSurface / AutoConvex append
│   ├── LevelState.{hpp,cpp}   LevelMetadata + gametype + particles + skybox
│   └── AnimEditing.{hpp,cpp}  clip CRUD, keyframes, tool-scoped vertex undo
├── Resources/       asset discovery and packaging (shared by UI + Subsystems)
│   ├── AssetScope.{hpp,cpp}   the two-root (GameData)/(Packages) tree
│   ├── AssetScan.{hpp,cpp}    file/package enumeration per asset kind
│   └── PackageIO.{hpp,cpp}    PackIntoPackage / HotLoadPackage
└── UI/              Win32 panels. Widget code and nothing else.
    ├── UiCommon.hpp/.cpp      CreateLabel/Button/Ctrl, CHOOSECOLORW, file pickers
    ├── UiShell.cpp            class registration + create/destroy (unity TU)
    └── <Panel>.cpp            one per window, unity-included by UiShell.cpp
```

Dependency rule: **`UI` may call `Subsystems` and `Resources`; `Subsystems` may
call `Resources`; nothing calls back up.** `Core` calls down and owns the loop.
A panel that needs a domain operation posts an event; it does not reach into
`PawnSystem` itself.

### EditorEventBus

Raylib-free and Win32-free **on purpose** — that is what makes the central
invariant testable headlessly, the same discipline as `ZoneTypes.hpp`,
`GameType.hpp` and `SurfaceFlags.hpp`.

```cpp
namespace ed {
enum class Ev { SpawnPawn, SpawnMesh, SpawnPickup, ..., DeleteEntity, ... };

struct Event {
    Ev kind = Ev::None;
    std::variant<std::monostate, int, float, bool, std::string,
                 SpawnDesc, Transform, SelRef> payload;
};
}
```

Three properties that the current design cannot offer:

- **The payload is typed.** `std::variant` means the post site is checked by the
  compiler against the event kind, so a `SpawnZone` cannot be posted carrying a
  filename. The 58 loose fields have no such constraint — `actionSpawnZone` is an
  `int` and `actionSpawnPawn` is a `std::string`, and nothing stops them being
  confused.
- **A selection-sensitive event carries its target, by value.** `SelRef` is
  `{ SelType type; int index; }` captured **at post time**. The stale-index bug
  above becomes impossible by construction rather than by remembering a guard.
- **Post/drain is observable.** `posted()`, `dropped()` and a bounded queue make
  "the handler did not run" a testable condition instead of a mystery.

Draining stays deferred to one point per frame (inline dispatch from inside
`WM_COMMAND` would mutate the world while a panel is mid-layout). The safety comes
from the payload being complete, not from the timing.

### Build wiring — use a unity TU, deliberately

Splitting one 6,100-line `.cpp` into 14 would mean **14** new compile rules in
`AngelEd/Makefile` **and** 14 lines plus 14 link entries in
`.github/workflows/ci.yml`, which builds the editor by hand. That is the
"added in one place, CI breaks silently" trap from `AGENTS.md`, magnified 14x,
and the Makefile and CI list can drift silently.

So `UI/UiShell.cpp` is a single translation unit that `#include`s the per-panel
`.cpp` fragments — the same pattern already used for `raygui.c` in both build
files. **One** new object, **one** Makefile rule, **one** CI line. The trade is
that a fragment can rely on an include it does not state; that is a real cost and
it is accepted here because the alternative is a CI that breaks invisibly.

`Core/` and `Subsystems/` get real `.cpp` files, because those are the layers
whose *interfaces* matter and they are far fewer per layer.

## Phases

| Phase | Item | Status |
|---|---|---|
| R0 | this document | **done** |
| R1 | `EditorEventBus` + headless suite + `make test` in CI | **done** |
| R2 | convert the action fields, batch by batch - **done** | 58 |
| R3 | split `Win32Dialogs.cpp` into `UI/` (unity TU) | pending |
| R4 | split `Main.cpp` into `Core/` + `Subsystems/` | pending |
| R5 | `AGENTS.md` + full verification | pending |

R2 is split into batches so each is independently reviewable and each deletes its
fields as it goes:

| Batch | Events | Fields |
|---|---|---|
| B1 | placement / spawn — **done, and it was deletion not conversion** | 10 |
| B2 | world graph — **done** (level list deferred to B5, it is a world *name*, not a selection) | 8 |
| B3 | surface / CSG / portal / reload / convert — **done** | 8 |
| B4 | animation - **done** (11 events; Save/Refresh stay fields) | 12 |
| B5 | assets + level list - **done** | 20 |

## Invariants — do not regress

- **A panel posts an event; it never mutates the world directly.** If a `UI/`
  file calls `PawnSystem::Add*`, the layer rule is already broken.
- **One payload per event kind**, checked by `std::visit` at the handler. No
  `if (e.kind == Ev::X) reinterpret_cast<...>`.
- **A selection-sensitive event captures its target at post time.** Never read
  `g_sel` inside a handler for an event that names an index.
- **Every converted field is deleted in the same commit that stops writing it.**
  A field that is written but never read is worse than one that does not exist —
  that is how `ID_PP_PORTALBROWSE` and `ID_BTN_GT_PREVIEW` survived.
- **CI must build the editor from the same object list the Makefile uses.** R3
  is not finished until `ci.yml` and `AngelEd/Makefile` agree; the check is a
  diff of the two lists, not a build.

## Ruled out — do not re-investigate

- **Converting the whole thing at once.** 58 fields, 14 panels and 4,774 lines of
  `Main.cpp` in one change cannot be reviewed and cannot be bisected.
- **A generic `EventBus` with string topics.** The engine has no reflection and
  no build-time key checking, so a string bus would trade a compile error for a
  runtime typo — strictly worse than the `action*` fields.
- **Immediate (inline) dispatch from `WM_COMMAND`.** A panel is mid-layout when it
  posts; mutating the world underneath it is how you get a crash that only
  reproduces when you click fast.
- **Moving `LevelMetadata` out of `UI/`.** It is level *state* and belongs to
  `Subsystems/LevelState`; it only appears to live in `UI/` because the deleted
  Zone window used to mirror it into a global.

## Progress log

- **R0** — plan written after the b88 tag. Survey at that point: `Win32Dialogs.cpp`
  6,109 lines / 14 window procs, `Main.cpp` 4,774 lines, 58 `action*` fields, 59
  tracked files under `AngelEd/`.

### R1 notes

- **`EventBus` is a value type with a public constructor, plus `instance()`.** It
  started as a private-ctor singleton and the test could not compile. A bus you
  cannot instantiate is a bus you cannot test, and testability is the entire
  reason this type is raylib-free — so the constructor is public and `instance()`
  is only the production convenience.
- **`Ev::Count` is a sentinel, not an event**, and the suite asserts
  `Ev::Count == named + 1`. That is the anti-drift mechanism: add an `Ev` without
  adding it to `kEvents` in the test and the suite fails. My first attempt
  hardcoded "39 values" and was wrong (39 events + None = 40), which is exactly
  the kind of stale constant the sentinel removes.
- **A wrong-shape payload read logs and returns an inert value** rather than
  aborting. Crashing an editor frame is worse than a logged mismatch, and the
  alternative — `reinterpret_cast` on the variant — is the bug this design exists
  to prevent.
- **CI now runs `make test`.** It previously ran only `worldcheck`, which is why
  `test_ozls_writer` and `test_surface` sat unrun. Two details that matter:
  - `make test` invokes each suite as `-./suite`, so it continues past failures and
    its exit code is useless. The new step tees the log and greps
    `^[[:space:]]*FAIL` — one pattern covers every suite's format.
  - A **compile** failure is still fatal, because only the `./suite` invocation is
    `-`-prefixed; the compile rules are not. So the grep covers execution and make
    covers build, with no gap.
- **CI runs the suites in the Linux job only.** The Windows job builds the editor,
  which is where AngelEd compilation is actually verified; the bus itself is
  platform-independent.

### R2 B1 — all ten fields were unreachable, so the batch is a deletion

Auditing the writers of every `action*` field before converting any of them found
that **the entire placement batch was dead**. For all nine `actionSpawn*` fields
the only assignments anywhere in `AngelEd/Source/` were the handlers' own resets to
their defaults:

```
$ grep -n 'actionSpawn\w*\s*=[^=]' AngelEd/Source/*.cpp AngelEd/Source/*.hpp
Main.cpp:4661:    g_editorPanels.actionSpawnParticleEmitter = false;
Main.cpp:4672:    g_editorPanels.actionSpawnPathNode = false;
...
```

So ~110 lines of handlers in `Main.cpp` could never run. Placement in fact happens
**synchronously** in `SpawnSelectedPawnTreeItem` (`Win32Dialogs.cpp:1778`), which
covers all nine types — pawn, pickup, player start, emitter, zone, mesh, particle
emitter, path node, wind zone — using `spawnPos`. The dead handlers would also
have placed at `MainCamera.target` instead of the ghost position, so they were not
merely redundant but *wrong*.

**This is why the batch is an audit first and a conversion second.** The plan
assumed 58 live channels; it is closer to 49. `Ev::Spawn*` still earns its place —
`SpawnSelectedPawnTreeItem` reaching into `PawnSystem` directly is exactly the
UI→Subsystems dependency R3/R4 removes — but its first real users arrive there,
not here.

### B1 also caught a regression I introduced in b88

`actionApplyLight` was the **only** writer of the legacy light apply handler in
`Main.cpp`, and `LightPropsProc` was that writer. Deleting the Light Properties
window in b88 therefore orphaned ~27 lines. It was not caught at the time because
"a handler nothing can reach" produces no build error and no test failure.

It was also already a strict **subset** of the panel's `tgtType == SelType::LIGHT`
branch (`Main.cpp:3940`), which additionally handles position, name and target and
clamps `type`/`effect` defensively. Removed, along with its 12 `light*` backing
fields — which nothing else referenced.

> **Lesson worth keeping:** removing a producer must include a check that its
> consumer went with it. `grep` for the *handler*, not just the field.

Action fields: **58 → 49** (9 `actionSpawn*` + `actionApplyLight` removed).

### R2 B2 — the first real bus use, and it fixed a live bug

Nine fields became four events (`SelectEntity`, `ApplyProperties`, `DeleteEntity`,
`DuplicateEntity`), and the conversion surfaced a defect that was live in the
editor right now:

```cpp
g_editorPanels.actionWorldGraphDelete = e.selIndex;          // writer
...
if (g_editorPanels.actionWorldGraphDelete >= 0) {
    if (g_sel.type != SelType::NONE) DeleteSelectedEntity();  // reads g_sel, NOT the index
```

The index was written and never read. Delete and Duplicate acted on whatever
`g_sel` was, which was correct **only** because the select field happened to be
written in the same `WM_NOTIFY` — an ordering coincidence, not a guarantee. The
same batch found:

- **Two index spaces in three near-identically named fields.**
  `actionWorldGraphProperties` held a ListView **row** index while
  `actionWorldGraphDelete`/`Dup` held an **entity** index.
- **`actionWorldGraphProperties` was never read at all.** Its handler just called
  `OpenPropertiesForSelection()`, i.e. acted on `g_sel` too.
- **One message spread over five fields.** `actionSelectFromGraph` plus its
  `Type`/`Name`/`Pos[3]` satellites were written together and read together with
  nothing coupling them, so a handler could read a new index against the previous
  frame's type and position without noticing. Now one `ed::Selection`.

`AdoptSelection()` takes the target from the event rather than from `g_sel`, which
makes the ordering irrelevant instead of merely currently-fine.

#### Decisions worth recording

- **`ToBusKind(SelType)` is a switch, not a `static_cast`.** The two enums have
  identical values so a cast would work — and would keep compiling after someone
  adds a 15th `SelType`, silently mis-dispatching every event for it. A switch with
  no `else` turns that omission into a compile error. It lives in `SelType.hpp`,
  not in the bus, because that is the only place both enums are visible.
- **The drain happens BEFORE dispatch**, and into a file-static vector. A handler
  that opens a panel can post further events; draining while iterating the live
  queue would dispatch those in the same frame.
- **`e.selType < 0` is mapped to `SelKind::None`.** `sel::DEF_ONLY` (-1) is a
  Script-Manager sentinel, not a `SelType`, and it means "no live target".
  `AdoptSelection` then rejects it, so Properties/Delete on such a row does nothing
  rather than acting on the previous selection.
- **Unhandled events are counted** (`g_editorUnhandledEvents`), not ignored, so a
  batch that posts an event nobody handles shows up as a counter rather than a
  click that silently does nothing. Should be 0 once R2 completes.

`actionLevelListOpen` / `actionLevelListLink` were **not** converted here: they
carry a world *name*, not a selection, and belong with the other asset/world
operations in B5.

Action fields: **49 → 40**. `test_editorbus` 46/46.

### R2 B3 — surface, CSG, portal, reload, convert (8 fields)

Converted to `ApplySurface`, `ResetSurface` (both a new `ed::SurfaceEdit`),
`CsgPlace`/`CsgCommit` (a new `ed::CsgIntent`), `DeletePortal`, `ReloadMesh`,
`ConvertToAnimated`.

Two more live defects fixed on the way:

- **`Reload` could reset an unrelated object's cache.** The handler read
  `g_sel.index` and only checked `g_sel.type == SelType::MESH` *after* indexing, so
  a Reload pressed while something else was selected called `GetMeshObject()` with
  a foreign index. It now carries a `SelRef` and guards on the kind as well.
- **`Convert to Animated` converted whatever was selected at drain time** rather
  than the mesh whose button was pressed. Same shape of bug as the WorldGraph one.

#### Decisions worth recording

- **`actionApplyProperties` deliberately STAYS a field.** There is no target to
  capture: the properties panel owns `propsTargetType` / `propsTargetIndex` itself
  and is both the only writer and the only reader, so an event would add a queue
  hop without adding a payload that could go stale. It converts in **R4**, when the
  panel stops owning the target directly — which is the last reader of live panel
  state. Recorded here so its absence is a decision, not an oversight.
- **`SurfaceEdit` carries the mask, and `ResetSurface` posts a mask too.** The
  viewport's `IDM_SURFACE_RESET` route previously set a bare bool, so it reset only
  the last-clicked face and ignored the Shift multi-selection the dialog title
  advertises ("(N Selected)"). It now rebuilds the mask from
  `g_selectedSurfaces`, which is what both routes should have used.
- **Surface events are drained in their own pass** (`g_editorSurfaceEvents`), not
  the shared `g_editorFrameEvents`. Those handlers are the ones moving to
  `Subsystems/SurfaceOps` in R4, and a separate pass means that move does not have
  to reorder the selection dispatch.
- **`CsgPlace` and `CsgCommit` stay separate kinds.** "Arm a primitive" resets the
  ghost to a default box at the camera; "commit the ghost" reads the ghost as it is.
  One merged event would either lose the reset or commit a brush nobody positioned.

#### The `Ev::Count` sentinel earned its keep

Adding `DeletePortal` made `test_editorbus` fail immediately:

```
FAIL  Ev::Count == named events + None (every Ev has a row above)
```

That is the anti-drift mechanism from R1 doing exactly its job on the first real
use — a new event kind with no test row is a **failing suite**, not an untested
path. It is the direct successor to `ID_PP_PORTALBROWSE` and `ID_BTN_GT_PREVIEW`,
which sat declared, created and never handled for a long time.

Action fields: **40 → 32**. `test_editorbus` 55/55.

#### Process note: a brace-balance check is worth having

Deleting the CSG handlers by line range left one `}` behind (the block's close was
at 4769, the cut ran to 4768), which closed the render loop early. The symptom was
a cascade of ~15 unrelated "expected constructor" errors 1,700 lines later. A
five-line brace-count loop after every range deletion found it immediately, and is
now part of the routine for the remaining batches.

### R2 B4 — animation (11 of 13 fields)

Eleven commands became `ed::Ev::Anim*` carrying a new `ed::AnimIntent` (mesh id,
clip name, playhead, fps/loop). Another live staleness bug fixed:

> **Delete Clip deleted a different clip than the one clicked.** The handler read
> `animClipName` from live panel state at drain time. A clip list re-selects on
> every click, so this was easy to trigger — click Delete, and the clip that goes
> is whatever the list holds when the frame drains. The same applied to Add/Delete
> Key (`animTime`) and ApplyClipMeta (`animFps`/`animLoop`).

Two fields **deliberately stayed**, and this is the batch's main judgement call:

| Field | Why it is not an event |
|---|---|
| `actionAnimSave` | An **intra-frame chaining signal**. NewClip, DeleteClip, AddKey, DeleteKey and ApplyClipMeta all set it from inside their own handlers, and the file write consumes it later in the *same* frame. Queueing it would delay every save by a frame for no benefit — and the write is part of the command completing, not an independent user action. |
| `actionAnimRefresh` | A **dirty flag**. Set by 7 sites including handlers; consumed by `RefreshAnimPanel()`. It means "the panel needs re-reading", not "the user did something". |

Treating these as events would be the "convert everything" reflex applied to two
signals that are not messages. `test_editorbus` asserts their **absence** from the
enum, so a future "fix" for that asymmetry has to be deliberate.

#### The mesh-id guard is a stopgap, and says so

`AnimIntent` carries `meshId`, but `AnimTarget()`, `AnimSnapshotPush()`, `AnimUndo`
and the snapshot stack all resolve the **current** target internally, so the id
cannot yet be threaded through them. Rather than leave that silent,
`ApplyAnimIntents` detects the mismatch and skips + logs the command:

```
EditorLog("Anim: command skipped, target moved (event=%d was for %d, now %d)", ...)
```

`Subsystems/AnimEditing` in R4 threads the id properly; until then a moved target
is a visible no-op instead of an edit to the wrong mesh.

- **Ctrl+Z / Ctrl+Y now posts the same events the toolbar does**, so keyboard undo
  is not a second code path with its own staleness behaviour.
- **`PostAnimIntent()` reads fps/loop off the controls**, not the mirrored panel
  fields, so an intent cannot be built from values the panel has already moved past.
- **All eleven handlers moved out of `main()`** into `ApplyAnimIntents()` — the
  first real step toward `Subsystems/AnimEditing`, and the reason animation drains
  into its own `g_editorAnimEvents` pass.

Action fields: **32 → 23**. `test_editorbus` 63/63.

### R2 B5 — assets, level list (18 fields) — and R2 is complete

New payloads: `PlacementRequest`, `TextureApply`, `HeightmapDesc`. Events:
`BeginPlacement`, `ApplyTextureToModel`, `PlaySound`, `StopSoundPreview`,
`GenerateHeightmap`, `RefreshModelBrowser`, `OpenWorld`, `LinkWorld`.

One more live staleness bug:

> **The Pickups panel could place the wrong pickup.** It sent a **list index** and
> the main loop resolved it with `LegacyPickupType(idx)` at drain time. A panel
> rebuild between the click and the frame placed a different def. The panel now
> resolves the name itself — the button IDs are minted from the same
> `FindByType(EntityType::PICKUP)` order the panel builds its list from, so the
> lookup is exact — and the **name** travels in the event.

The six `actionHm*`/`actionHeightmap*` fields collapse into one `HeightmapDesc`;
four of them were read at drain time, so a panel edit between click and frame
generated the heightmap from values the panel had already moved past.

#### Three fields renamed off the `action*` prefix

The prefix was *lying* about these — they are live state the loop re-reads every
frame, not messages:

| Was | Now | Why |
|---|---|---|
| `actionSoundVolume` | `previewSoundVolume` | Volume slider; re-applied every frame while the preview plays |
| `actionSoundLoop` | `previewSoundLoop` | Same |
| `actionSoundCategory` | `previewSoundCategory` | Same |
| `actionRefreshBrowser` | `refreshModelBrowser` | Dirty flag, exactly like `actionAnimRefresh` |

Only the **path** became an event. That split matters: a category switch between
the click and the frame would otherwise preview from the wrong list, which is
precisely why the path travels and the sliders do not.

## R2 result: 58 fields → 4, and every one of the 4 is justified

| Remaining field | Why it is not an event |
|---|---|
| `actionApplyProperties` | No target to capture — the properties panel owns `propsTargetType`/`propsTargetIndex` itself and is both the only writer and only reader. Converts in **R4** when the panel stops owning the target. |
| `actionApplyTextureToSel` | Reads `g_sel`/`activeTexturePath` at drain time — normally the bug this refactor kills, but both writers are **context-menu** commands and `TrackPopupMenu` is **modal**, so nothing else pumps input and the selection cannot move. Capturing a Selection would mean duplicating the brush renderable resolution (which depends on `g_sel.pos` to tell a renderable index from a collision-volume index) for no behavioural gain. |
| `actionAnimSave` | **Intra-frame chaining signal.** The anim handlers set it themselves; the file write consumes it in the *same* frame. Queueing delays every save a frame. |
| `actionAnimRefresh` | **Dirty flag** consumed by `RefreshAnimPanel()`. Means "re-read the panel", not "the user did something". |

`58 → 4`. Nine of the original 58 turned out to have **no writer at all** (B1), so
the number of channels that actually needed converting was ~45, not 58. Nine fields
that were never messages were renamed to stop pretending otherwise.

**Live bugs found across R2: five.** WorldGraph Delete/Duplicate read `g_sel`
instead of their own index; Reload indexed `GetMeshObject()` with a foreign index;
Convert-to-Animated converted the wrong mesh; the Pickups panel resolved an index
at drain time; and the b88 Light-window deletion orphaned its apply handler. All
fixed.

## Verification

- `make MODE=debug OTENGINE AngelServ AngelMaster ozpack` and
  `make -C AngelEd MODE=debug` clean at every phase.
- `make test` green — **including `test_editorbus`, which does not exist until R1.**
- `ci.yml`'s AngelEd object list must equal `AngelEd/Makefile`'s `EDITOR_OBJS`,
  verified by hand after R3.
- Manual: every menu entry, toolbar button, panel context menu and drag in the
  editor, because a layer split changes no behaviour and so has **no** headless
  signal at all. That is the main risk in this document and it is why R3/R4 are
  separate commits with a manual pass between them.