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

### Editor

AngelEd loads, edits and exports OZONE only. Export path:
`ExportToOzone()` in `AngelEd/Source/Main.cpp`. Reference worlds:
`GameData/Worlds/*/World.ozone`.

### Shared I/O helpers

`Source/PPGIO.hpp` provides `LoadFile`, `WSplitValue`, `WReadValue`, `ToFloat`
(save/config parsing only).
