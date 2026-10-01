# LightningScript Reference

LightningScript is the OmegaTech Engine's scripting language for defining entity behaviors, world interactions, and dynamic events. It replaces the legacy ParasiteScript system.

Files use the `.ozls` extension.

## Entity Definition Format (`.ozls`)

```
entity "<name>" : <type> {
    mesh = "<path>"
    texture = "<path>"
    icon = "<path>"

    stats {
        <name> = <value>
        vec3 <name> = (<x>, <y>, <z>)
    }

    variants {
        "<variantName>" { mesh_override = "<path>"  texture_override = "<path>" }
    }

    actions {
        on_<action> {
            <script lines>
        }
    }
}
```

### Types

| Type | Purpose |
|---|---|
| `weapon` | Ranged/melee weapon (hotbar, projectile dispatch) |
| `armor` / `upgrade` | Equipment items |
| `consumable` | Consumables (use via hotbar `on_use`) |
| `pickup` | World pickup (item_id/respawn_time stats) |
| `skyzone` | Zone volume with env/fog/skybox/zone actions |
| `pawn` | NPC hook-carrier (FSM actions: on_patrol/on_chase/on_return/on_death) |
| `projectile` | Reserved (no runtime consumer yet) |

### Stats Block

Defines runtime numeric properties:

```
stats {
    damage = 25.0
    magazine = 12
    vec3 color = (1.0, 0.2, 0.1)
}
```

#### Weapon-Specific Stats

| Stat | Type | Ranged Default | Melee Default | Description |
|---|---|---|---|---|
| `damage` | float | 10 | 10 | Damage per projectile or melee hit |
| `fire_rate` | float | 0.25 | — | Seconds between shots (ranged) |
| `swing_speed` | float | — | 0.25 | Seconds between swings (melee) |
| `magazine` | float | none | — | Ammo capacity per magazine (ranged only) |
| `reload_time` | float | 2.0 | — | Seconds to complete a reload |
| `projectile_speed` | float | 20 | — | Projectile travel speed in units/sec |
| `projectile_lifetime` | float | 2.0 | — | Projectile lifespan in seconds (legacy key `lifetime` also accepted) |
| `projectile_count` | float | 1 | — | Number of projectiles per shot |
| `spread` | float | 0 | — | Random spread angle in degrees |
| `reach` | float | — | 3.0 | Melee attack range in units |

### Action Blocks

Scripts attached to specific events:

| Action | Trigger |
|---|---|
| `on_use` | Player selects the entity from hotbar and presses E/Enter |
| `on_equip` | Entity is equipped |
| `on_unequip` | Entity is unequipped |
| `on_fire` | Ranged weapon fired (before projectile spawn) |
| `on_swing` | Melee weapon swung (before range check) |
| `on_hit` | Melee swing connects with a target |
| `on_reload` | Weapon reload triggered (auto or manual via R key) |
| `on_collect` | Entity is picked up |
| `on_enter` | Player enters a zone/skyzone volume |
| `on_exit` | Player exits a zone/skyzone volume |
| `on_tick` | Called every frame while active |
| `on_patrol` / `on_chase` / `on_return` / `on_death` | Pawn FSM state transitions (requires a pawn-named `.ozls`, e.g. `Walker.ozls`) |

## Opcodes

### Variable Operations

| Opcode | Syntax | Description |
|---|---|---|
| `var` | `var x = 5` | Declare int (no decimal) or float (has decimal) |
| `$` | `$x = 42` | Assign to runtime variable (creates if not exists) |
| `$ +=` | `$x += 5` | Add and assign |
| `$ -=` | `$x -= 3` | Subtract and assign |
| `$ *=` | `$x *= 2` | Multiply and assign |
| `$ /=` | `$x /= 2` | Divide and assign |
| RHS arithmetic | `$x = $x - 1` | Assignments support one binary op (`+ - * /`) with variables/numbers |

Variables are referenced in conditions with `$` prefix (e.g., `$health > 0`).
`$flag<idx>` (e.g. `$flag0`) reads the instance's toggle flags set by `wtflag`/`toggle_flag`.

### Control Flow

| Opcode | Syntax | Description |
|---|---|---|
| `if` / `else` / `endif` | `if ($x == 0) ... endif` | Conditional execution (brace style `if (...) { ... }` also supported) |
| `goto` / `jump` | `goto label_name` | Jump to a label |
| `stop` / `end` | `stop` | Halt script execution |
| `say` | `say "hello"` | Print to log |
| `set_cooldown` | `set_cooldown 1.5` | Set cooldown timer in seconds |

### Flags

| Opcode | Syntax | Description |
|---|---|---|
| `wtflag` | `wtflag idx val` | Write toggle flag (0-63) |
| `rtflag` | `rtflag idx` | Read flag value into `$result` |
| `toggle_flag` | `toggle_flag idx` | Flip a toggle flag (0-63) |

Hotbar/equipment instance flags are persisted in save games (`flags=` section). Zone-instance flags are session-only.

### World Interaction

| Opcode | Syntax | Description |
|---|---|---|
| `set_fog` | `set_fog r g b density` | Set fog color and density |
| `restore_fog` | `restore_fog` | Restore fog to world default |
| `set_ambient` | `set_ambient r g b` | Set ambient light color |
| `restore_ambient` | `restore_ambient` | Restore default ambient |
| `set_skybox` | `set_skybox "name"` | Set skybox by name |
| `restore_skybox` | `restore_skybox` | Restore default skybox |
| `play_sound` | `play_sound "path"` | Queue sound playback (package-aware: resolves `.ozsnd` via `SoundManager::PlayScriptSound`) |
| `msg` | `msg "text"` | On-screen text message |
| `heal` / `damage` | `heal 25` | Immediate player health delta |
| `playerstat` | `playerstat health -= 5` | Deferred write to a player stat |
| `consume` | `consume` | Mark consumable as used (removes from hotbar) |
| `spawn_pawn` | `spawn_pawn "Walker" x y z` | Spawn an NPC |
| `spawn_pickup` | `spawn_pickup "Coin" x y z [respawn]` | Spawn a pickup |

## Conditions

Used in `if` statements within action blocks (both styles work):

```
if ($health <= 0) {
    say "Player defeated"
    end
}
```

```
if ($result == 0)
    say "first time"
endif
```

Supported operators: `==`, `!=`, `>`, `<`, `>=`, `<=`

## Example: Ranged Weapon (automag)

```
entity "automag" : weapon {
    mesh = "automag_lvl1.obj"
    texture = "automag_lvl1_texture.png"
    stats {
        damage = 15
        fire_rate = 0.4
        magazine = 12
        reload_time = 2.0
        projectile_lifetime = 2.5
    }
    actions {
        on_fire {
            say "Bang! (15 damage)"
            set_cooldown 0.4
        }
        on_reload {
            say "Reloading..."
        }
    }
    variants {
        "lvl1" { mesh_override = "automag_lvl1" }
        "lvl2" { mesh_override = "automag_lvl2" }
        "lvl3" { mesh_override = "automag_heavy_rifle_lvl3" }
    }
}
```

## Example: Zone with Damage Loop (acidpool)

```
entity "acidpool" : skyzone {
    actions {
        on_enter {
            wtflag 0 1
            var timer = 60
            play_sound "GameData/Global/Sounds/Hurt.mp3"
            msg "Acid! The pooled run-off burns your boots!"
        }
        on_tick {
            if ($flag0 >= 1) {
                $timer = $timer - 1
                if ($timer <= 0) {
                    playerstat health -= 5
                    var timer = 60
                }
            }
        }
        on_exit {
            wtflag 0 0
            msg "You scramble clear of the acid."
        }
    }
}
```

## Integration

- Entity `.ozls` files are parsed by `LightningScriptParser` into `EntityDef` structs (recursively scanned from all of `GameData/` + packages)
- `LightningEntityRegistry` stores all registered definitions
- `LightningEntityManager` manages runtime instances with individual `LightningScriptContext` per instance
- Instances start idle — action bodies run only when triggered (spawn does not auto-execute)
- Hotbar integration: slot selection triggers `on_equip`/`on_unequip`; use triggers `on_use`
- Zone triggers: `PawnSystem` calls zone actions on entry/exit; pawn FSM transitions dispatch the matching `on_*` action
