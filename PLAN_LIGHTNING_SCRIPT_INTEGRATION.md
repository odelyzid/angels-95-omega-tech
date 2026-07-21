# PLAN: LightningScript (.ozls) Integration

## Overview

Replace the hardcoded `GameObject` class (`Source/Pawn/Objects.hpp`: 407 lines
of Object1-5, Armory1-2, Jewelry1-2, Helmet, Boots, Legs, Accessory1-2
repetitive boilerplate) with a dynamic prototype-based entity system powered
by `.ozls` script definitions.

The existing `ParasiteScript` interpreter (`Source/Parasite/`) provides the
foundation instruction-cycle engine. This project refactors it into
`Source/Script/Lightning*` classes with per-instance state, entity-definition
parsing, a fully dynamic hotbar (512-entity cap, swappable per-slot), runtime
entity instantiation covering all weapon types (ranged + melee), armor,
consumables, upgrades, custom pickups, and **skyzone definitions** — replacing
the parallel SkyZoneInfo tracking in PawnSystem with scripted zone behavior.

---

## File naming convention

All new files go under `Source/Script/` with the `Lightning` prefix, crediting
the original ParasiteScript implementation in a header comment:

```cpp
// LightningScriptContext.hpp — refactored from Source/Parasite/ParasiteScript.hpp
// Original ParasiteScript by ODeLyZiD / OmegaTech
```

| New name | Purpose | Origin |
|----------|---------|--------|
| `LightningScriptContext.hpp/.cpp` | Per-instance script execution engine | ParasiteScript `CycleInstruction()` |
| `LightningScriptParser.hpp/.cpp` | `.ozls` entity schema parser | New |
| `LightningEntityDef.hpp` | Entity type system structs | New |
| `LightningEntityRegistry.hpp/.cpp` | Global registry of entity prototypes | New |
| `LightningEntityManager.hpp/.cpp` | Runtime entity instances + hotbar | New |
| `LightningInventoryBridge.hpp/.cpp` | Links entities to `InventorySystem` | New |

---

## Entity types

| Type | Slot type | Examples |
|------|-----------|---------|
| `weapon` | Any hotbar slot (0-7) | automag (ranged), selenite_blade (melee) |
| `armor` | Any hotbar slot | iron_helmet, plate_boots, amulet |
| `consumable` | Any hotbar slot | mana_vial, health_vial, energy_crystal |
| `upgrade` | Any hotbar slot | damage_upgrade_mk1, fire_rate_upgrade |
| `pickup` | World-spawned, not on hotbar | coin, key, mysterious_relic |
| `projectile` | Not on hotbar | bullet_pistol, energy_bolt |
| `pawn` | Not on hotbar | walker, skaarj, brute, floater |
| `skyzone` | Not on hotbar | snowy_zone, cave_zone, boss_arena |

### Slot system

8 hotbar slots, fully dynamic — any entity type can go in any slot.
Hard cap of 512 total entity instances to protect performance.
Slots are individually swappable at runtime via `hotbar_swap` opcode
or drag-and-drop.

---

## 1. LightningScriptContext (refactored from ParasiteScript)

**Goal:** Extract `CycleInstruction()` from global `ParasiteScriptCoreData`
into a per-instance `LightningScriptContext` class.

```cpp
class LightningScriptContext {
    bool Load(const char* scriptText);       // Load script lines into buffer
    bool ExecuteNext();                       // One instruction cycle
    void Reset();                             // Rewind program counter

    // Variable I/O — each instance is independent
    void SetInt(const char* name, int val);
    int  GetInt(const char* name);
    void SetStr(const char* name, const char* val);
    const char* GetStr(const char* name);

    // Toggle flags — per-instance
    void SetFlag(int idx, int val);
    int  GetFlag(int idx);

    bool HasMore() const;
};
```

**Opcodes carried over from ParasiteScript:**
`var`, `if/else` (with `==`, `>`, `<`, `>=`, `<=`, `!=`),
`+` `-` `*` `/`, `+=` `-=` `*=` `/=`, `=`,
`wtflag`, `rtflag`, `say`, `stop`, `kill`, `end`,
`addwdli`, `clrwdli`, `ownobj`, `setscene`, `setcampos`.

**New opcodes for LightningScript:**

| Opcode | Args | Effect |
|--------|------|--------|
| `spawn_projectile` | `"name" [speed]` | Spawns projectile entity from registry |
| `play_sound` | `"name"` | Plays sound from package |
| `play_music` | `"name"` | Crossfades to music track |
| `recoil` | `x y` | Applies camera recoil |
| `play_animation` | `"name"` | Plays entity animation |
| `deal_damage` | `amount [type]` | Damages current target |
| `apply_force` | `x y z magnitude` | Applies physics force |
| `set_cooldown` | `seconds` | Sets ability cooldown |
| `equip_item` | `"entity_name"` | Equips entity to a slot |
| `drop_loot` | `"entity_name" count` | Spawns loot pickup |
| `hotbar_swap` | `slot_index` | Swaps current entity to given slot |
| `upgrade_weapon` | `stat value` | Applies permanent stat upgrade |
| `set_skybox` | `"name"` | Swaps skybox cubemap |
| `restore_skybox` | — | Restores default skybox |
| `set_fog` | `r g b density` | Sets fog parameters |

**Task list:**

| Task | Files | Description |
|------|-------|-------------|
| 1.1 | `Source/Script/LightningScriptContext.hpp` | New class: program buffer, PC, flags, variable memory, jump points — all instance-scoped. Header comment crediting `Source/Parasite/`. |
| 1.2 | `Source/Script/LightningScriptContext.cpp` | Port `CycleInstruction()` into `ExecuteNext()`. Add new opcodes listed above. |
| 1.3 | `Source/Parasite/` | Unchanged. LightningScript contexts work independently. |

---

## 2. Entity schema parser & prototype registry

**Goal:** Parse `.ozls` definitions into typed `EntityDef` prototypes. `.ozls`
files live in the entity's asset folder alongside models + textures and are
packed into `.ozpak` like any other asset.

### .ozls syntax examples

**Ranged weapon (bullet):**
```lightningscript
entity "automag" : weapon {
    mesh = "automag_lvl1.obj"
    texture = "automag_lvl1_texture.png"
    icon = "automag_lvl1_icon.png"
    stats {
        damage = 15
        fire_rate = 0.4
        range = 50.0
        magazine = 12
        reload_time = 2.0
    }
    actions {
        on_fire {
            spawn_projectile "bullet_pistol" 800
            play_sound "gun_pistol_fire"
            recoil 0.5 1.0
        }
        on_reload {
            play_sound "gun_pistol_reload"
            play_animation "reload"
        }
    }
    variants {
        "lvl1" { mesh_override = "automag_lvl1" }
        "lvl2" { mesh_override = "automag_lvl2_alt" }
        "lvl3" { mesh_override = "automag_heavy_rifle_lvl3" }
    }
}
```

**Melee weapon:**
```lightningscript
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
            play_sound "blade_swing"
            play_animation "slash"
            deal_damage $damage cone 60 $reach    // 60-degree arc
        }
        on_hit {
            apply_force $target_x $target_y $target_z 500
            play_sound "blade_impact"
        }
    }
}
```

**Armor:**
```lightningscript
entity "iron_helmet" : armor {
    mesh = "Helmet.obj"
    texture = "HelmetTexture.png"
    icon = "HelmetIcon.png"
    stats {
        defense = 5
        weight = 2.0
    }
    actions {
        on_equip { $defense_bonus = 5 }
        on_unequip { $defense_bonus = 0 }
    }
}
```

**Consumable:**
```lightningscript
entity "mana_vial" : consumable {
    icon = "ManaVial.png"
    stats {
        restore = 25
        max_stack = 10
    }
    actions {
        on_use {
            $mana = $mana + $restore
            if ($mana > $max_mana) { $mana = $max_mana }
            play_sound "drink"
        }
    }
}
```

**Weapon upgrade:**
```lightningscript
entity "damage_upgrade_mk1" : upgrade {
    icon = "upgrade_damage.png"
    stats { damage_bonus = 5 }
    actions {
        on_apply {
            $target.damage = $target.damage + $damage_bonus
            play_sound "upgrade_apply"
            $target.variant = "lvl2"
        }
    }
}
```

**Custom pickup:**
```lightningscript
entity "mysterious_relic" : pickup {
    mesh = "Relic.obj"
    texture = "RelicTexture.png"
    icon = "RelicIcon.png"
    stats { value = 1000 }
    actions {
        on_pickup {
            $coins = $coins + $value
            play_sound "relic_pickup"
            hotbar_swap 3
        }
    }
}
```

**SkyZone definition (replaces hardcoded SkyZoneInfo):**
```lightningscript
entity "snowy_zone" : skyzone {
    fog_color = (0.8, 0.85, 0.9)
    fog_density = 0.015
    ambient_light = (0.6, 0.6, 0.7)
    skybox = "skybox_snowy"
    music = "ambient_winter"
    actions {
        on_enter {
            play_music "ambient_winter"
            set_fog 0.8 0.85 0.9 0.015
            set_skybox "skybox_snowy"
        }
        on_exit {
            set_fog 0.5 0.5 0.5 0.002
            restore_skybox
        }
    }
}
```

### Task list

| Task | Files | Description |
|------|-------|-------------|
| 2.1 | `Source/Script/LightningEntityDef.hpp` | Define `EntityDef`, `EntityStatBlock`, `EntityAction`, `EntityVariant`, `EntityType` enum (weapon, armor, consumable, upgrade, pickup, projectile, pawn, skyzone). |
| 2.2 | `Source/Script/LightningScriptParser.hpp/.cpp` | Block-structure parser reading `.ozls` into `EntityDef`. Handles nested `{ }`, `key = value`, `stats { }`, `actions { on_X { ... } }`, `variants { "name" { ... } }`, and skyzone-specific fields (`fog_color`, `skybox`, etc.). |
| 2.3 | `Source/Script/LightningEntityRegistry.hpp/.cpp` | Singleton `LightningEntityRegistry::Instance()`. Stores `std::unordered_map<std::string, EntityDef*>`. `Init()` scans packages + `GameData/` for `*.ozls`. `Find(name)`, `FindByType(type)`, `Count()`. |

---

## 3. Dynamic entity instance system (replaces `Objects.hpp`)

**Goal:** Replace `OmegaTechGameObjects` with a dynamic entity manager.
512 instance cap. 8 hotbar slots, fully swappable.

### Architecture

```
                    ┌─────────────────────────────────┐
                    │  LightningEntityManager          │
                    │  ┌───────────────────────────┐   │
                    │  │ m_instances[512 max]       │   │
                    │  │  ├─ EntityInstance*        │   │
                    │  │  ├─ EntityInstance*        │   │
                    │  │  └─ ...                    │   │
                    │  └───────────────────────────┘   │
                    │  ┌───────────────────────────┐   │
                    │  │ m_hotbar[8] — any type     │   │
                    │  │  ├─ slot 0 → idx 3        │   │
                    │  │  ├─ slot 1 → idx 7        │   │
                    │  │  └─ ...                   │   │
                    │  └───────────────────────────┘   │
                    └─────────────────────────────────┘
```

```cpp
struct EntityInstance {
    const EntityDef* def;
    LightningScriptContext ctx;
    Model model;
    Texture2D texture;
    Texture2D icon;
    bool owned;
    int variant_index;
    std::unordered_map<std::string, float> stats;  // runtime (with upgrades)
    float cooldown_remaining;
};

class LightningEntityManager {
    static constexpr int MAX_ENTITIES = 512;
    static constexpr int HOTBAR_SIZE = 8;

    std::vector<EntityInstance> m_instances;
    int m_hotbar[HOTBAR_SIZE];      // index into m_instances, -1 = empty
    int m_selected_slot;

public:
    void Init();
    void Update(float dt);

    // Lifecycle
    EntityInstance* Spawn(const char* defName);
    void Despawn(int index);
    EntityInstance* Get(int index);
    int Count() const;

    // Hotbar
    void HotbarAssign(int slot, int instanceIndex);
    void HotbarSwap(int slotA, int slotB);
    int  HotbarAt(int slot) const;
    void SelectSlot(int slot);
    int  SelectedSlot() const;
    EntityInstance* SelectedEntity() const;

    // Input + rendering (ported from UpdateObjectBar)
    void HandleInput();
    void DrawHotbar();
};
```

### Task list

| Task | Files | Description |
|------|-------|-------------|
| 3.1 | `Source/Script/LightningEntityManager.hpp/.cpp` | Full implementation. `Spawn()` clones `EntityDef`, loads model+texture, inits `LightningScriptContext` with stat variables. `Update()` ticks cooldowns + active scripts. |
| 3.2 | `Source/Script/LightningEntityManager.cpp` | `DrawHotbar()`: iterate `m_hotbar[0..7]`, draw icon + name + selection per instance. `HandleInput()`: keys 1-8, Enter to use, mouse wheel, gamepad. Ported from `Objects.hpp::UpdateObjectBar()`. |
| 3.3 | `Source/Main.cpp` | Replace `#include "../Pawn/Objects.hpp"` with `LightningEntityManager`. Replace `InitObjects()` with `LightningEntityManager::Instance().Init()`. |

---

## 4. Inventory bridge

| Task | Files | Description |
|------|-------|-------------|
| 4.1 | `Source/Script/LightningInventoryBridge.hpp/.cpp` | On pickup: look up `.ozls` by name → if `consumable`, add to `InventorySystem::backpack`; if `weapon`/`armor`, `Spawn()` entity + assign to hotbar. On UseItem: find entity → run `on_use` action block. |
| 4.2 | `Source/Pawn/Items.hpp` | Extend `ItemDBEntry` with optional `EntityDef*` pointer. Add `BuildItemDB()` to generate runtime `ItemDB` from `.ozls` consumable defs. |
| 4.3 | `Source/Parasite/ParasiteScript.hpp` | Update `ownobj` opcode to reference `LightningEntityManager` instead of `OmegaTechGameObjects.ObjectXOwned`. |

---

## 5. SkyZone integration

**Goal:** Zone `.ozls` definitions replace the hardcoded `SkyZoneInfo`
tracking. The other agent provides low-level zone detection (which brush the
player is in). LightningScript layers on top: zone `.ozls` files define
what happens on enter/exit via `on_enter`/`on_exit` action blocks.

### Integration point

```
PawnSystem::Update()
  → detects zone name from OZONE brush tags
  → LightningEntityRegistry::Find(zoneName)
  → if found and type == skyzone:
      → if entering: run on_enter block
      → if exiting:  run on_exit block
```

This means zone behavior is fully data-driven — no C++ changes needed to
add new zone types, fog settings, music, or skybox assignments.

### Task list

| Task | Files | Description |
|------|-------|-------------|
| 5.1 | `Source/Pawn/OzPawnSystem.hpp/.cpp` | After zone detection, call `LightningEntityManager::TriggerZoneAction(zoneName, "on_enter")` / `"on_exit"`. |
| 5.2 | `Source/Script/LightningEntityManager.cpp` | `TriggerZoneAction()`: find `EntityDef` by name, get or create instance, run the named action block via `LightningScriptContext`. |
| 5.3 | `Source/Script/LightningScriptContext.cpp` | Implement `play_music`, `set_skybox`, `restore_skybox`, `set_fog` opcodes. |

---

## 6. Package & startup pipeline

| Task | Files | Description |
|------|-------|-------------|
| 6.1 | `Source/Package/OzAssetMapper.hpp` | After `PackageAssetLoader::Init()`, feed `.ozls` paths to `LightningEntityRegistry::Init()`. |
| 6.2 | `Source/Main.cpp` | Startup order: `PackageAssetLoader::Init()` → `LightningEntityRegistry::Init()` → `LightningEntityManager::Init()`. |
| 6.3 | `build-data.ps1` | No changes — `.ozls` files in any `GameData/` subdirectory are packed by the existing recursive file loop. |

---

## 7. Editor (AngelEd) support

| Task | Files | Description |
|------|-------|-------------|
| 7.1 | `AngelEd/Source/Win32Dialogs.cpp` | "LightningScript Browser" panel: tree view of loaded entities by type. Click shows stats, mesh preview, action blocks, zone properties. |
| 7.2 | `AngelEd/Source/Main.cpp` | "Script" menu: "Reload .ozls", "Spawn Selected", "Edit Entity" (external editor), "Create New" (type picker → template). |
| 7.3 | `AngelEd/Source/Win32Dialogs.cpp` | Viewport integration: right-click → spawn entity at cursor. |

---

## 8. Port `Objects.hpp` to prototype pattern

**Goal:** `Objects.hpp` becomes obsolete. All functionality moves to
`LightningEntityManager` + dynamic hotbar.

| What `Objects.hpp` had | Where it goes |
|------------------------|---------------|
| `Object1-5` (Model, Name, Texture, Icon, Owned) | `Spawn("energy_wand")` — dynamic, up to 512 |
| `Object1-5Owned` | `EntityInstance::owned` |
| `Object1-5Name` | `EntityDef::name` |
| `Object1-5Texture` | `EntityDef::texture` → loaded in `Spawn()` |
| `Object1-5Icon` | `EntityDef::icon` → loaded in `Spawn()` |
| `Armory1-2` + `Jewelry1-2` | `entity "amulet_of_power" : armor { ... }` |
| `Helmet` / `Boots` / `Legs` / `Accessory1-2` | `.ozls` with armor type |
| Consumable icons | `.ozls` consumable entities |
| `InitObjects()` — 120 lines | `LightningEntityManager::Init()` |
| `UpdateObjectBar()` — 120 lines | `LightningEntityManager::DrawHotbar()` + `HandleInput()` |
| `SelectedObject` — static int | `LightningEntityManager::m_selected_slot` |
| `DrawHotbarSlot()` — render utility | Kept, called from `DrawHotbar()` |

| Task | Files | Description |
|------|-------|-------------|
| 8.1 | `Source/Script/LightningEntityManager.cpp` | Full hotbar rendering. `DrawHotbar()` draws 8 slots using ported `DrawHotbarSlot()` utility. |
| 8.2 | `Source/Main.cpp` | Remove `Objects.hpp` include. Remove `InitObjects()`/`UpdateObjectBar()` calls. |
| 8.3 | `Source/Parasite/ParasiteScript.hpp` | Update `ownobj` to use `LightningEntityManager` entity lookup. |
| 8.4 | `GameData/Global/gun/automag/automag.ozls` | Create example .ozls for automag weapon. |
| 8.5 | `GameData/Global/Objects/*.ozls` | Create .ozls for each existing Object1-5, Armory1-2, Jewelry1-2, Helmet, Boots, Legs, Accessory1-2. |

---

## Files to create

```
Source/Script/
  LightningScriptContext.hpp
  LightningScriptContext.cpp
  LightningEntityDef.hpp
  LightningScriptParser.hpp
  LightningScriptParser.cpp
  LightningEntityRegistry.hpp
  LightningEntityRegistry.cpp
  LightningEntityManager.hpp
  LightningEntityManager.cpp
  LightningInventoryBridge.hpp
  LightningInventoryBridge.cpp
PLAN_LIGHTNING_SCRIPT_INTEGRATION.md
```

## Files to modify

- `Source/Main.cpp` — replace `Objects.hpp` → `LightningEntityManager.hpp`; replace `InitObjects()` + `UpdateObjectBar()`
- `Source/Parasite/ParasiteScript.hpp` — update `ownobj` to use `LightningEntityManager`
- `Source/Pawn/Items.hpp` — optional `EntityDef*` in `ItemDBEntry`; `BuildItemDB()`
- `Source/Pawn/OzPawnSystem.hpp/.cpp` — call `TriggerZoneAction()` on zone enter/exit
- `Source/Package/OzAssetMapper.hpp` — register `.ozls` scanning phase
- `Makefile` — add `Source/Script/Lightning*.o` to compile + link
- `AngelEd/Source/Win32Dialogs.cpp` — LightningScript Browser panel
- `AngelEd/Source/Main.cpp` — Script menu

## Files obsoleted

- `Source/Pawn/Objects.hpp` — all functionality replaced by `LightningEntityManager`
- `GameData/Global/Objects/Object1.info` etc. — replaced by `.ozls`

---

## Parallel work (no dependency)

These are handled by another agent and have no dependency on LightningScript:

- **BSP/CSG collision pipeline** (`Source/Physics/OzBsp.*`)
- **OzoneParser CSG** — add/sub/intersect/deresc fields
- **CsgProcessor → OzOzoneLoader** integration
- **SkyZoneInfo low-level detection** in `PawnSystem`
- **Skybox rendering** from tagged OZONE brushes

LightningScript's skyzone entity type builds on top of the low-level zone
detection: once `PawnSystem` knows which zone the player is in, LightningScript
handles the behavioral response (fog, music, skybox swap).

---

## Hard constraints

| Constraint | Value | Reason |
|------------|-------|--------|
| Max entity instances | 512 | Prevent engine performance degradation |
| Hotbar slots | 8 | Matches current KEY_ONE..EIGHT + screen space |
| .ozls location | Alongside entity assets | Ensures packaging picks them up automatically |
| Instance cap per type | Unlimited (up to global 512) | Dynamic hotbar allows any configuration |
| Memory per instance | ~200 bytes + model assets | Acceptable at 512 instances |

---

## Milestones

| # | Milestone | Phases | Verification |
|---|-----------|--------|-------------|
| M1 | `LightningScriptContext` runs `.ps` scripts per-instance | 1 | Load 2 scripts, step independently, compare variable states |
| M2 | `.ozls` entity definition parses into `EntityDef` | 2 | `registry.Find("automag")` returns valid def with correct stats |
| M3 | Entities auto-load from `.ozpak` at startup | 2, 6 | Console: "Loaded N entity definitions from packages" |
| M4 | `Spawn()` creates entity with model + texture + icon | 3 | Spawned entity renders with correct texture |
| M5 | Hotbar renders 8 slots, input works, slots swappable | 3 | Key 1-8 selects, Enter fires action, swap works |
| M6 | Ranged weapon fires projectile + sound | 3, 4 | `on_fire` runs: projectile spawns, sound plays |
| M7 | Melee weapon swings, deals cone damage | 3, 4 | `on_swing` animation plays, enemies in arc take damage |
| M8 | Consumable heals through backpack | 4 | Pick up → use → health restored |
| M9 | Weapon upgrade changes stats + variant | 2, 4 | Apply upgrade → damage increases, mesh variant switches |
| M10 | Armor entity equips, defense bonus applied | 3, 4 | Equip helmet → `on_equip` → defense stat changes |
| M11 | SkyZone `.ozls` triggers fog + music on zone entry | 5 | Walk into snowy zone → fog changes, music crossfades |
| M12 | AngelEd browser shows all entities + skyzones | 7 | Editor lists by type, "Spawn" button works |
