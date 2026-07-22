# World Format (WDL / OZONE)

OmegaTech uses two world description formats:

- **WDL** (World Description Language) — colon-delimited plain text, loaded by client and server
- **OZONE** — extended format used by the editor, supports CSG primitives and additional metadata

## WDL Format

### Syntax

```
Instruction:arg1:arg2:...:
```

Each line is an instruction with colon-delimited arguments. Lines are parsed at world load time by `WDLParser` (`Source/Server/WDLParser.hpp`).

### Instructions

| Instruction | Arguments | Description |
|---|---|---|
| `HeightMap` | `x y z scale sizeX sizeY sizeZ` | Terrain heightmap position and scale |
| `Model1`–`Model20` | `x y z scale rot` | Place a 3D model (Model1.obj etc. loaded from `Models/` directory) |
| `Object1`–`Object5` | `x y z scale rot` | Place a collectible object/item |
| `Pickup` | `type x y z scale rot` | Place a pickup node (type=0-5: Health, Mana, Psychic, Armor, Weapon, Ammo) |
| `Spawn` | `x y z 0 0` | Player spawn point |
| `NPC` | `x y z defName` | NPC spawn (`defName`: Walker, Skaarj, Brute, Floater) |
| `Walker` | `x y z` | Legacy NPC spawn (creates a "Walker" pawn) |
| `Light` | `x y z radius r g b` | Point light |
| `ClipBox` | `x y z scale rot w h d` | Collision clip box |
| `Collision` | `x y z scale rot` | Simple collision volume |
| `AdvCollision` | `x y z scale rot w h d` | Advanced collision volume |
| `Script` | `id x y z scale rot` | In-world script trigger |
| `ZoneInfo` | `minX minY minZ maxX maxY maxZ type` | Zone volume (type: 0=Water, 1=Ladder, 2=Sky, 3=Reverb, 4=GameplaySound) |
| `NE1`–`NE3` | `x y z` | Noise emitter (ambient sound at position) |
| `Music` | `path` | Override background music |

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

OZONE is an extended plain-text format used by the AngelEd editor. It supports the same model/entity placement as WDL plus CSG brush primitives.

### Primitive Types

| Type | Syntax | Description |
|---|---|---|
| `box` | `box x y z w h d rot` | Cuboid primitive |
| `cyl` | `cyl x y z rTop rBot h slices rot` | Cylinder primitive |
| `sph` | `sph x y z r segments` | Sphere primitive |
| `pyr` | `pyr x y z w d h` | Pyramid primitive |
| `pln` | `pln x y z nx ny nz dist` | Plane primitive |
| `heightmap` | `heightmap imgPath texPath x y z scale sizeX sizeY sizeZ` | Terrain heightmap |

### Entity Instructions

Same as WDL format, embedded in the same file:

```
Spawn:x:y:z:0:0:
NPC:x:y:z:defName:
Pickup:type:x:y:z:scale:rot:
ZoneInfo:minX:minY:minZ:maxX:maxY:maxZ:type:
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

The `CsgProcessor` in `Source/Physics/OzBsp.hpp` implements AABB-based boolean operations. In the editor, brushes are placed with their CSG operation stored as metadata.

## Package Loading

World files can be packaged into `.ozone` containers in `System/Data/Zones/`. When loading from a package, file paths inside the OZONE file are resolved relative to the world directory:

```
System/Data/Zones/world_<WorldName>.ozone
    → resolves Models/HeightMap.png to GameData/Worlds/<WorldName>/Models/HeightMap.png
```

World textures (`oztex/tileset/*.png`) are loaded from packages by `OzoneLoader::LoadWorldTextures()`.
