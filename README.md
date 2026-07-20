# OmegaTech Engine — Angels95 Edition

Angels95 reimagines the OmegaTech Engine as a **multiplayer game world** — a persistent, server-authoritative realm where players explore partitioned worlds, collect power-ups, level up, and fight NPCs alongside other connected players.

Built on [raylib](https://www.raylib.com/) with PS1-inspired retro aesthetics and a custom WDL world format.

---

## Angels95 — Core Game Mechanics

| Feature | Description |
|---|---|
| **Persistent Worlds** | Server-hosted 32–64 player partitioned worlds. Each world is an 8×8 grid of partitions (area-of-interest) for scalable multiplayer. |
| **Level & XP System** | Exponential XP curve (`XP_BASE_TO_NEXT=100`, growth `1.3×` per level). XP is gained from pickups, NPC kills, and exploration. |
| **Health / Mana / Psychic Energy** | Three resource pools with automatic regeneration (tick-based). Max values default to 100. |
| **9 Pickup Types** | Health, Mana, Psychic, Armor, Weapon, Ammo, Key, Coin, Powerup — each with per-type respawn timers and values. |
| **NPC AI** | Four-state finite state machine (`IDLE → PATROL → CHASE → RETURN`). NPCs patrol spawn points, aggro nearby players, chase, and return if the target strays too far. |
| **5 Inventory Slots** | Object1–Object5 can be collected in-world. Slot 1 is the default **Wand / Energy Bolt** weapon. |
| **Armory & Jewelry** | Equipment slots beyond the 5 hotbar items. Armory1/2 and Jewelry1/2 tracked in save data. |
| **Weapon Fire** | Left-click fires an energy projectile from slot 1. Projectiles are synced over the network and visible to all connected players. |
| **1–8 Hotkeys** | Number keys 1–8 directly select inventory or equipment slots. |
| **Save System** | `TF.sav` (toggle flags + object ownership), `POS.sav` (position + level), `Script.sav` (dynamic WDL instructions). |

---

## Server Hosting

The dedicated server (`oz_server`) has **no raylib dependency** and runs on any Linux or Windows machine (including headless VPS).

```
# Build
make oz_server

# Run (default port 27015, HTTP 8080)
./oz_server

# Custom ports
./oz_server --port 27015 --http-port 8080 --dir GameData
```

| Flag | Default | Description |
|---|---|---|
| `--port` | `27015` | UDP game server port |
| `--http-port` | `8080` | HTTP map API port (`GET /map?name=X`, `GET /map?list`) |
| `--dir` | `GameData` | Path to game data directory |

The server scans `GameData/Worlds/` for any subdirectory containing `World.wdl`. Each discovered world is served to connecting clients.

### HTTP Map API

```
GET /map?list         → {"ok":true,"worlds":["EtheralTestRealm","World1","World2"]}
GET /map?name=World1  → {"ok":true,"world":"World1","elements":[...]}
```

### LAN Discovery

The server announces itself on UDP port `27100`. Clients can discover local servers without manual IP entry.

---

## Building

### Linux

```
sudo apt install g++ make cmake libgl1-mesa-dev \
  libx11-dev libxrandr-dev libxcursor-dev \
  libxi-dev libxinerama-dev libxext-dev \
  libasound2-dev libpulse-dev

# raylib 5.5 (built from source)
git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git /tmp/raylib
cmake -S /tmp/raylib -B /tmp/raylib/build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF -DBUILD_GAMES=OFF
cmake --build /tmp/raylib/build --parallel && sudo cmake --install /tmp/raylib/build

make -j$(nproc)
```

### Windows (MSYS2 / MINGW64)

```
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-raylib make
make -j$(nproc)
```

### Outputs

| Target | Description |
|---|---|
| `Angels95` / `Angels95.exe` | Game client (requires raylib) |
| `oz_server` / `oz_server.exe` | Dedicated server (no raylib) |

---

## Client Controls

| Key | Action |
|---|---|
| WASD | Movement (first-person) |
| Mouse | Look |
| Left Click | Fire weapon (slot 1) |
| 1–8 | Select slot |
| Mouse Wheel / Arrow Keys | Cycle slots |
| E | Collect nearby pickup |
| Tab | Toggle inventory overlay |
| F11 | Toggle fullscreen |
| Escape | Quit (on start menu) |

---

## World Format (WDL)

OmegaTech uses the **W**orld **D**escription **L**anguage — a compact, colon-delimited plain-text format.

```
HeightMap:-100.0:-10.0:-100.0:4.0:0.0:
Model1:0:0:0:1:0:0:0:1:
Object1:10:0:10:1:0:0:0:
```

Instructions include `HeightMap`, `Model1`–`Model20`, `Object1`–`Object5`, `Walker` (NPC spawn), `Light`, `ClipBox`, `Collision`, `Script`, and `NE1`–`NE3` (noise emitters).

Worlds are stored in `GameData/Worlds/<WorldName>/World.wdl` alongside optional `Models/`, `Scripts/`, `Music/`, and `NoiseEmitter/` subdirectories.

---

## License

MIT — see [LICENSE](LICENSE).

## Acknowledgments

- [raylib](https://www.raylib.com/) — Ramon Santamaria
- [raygui](https://github.com/raysan5/raygui) — Immediate-mode GUI
- [pl_mpeg](https://github.com/phoboslab/pl_mpeg) — MPEG1 video playback
- [c99-raylib-video-player](https://github.com/WEREMSOFT/c99-raylib-vide-player) — Video integration
