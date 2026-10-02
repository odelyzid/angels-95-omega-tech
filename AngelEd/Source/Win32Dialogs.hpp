#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../Source/World/GameType.hpp"
#include "../../Source/World/SurfaceFlags.hpp"

// =====================================================================
// Win32Dialogs â€” Real OS-level window panels for AngelEd
// =====================================================================
// On Windows, these are actual native OS windows.
// On other platforms, they are stubs (no-ops).
// =====================================================================

// --- Shared state communicated between dialogs and main raylib loop ---
struct ModelBrowserEntry {
    std::string name;
    std::string path;
    int triangles = 0;
    int vertices = 0;
    bool loaded = false;
    int modelIndex = -1;
};

// --- Asset scoping (shared by the Model Browser and the Texture Manager) ---
// Both managers present their entries as a two-root tree instead of one flat
// list, so it is always obvious whether an asset is an editable file on disk or
// a read-only record inside a package:
//
//   (GameData)   loose files, nested by their real folder under GameData/
//   (Packages)   assets that only exist inside a .oz* package
//
// A .oz* file is the ONLY thing treated as a package (see PackageAssetLoader);
// anything reachable on disk is a real file even if a package also holds a copy
// of the same name.
struct AssetScopeItem {
    std::string name;        // display/file stem
    std::string path;        // on-disk path, or package key
    bool        fromPackage = false;
};

struct AssetScopeNode {
    std::string label;       // display text for this node
    int         entryIndex = -1;              // >= 0 => leaf into the caller's vector
    std::vector<AssetScopeNode> children;
};

// Group `items` into the (GameData)/(Packages) tree. `search` is a
// case-insensitive substring filter on name+path; when non-empty, only matching
// leaves are kept (and empty folders are pruned). Roots are auto-expanded when
// filtering so matches are visible without clicking.
AssetScopeNode BuildAssetScope(const std::vector<AssetScopeItem>& items,
                               const std::string& search);

#ifdef _WIN32
// Collect the leaf entry indices of the subtree rooted at `node` (walked with
// TVGN_CHILD). Pass TVI_ROOT to collect every root. Drives the Texture Manager's
// thumbnail grid from the scope tree. `node` is an HTREEITEM, typed as void*
// because this header is included before <windows.h>.
void CollectScopeLeavesUnder(HWND tree, void* node, std::vector<int>& out);
#endif

struct EditorPanelState {
    bool showSoundMgr = false;
    bool showTextureMgr = false;
    bool showPawnMgr = false;
    bool showScriptMgr = false;
    bool showModelBrowser = false;
    bool showPickupPanel = false;
    bool showNodePanel = false;
    bool showSettingsPanel = false;
    bool showHeightmapEditor = false;
    // Model browser state (used cross-platform)
    std::vector<ModelBrowserEntry> modelEntries;
    int selectedModel = -1;

    // Action flags (set by dialog procs, read by main raylib loop)
    int actionPickupType = -1;
    int actionNodeType = -1;
    int actionPlaceModel = -1;
    bool actionRefreshBrowser = false;
    std::string actionSpawnPawn;
    std::string actionSpawnMesh;    // "static" | "skeletal" — GameEngine.Mesh placement
    bool actionSpawnParticleEmitter = false; // place a default ParticleEmitter at camera
    bool actionSpawnPathNode = false;        // place a GameEngine.PathNode at camera
    bool actionSpawnWindZone = false;        // place a WindZone at camera
    bool actionSpawnPlayerStart = false;     // place a PlayerStartNode at camera
    std::string actionSpawnEmitter;          // "sound" | "music" — place an EmitterNode
    int actionSpawnZone = -1;                // ZoneType index — place a ZoneVolumeNode
    // Camera aim point, refreshed every frame by Main.cpp, so Win32 panels can
    // spawn entities synchronously without waiting on the main-loop action flags.
    float spawnPos[3] = {0.0f, 0.0f, 0.0f};
    std::string actionTexturePath;
    int actionTextureTarget = -1;
    std::string actionPreviewSoundPath;
    bool actionStopSoundPreview = false;
    int actionSoundCategory = 0;    // 0=SFX, 1=Music, 2=Ambience
    int actionSoundLoop = 0;        // 0=no loop, 1=loop
    int actionSoundVolume = 80;     // 0-100
        // Heightmap editor action flags
    std::string actionHeightmapImage;
    std::string actionHeightmapTexture;
    float actionHmPosX = 0, actionHmPosY = 0, actionHmPosZ = 0;
    float actionHmSx = 100, actionHmSy = 50, actionHmSz = 100;
    float actionHmScale = 1.0f;
    bool actionGenerateHeightmap = false;

    // Light properties. Owned entirely by the Entity Properties panel (see
    // propLight*); these are the panel's backing store, read on Apply.
    int lightPropTarget = -1;   // index into GameLights
    float lightColorR = 255, lightColorG = 255, lightColorB = 255;
    float lightIntensity = 1.0f, lightRadius = 50.0f;
    int lightType = 1;         // 0=directional, 1=point, 2=spot
    int lightEffect = 0;       // 0=none, 1=watery, 2=torch, 3=fire, 4=lamp
    bool lightFlare = false, lightCorona = false;
    float lightInnerAngle = 15.0f;   // spot inner cone half-angle (degrees)
    float lightOuterAngle = 45.0f;   // spot outer cone half-angle (degrees)
    bool actionApplyLight = false;
    // --- Surface Properties (UT99-style, per-face) -------------------------
    // The dialog edits one BrushSurface at a time. `surfaceFaces` is the set of
    // faces the current selection covers (a bitmask, so "(3 Selected)" is just a
    // popcount); Apply writes to every one of them.
    bool showSurfaceProps = false;
    int  surfaceRenderable = -1;      // OzoneRenderable index
    uint32_t surfaceFaceMask = 0;     // bitmask of SurfaceFace
    bool actionApplySurface = false;
    bool actionResetSurface = false;
    // Working copy handed from the dialog to the main loop on Apply. Carrying
    // the values rather than a "read the controls" flag means an Apply that is
    // processed a frame later still writes exactly what the user saw.
    oz::surface::SurfaceProps surfaceEdit;
    int actionCsgPlace = -1;    // CSG sidebar: 0=box,1=cyl,2=sph,3=pyr,4=pln
    int currentToolMode = 0;    // persistent tool mode: 0=cam,1=move,2=scale,3=rotate
    std::string actionSpawnPickup;   // Pawn Manager "Spawn Selected" — weapon/item pickup def name

    // Terrain editing
    int terrainBrushMode = 0;       // 0=raise, 1=lower, 2=flatten
    int terrainBrushSize = 4;       // radius in grid cells
    float terrainBrushStrength = 0.05f; // height change per click [0..1]
    int actionCsgCommitNow = -1; // CSG operation to place immediately (-1 = inactive)
    int actionWorldGraphProperties = -1; // WorldGraph item index to open properties
    int actionWorldGraphDelete = -1; // WorldGraph entity index to delete
    int actionWorldGraphDup = -1;    // WorldGraph entity index to duplicate

    // Active texture tracking (for context menu apply + auto-apply)
    std::string activeTexturePath;   // currently selected texture in browser
    int activeTextureSlot = 0;       // tileset slot index if applicable
    bool actionApplyTextureToSel = false;  // flag: apply activeTexturePath to selected entity

    // WorldGraph Explorer
    bool showWorldGraph = false;
    int actionSelectFromGraph = -1;         // item index selected
    int actionSelectFromGraphType = -1;     // SelType encoded
    std::string actionSelectFromGraphName;
    float actionSelectFromGraphPos[3] = {0,0,0};

    // LevelList / Campaign panel
    bool showLevelList = false;
    std::string actionLevelListOpen;        // world folder name to open
    std::string actionLevelListLink;        // create portal in current world -> target world
    std::string portalTargetWorld;          // default target for newly placed portals

    // Portal deletion action. Portal *editing* is the Entity Properties panel's
    // PORTAL section; only the delete needs an out-of-band channel because it
    // mutates the vector the panel is indexing into.
    int  actionDeletePortal = -1;           // portal index to delete

    // Properties panel (context-sensitive)
    bool showPropsPanel = false;

    // Viewport collision visualisation. Draws the post-CSG collision volumes as
    // wireframes so it is possible to see what the player will actually stand
    // on, and also reveals the generated SURF_COLLISION_PROXY boxes.
    bool showCollisionBounds = false;
    // Set when a Sub/Intersect brush produced no collision volume at all. The
    // sidebar paints the collision count red until the next brush is committed.
    bool collisionOpWarning = false;
    // Pawn Manager: show metadata-only defs (EntityType::GAMETYPE) that are not
    // placeable. Off by default so the tree stays focused on placeable actors.
    bool showHidden = false;

    int propsTargetType = -1;       // SelType encoded
    int propsTargetIndex = -1;
    std::string propsTargetName;
    float propsTargetPos[3] = {0,0,0};
    float propsTargetScale = 1.0f;
    float propsTargetRotation = 0.0f;
    // False when the selection has no yaw concept (zones, pickups, portals...).
    // The apply handler must not write propsTargetRotation in that case: the
    // Rot row is seeded from a 0.0f default and used to overwrite the authored
    // value (notably `playerstart`'s yaw) on every Apply.
    bool propsTargetHasRotation = false;
    // Apply results (set by panel, consumed by Main.cpp)
    bool actionApplyProperties = false;
    float propPosX = 0, propPosY = 0, propPosZ = 0;
    float propSizeX = 1, propSizeY = 1, propSizeZ = 1;  // for brush/zone
    float propRotation = 0;
    float propScale = 1;
    float propTexScaleU = 1.0f;
    float propTexScaleV = 1.0f;
    float propTexOffsetU = 0.0f;
    float propTexOffsetV = 0.0f;

    // Def-aligned property sections (filled by ShowPropertiesPanel, read by Main.cpp)
    std::string propDefPath;        // .ozls/.cfg source path ("" = no def / section hidden)
    std::string propDefTitle;       // e.g. "HealthVial  [pickup]"
    std::string propDefSource;      // source file path shown as a row
    struct DefField { std::string key; std::string value; };
    std::vector<DefField> propDefPawnFields; // PawnDefs/*.cfg values (pawns only)
    std::vector<DefField> propDefFields;     // .ozls stats (floats + strings)
    std::vector<DefField> propDefActions;    // action name -> "N lines"

    // --- Editable .ozls stat rows -----------------------------------------
    //
    // propDefFields above is the full authored set, rendered read-only.
    // These are the subset the panel knows how to edit for the selected def's
    // entity type, with a per-row control ID so Apply can read the values back.
    //
    // Keys NOT in this list still render as read-only rows, so an authored stat
    // the editor has never heard of is visible rather than silently dropped.
struct DefStatRow {
    std::string key;
    std::string value;
    // Human label from the schema. The panel used to print only `key`, so every
    // authored label was dead data — yet the label is what makes the row
    // readable ("Reload Time" vs "reload_time").
    std::string label;
    // Schema group this row belongs under; empty means "continues the previous
    // group". Drives the sub-headers inside the Edit stats section.
    std::string group;
    int controlId = 0;          // ID_PP_STATS_BASE + index
    bool isFloat = false;       // right-aligned numeric field
    bool isSoundPath = false;   // gets a Browse... and a Preview button
    };
    std::vector<DefStatRow> propDefEditable;
    // Collected on Apply and consumed by Main.cpp, which performs the actual
    // patch via ozls::PatchOzlsStats (Win32Dialogs must not write files itself).
    std::vector<DefField> propDefPendingEdits;
    // True when propDefPath names a real file on disk. False means the def came
    // from a package, which cannot be written back to - the panel shows the
    // rows read-only in that case rather than failing on Apply.
    bool propDefWritable = false;
    // Set when the user presses Preview on a sound row, holding the path.
    std::string propDefPreviewSound;
    float propHealth = 100;         // NPC instance health override
    float propSpeed = 1.5f;         // NPC instance speed override
    float propRespawnTime = 30;     // pickup instance respawn time
    int   propZoneType = 0;         // ZoneType index (0=water 1=ladder 2=sky 3=reverb 4=sound)
    float propZoneIntensity = 1;
    std::string propZoneName;       // zone script-hook name
    // Per-zone physics overrides (mirrors oz::physics::PhysicsInfo)
    float propZoneGravity = 20.0f;
    float propZoneJump = 8.0f;
    float propZoneTerminal = 60.0f;
    float propZoneWaterGravity = 8.0f;
    float propZoneWaterDrag = 0.95f;
    float propZoneSwimUp = 5.0f;
    float propZoneLadderSpeed = 6.0f;
    float propZoneFlyMult = 1.5f;
    // Per-zone environment overrides (mirrors oz::ZoneEnvOverrides).
    //
    // These are the values the GAME reads: ZoneManager merges them per zone into
    // PointRegion::combinedEnv and Core.hpp applies them on zone entry. Until
    // these rows existed they had NO editor surface at all — envOverrides was
    // written nowhere in AngelEd, so per-zone fog in a shipped level could only
    // be produced by hand-editing the .ozone. The Zone Properties dialog's Fog /
    // Ambient tabs looked like they did this but were level-global shader pokes.
    //
    // applyFog / applyAmbient are the flags the exporter gates on (Main.cpp's
    // ExportToOzone), so they are per-zone booleans here rather than inferred.
    bool  propZoneApplyFog = false;
    int   propZoneFogR = 179, propZoneFogG = 179, propZoneFogB = 204;
    float propZoneFogDensity = 1.0f;
    float propZoneFogStart = 10.0f, propZoneFogEnd = 100.0f;
    bool  propZoneApplyAmbient = false;
    int   propZoneAmbR = 180, propZoneAmbG = 180, propZoneAmbB = 200;
    float propZoneAmbIntensity = 0.4f;
    float propZoneReverbMix = 0.0f, propZoneReverbDecay = 0.0f;
    std::string propPortalWorld;    // portal target world
    float propPortalSpawn[3] = {0,0,0};
    bool  propPortalBidir = true;

    // --- SelType::MAP (the level itself) ---
    // These mirror LevelMetadata field for field. They are a flat copy rather
    // than a LevelMetadata member so the panel can show one flat row per field
    // and diff "changed" against the value the panel was opened with, exactly as
    // the .ozls stat rows do.
    int   propMapGameType = 0;            // oz::gametype::GameType index
    int   propMapMaxPlayers = 8;
    float propMapRespawnTime = 5.0f;
    bool  propMapTimeLimitEnabled = false;
    float propMapTimeLimitMinutes = 10.0f;
    int   propMapScoreLimit = 50;
    bool  propMapFriendlyFire = false;
    std::string propMapSkybox;            // level skybox texture path
    std::string propMapSkyboxSide;        // side/cap skybox path
    int   propMapParticleType = 0;        // ParticleType index
    float propMapParticleDensity = 50.0f;
    float propMapParticleSpeed = 1.0f;
    int   propMapParticleR = 200, propMapParticleG = 200, propMapParticleB = 200;
    float propMapParticleWindX = 0.0f, propMapParticleWindZ = 0.0f;

    // GameEngine.Mesh object editing
    std::string propMeshPath;       // model path
    std::string propMeshTex;        // texture path
    std::string propAnimClip;       // skeletal clip name
    std::string propMeshAnimFile;   // external .ozanim clip path
    float propMeshAnimSpeed = 1.0f; // playback speed
    bool actionReloadMesh = false;  // force asset reload

    // GameEngine.ParticleEmitter editing
    std::string propEmitterType;    // fire/sparks/smoke/...
    std::string propEmitterTex;     // billboard texture path
    float propEmitterRate = 20.0f;
    float propEmitterLife = 1.0f;
    float propEmitterSpeed = 2.0f;
    float propEmitterSize = 0.4f;
    float propEmitterSpread = 0.4f;
    int propEmitterR = 255, propEmitterG = 180, propEmitterB = 80;

    // GameEngine.PathNode editing
    std::string propPathName;
    float propPathRadius = 1.0f;
    std::string propPathNext;    // comma-separated successor names
    bool propPathLoop = false;

    // GameEngine.Mesh wind-affection flag
    bool propMeshWind = false;
    // WindZone editing
    float propWindDirX = 1.0f, propWindDirY = 0.0f, propWindDirZ = 0.0f;
    float propWindStrength = 1.0f;
    float propWindFrequency = 1.0f;
    float propWindSizeX = 10.0f, propWindSizeY = 10.0f, propWindSizeZ = 10.0f;

    // GameEngine.Light editing (Properties panel, SelType::LIGHT). This is the
    // supported home for light settings; the standalone Light Properties window
    // is a legacy shell that writes the same values via lightColor*/actionApplyLight.
    std::string propLightName;
    int   propLightR = 255, propLightG = 255, propLightB = 255;
    float propLightIntensity = 1.0f;
    float propLightRadius = 20.0f;
    int   propLightType = 1;      // LitLightType: 0=directional 1=point 2=spot
    int   propLightEffect = 0;    // LitLightEffect: none/watery/torch/fire/lamp
    float propLightInnerAngle = 18.0f;   // spot inner cone half-angle, degrees
    float propLightOuterAngle = 37.0f;   // spot outer cone half-angle, degrees
    bool  propLightFlare = false, propLightCorona = false;
    float propLightTarget[3] = {0,0,0};

    // Animation / vertex-keyframe tool
    bool showAnimPanel = false;
    int animTargetMesh = -1;        // MeshObjectNode id (set by Main.cpp from selection)
    std::string animClipName;       // selected clip name
    float animTime = 0.0f;          // scrub time (seconds)
    float animFps = 30.0f;
    bool animLoop = true;
    bool animPlaying = false;
    int animTimeSlider = 0;         // 0..1000 (drives animTime)
    std::string animStatus;         // read-only status line
    bool actionAnimNewClip = false;
    bool actionAnimDeleteClip = false;
    bool actionAnimSave = false;
    bool actionConvertToAnimated = false;
    bool actionAnimRefresh = false;
    bool actionAnimScrub = false;       // timeline dragged
    bool actionAnimAddKey = false;      // Phase C
    bool actionAnimDeleteKey = false;   // Phase C
    bool actionAnimApplyClipMeta = false; // fps/loop edited in the panel
    // Vertex definition tool (Phase C)
    bool animEditVerts = false;         // vertex-edit mode active
    std::vector<int> animSelVerts;      // selected global vertex indices
    // Undo/redo snapshot stacks live in Main.cpp (they hold ozanim::Animation).
    float animPrevX = 0, animPrevY = 0, animPrevZ = 0, animPrevR = 0;
    bool animPrevValid = false;
    bool actionAnimToggleEdit = false;
    bool actionAnimUndo = false;
    bool actionAnimRedo = false;
    bool actionAnimSelectAll = false;
    bool actionAnimClearSel = false;

#ifdef _WIN32
    // Window handles (Windows only)
    void* hSoundMgr = nullptr;
    void* hTextureMgr = nullptr;
    void* hPawnMgr = nullptr;
    void* hScriptMgr = nullptr;
    void* hModelBrowser = nullptr;
    void* hPickupPanel = nullptr;
    void* hNodePanel = nullptr;
    void* hSettingsPanel = nullptr;
    void* hHeightmapEditor = nullptr;
    void* hWorldGraph = nullptr;
    void* hPropsPanel = nullptr;
    void* hStatsSidebar = nullptr;
    void* hLevelList = nullptr;
    void* hAnimPanel = nullptr;
    void* hSurfaceProps = nullptr;

    // Preview bitmap (Windows only)
    void* hPreviewBitmap = nullptr;
    int previewW = 0, previewH = 0;

    struct WinPos { int x, y, w, h; };
    WinPos soundMgrPos   = {50, 50, 400, 280};
    WinPos textureMgrPos = {480, 50, 520, 480};
    WinPos pawnMgrPos    = {50, 300, 400, 300};
    WinPos scriptMgrPos  = {440, 340, 560, 450};
    WinPos modelBrwPos   = {100, 80, 540, 500};
    WinPos pickPanelPos  = {60, 400, 200, 280};
    WinPos nodePanelPos  = {290, 400, 200, 200};
    WinPos settingsPos        = {50, 50, 400, 600};
    WinPos heightmapEditorPos = {120, 100, 520, 480};
    WinPos worldGraphPos = {540, 100, 600, 400};
    // 540 (not 470): the vertical scrollbar costs ~17px of client width, so this
    // keeps the usable value column as wide as it was before it was added.
WinPos propsPanelPos = {300, 120, 540, 560};
    WinPos levelListPos = {200, 120, 560, 420};
    WinPos animPanelPos  = {430, 180, 380, 390};
    WinPos surfacePropsPos = {760, 140, 520, 440};
#endif
};

extern EditorPanelState g_editorPanels;

// Open the Entity Properties panel on a DEF by name rather than a world
// instance. Used by the Script Manager's Properties button, which is the only
// route to a def with no instance in the open world (Player.ozls).
void ShowDefPropertiesFor(const std::string& defName);

// --- Level state ---
// GameType is now defined in Source/World/GameType.hpp (oz::gametype::GameType)
// so the editor, server and tests share one source of truth. The numeric values
// are identical to the legacy enum, so every saved levelinfo line still loads.
using oz::gametype::GameType;

enum class ParticleType : uint8_t {
    NONE,
    SNOW,
    RAIN,
    VOID_REALM,
    PSYCHIC_REALM
};

// Level metadata — persisted via LevelInfo/Particles instructions in both formats
struct LevelMetadata {
    // GameType / rules
    GameType gameType = GameType::SINGLEPLAYER;
    int maxPlayers = 8;
    float respawnTime = 5.0f;
    bool timeLimitEnabled = false;
    float timeLimitMinutes = 10.0f;
    int scoreLimit = 50;
    bool friendlyFire = false;
    std::string skyboxTexturePath;
    // Side/cap skybox, the second path token on the OZONE `levelinfo` line. It
    // was previously parsed into OzonePrimitive::entitySubType and then dropped:
    // nothing read it on load and ExportToOzone never wrote it, so opening and
    // re-saving a world silently deleted it.
    std::string skyboxSidePath;
    // Ambient particles
    ParticleType particleType = ParticleType::NONE;
    float particleDensity = 50.0f;
    float particleSpeed = 1.0f;
    int particleColorR = 200, particleColorG = 200, particleColorB = 200;
    float particleWindX = 0.0f, particleWindZ = 0.0f;
};

LevelMetadata GetLevelMetadata();
void SetLevelMetadata(const LevelMetadata& meta);   // the Map row's only owner

// --- Portals ---
// Portals are edited in the Entity Properties panel's PORTAL section, which reads
// and writes ZoneManager::GetPortals() directly. These three helpers survive only
// because Main.cpp already called them; GetPortalCount/GetPortalTargetWorld have
// no callers at all and are logged as dead in
// Wiki/Editor-PropertyPanel-Refactor.md (P4).
void RefreshPortalList();           // intentionally inert now — see the .cpp
int  GetPortalCount();
const char* GetPortalTargetWorld(int index);

// --- Pawn management ---
void PawnManagerAddPawn(const char* name, const char* meshPath);
int GetPawnCount();
const char* GetPawnName(int index);

// Pawn tree node for hierarchical view
struct PawnTreeNode {
    std::string label;
    bool isExpanded = false;
    std::vector<PawnTreeNode> children;
    std::string defName;  // empty for category nodes, valid for leaf nodes
    std::string typeTag;  // "enemy", "weapon", "pickup", "playerstart", "zone", "emitter", ""
};
// Build the full tree from current PawnSystem state
PawnTreeNode BuildPawnTree();

#ifdef _WIN32
// --- Windows-only functions ---
void CreateAllEditorWindows(void* hInst, void* hRaylibWnd);

// Rebuild the Pawn Manager "Actor Hierarchy" tree from the current registries.
// Call after PawnDefs/.ozls defs are loaded (windows are created before then).
void RefreshPawnManager();
void DestroyAllEditorWindows();
void ShowSoundManager(bool show);
void ShowTextureManager(bool show);
void ShowPawnManager(bool show);
void ShowScriptManager(bool show);
void ShowModelBrowser(bool show);
void ShowPickupPanel(bool show);
void ShowNodePanel(bool show);
void ShowHeightmapEditor(bool show);
void ShowWorldGraph(bool show);

    // Surface Properties (UT99-style, per-face). `renderable` is an
    // OzoneRenderable index and `faceMask` a bitmask of SurfaceFace, so one
    // dialog can edit "(3 Selected)" at once. Rebuilds the controls on open.
    void ShowSurfaceProps(bool show, int renderable, uint32_t faceMask);
    // Re-read from the renderable and rebuild the controls, so the dialog shows
    // what was actually stored after an Apply.
    void SurfacePropsRefresh(void* hwnd);
void ShowPropertiesPanel(bool show);
void ShowAnimPanel(bool show);
void RefreshAnimPanel();
void ShowLevelList(bool show);
void RefreshLevelList();
void RefreshWorldGraph();
void UpdateModelPreview(void* hBmp, int w, int h);
void ScanModelBrowserFiles();
void SetTextureTargetNames(const std::vector<std::string>& names);
bool ChooseOpenWorldFile(std::string& outPath);
bool ChooseSaveWorldFile(std::string& outPath);
void UpdateStatsSidebar(float posX, float posY, float posZ,
                        float sizeX, float sizeY, float sizeZ,
                        float rot, float scale,
                        int collisionVols, int chunks,
                        const char* mode,
                        float camX, float camY, float camZ);
void LayoutStatsSidebar(int clientW, int clientH, int topOffset, int width);
int GetStatsSidebarWidth();
#else
// Stub implementations for non-Windows
inline void CreateAllEditorWindows(void*, void*) {}
inline void RefreshPawnManager() {}
inline void DestroyAllEditorWindows() {}
inline void ShowSoundManager(bool) {}
inline void ShowTextureManager(bool) {}
inline void ShowPawnManager(bool) {}
inline void ShowScriptManager(bool) {}
inline void ShowModelBrowser(bool) {}
inline void ShowAnimPanel(bool) {}
inline void RefreshAnimPanel() {}
inline void ShowPickupPanel(bool) {}
inline void ShowNodePanel(bool) {}
inline void ShowHeightmapEditor(bool) {}
inline void ShowWorldGraph(bool) {}
inline void ShowPropertiesPanel(bool) {}
inline void ShowLevelList(bool) {}
inline void RefreshLevelList() {}
inline void RefreshWorldGraph() {}
inline void UpdateModelPreview(void*, int, int) {}
inline void ScanModelBrowserFiles() {}
inline void SetTextureTargetNames(const std::vector<std::string>&) {}
inline bool ChooseOpenWorldFile(std::string&) { return false; }
inline bool ChooseSaveWorldFile(std::string&) { return false; }
inline void UpdateStatsSidebar(float, float, float, float, float, float, float, float, int, int, const char*, float, float, float) {}
inline void LayoutStatsSidebar(int, int, int, int) {}
inline int GetStatsSidebarWidth() { return 200; }
inline LevelMetadata GetLevelMetadata() { return {}; }
inline void SetLevelMetadata(const LevelMetadata&) {}
inline void RefreshPortalList() {}
inline int GetPortalCount() { return 0; }
inline const char* GetPortalTargetWorld(int) { return nullptr; }
inline void PawnManagerAddPawn(const char*, const char*) {}
inline int GetPawnCount() { return 0; }
inline const char* GetPawnName(int) { return nullptr; }
#endif
