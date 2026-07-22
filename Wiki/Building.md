# Building

## Prerequisites

- **g++** with C++20 support (GCC 10+ or MinGW-w64)
- **make** (GNU Make)
- **raylib 5.5** — must be installed system-wide (Linux) or in `C:\raylib\w64devkit` (Windows)

### Linux Dependencies

```bash
sudo apt install g++ make cmake libgl1-mesa-dev \
  libx11-dev libxrandr-dev libxcursor-dev \
  libxi-dev libxinerama-dev libxext-dev \
  libasound2-dev libpulse-dev
```

### raylib 5.5 (Linux — build from source)

```bash
git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git /tmp/raylib
cmake -S /tmp/raylib -B /tmp/raylib/build \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF -DBUILD_GAMES=OFF
cmake --build /tmp/raylib/build --parallel && sudo cmake --install /tmp/raylib/build
```

`build.sh` handles both steps automatically on Linux.

### Windows — w64devkit (recommended)

1. Install [w64devkit](https://github.com/skeeto/w64devkit) to `C:\raylib\w64devkit` (GCC 15.2.0)
2. Place raylib 5.5 static lib at `C:\raylib\w64devkit\lib\libraylib.a`
3. Place raylib headers at `C:\raylib\w64devkit\include\raylib.h`

**Important**: Do NOT use WinGet GCC 16.1.0 — its C++ headers are broken with POSIX UCRT.

### Windows — MSYS2 / MINGW64

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-raylib make
```

## Build Commands

### All Targets

```bash
make -j$(nproc)    # Linux
mingw32-make -j8  # Windows
```

Builds: `Angels95`, `AngelServ`, `OzPack`

### Individual Targets

| Target | Command | Dependencies |
|---|---|---|
| Game client | `make OTENGINE` | raylib 5.5 |
| Dedicated server | `make AngelServ` | None (standalone) |
| Asset packer | `make ozpack` | None (standalone) |
| Level editor | See build scripts | raylib 5.5 + Win32 |

### Windows Build Scripts

```powershell
.\build-native-win.ps1              # w64devkit — full System/ release
.\build-native-win.ps1 -SkipData    # skip asset packaging
.\build-native-win.ps1 -SkipClean   # skip make clean

.\build.ps1                         # MSYS2 — full System/ release
.\build.ps1 -SkipData               # skip asset packaging
```

Both scripts:
1. Build all 4 targets (Angels95, AngelServ, OzPack, AngelEd)
2. Assemble `System/` with EXEs, INI files, run scripts
3. Package assets via `build-data.ps1`

### Asset Packaging

```powershell
.\build-data.ps1        # packages all GameData assets into System/Data/
```

Uses `OzPack.exe` to create `.oz*` containers from `GameData/` subdirectories.

## Testing

```bash
make test                # runs all test suites
make test_parser         # LightningScriptParser tests
make test_context        # LightningScriptContext tests
make test_registry       # LightningEntityRegistry tests
make test_entity_manager # LightningEntityManager lifecycle tests
```

Tests are standalone `.test.cpp` files compiled directly into executables (no test framework). No raylib dependency — uses `SERVER_CXX` compiler.

## Outputs

| File | Location | Description |
|---|---|---|
| `Angels95.exe` | `System/` | Game client |
| `AngelServ.exe` | `System/` | Dedicated server |
| `AngelEd.exe` | `System/` | Level editor (Win32) |
| `OzPack.exe` | `System/` | Asset packer CLI |

## CI

GitHub Actions workflow (`.github/workflows/ci.yml`):

### Linux Job
1. Build raylib from source (cached by version)
2. `make AngelServ OTENGINE ozpack`
3. Smoke test: `timeout 3 ./AngelServ --dir GameData --port 27015 --http-port 8080`
4. Upload binaries

### Windows Job (MSYS2)
1. Install `mingw-w64-x86_64-{gcc,make,raylib}`
2. Build all 4 targets
3. Assemble `System/` release
4. Run `build-data.ps1`
5. Upload artifact

Tags matching `b*` trigger a GitHub Release with zipped `System/`.

## Running

### Client

```bash
.\System\Angels95.exe                 # from repo root
.\System\run.bat                      # via batch script (sets cwd)
Angels95.exe --world MyWorldName      # load a specific world
```

### Server

```bash
./AngelServ --port 27015 --http-port 8080 --dir GameData
```

| Flag | Default | Description |
|---|---|---|
| `--port` | 27015 | UDP game server port |
| `--http-port` | 8080 | HTTP map API port |
| `--dir` | GameData | Path to game data directory |
