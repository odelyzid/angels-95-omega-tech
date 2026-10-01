# World Format (OZONE)

OmegaTech uses a single world description format: **OZONE**.

> **Note:** The legacy **WDL** (World Description Language) format and its runtime
> fallback loader have been **removed**. OZONE plus the LightningScript/Pawn
> entity system are the only source of truth. A world without a
> `GameData/Worlds/<Name>/World.ozone` is a hard load error.

## OZONE Format

OZONE is a line-oriented text format supporting CSG brush primitives and
entity metadata (player starts, pickups, NPCs, zones, lights, meshes, particle
emitters, path nodes, wind zones).

### Primitives

```
add box    <cx> <cy> <cz> <sx> <sy> <sz> <rot> [texPath=... texScaleU=.. texScaleV=.. texOffsetU=.. texOffsetV=.. flags=..]
add cyl    <cx> <cy> <cz> <rt> <rb> <h> <slices> <rot> [...]
add sph    ...
add pyr    ...
add pln    ...
sub / intersect <primitive> ...   # CSG carve
```

### Surface flags

| Bit | Name | Meaning |
|---|---|---|
| `1<<3` (8) | `SURF_FAKEBACKDROP` | brush renders as a sky backdrop (drawn by the zone pass, skipped by the world pass) |
| `1<<4` (16) | `SURF_COLLISION_PROXY` | generated AutoConvex collision box — feeds the CSG collision world but is **never drawn in game** |

`SURF_COLLISION_PROXY` lines are ordinary `add box` lines with a `flags=16`
kwarg; they round-trip through the text format unchanged. AngelEd writes them
when you use **Append AutoConvex Collision**, and only shows them when the
sidebar **View → Collision** toggle is on.

### Collision is AABB-only

The collision world is built by `CsgProcessor` (`Source/Physics/OzBsp.hpp`), which
consumes **axis-aligned boxes only**. Two consequences:

- `sub` / `intersect` are **modifiers**, not shapes. They subtract from, or
  intersect with, solids that already exist. A world whose only brush is
  `sub box ...` has **zero** collision volumes — the brush still renders, so it
  looks like a floor while the player falls straight through it. Author floors
  as `add`, and carve openings with `sub`.
- A placed `Mesh.Static` / `Mesh.Skeletal` entity has **no collision at all**;
  the mesh entities are client-cosmetic. Use AngelEd's
  **Append AutoConvex Collision** to generate boxes for it.

### Entities

| Entity | Syntax |
|---|---|
| Player start | `playerstart <x> <y> <z> <yaw>` |
| Pickup | `pickup <name> <x> <y> <z> [respawn=]` |
| NPC | `npc <type> <x> <y> <z> <yaw>` |
| Light | `light <point|spot|dir> <x> <y> <z> ...` |
| Zone | `zone <type> <minX> <minY> <minZ> <maxX> <maxY> <maxZ> <intensity>` |
| Static mesh | `Mesh.Static <path> <x> <y> <z> <yaw> [scale=] [tex=] [wind=1]` |
| Skeletal mesh | `Mesh.Skeletal <path> <x> <y> <z> <yaw> [scale=] [tex=] [anim=Clip] [speed=] [animfile=] [wind=1]` |
| Particle emitter | `ParticleEmitter <type> <x> <y> <z> [rate life speed ...] [tex=]` |
| Path node | `PathNode <name> <x> <y> <z> [radius=R] [next=a,b,c] [loop]` |
| Wind zone | `WindZone <minX> <minY> <minZ> <maxX> <maxY> <maxZ> <dirX> <dirY> <dirZ> <strength> [freq]` |

Zones are scripted by sibling `<name>.ozls` skyzone files. The axis convention
is **Z-up**.

#### Per-zone physics overrides (b80)

Any `zone` line may carry named kwargs that override
`oz::physics::PhysicsInfo` (defaults: gravity 18, jump 9, terminal 60,
water gravity 8 / drag 0.95 / swim-up 5, ladder speed 6, fly multiplier 1.5):

```
zone water ... gravity=12 jump=7 terminal=40 water_gravity=6 water_drag=0.9 swim_up=4
zone ladder ... ladder_speed=8
zone sky ... fly_mult=2.0
```

Accepted keys: `gravity=`, `jump=`, `terminal=`, `water_gravity=`,
`water_drag=`, `swim_up=`, `ladder_speed=`, `fly_mult=`. Also order-independent
named kwargs on entity lines: `name=`, `tex=`, `anim=`, `speed=`, `next=`,
`loop`, `radius=`, `scale=` (see authoring traps below for the positional
traps).

#### Light `.ozls` defaults (`: light`)

A `light` line's `name=` resolves an `.ozls` def of type `: light` — exactly
the way a zone's `name=` resolves its skyzone def. A world-scoped `torch.ozls`
sitting beside `World.ozone` is picked up automatically; the reference def is
`GameData/Global/Lights/Light.ozls`.

```
light point 0 0 4 255 180 90 1.2 12 name=torch flare=1
```

**The def is a defaults layer, not an override.** Anything the light line
authored always wins, which means:

| Key | Effect | When |
|---|---|---|
| `effect` | `LitLightEffect` (0 none, 1 watery, 2 torch, 3 fire, 4 lamp) | line omitted `effect=` |
| `flare` / `corona` | billboard halo flags | line omitted the matching kwarg |
| `period` | flicker cycle length, seconds | always |
| `cast_shadow` / `is_static` | `LightNode::castShadow` / `::isStatic` | always |
| `inner_cone` / `outer_cone` | spot cone **cosines** (`LightNode` stores cosines, not degrees) | always |

`intensity`, `radius`, `color`, `position` and `target` are **not** in the schema:
they are positional on every light line, so a def able to override them would
silently retune every already-saved level the next time the def was touched.

### Editor

AngelEd loads, edits and exports OZONE only. Export path:
`ExportToOzone()` in `AngelEd/Source/Main.cpp`. Reference worlds:
`GameData/Worlds/*/World.ozone`.

### Shared I/O helpers

`Source/PPGIO.hpp` provides `LoadFile`, `WSplitValue`, `WReadValue`, `ToFloat`
(save/config parsing only).

## Authoring traps

Each of these fails **silently** - the line parses, the value is just ignored,
or the wrong thing happens. All four shipped worlds hit at least one of them
before being fixed.

### `texSlot` is a bare positional, not `key=value`

`texSlot` is read as positional argument 8 for `box` (and 9 for `cyl`), i.e.
immediately after `<rot>`:

```
add box 0 0 0.75 4 1.5 4 0 3        # correct: texSlot = 3
add box 0 0 0.75 4 1.5 4 0 texSlot=3   # WRONG: token silently dropped
```

`OzoneParser` only recognises `flags=`, `texScaleU/V`, `texOffsetU/V` and
`texPath=` as brush keywords. Writing `texSlot=3` hits the catch-all branch,
is thrown away, and the primitive falls back to auto-selection (`h < 1` -> slot
1, otherwise slot 2). The brush still looks *plausible*, just wrongly textured.

Tile slot order is the tileset filenames **sorted ascending**
(`oztex/tileset/*.png`), so `01_..`, `02_..`, `03_..` map to slots 1, 2, 3.
The loader sorts explicitly, so slots are stable across machines.

### `light directional <x> <y> <z>` is the light SOURCE

The authored point is where the sun/moon **is**; the light is aimed at the world
origin. A large positive `z` therefore puts the source overhead and lights the
ground. Writing it the other way round (target above the map) makes `lightDir`
point downward, every up-facing surface gets `NdotL = 0`, and the level renders
almost black regardless of how high you crank the intensity.

### `cyl` spans `z = [cz-h, cz]`

The authored `cz` is the **top** of the cylinder, not its centre - the loader
shifts the generated mesh down by `h/2`. `box` is centre-based. Mixing these up
is why props and columns in old revisions sat half-buried in the floor.

`rTop`/`rBot` are also collapsed to `max(rTop, rBot)`: cones are not cones, at
load time or on editor round-trip.

### Always name your zones, and expect `zone sound` to be script-inert

Auto-generated zone names (`zone_sky_0`, `zone_water_0`, ...) are per-load
counters, so **every world collides on them**. `.ozls` defs live in one global
registry keyed by name; `LightningEntityRegistry::LoadWorldOverrides()` is called
on every `LoadWorld()` to re-point that map at the world being played. Without a
`name=` you are relying on that, and a mismatch means the level runs another
world's `set_ambient` / `set_fog` / `set_skybox` (or none at all).

Relatedly, `Main.cpp` deliberately skips `ZONE_SKY` and `ZONE_GAMEPLAY_SOUND`
when dispatching LightningScript `on_enter`/`on_exit`. A `zone sound` still works
for audio - `SoundManager::UpdateSoundZones` consumes its `GameplaySoundProfile`
- but its `.ozls` message will never fire.

### Trailing comments eat bare numbers

Brush lines ignore unrecognised tokens, so `# at 12 4` is harmless. But
`std::stof` is attempted first, and a bare number in a comment is swallowed as
the *next positional argument* - which for `box` is `texSlot`. Write comments
without numbers, or put them on their own line.

### `sub` never cuts the render mesh

`sub` and `intersect` carve **collision volumes only**. The visible brush stays
whole, so a subtracted pit is an invisible hole the player falls into. Build
openings from separate pier/lintel boxes and stepped runs instead.

## Previewing / screenshotting a world

`Angels95.exe --world <Name> --shot <out.png> [--shot-delay N] [--shot-res WxH]
[--shot-cam "x,y,z,yaw[,pitch]"]... [--shot-hud]` captures deterministic,
HUD-free frames. Coordinates are **OZONE Z-up** with yaw/pitch in degrees.
Shot mode skips the splash, title menu and audio, forces vsync/MSAA/pixel/
jitter/fog/head-bob off, hides the hotbar, HUD, crosshair, view-model, message
log and all authoring gizmos, and freezes the camera. `GameData/Launch.conf` is
skipped so `--shot-res` is authoritative. See `Source/Screenshot.hpp`.

## Level-scale conventions

The player collision box is **3 units tall with a 2-unit eye height**
(`PlayerMovement::Height` / `PLAYER_EYE_HEIGHT`), and `kStepHeight = 1.0`
(`Physics/PlayerPhysics.hpp`).

- Floor tops at a single shared `z`. Overlapping slabs z-fight visibly.
- Ceiling undersides at `4.0` or more.
- Stair treads rise exactly `1.0`, or they are not walkable.
- Cover props must top out at least `1.5` above their floor - anything within
  `1.0` of the feet is a step, not a wall.

Walls taller than that only *read* as taller; a 10-unit wall in a 30-unit room
is what made the old maps read as shafts rather than halls.
