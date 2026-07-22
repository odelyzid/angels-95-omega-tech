# Engine Overview

## Source Tree

```
Angels95/
  Source/
    Main.cpp              # Client entrypoint, game loop, rendering
    Core.hpp              # Engine init, splash, menu, world loading (~2200 lines)
    Data.hpp              # Game globals, models, sounds, flags
    Settings.hpp          # Runtime settings fog, particles, debug, resolution
    WindowsCompat.hpp     # Win32/raylib name collision fixes (CloseWindow, etc.)
    Editor.hpp            # Legacy embedded editor (disabled by default)

    Server/
      Server.cpp          # Dedicated server, HTTP API on :8080
      WDLParser.hpp       # WDL world format parser (standalone, no raylib)
      GameState.cpp       # Server-side world state, NPCs, pickups
      OzoneParser.hpp     # OZONE format parser (standalone)

    Network/
      Network.hpp         # Custom UDP protocol, packed structs, LAN discovery

    Script/
      LightningScript*.hpp/.cpp   # LightningScript scripting system
      LightningEntityDef.hpp      # Entity type definitions (.ozls)

    Pawn/
      OzPawnSystem.hpp/.cpp       # Dynamic NPC system, FSM, zones, pickups

    Package/
      PackageAssetLoader.hpp      # Runtime asset loading from .oz* packages

    Physics/
      OzBsp.hpp/.cpp              # CSG AABB boolean processor
      WorldChunk.hpp/.cpp         # Spatial partitioning for collision

    ParticleDemon/
      ParticleDemon.hpp           # 50-particle array system (explosion/trail/rain)

  AngelEd/                  # Level editor (Win32 only)
    Source/
      Main.cpp             # Editor entrypoint, toolbar, 3D viewport
      Editor.hpp           # Editor state, camera, lighting, models
      Win32Dialogs.cpp     # Win32 native panels (Model Browser, Texture, etc.)
      Win32Dialogs.hpp     # Panel function declarations + non-Win32 stubs

  GameData/                 # Loose assets, worlds, saves
    Worlds/
      <WorldName>/
        World.wdl           # World description (WDL format)
        Models/             # .obj files, textures, heightmap
        Scripts/            # WDL script files
        Music/              # Background music
        NoiseEmitter/       # Ambient sound emitters
    Saves/                  # Binary save files (gitignored)

  System/                   # Release directory
    Angels95.exe
    AngelServ.exe
    AngelEd.exe
    OzPack.exe
    Data/*.oz*              # Packaged assets
    Cache/                  # Runtime temp cache (model extraction)
```

## Key Architecture Points

### Single g++ invocation, no CMake
Every target is compiled and linked with a single `g++` command. Flags: `-O3 --std=c++20`. The `Makefile` defines per-target object lists manually.

### No raylib dependency for server
`AngelServ` (dedicated server) uses raw POSIX/Winsock sockets only. No raylib headers or libraries are linked. The `SERVER_CXX` compiler is used for server-side code.

### WindowsCompat.hpp
Included early in any file that touches both raylib and `winsock2.h`. Renames conflicting Windows symbols (`CloseWindow`, `ShowCursor`, `Rectangle`, `DrawText`) before `#include <windows.h>`, then `#undef`s them.

### using namespace std
Used in `PPGIO.hpp`, `Data.hpp`, `Encoder.hpp`, `TextSystem.hpp`, `ParasiteScriptData.hpp`.

### #pragma pack(push,1)
Used for all network packet structs to ensure binary compatibility between client and server.

## Package System (OzPackage)

Assets can be distributed as loose files in `GameData/` OR packaged into `.oz*` containers:

| Extension | Type | Magic | Contents |
|---|---|---|---|
| `.ozpak` | Generic | OZPK | Models, scripts, shaders |
| `.oztex` | Texture | OZTX | PNG textures |
| `.ozsnd` | Sound | OZSD | WAV/MP3/OGG |
| `.ozmux` | Music | OZMX | WAV/MP3 |
| `.ozone` | World | OZWN | World files |

Loading is handled by `PackageAssetLoader::Instance().Init()` which scans `System/Data/*.oz*`. Use `*WithFallback` wrappers (`LoadTextureWithFallback`, `LoadModelWithFallback`, etc.) which check the filesystem first, then search packages.

Models require a temp-file cache in `System/Cache/` because raylib has no `LoadModelFromMemory`.

Packaging is done via `.\build-data.ps1` which uses `OzPack.exe`.

## Particle System

`ParticleDemon.hpp` implements a 50-particle array system with three effect types:
- **Explosion** — burst of colored particles in random directions
- **Trail** — sequential particle trail
- **Rain** — falling particles from top of screen

The `RainParticles` instance is in `EngineData` (Core.hpp).

## Known Editor Gaps

- Lighting toggle (Lit/Unlit) does not actually unset shader from model materials
- No right-click context menu or entity properties dialog
- CSG Add/Subtract UI is wired but backend `CsgProcessor` never called for actual boolean geometry
- Texture browser uses plain listbox — tile thumbnails are now implemented
- No test-play functionality (Play button launches client as subprocess)
