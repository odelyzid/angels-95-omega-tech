# OTEditor

OTEditor is a Windows level editor for the OmegaTech / Angels95 game engine. It combines Win32 native panels with a raylib 3D viewport for creating and editing game worlds.

## Features

### Win32 Panels
- **Model Browser** — Browse and place 3D models from `GameData/`
- **Environment Settings** — Fog, ambient light, skybox configuration
- **Pickups** — Place health, mana, keys, coins, powerups
- **Nodes** — Player spawns, NPC spawns, point lights
- **Pawn Manager** — Spawn and configure NPCs (Walker, Skaarj, Brute, Floater)
- **Texture Manager** — Manage world textures
- **Sound Manager** — Configure world sounds
- **Script Manager** — Edit WDL scripts

### 3D Viewport
- Real-time raylib rendering
- Heightmap visualization
- Model preview with texture mapping
- Grid snapping and coordinate display

### World Formats
- **WDL** — World Description Language (colon-delimited plain text)
- **OZONE** — Editor binary format with embedded textures

## Prerequisites

- **Windows 10/11** (Win32 API required for panels)
- **raylib 5.5** (statically linked via w64devkit or MSYS2)
- **C++20 compiler** (GCC 15.2.0 via w64devkit recommended)

## Building

### Native Windows (w64devkit)

```powershell
# From repo root
.\build-native-win.ps1
```

This builds all targets (Angels95, oz_server, oz_editor, OzPack) and assembles the `System/` release.

### MSYS2 / MINGW64

```powershell
# From repo root
.\build.ps1
```

Both scripts produce `System/oz_editor.exe` along with INI files and launch scripts.

## Usage

1. Launch the editor:
   ```
   System\oz_editor.exe
   ```

2. Open a world:
   - File → Open → Select `GameData/Worlds/<WorldName>/World.wdl`
   - Or drag-and-drop a `.wdl` file onto the executable

3. Edit the world:
   - Use the menu bar to toggle panels
   - Place models, pickups, nodes via panel buttons
   - Adjust environment settings (fog, ambient)
   - Spawn NPCs via Pawn Manager

4. Save the world:
   - File → Save (writes `World.wdl`)
   - File → Save As OZONE (writes `World.ozone`)

## Configuration

`System/oz_editor.ini`:
```ini
[Editor]
GridSize=1.0
SnapToGrid=1
ShowGrid=1

[Video]
Width=1600
Height=900
```

## Architecture

- **Entry point:** `OTEditor/Source/Main.cpp`
- **Win32 dialogs:** `OTEditor/Source/Win32Dialogs.cpp` — uses `HWND` handles stored as `void*` for cross-platform compat
- **Editor state:** `OTEditor/Source/Editor.hpp` — WDL cache, models, placement modes
- **Engine integration:** Links `oz_pawn_system.o`, `oz_ozone_loader.o`, `OzoneParser.o`

## License

MIT — see [LICENSE](../LICENSE).

## Acknowledgments

- [raylib](https://www.raylib.com/) — Ramon Santamaria
- [raygui](https://github.com/raysan5/raygui) — Immediate-mode GUI
