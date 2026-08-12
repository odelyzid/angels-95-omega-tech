# LightningScript Reference

LightningScript is the OmegaTech Engine's scripting language for defining entity behaviors, world interactions, and dynamic events. It replaces the legacy ParasiteScript system.

Files use the `.ozls` extension.

## Entity Definition Format (`.ozls`)

```
entity <name> {
    type = <weapon|armor|consumable|skystone|misc>
    mesh = "<path>"
    texture = "<path>"
    icon = "<path>"

    stats {
        float <name> = <value>
        int <name> = <value>
        vec3 <name> = <x> <y> <z>
    }

    variants {
        { float <name> = <value> }
    }

    on_<action> {
        <script lines>
    }
}
```

### Fields

| Field | Type | Description |
|---|---|---|
| `name` | string | Logical entity name |
| `type` | enum | One of: `weapon`, `armor`, `consumable`, `skystone`, `misc` |
| `mesh` | path | 3D model file path |
| `texture` | path | Diffuse texture path |
| `icon` | path | Hotbar icon path |

### Stats Block

Defines runtime numeric properties:

```
stats {
    float damage = 25.0
    float range = 8.0
    float fire_rate = 0.5
    int max_ammo = 30
    vec3 color = 1.0 0.2 0.1
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
| `projectile_lifetime` | float | 2.0 | — | Projectile lifespan in seconds |
| `projectile_count` | float | 1 | — | Number of projectiles per shot |
| `spread` | float | 0 | — | Random spread angle in degrees |
| `reach` | float | — | 3.0 | Melee attack range in units |
| `recoil` | float | 2.0 | 1.0 | Camera recoil kick intensity |

### Variants Block

Defines alternate configurations (e.g., weapon levels):

```
variants {
    { float damage = 10.0 }
    { float damage = 20.0 }
    { float damage = 30.0 }
}
```

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
| `on_zone_enter` | Player enters a skyzone entity |
| `on_zone_exit` | Player exits a skyzone entity |
| `on_tick` | Called every frame while active |

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

Variables are referenced in conditions with `$` prefix (e.g., `$health > 0`).

### Control Flow

| Opcode | Syntax | Description |
|---|---|---|
| `end` | `end` | Marks end of instruction block |
| `jump` | `jump label_name` | Jump to a label |
| `say` | `say "hello"` | Print to log |
| `set_cooldown` | `set_cooldown 1.5` | Set cooldown timer in seconds |

### World Interaction

| Opcode | Syntax | Description |
|---|---|---|
| `set_fog` | `set_fog r g b density` | Set fog color and density |
| `set_skybox` | `set_skybox "name"` | Set skybox by name |
| `restore_skybox` | `restore_skybox` | Restore default skybox |
| `play_sound` | `play_sound "path"` | Queue sound playback |
| `toggle_flag` | `toggle_flag idx` | Toggle a save flag (0-63) |
| `rtflag` | `rtflag idx` | Read flag value into `$result` |

### Conditions

Used in `if` statements within action blocks:

```
if ($health <= 0) {
    say "Player defeated"
    end
}
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
        range = 50.0
        magazine = 12
        reload_time = 2.0
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

## Example: Melee Weapon (selenite_blade)

```
entity "selenite_blade" : weapon {
    mesh = "selenite_blade_lvl1.obj"
    texture = "selenite_blade_lvl1_texture.png"
    icon = "selenite_blade_icon.png"
    stats {
        damage = 40
        swing_speed = 0.8
        reach = 3.0
        stamina_cost = 15
    }
    actions {
        on_swing {
            say "Slash! (40 damage)"
            set_cooldown 0.8
        }
        on_hit {
            say "Blade connects!"
        }
    }
}
```

## Example: Skyzone

```
entity StormSky {
    type = skystone

    on_zone_enter {
        set_fog 0.3 0.3 0.4 0.8
        set_skybox "Storm"
        say "Entered storm zone"
    }

    on_zone_exit {
        restore_skybox
        say "Exited storm zone"
    }
}
```

## Integration

- Entity `.ozls` files are parsed by `LightningScriptParser` into `EntityDef` structs
- `LightningEntityRegistry` stores all registered definitions
- `LightningEntityManager` manages runtime instances with individual `LightningScriptContext` per instance
- The manager ticks contexts in `Update()` (up to 10 instructions per frame)
- Hotbar integration: slot selection triggers `on_use` action
- Zone triggers: `PawnSystem` calls `TriggerZoneAction()` on zone entry/exit
