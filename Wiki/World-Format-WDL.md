# World Format (WDL / OZONE)

OmegaTech uses two world description formats:

- **WDL** (World Description Language) — colon-delimited plain text, parsed by `WDLParser` (standalone, no raylib)
- **OZONE** — extended format used by the editor, supports CSG primitives and additional entity metadata

## WDL Format

### Syntax

```
Instruction:arg1:arg2:...:
```

Each line is an instruction with colon-delimited arguments. Lines are parsed at world load time by `WDLParser` (`Source/Server/WDLParser.cpp`). Comments starting with `#` are supported. Trailing colons are normalized.

### Instructions

| Instruction | Arguments | Description |
|---|---|---|
| `HeightMap` | `x y z scale rotation` | Terrain heightmap position and scale |
| `Model<N>` | `x y z scale rotation` | Place a 3D model (Model1.obj etc. from `Models/` directory, N=1-20) |
| `Object<N>` | `x y z scale rotation` | Place a collectible object/item |
| `Script<N>` | `x y z scale rotation` | In-world script trigger |
| `NE<N>` | `x y z` | Noise emitter (ambient sound at position) |
| `Pickup` | `type x y z` | Place a pickup node (type is string name, e.g. `HealthVial`, `Coin`, `Key`, or legacy numeric ID) |
| `Spawn` | `x y z yaw` | Player spawn point with yaw rotation |
| `NPC` | `type x y z` | NPC spawn by definition name (e.g. `Walker`, `Skaarj`, `Brute`, `Floater`). Legacy: `Walker:x:y:z:` also accepted |
| `Light` | `x y z` | Point light position |
| `LightType` | `type x y z` | Light with type string |
| `Fog` | `r g b density` | Global fog settings |
| `Ambient` | `r g b intensity` | Global ambient lighting |
| `ClipBox` | `x y z scale rotation w h d` | Collision clip box |
| `Collision` | `x y z scale rotation` | Simple collision volume |
| `AdvCollision` | `x y z scale rotation w h l` | Advanced collision volume |
| `ZoneInfo` | `type minX minY minZ maxX maxY maxZ intensity` | Zone volume (type: 0=Water, 1=Ladder, 2=Sky, 3=Reverb, 4=GameplaySound) |
| `C` | _(none)_ | Collision flag — marks the next placed model as having collision |

### World Directory Layout

```
GameData/Worlds/<WorldName>/
  World.wdl                # World description file
  Models/
    HeightMap.png           # Grayscale heightmap image
    HeightMapTexture.png    # Texture overlay for terrain
    Model1.obj              # 3D model files (Model1-20.obj)
    Model1Texture.png       # Per-model textures
    Model2.obj
    Model2Texture.png
    Skybox.png              # Skybox cubemap texture
  Scripts/
    Launch.ps               # Launch script (ParasiteScript format)
  Music/
    Main.mp3                # Background music
  NoiseEmitter/
    NE1.mp3                 # Ambient loop for noise emitter 1
    NE2.mp3
    NE3.mp3
```

## OZONE Format

OZONE is an extended plain-text format used by the AngelEd editor. It supports model/entity placement plus CSG brush primitives.

### Primitive Types

| Type | Syntax | Description |
|---|---|---|
| `box` | `box x y z w h d rot` | Cuboid primitive |
| `cyl` | `cyl x y z rTop rBot h slices rot` | Cylinder primitive |
| `sph` | `sph x y z r segments` | Sphere primitive |
| `pyr` | `pyr x y z w d h` | Pyramid primitive |
| `pln` | `pln x y z nx ny nz dist` | Plane primitive |
| `heightmap` | `heightmap imgPath texPath x y z scale sizeX sizeY sizeZ` | Terrain heightmap |

### Entity Instructions (exported from editor)

```
playerstart x y z yaw
pickup type x y z [respawnTime]
npc defName x y z
zone type minX minY minZ maxX maxY maxZ intensity [fogR fogG fogB fogDensity fogStart fogEnd ambR ambG ambB ambIntensity reverbMix reverbDecay]
emitter sound|music x y z
```

### CSG Operations

Each brush primitive stores a CSG operation metadata:

| Value | Operation |
|---|---|
| 0 | SOLID (additive, no boolean) |
| 1 | ADD (additive volume) |
| 2 | SUB (subtractive — carves void) |
| 3 | INTERSECT (keep only overlap) |
| 4 | DE_RESC (same as SUB) |

The `CsgProcessor` in `Source/Physics/OzBsp.hpp` implements AABB-based boolean operations. In the editor, brushes are placed with their CSG operation stored as metadata but the backend processor is not yet called.

## Package Loading

World files can be packaged into `.ozone` containers in `System/Data/Zones/`. When loading from a package, file paths inside the OZONE file are resolved relative to the world directory:

```
System/Data/Zones/world_<WorldName>.ozone
    -> resolves Models/HeightMap.png to GameData/Worlds/<WorldName>/Models/HeightMap.png
```

World textures (`.oztex`) are loaded by `OzoneLoader::LoadWorldTextures()`.
