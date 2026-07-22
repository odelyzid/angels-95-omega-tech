# Core Game

## Controls

| Key | Action |
|---|---|
| WASD | Movement (first-person) |
| Mouse | Look |
| Left Click | Fire weapon (slot 1) |
| 1–8 | Select inventory/hotbar slot |
| Mouse Wheel / Arrow Keys | Cycle slots |
| E | Collect nearby pickup |
| Tab | Toggle inventory overlay |
| Escape | Pause menu (Resume / Settings / Main Menu / Quit) |
| F11 | Toggle fullscreen |
| Grave (`) | Toggle developer console |

## HUD

- **Health / Mana / Psychic Energy** — three resource pools displayed at top, automatic regeneration (tick-based). Max values default to 100.
- **Level & XP** — exponential XP curve (`XP_BASE_TO_NEXT=100`, growth 1.3× per level). XP gained from pickups, NPC kills, and exploration.
- **Hotbar** — 5 weapon slots + 3 equipment slots (8 total). Slot 1 is the default Wand / Energy Bolt weapon.
- **Ping** — displayed when connected to a network server.

## Pickups

9 pickup types, each with per-type respawn timers and values:

| Type | ID |
|---|---|
| Health | 0 |
| Mana | 1 |
| Psychic Energy | 2 |
| Armor | 3 |
| Weapon | 4 |
| Ammo | 5 |
| Key | 6 |
| Coin | 7 |
| Powerup | 8 |

## Inventory

- **20 backpack slots** — collected items go here
- **8 equipment slots** — armor, jewelry (Armory1/2, Jewelry1/2)
- **5 weapon hotbar slots** — Object1–Object5, directly selectable with number keys
- Press Tab to open/close the inventory overlay

## NPC AI

Four-state finite state machine:

```
IDLE → PATROL → CHASE → RETURN
```

- **IDLE**: standing still, waiting
- **PATROL**: circles around spawn point
- **CHASE**: pursues nearby player within aggro range
- **RETURN**: goes back to spawn if target strays too far

NPCs are configured via WDL `Walker` instructions or placed in the editor via the Pawn Manager.

## Networking

- **Game port**: UDP 27015
- **LAN discovery**: UDP 27100 (servers broadcast presence)
- **HTTP API**: port 8080 (`GET /map?list`, `GET /map?name=X`)
- Client connects via `Join Game` in the home screen

## Save System

Three binary save files in `GameData/Saves/` (all `.gitignore`d):

| File | Contents |
|---|---|
| `TF.sav` | Toggle flags + object ownership (100 flags) |
| `POS.sav` | Player position + level |
| `Script.sav` | Dynamic WDL script state |

## Pause Menu

Press **Escape** during gameplay to open the pause menu overlay:

- **Resume** — return to gameplay
- **Settings** — open developer settings panel
- **Main Menu** — return to title screen (world is unloaded)
- **Quit** — exit the game

The pause menu uses custom button textures (`menu_button.png`, `menu_button_hover.png`, `menu_button_clicked.png`) and heading (`menu_heading.png`) from `GameData/Global/Title/`.
