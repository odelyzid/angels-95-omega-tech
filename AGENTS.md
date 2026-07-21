# AGENTS.md — Angels95 / OmegaTech Engine

## Build

```bash
make OTENGINE          # game client → Angels95
make oz_server         # dedicated server (no raylib dep)
make -j$(nproc)        # both
```

- Single g++ invocation per target, no cmake. Flags: `-O3 --std=c++20`.
- **raylib 5.5** must be installed system-wide (`/usr/local/lib/libraylib.a`). Not vendored.
- Windows: `mingw32-make` from MSYS2/MINGW64; `pacman -S mingw-w64-x86_64-raylib`.
- `build.sh` (Linux) and `build.ps1` (Windows) scripts handle raylib install + make.

## No tests, linter, formatter, or typechecker

Zero testing infrastructure. No `.clang-*`, no `.editorconfig`, no pre-commit hooks.

## Architecture

- **Client entrypoint:** `Source/Main.cpp:674` `main()`
- **Server entrypoint:** `Source/Server/Server.cpp:666` `main()` — no raylib dep, uses raw sockets
- **Engine core:** `Source/Core.hpp` (~2200 lines, single header) — init, menu, world loading, render loop, shaders
- **World format:** WDL (colon-delimited plain text) + OZONE (editor format). Parser in `Source/Server/WDLParser.hpp` (standalone, no raylib)
- **Networking:** Custom UDP protocol (`Source/Network/Network.hpp`). Packed structs (`#pragma pack(push,1)`). Discovery on UDP 27100, game on 27015.
- **Server HTTP API:** raw socket server in `Server.cpp` — routes `GET /map?list` and `GET /map?name=X` on port 8080.
- **Inventory:** 20 backpack slots + 8 equipment slots + 5 weapon hotbar slots. Items defined in `Source/Items.hpp`. Server maps pickup types to item_id.
- **Save files (binary, do not commit):** `GameData/Saves/TF.sav` (flags), `POS.sav` (position), `Script.sav` (WDL scripts).

## Conventions & quirks

- **Windows compat hack:** `Source/WindowsCompat.hpp` `#define CloseWindow __WIN32_CloseWindow` to avoid raylib/winsock2 name collision. Must be included early.
- **OTCustom statically linked** to avoid DLL issues cross-platform.
- **All asset paths are hardcoded** relative to `GameData/`. Server `--dir` flag overrides.
- **`#include <bits/stdc++.h>`** in `ParticleDemon.hpp` (non-standard, GCC-only, slow).
- **Game data encryption** (`GameDataEncoded` flag) uses a substitution cipher in `Encoder.hpp`. Currently disabled (`false`).
- **`using namespace std;`** used in multiple headers.
- **Hardcoded `EntityCount 10`** — server NPCs >10 get clamped on client.

## CI (.github/workflows/ci.yml)

Two-job matrix (Linux + Windows MSYS2):
1. Build raylib from source (cached by version)
2. `make oz_server` then `make OTENGINE`
3. Linux: `timeout 3 ./oz_server --dir GameData --port 27015 --http-port 8080` (smoke test)
4. Uploads `Angels95-linux/windows` + `oz_server-linux/windows`
5. Windows bundles `libraylib*.dll` + `libglfw*.dll` from `/mingw64/bin/`

## Branch workflow

Active development on `feat/items-pickups`. Master is fast-forwarded from that branch. No merge commits.
