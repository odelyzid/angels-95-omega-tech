# Engine Overview

## Source Tree

```
Angels95/
  Source/
    Main.cpp              # Client entrypoint, game loop, rendering
    Core.hpp              # Engine init, splash, menu, world loading (~2200 lines)
    Data.hpp              # Game globals, models, sounds, flags
    Settings.hpp          # Runtime settings: fog, particles, debug, resolution
    WindowsCompat.hpp     # Win32/raylib name collision fixes (CloseWindow, etc.)
    Log.cpp/.hpp          # Logging system
    IniConfig.hpp         # INI config file reader
    PPGIO.hpp             # WDL format I/O helpers
    OzOzoneLoader.cpp/.hpp # OZONE world format loader
    OzPack.cpp            # Standalone packer/unpacker CLI tool
    Editor.hpp            # Shared editor integration header

    Server/
      Server.cpp          # Dedicated server, HTTP API on :8080
      WDLParser.cpp/.hpp  # WDL world format parser (standalone, no raylib)
      OzoneParser.cpp/.hpp # OZONE format parser (standalone)
      GameState.cpp/.hpp  # Server-side world state, NPCs, pickups

    Network/
      Network.cpp/.hpp    # Custom UDP protocol, packed structs, LAN discovery

    Script/
      LightningScriptParser.cpp/.hpp   # LightningScript parser
      LightningScriptContext.cpp/.hpp  # Script execution context
      LightningEntityRegistry.cpp/.hpp # Entity type registry (.ozls)
      LightningEntityManager.cpp/.hpp  # Runtime entity management
      LightningEntityDef.hpp           # Entity definition structs

    Pawn/
      OzPawnSystem.cpp/.hpp  # Dynamic NPC system, FSM, zones, pickups
      Items.hpp              # Item definitions (20 backpack + 8 equip + 5 weapon slots)
      Entities.hpp           # Entity definitions
      Objects.hpp            # World object definitions
      Player.hpp             # Player state/capabilities

    Package/
      PackageAssetLoader.hpp # Runtime asset loading from .oz* packages
      OzPackage.hpp          # OzPackage format reader/writer
      OzAssetMapper.cpp/.hpp # Engine/item texture mapper

    Physics/
      OzBsp.cpp/.hpp         # CSG AABB boolean processor
      WorldChunk.cpp/.hpp    # Spatial partitioning for collision

    Renderer/
      LitLightning.cpp/.hpp  # Lighting renderer
      EngineBillboard.hpp    # Billboard sprite rendering (pickups, icons)
      TextSystem.hpp         # Text rendering system
      Video.hpp              # Video playback support

    Audio/
      OzSoundLoader.cpp/.hpp # Sound loading with fallback
      DspReverb.hpp          # Audio reverb DSP

    Client/
      Client.cpp/.hpp        # Client networking layer

    Custom/
      OTCustom.cpp/.hpp      # Custom engine code (statically linked)

    Encoder/
      Encoder.cpp/.hpp       # Asset encoder

    Menu/
      TitleMenu.hpp          # Title/home screen menu

    Parasite/
      ParasiteScript.hpp     # Parasite script definitions
      ParasiteScriptData.hpp # Parasite script data

    ParticleDemon/
      ParticleDemon.hpp      # 50-particle array system (explosion/trail/rain)

    plmpeg/
      pl_mpeg.h              # MPEG1 video decoder

    rlights/
      rlights.cpp/.h         # raylib lights helper

    raygui/
      raygui.c/.h, dark.h    # raygui UI library

  AngelEd/                  # Level editor (Win32 only)
    Source/
      Main.cpp              # Editor entrypoint, toolbar, 3D viewport, selection
      Editor.hpp            # Editor state, camera, lighting, cached models
      Win32Dialogs.cpp/.hpp # Win32 native panels (Model Browser, Texture, etc.)
      EditorIcons.cpp/.hpp  # Toolbar icon loader (AngelEd/UI/*.bmp)
      PPGIO.hpp             # WDL I/O helpers (shared with Source/)
      raygui/               # Bundled raygui (dark.h, raygui.c/.h)
    UI/                     # 45 toolbar icon .bmp files
    Makefile                # Separate editor Makefile

  GameData/                 # Loose assets, worlds, saves
    Worlds/
      <WorldName>/
        World.wdl           # World description (WDL format)
        Models/             # .obj files, textures, heightmap
        Scripts/            # WDL script files
        Music/              # Background music
        NoiseEmitter/       # Ambient sound emitters
    Global/
      PawnDefs/*.cfg        # Data-driven NPC definitions
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

## Pawn System

NPC definitions are loaded from `GameData/Global/PawnDefs/*.cfg` (name, speed, aggroRange, attackRange, damage, maxHealth, sprite_path, scream_path). Fallback hardcoded defs: Walker, Skaarj, Brute, Floater.

FSM states: IDLE, PATROL, CHASE, ATTACK, RETURN, DEAD.

Pickup types from LightningScript entity registry (`.ozls` definitions).

## Known Editor Gaps

- Lighting toggle (Lit/Unlit) does not actually unset shader from model materials
- CSG Add/Subtract UI is wired but backend `CsgProcessor` never called for actual boolean geometry
- No undo/redo system
- No test-play save prompts ("Reload world from playtest changes?")
- Lighting effects (watery, torch, fire, lamp) are UI-only — not rendered in viewport
