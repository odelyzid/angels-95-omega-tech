Here is the detailed implementation plan covering Phases 4 through 6, structured step-by-step for full execution across the OmegaTech / Angels95 codebase.

---

## Phase 4: Legacy System Cleanup

### 4.1 Remove `OmegaEnemy[]` Legacy Array

The fixed-size `OmegaEnemy[10]` array in `Entities.hpp` and its state writes in `Core.hpp` and `Main.cpp` will be removed. All entity operations will query `PawnSystem::Instance()`.

#### Step 4.1.1: Refactor `Entities.hpp`

* **File:** `Source/Entities.hpp`
* **Change:** Remove `OmegaEnemy[10]`, `EntityCount`, and `Enemys` class definitions. Keep texture definitions for rendering fallback.

```cpp
#pragma once
#include "raylib.h"

// Legacy fixed array replaced by PawnSystem dynamic vector.
// EnemyTexture retained for legacy texture/sound handles.
class EnemyTexture {
public:
    Texture2D Frame1;
    Sound Scream;
};

static EnemyTexture EnemyTextures;

```

#### Step 4.1.2: Remove Writes & Update WDL Parsing in `Core.hpp`

* **File:** `Source/Core.hpp`
* Remove array clearing in `LoadWorld()` and legacy state updates in `SpawnWDLProcess()`.

```cpp
// In SpawnWDLProcess() within Core.hpp:
if (WReadValue(Instruction, 0, 5) == L"Walker")
{
    float x = ToFloat(WSplitValue(WData, i + 1));
    float y = ToFloat(WSplitValue(WData, i + 2));
    float z = ToFloat(WSplitValue(WData, i + 3));
    
    // Spawn exclusively through PawnSystem (unlimited dynamic capacity)
    PawnSystem::Instance().Spawn({x, y, z}, "Walker");
    EntityCounter++;
}

```

#### Step 4.1.3: Update Network Sync in `Main.cpp`

* **File:** `Source/Main.cpp`
* Update `on_server_message` and the tick loop to write server NPC positions directly to `PawnSystem` instead of `OmegaEnemy[]`.

```cpp
// Inside main update loop in Main.cpp:
if (g_network_enabled && g_client.is_connected()) {
    const auto& npcs = g_client.npcs();
    for (size_t i = 0; i < npcs.size(); i++) {
        Pawn* p = PawnSystem::Instance().Get(static_cast<int>(i + 1));
        if (p) {
            p->position = { npcs[i].position.x, npcs[i].position.y, npcs[i].position.z };
            p->active = npcs[i].active;
        } else if (npcs[i].active) {
            // Dynamically register dynamic server NPC if not local
            PawnSystem::Instance().Spawn({ npcs[i].position.x, npcs[i].position.y, npcs[i].position.z }, "Walker");
        }
    }
}

```

---

### 4.2 Unify Server/Client NPC Systems (Option A)

Unify `ServerNPC` inside `Server/GameState.hpp` with client `PawnSystem` logic by sharing state definitions and pathing FSM logic.

#### Step 4.2.1: Extract Shared NPC FSM Definitions

* **File:** `Source/Shared/PawnTypes.hpp` (New Header)

```cpp
#pragma once
#include <cstdint>

enum class PawnState : uint8_t {
    IDLE,
    PATROL,
    CHASE,
    RETURN,
    DEAD
};

struct PawnAttributes {
    float speed = 1.5f;
    float aggroRange = 6.0f;
    float attackRange = 1.5f;
    float damage = 10.0f;
    int maxHealth = 100;
};

```

#### Step 4.2.2: Refactor `GameState.hpp` and `oz_pawn_system.h`

* Update `ServerNPC` in `Server/GameState.hpp` to import `PawnState` and `PawnAttributes` from `PawnTypes.hpp`.
* Align FSM state names and state timers across server ticks (`GameState::tick_npcs`) and client ticks (`PawnSystem::Update`).

---

### 4.3 Clean Header Includes in `ParticleDemon.hpp`

#### Step 4.3.1: Replace Standard Header Include

* **File:** `Source/ParticleDemon/ParticleDemon.hpp`
* **Change:** Remove line 3 `#include <bits/stdc++.h>` and specify granular includes.

```cpp
#pragma once

// Replaced broad <bits/stdc++.h> header for standard C++ toolchains
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <memory>
#include "raylib.h"
#include "raymath.h"

```

---

## Phase 5: Documentation & CI

### 5.1 CI Pipeline setup (`.github/workflows/ci.yml`)

Create `.github/workflows/ci.yml` supporting dual-OS compilation and release artifact assembly:

```yaml
name: Angels95 Engine Build & Verification CI

on:
  push:
    branches: [ main, master, develop ]
  pull_request:
    branches: [ main, master ]

jobs:
  build-linux:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Install Dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y build-essential libraylib-dev libgl1-mesa-dev libx11-dev libxi-dev libxcursor-dev libxrandr-dev libxinerama-dev

      - name: Compile Targets
        run: |
          make -j$(nproc)

      - name: Server Startup Smoke Test
        run: |
          ./oz_server --help
          timeout 3s ./oz_server --port 27015 --http-port 8080 || [ $? -eq 124 ]

      - name: Upload Linux Build Artifacts
        uses: actions/upload-artifact@v4
        with:
          name: Angels95-Linux-System
          path: System/

  build-windows:
    runs-on: windows-latest
    defaults:
      run:
        shell: msys2 {0}
    steps:
      - uses: actions/checkout@v4

      - name: Setup MSYS2
        uses: msys2/setup-msys2@v2
        with:
          msystem: MINGW64
          update: true
          install: >-
            git
            mingw-w64-x86_64-toolchain
            mingw-w64-x86_64-raylib
            mingw-w64-x86_64-cmake

      - name: Build System
        run: |
          ./build.ps1

      - name: Upload Windows Build Artifacts
        uses: actions/upload-artifact@v4
        with:
          name: Angels95-Windows-System
          path: System/

```

---

### 5.2 Create `LICENSE`

* **File:** `LICENSE`
* Standard MIT license as specified by root `README.md`.

```text
MIT License

Copyright (c) 2026 TribeWarez / Angels95 Engine Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY FROM...

```

---

### 5.3 Update `AGENTS.md`

Update architectural guidelines to document parser updates, panel operational status, and dynamic entity subsystems:

* **WDL Entities List:**
* `Model[1-20]`: Static geometry mesh pointers
* `Walker`, `Skaarj`, `Brute`, `Floater`: Dynamic `PawnSystem` NPCs
* `Light`: Point-light positional node
* `Sound`, `Music`, `ZoneInfo`: Spatial audio and area trigger nodes
* `AdvCollision`, `ClipBox`: Collision bounding geometry


* **PawnSystem vs. ServerNPC:**
* Client: `PawnSystem` handles local interpolation, billboard generation, rendering, and predictive AI.
* Server: `GameState` processes tick-based authoritative spatial checks, AI transitions, and player damage routines.


* **Editor Panel Status:**
* Functional: Model Browser, Environment Settings, Pickups, Node Placer, Pawn Manager, Win32 Dialog integration.
* Stubs: Advanced Shader Graph, Custom Particle Designer.



---

### 5.4 Update `README.md`

Add explicit syntax examples for WDL entity placement, audio nodes, and asset mapping.

```markdown
## WDL Entity Placement Reference

Entities in `.wdl` files use colon-delimited positional arguments:

```text
Model1:X:Y:Z:Scale:Rotation:
Walker:X:Y:Z:
Light:X:Y:Z:
Sound:X:Y:Z:Volume:Radius:SoundID:
Music:X:Y:Z:TrackPath:
ZoneInfo:X:Y:Z:Width:Height:Length:ZoneName:

```

### Editor Panel Capabilities

* **Model Browser:** Fast search & view viewport previews for `.obj`/`.gltf` assets.
* **Pawn Manager:** Adjust NPC aggro range, movement speed, base health, and damage values.
* **Sound/Music/ZoneInfo:** Directly drag node billboards into the raylib 3D canvas.
* **Load/Save ozone:** Directly Load and Save ozone files from and to files

```

---

## Phase 6: Testing & Verification

### 6.1 Add Unit Tests
Create unit tests in `Tests/UnitTestSuite.cpp` with standard `cassert` checks.

```cpp
#include <cassert>
#include <iostream>
#include "../Source/OzPackage.hpp"
#include "../Source/Server/WDLParser.hpp"
#include "../Source/oz_pawn_system.h"
#include "../Source/PackageAssetLoader.hpp"

void TestOzPackageRoundTrip() {
    OzPackageWriter writer(OZ_PACKAGE_MAGIC_PK);
    std::string testData = "Test binary stream data";
    writer.AddFile("test.txt", testData.data(), testData.size());
    assert(writer.WriteToFile("test.ozpak"));

    OzPackageReader reader;
    assert(reader.Open("test.ozpak"));
    assert(reader.EntryCount() == 1);
    
    size_t sz = 0;
    const uint8_t* ptr = reader.GetData("test.txt", sz);
    assert(sz == testData.size());
    assert(memcmp(ptr, testData.data(), sz) == 0);
    std::cout << "[PASS] TestOzPackageRoundTrip\n";
}

void TestWDLParser() {
    std::string wdlData = "Model1:0:10:0:1:90:\nWalker:5:0:5:\nLight:0:20:0:";
    auto elems = WDLParser::parse_string(wdlData);
    assert(elems.size() == 3);
    assert(elems[0].type == WDLElementType::MODEL);
    assert(elems[1].type == WDLElementType::ENTITY_WALKER);
    assert(elems[2].type == WDLElementType::LIGHT);
    std::cout << "[PASS] TestWDLParser\n";
}

void TestPawnSystemFSM() {
    auto& ps = PawnSystem::Instance();
    ps.RegisterDef({"Walker", 1.5f, 6.0f, 1.5f, 10.0f, 100});
    int id = ps.Spawn({0, 0, 0}, "Walker");
    assert(id > 0);
    
    Pawn* p = ps.Get(id);
    assert(p->state == PawnState::IDLE);
    
    // Simulating player within aggro range (2.0f < 6.0f)
    ps.Update({2.0f, 0.0f, 0.0f}, 0.1f);
    assert(p->state == PawnState::CHASE);
    std::cout << "[PASS] TestPawnSystemFSM\n";
}

int main() {
    TestOzPackageRoundTrip();
    TestWDLParser();
    TestPawnSystemFSM();
    std::cout << "All Unit Tests Executed Successfully.\n";
    return 0;
}

```

---

### 6.2 Integration Tests

#### Automation Integration Test (`Tests/IntegrationTest.cpp`)

Validate world load stability, multiplayer NPC visibility, and WDL round-trip persistence:

1. **World Load:** Load `GameData/Worlds/World1/World.wdl` through `WDLParser`, verifying parse output is non-empty and contains no null pointers.
2. **Multiplayer Pawn Sync:** Initialize `net::NetworkServer` and `net::NetworkClient`, dispatch an `NPC_STATE_UPDATE` payload, and confirm `PawnSystem` updates client-side coordinate values.
3. **Editor Persistence:** Instantiate `InEditor`, place each entity type (`Model`, `Walker`, `Light`, `Sound`, `Music`, `ZoneInfo`), save out a temporary `.wdl` file, parse it back, and assert token equality.

---

### 6.3 Manual Testing Verification Matrix

| Checklist Item | Target Subsystem | Pass Criteria |
| --- | --- | --- |
| **World 3 Collision** | Physics/Camera | Camera snaps onto floor platform at $Y=2.0\text{f}$ without falling endlessly into the void. |
| **Server Startup Output** | `oz_server` | Outputs clean log messages with listening ports (`UDP 27015`, `HTTP 8080`) without header parsing errors. |
| **Win32 Panels** | `oz_editor` | Modeless panels can be moved, minimized, and closed without blocking the primary OpenGL viewport rendering thread. |
| **Network NPC Sync** | Client/Server | Server-spawned NPCs display correctly in the client's `PawnSystem` canvas rendering pass. |
| **Asset Fallback** | `PackageAssetLoader` | When `GameData/` files are absent, assets stream directly from `.ozpak` files in `System/Data/`. |

---

## Execution Summary & Order

```
┌──────────────────────────────────────────────┐
│ Phase 1: Critical Fixes                      │ (Completed)
└──────────────────────┬───────────────────────┘
                       │
┌──────────────────────▼───────────────────────┐
│ Phase 2: WDL Expansion                       │ (Completed)
└──────────────────────┬───────────────────────┘
                       │
┌──────────────────────▼───────────────────────┐
│ Phase 3: Editor Panels                       │ (Completed)
└──────────────────────┬───────────────────────┘
                       │
┌──────────────────────▼───────────────────────┐
│ Phase 4: Legacy Cleanup (2 hours)            │ ◄── Current Step
│ - Remove OmegaEnemy[] array                  │
│ - Unify Server/Client NPC FSM               │
│ - Remove <bits/stdc++.h> in ParticleDemon    │
└──────────────────────┬───────────────────────┘
                       │
┌──────────────────────▼───────────────────────┐
│ Phase 5: Documentation & CI (2 hours)        │
│ - Add .github/workflows/ci.yml               │
│ - Create MIT LICENSE                         │
│ - Update AGENTS.md & README.md               │
└──────────────────────┬───────────────────────┘
                       │
┌──────────────────────▼───────────────────────┐
│ Phase 6: Testing & Verification (2-3 hours)  │
│ - Unit Tests (OzPackage, WDL, PawnSystem)    │
│ - Integration Tests                          │
│ - Final Manual Verification Checklist        │
└──────────────────────────────────────────────┘

```