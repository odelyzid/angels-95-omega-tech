#pragma once
#include <string>
#include <vector>

// =====================================================================
// Win32Dialogs — Real OS-level window panels for oz_editor
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

    // Model browser state (used cross-platform)
    std::vector<ModelBrowserEntry> modelEntries;
    int selectedModel = -1;

    // Action flags (set by dialog procs, read by main raylib loop)
    int actionPickupType = -1;
    int actionNodeType = -1;
    int actionPlaceModel = -1;
    bool actionRefreshBrowser = false;
    int actionSpawnPawn = -1;

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

    // Preview bitmap (Windows only)
    void* hPreviewBitmap = nullptr;
    int previewW = 0, previewH = 0;

    struct WinPos { int x, y, w, h; };
    WinPos soundMgrPos   = {50, 50, 400, 280};
    WinPos textureMgrPos = {480, 50, 480, 320};
    WinPos pawnMgrPos    = {50, 360, 360, 200};
    WinPos scriptMgrPos  = {440, 400, 420, 300};
    WinPos modelBrwPos   = {100, 80, 540, 500};
    WinPos envPanelPos   = {60, 60, 420, 360};
    WinPos pickPanelPos  = {60, 400, 200, 280};
    WinPos nodePanelPos  = {290, 400, 200, 200};
#endif
};

extern EditorPanelState g_editorPanels;

// --- Environment settings getters (for editor rendering) ---
struct EnvSettings {
    int fogR = 200, fogG = 200, fogB = 210;
    float fogDensity = 0.02f;
    int ambR = 180, ambG = 180, ambB = 200;
    float ambIntensity = 0.4f;
    bool applyFog = false;
    bool applyAmbient = false;
};
EnvSettings GetEnvSettings();
void ClearEnvApplyFlags();

// --- Pawn management ---
void PawnManagerAddPawn(const char* name, const char* meshPath);
int GetPawnCount();
const char* GetPawnName(int index);

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
void UpdateModelPreview(void* hBmp, int w, int h);
void ScanModelBrowserFiles();
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
inline void UpdateModelPreview(void*, int, int) {}
inline void ScanModelBrowserFiles() {}
inline EnvSettings GetEnvSettings() { return {}; }
inline void ClearEnvApplyFlags() {}
inline void PawnManagerAddPawn(const char*, const char*) {}
inline int GetPawnCount() { return 0; }
inline const char* GetPawnName(int) { return nullptr; }
#endif
