# Plan: Lit Fog Rendering & EngineTextures Billboard Display

## Overview
Implement two major rendering improvements:
1. **Lit Fog** - Fog that interacts with lighting instead of flat post-process blend
2. **EngineTextures as Billboards** - Display engine entity icons (Light, Sound, Music, etc.) as lit sprites in both client and editor viewports

---

## Part 1: Lit Fog Rendering

### Current State
- **Client**: Uses `GameData/Shaders/Fog.fs` as a post-process shader that blends the entire frame with a flat fog color
- **Editor**: Uses `ClearBackground()` with fog color - no actual fog shader
- **Problem**: Fog doesn't interact with lighting, no depth-based falloff, no volumetric feel

### Implementation Plan

#### 1.1 Create New Lit Fog Shader
**File**: `GameData/Shaders/Lights/LitFog.fs` (new file)

**Approach**: Integrate fog calculation into the lighting shader pass instead of post-process
- Calculate fog factor based on distance from camera to fragment
- Apply fog color modulation to the lit result
- Support both directional and point lights affecting fog color/intensity

**Shader Logic**:
```glsl
// Calculate distance-based fog
float fogDistance = length(viewPos - fragPosition);
float fogFactor = clamp((fogDistance - fogStart) / (fogEnd - fogStart), 0.0, 1.0);

// Apply fog to lit color
vec3 fogColor = ambient.rgb * fogDensity;
vec3 finalLit = mix(litColor, fogColor, fogFactor * fogIntensity);
```

**Uniforms to Add**:
- `float fogStart` - distance where fog begins
- `float fogEnd` - distance where fog is fully opaque
- `float fogDensity` - overall fog density multiplier
- `vec3 fogColor` - base fog color (can be modulated by lights)

#### 1.2 Update Client Fog Implementation
**Files**: 
- `Source/Core.hpp` (line 681)
- `Source/Main.cpp` (lines 883-891)
- `Source/Settings.hpp` (line 20)

**Changes**:
1. Remove post-process fog shader application from Main.cpp
2. Assign `LitFog.fs` to all world models instead of `Lighting.fs`
3. Add fog uniforms to shader initialization
4. Update fog settings UI to control fogStart/fogEnd/fogDensity

**Code Changes**:
```cpp
// In Core.hpp Init():
OmegaTechData.WorldShader = LoadShaderWithFallback(
    "GameData/Shaders/Lights/Lighting.vs",
    "GameData/Shaders/Lights/LitFog.fs"
);

// Set fog uniforms
SetShaderValue(OmegaTechData.WorldShader, GetShaderLocation(OmegaTechData.WorldShader, "fogStart"), &fogStart, SHADER_UNIFORM_FLOAT);
SetShaderValue(OmegaTechData.WorldShader, GetShaderLocation(OmegaTechData.WorldShader, "fogEnd"), &fogEnd, SHADER_UNIFORM_FLOAT);
SetShaderValue(OmegaTechData.WorldShader, GetShaderLocation(OmegaTechData.WorldShader, "fogDensity"), &fogDensity, SHADER_UNIFORM_FLOAT);

// Assign to all models
WDLModels.Model1.materials[0].shader = OmegaTechData.WorldShader;
// ... repeat for Model2-20
```

#### 1.3 Update Editor Fog Implementation
**Files**:
- `OTEditor/Source/Editor.hpp` (lines 200-299, model loading)
- `OTEditor/Source/Main.cpp` (rendering loop)

**Changes**:
1. Load `LitFog.fs` in editor initialization
2. Assign to all editor models
3. Remove `ClearBackground()` fog hack
4. Add fog uniform controls to EnvPanel

**Code Changes**:
```cpp
// In Editor.hpp Init():
Shader editorFogShader = LoadShaderWithFallback(
    "GameData/Shaders/Lights/Lighting.vs",
    "GameData/Shaders/Lights/LitFog.fs"
);

// Assign to all models
WDLModels.Model1.materials[0].shader = editorFogShader;
// ... repeat for Model2-20, HeightMap
```

#### 1.4 Update Fog Settings UI
**File**: `OTEditor/Source/Win32Dialogs.cpp` (EnvPanelProc)

**Changes**:
- Add fogStart, fogEnd sliders to Environment Settings panel
- Update fogDensity to actually affect shader uniform
- Apply fog color to shader uniform instead of ClearBackground

---

## Part 2: EngineTextures Billboard Display

### Current State
- **EngineTextures**: 6 PNG files in `GameData/Global/Engine/` (Light, Music, PawnNode, PlayerStart, Sound, ZoneInfo)
- **AssetMapper**: Registers these textures but never loads/displays them
- **Editor**: Draws wireframe primitives (cubes, spheres) for entities
- **Client**: Doesn't display entity icons at all
- **Problem**: No visual representation of engine entities in viewport

### Implementation Plan

#### 2.1 Load EngineTextures via AssetMapper
**File**: `Source/oz_assetmapper.cpp` (lines 93-108)

**Current**: Only registers paths, doesn't preload textures

**Changes**:
```cpp
void AssetMapper::RegisterEngineTextures() {
    struct { const char* alias; } engineIcons[] = {
        {"Light"}, {"Music"}, {"PawnNode"}, {"PlayerStart"}, {"Sound"}, {"ZoneInfo"},
    };
    const char* baseDir = "GameData/Global/Engine";
    for (auto& ic : engineIcons) {
        std::string path = probe_path(baseDir, ic.alias, ".png");
        m_strings.push_back(path);
        AssetMapEntry entry = {ic.alias, m_strings.back().c_str(), "engine", {0}};
        m_entries.push_back(entry);
    }
}

// Add preload call in Init()
void AssetMapper::Init() {
    RegisterEngineTextures();
    RegisterItemTextures();
    PreloadCategory("engine");  // <-- ADD THIS
}
```

#### 2.2 Create Billboard Drawing Helper
**File**: `Source/EngineBillboard.hpp` (new file)

**Purpose**: Centralized billboard drawing with lighting support

**API**:
```cpp
namespace EngineBillboard {
    void Draw(const char* entityName, Vector3 position, float size = 1.0f);
    void DrawLit(const char* entityName, Vector3 position, float size, Shader litShader);
    void Init();  // Preload all engine textures
    void Shutdown();  // Unload textures
}
```

**Implementation**:
```cpp
void Draw(const char* entityName, Vector3 position, float size) {
    Texture2D tex = AssetMapper::Instance().GetTexture(entityName);
    if (tex.id == 0) return;
    
    // Billboard always faces camera
    DrawBillboard(GetCurrentCamera(), tex, position, size, WHITE);
}

void DrawLit(const char* entityName, Vector3 position, float size, Shader litShader) {
    Texture2D tex = AssetMapper::Instance().GetTexture(entityName);
    if (tex.id == 0) return;
    
    // Create a simple quad mesh for the billboard
    Mesh quad = GenMeshQuad(size, size, 1);
    Model model = LoadModelFromMesh(quad);
    model.materials[0].shader = litShader;
    model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    
    // Position and billboard toward camera
    Vector3 cameraPos = GetCameraPosition();
    float yaw = atan2(cameraPos.x - position.x, cameraPos.z - position.z) * RAD2DEG;
    
    DrawModelEx(model, position, {0, yaw, 0}, 0, {1, 1, 1}, WHITE);
    UnloadModel(model);
}
```

#### 2.3 Update Editor to Draw EngineTextures
**File**: `OTEditor/Source/Editor.hpp` (lines 554-559)

**Current**:
```cpp
if (Instruction == L"Collision") DrawCubeWires({X, Y, Z}, S, S, S, RED);
if (WReadValue(Instruction, 0, 5) == L"Script") DrawCubeWires({X, Y, Z}, S, S, S, YELLOW);
if (WReadValue(Instruction, 0, 6) == L"Pickup")  DrawCubeWires({X, Y, Z}, 0.5f, 0.5f, 0.5f, GREEN);
if (WReadValue(Instruction, 0, 5) == L"Spawn")  DrawCubeWires({X, Y, Z}, 1.0f, 0.2f, 1.0f, BLUE);
if (WReadValue(Instruction, 0, 3) == L"NPC")    DrawCubeWires({X, Y, Z}, 1.0f, 2.0f, 1.0f, MAGENTA);
if (WReadValue(Instruction, 0, 5) == L"Light")  DrawSphereWires({X, Y, Z}, 1.0f, 8, 8, YELLOW);
```

**New**:
```cpp
if (Instruction == L"Collision") DrawCubeWires({X, Y, Z}, S, S, S, RED);
if (WReadValue(Instruction, 0, 5) == L"Script") DrawCubeWires({X, Y, Z}, S, S, S, YELLOW);
if (WReadValue(Instruction, 0, 6) == L"Pickup") {
    // Draw pickup icon as billboard
    const char* pickupNames[] = {"HealthVial", "ManaVial", "EnergyCrystal", "Key", "Coin", "Powerup"};
    int pickupType = ToInt(WSplitValue(WData, i + 1));
    if (pickupType >= 0 && pickupType < 6) {
        EngineBillboard::DrawLit(pickupNames[pickupType], {X, Y + 0.5f, Z}, 0.8f, editorFogShader);
    }
}
if (WReadValue(Instruction, 0, 5) == L"Spawn") {
    EngineBillboard::DrawLit("PlayerStart", {X, Y + 0.5f, Z}, 1.2f, editorFogShader);
}
if (WReadValue(Instruction, 0, 3) == L"NPC") {
    EngineBillboard::DrawLit("PawnNode", {X, Y + 1.0f, Z}, 1.5f, editorFogShader);
}
if (WReadValue(Instruction, 0, 5) == L"Light") {
    EngineBillboard::DrawLit("Light", {X, Y + 0.5f, Z}, 1.0f, editorFogShader);
}
```

**Additional Entity Types** (if added to WDL):
```cpp
if (WReadValue(Instruction, 0, 5) == L"Sound") {
    EngineBillboard::DrawLit("Sound", {X, Y + 0.5f, Z}, 1.0f, editorFogShader);
}
if (WReadValue(Instruction, 0, 5) == L"Music") {
    EngineBillboard::DrawLit("Music", {X, Y + 0.5f, Z}, 1.0f, editorFogShader);
}
if (WReadValue(Instruction, 0, 8) == L"ZoneInfo") {
    EngineBillboard::DrawLit("ZoneInfo", {X, Y + 0.5f, Z}, 1.0f, editorFogShader);
}
```

#### 2.4 Update Client to Draw EngineTextures
**File**: `Source/Core.hpp` (UpdateEntities, DrawWorld)

**Changes**:
1. Load engine textures at startup
2. Draw billboards for spawned entities (NPCs, lights, etc.)
3. Use lit shader for billboard rendering

**Code**:
```cpp
// In UpdateEntities() or DrawWorld():
for (auto& pawn : PawnSystem::Instance().GetAllPawns()) {
    if (!pawn.active) continue;
    
    // Draw pawn sprite as lit billboard
    const char* iconName = "PawnNode";  // Default
    if (pawn.type == "Walker") iconName = "PawnNode";
    // ... map other pawn types to icons
    
    EngineBillboard::DrawLit(iconName, pawn.position, 1.5f, OmegaTechData.WorldShader);
}

// Draw placed lights
for (auto& light : placedLights) {
    EngineBillboard::DrawLit("Light", light.position, 1.0f, OmegaTechData.WorldShader);
}
```

#### 2.5 Integrate Billboards with Lighting System
**File**: `GameData/Shaders/Lights/Lighting.vs` (vertex shader)

**Changes**:
- Generate fake normals for billboard quads (always face camera)
- Pass world position for fog calculation

**Shader Code**:
```glsl
// In vertex shader, for billboard meshes:
if (isBillboard == 1) {
    // Calculate normal pointing toward camera
    fragNormal = normalize(viewPos - vertexPosition);
} else {
    fragNormal = normal;
}
```

---

## Part 3: Integration & Testing

### 3.1 Update Build System
**File**: `build-data.ps1`

**Changes**:
- Ensure `GameData/Shaders/Lights/LitFog.fs` is packaged
- Ensure `GameData/Global/Engine/*.png` are packaged

### 3.2 Update Package System
**File**: `Source/PackageAssetLoader.hpp`

**Changes**:
- Add shader loading with fallback
- Add texture loading for engine icons

### 3.3 Testing Checklist
- [ ] Fog shader compiles and runs without errors
- [ ] Fog intensity increases with distance from camera
- [ ] Fog color is modulated by nearby lights
- [ ] EngineTextures load correctly from AssetMapper
- [ ] Billboards face camera in editor viewport
- [ ] Billboards face camera in client viewport
- [ ] Billboards are lit by world lights
- [ ] Fog affects billboard rendering
- [ ] Performance is acceptable (no major FPS drop)

---

## Files to Create/Modify

### New Files
1. `GameData/Shaders/Lights/LitFog.fs` - Lit fog fragment shader
2. `Source/EngineBillboard.hpp` - Billboard drawing helper

### Modified Files
1. `Source/Core.hpp` - Load lit fog shader, assign to models
2. `Source/Main.cpp` - Remove post-process fog, update fog uniforms
3. `Source/Settings.hpp` - Add fogStart/fogEnd settings
4. `Source/oz_assetmapper.cpp` - Preload engine textures
5. `OTEditor/Source/Editor.hpp` - Load lit fog shader, draw billboards
6. `OTEditor/Source/Main.cpp` - Update fog rendering
7. `OTEditor/Source/Win32Dialogs.cpp` - Add fog controls to EnvPanel
8. `build-data.ps1` - Package new shader and textures

---

## Execution Order

1. **Create LitFog.fs shader** (30 min)
2. **Update client to use lit fog** (1 hour)
3. **Update editor to use lit fog** (1 hour)
4. **Create EngineBillboard helper** (1 hour)
5. **Update AssetMapper to preload textures** (30 min)
6. **Update editor to draw billboards** (1.5 hours)
7. **Update client to draw billboards** (1.5 hours)
8. **Integrate billboards with lighting** (1 hour)
9. **Testing & bug fixes** (2 hours)

**Total Estimated Time**: 9-10 hours

---

## Technical Notes

### Fog Shader Integration
- The lit fog shader should be a drop-in replacement for `Lighting.fs`
- All models using `Lighting.fs` should switch to `LitFog.fs`
- Fog uniforms are set once at initialization, updated when settings change

### Billboard Lighting
- Billboards need a mesh to receive lighting (can't use DrawBillboard directly)
- Generate a simple quad mesh, apply texture, use lit shader
- Billboard rotation: calculate yaw from camera position to billboard position

### Performance Considerations
- Billboards with lighting are more expensive than wireframes
- Limit billboard draw distance if needed (LOD system)
- Cache billboard meshes instead of generating per-frame

### Backward Compatibility
- Keep wireframe drawing as fallback if textures fail to load
- Provide toggle to disable billboards (use wireframes instead)
- Old saves without entity icons should still load correctly

---

## Success Criteria

✅ Fog interacts with lighting (not flat overlay)  
✅ Fog density increases with distance  
✅ EngineTextures display as billboards in editor  
✅ EngineTextures display as billboards in client  
✅ Billboards are lit by world lights  
✅ Billboards face camera correctly  
✅ Performance is acceptable (60 FPS with 50+ billboards)  
✅ No crashes or memory leaks  
✅ Works in both packaged and unpacked builds
