#pragma once
#include <string>
#include <vector>

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

struct EditorPanelState {
    bool showSoundMgr = false;
    bool showTextureMgr = false;
    bool showPawnMgr = false;
    bool showScriptMgr = false;
    bool showModelBrowser = false;
    bool showEnvPanel = false;
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
    int actionSpawnPawn = -1;
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

    // Light properties panel
    bool showLightProps = false;
    int lightPropTarget = -1;   // index into GameLights
    float lightColorR = 255, lightColorG = 255, lightColorB = 255;
    float lightIntensity = 1.0f, lightRadius = 50.0f;
    int lightType = 1;         // 0=directional, 1=point, 2=spot
    int lightEffect = 0;       // 0=none, 1=watery, 2=torch, 3=fire, 4=lamp
    bool lightFlare = false, lightCorona = false;
    bool actionApplyLight = false;
    int actionCsgPlace = -1;    // CSG sidebar: 0=box,1=cyl,2=sph,3=pyr,4=pln

#ifdef _WIN32
    // Window handles (Windows only)
    void* hSoundMgr = nullptr;
    void* hTextureMgr = nullptr;
    void* hPawnMgr = nullptr;
    void* hScriptMgr = nullptr;
    void* hModelBrowser = nullptr;
    void* hEnvPanel = nullptr;
    void* hPickupPanel = nullptr;
    void* hNodePanel = nullptr;
    void* hSettingsPanel = nullptr;
    void* hHeightmapEditor = nullptr;
    void* hLightProps = nullptr;

    // Preview bitmap (Windows only)
    void* hPreviewBitmap = nullptr;
    int previewW = 0, previewH = 0;

    struct WinPos { int x, y, w, h; };
    WinPos soundMgrPos   = {50, 50, 400, 280};
    WinPos textureMgrPos = {480, 50, 520, 480};
    WinPos pawnMgrPos    = {50, 360, 360, 200};
    WinPos scriptMgrPos  = {440, 400, 420, 300};
    WinPos modelBrwPos   = {100, 80, 540, 500};
    WinPos envPanelPos   = {60, 60, 440, 560};
    WinPos pickPanelPos  = {60, 400, 200, 280};
    WinPos nodePanelPos  = {290, 400, 200, 200};
    WinPos settingsPos        = {50, 50, 400, 600};
    WinPos heightmapEditorPos = {120, 100, 520, 480};
    WinPos lightPropsPos = {400, 100, 340, 480};
#endif
};

extern EditorPanelState g_editorPanels;

// --- ZoneProperties â€” replaces old EnvSettings ---
enum class GameType : uint8_t {
    SINGLEPLAYER,
    COOP,
    ETHERAL_MATCH,
    ANGEL_TEAM_GAME,
    ANGEL_RUN,
    CAPTURE_THE_ORB,
    TIME_SHIFT
};

enum class ParticleType : uint8_t {
    NONE,
    SNOW,
    RAIN,
    VOID_REALM,
    PSYCHIC_REALM
};

struct ZoneProperties {
    // Fog
    int fogR = 200, fogG = 200, fogB = 210;
    float fogDensity = 0.02f;
    float fogStart = 10.0f, fogEnd = 100.0f;
    bool applyFog = false;
    // Ambient
    int ambR = 180, ambG = 180, ambB = 200;
    float ambIntensity = 0.4f;
    bool applyAmbient = false;
    // GameType
    GameType gameType = GameType::SINGLEPLAYER;
    int maxPlayers = 8;
    float respawnTime = 5.0f;
    bool timeLimitEnabled = false;
    float timeLimitMinutes = 10.0f;
    int scoreLimit = 50;
    bool friendlyFire = false;
    // Particles
    ParticleType particleType = ParticleType::NONE;
    float particleDensity = 50.0f;
    float particleSpeed = 1.0f;
    int particleColorR = 200, particleColorG = 200, particleColorB = 200;
    float particleWindX = 0.0f, particleWindZ = 0.0f;
    bool applyParticles = false;
    // Skybox
    std::string skyboxTexturePath;  // path to skybox texture, empty = world default
};

ZoneProperties GetZoneProperties();
void ClearZoneApplyFlags();

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
void DestroyAllEditorWindows();
void ShowSoundManager(bool show);
void ShowTextureManager(bool show);
void ShowPawnManager(bool show);
void ShowScriptManager(bool show);
void ShowModelBrowser(bool show);
void ShowEnvPanel(bool show);
void ShowPickupPanel(bool show);
void ShowNodePanel(bool show);
void ShowHeightmapEditor(bool show);
void ShowLightProps(bool show);
void UpdateModelPreview(void* hBmp, int w, int h);
void ScanModelBrowserFiles();
void SetTextureTargetNames(const std::vector<std::string>& names);
bool ChooseOpenWorldFile(std::string& outPath);
bool ChooseSaveWorldFile(std::string& outPath);
#else
// Stub implementations for non-Windows
inline void CreateAllEditorWindows(void*, void*) {}
inline void DestroyAllEditorWindows() {}
inline void ShowSoundManager(bool) {}
inline void ShowTextureManager(bool) {}
inline void ShowPawnManager(bool) {}
inline void ShowScriptManager(bool) {}
inline void ShowModelBrowser(bool) {}
inline void ShowEnvPanel(bool) {}
inline void ShowPickupPanel(bool) {}
inline void ShowNodePanel(bool) {}
inline void ShowHeightmapEditor(bool) {}
inline void ShowLightProps(bool) {}
inline void UpdateModelPreview(void*, int, int) {}
inline void ScanModelBrowserFiles() {}
inline void SetTextureTargetNames(const std::vector<std::string>&) {}
inline bool ChooseOpenWorldFile(std::string&) { return false; }
inline bool ChooseSaveWorldFile(std::string&) { return false; }
inline EnvSettings GetEnvSettings() { return {}; }
inline void ClearEnvApplyFlags() {}
inline void PawnManagerAddPawn(const char*, const char*) {}
inline int GetPawnCount() { return 0; }
inline const char* GetPawnName(int) { return nullptr; }
#endif
