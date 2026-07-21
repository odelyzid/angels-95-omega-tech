#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include "../../Source/WindowsCompat.hpp"
#include "../../Source/Package/PackageAssetLoader.hpp"
#include "../../Source/Pawn/OzPawnSystem.hpp"
#include "Win32Dialogs.hpp"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// =====================================================================
// Globals
// =====================================================================
EditorPanelState g_editorPanels;

static HINSTANCE g_hInst = nullptr;
static HWND g_hRaylibWnd = nullptr;
static HWND g_hMainWnd = nullptr; // alias for g_hRaylibWnd in message handling

static const wchar_t* CLASS_SOUNDMGR    = L"OzSoundMgr";
static const wchar_t* CLASS_TEXTUREMGR  = L"OzTextureMgr";
static const wchar_t* CLASS_PAWNNMGR    = L"OzPawnMgr";
static const wchar_t* CLASS_SCRIPTMGR   = L"OzScriptMgr";
static const wchar_t* CLASS_MODELBRW    = L"OzModelBrw";
static const wchar_t* CLASS_ENVPANEL    = L"OzEnvPanel";
static const wchar_t* CLASS_PICKUPPANEL = L"OzPickupPanel";
static const wchar_t* CLASS_NODEPANEL   = L"OzNodePanel";
static const wchar_t* CLASS_HMEDITOR   = L"OzHmEditor";
static const wchar_t* CLASS_CSGSIDEBAR = L"OzCsgSidebar";

// Environment settings (read by editor rendering loop)
static EnvSettings g_env;

// Entry structures for dynamic resource browsers
struct ResourceEntry {
    std::string name;
    std::string path;
};

static std::vector<ResourceEntry> g_textureFiles;
static std::vector<ResourceEntry> g_soundFiles;

// Texture target model names (set from Main.cpp after model loading)
static std::vector<std::string> g_textureTargetNames;

// Pawn data (editor-local, distinct from oz_pawn_system.h::PawnDef)
struct EditorPawnDef {
    std::string name;
    std::string meshPath;
};
static std::vector<EditorPawnDef> g_pawns;

// Model preview sequence number (for refresh)
static int g_previewSeq = 0;

// =====================================================================
// Helper: scan GameData/ filesystem + packages for matching extensions
// =====================================================================
static void ScanFilesAndPackages(const std::string& subdir,
                                  const std::vector<std::string>& exts,
                                  std::vector<ResourceEntry>& out) {
    out.clear();
    // Filesystem scan under GameData/
    fs::path base = fs::current_path() / "GameData";
    if (!subdir.empty()) base /= subdir;
    try {
        if (fs::exists(base)) {
            for (auto& entry : fs::recursive_directory_iterator(base)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    for (const auto& e : exts) {
                        if (ext == e) {
                            out.push_back({ entry.path().stem().string(), entry.path().string() });
                            break;
                        }
                    }
                }
            }
        }
    } catch (...) {}

    // Package entries
    std::vector<std::string> pkgFiles;
    PackageAssetLoader::Instance().ListAllFiles(pkgFiles);
    for (const auto& pkgPath : pkgFiles) {
        std::string ext = pkgPath.substr(pkgPath.rfind('.'));
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        for (const auto& e : exts) {
            if (ext == e) {
                // Extract filename without extension for display
                std::string name = pkgPath;
                size_t slash = name.rfind('/');
                if (slash != std::string::npos) name = name.substr(slash + 1);
                size_t dot = name.rfind('.');
                if (dot != std::string::npos) name = name.substr(0, dot);
                out.push_back({ name, pkgPath });
                break;
            }
        }
    }

    std::sort(out.begin(), out.end(),
        [](const ResourceEntry& a, const ResourceEntry& b) { return a.name < b.name; });
}

// =====================================================================
// Forward declarations
// =====================================================================
static LRESULT CALLBACK SoundMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK TextureMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK PawnMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK ScriptMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK ModelBrwProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK EnvPanelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK PickupPanelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK NodePanelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK HmEditorProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK CsgSidebarProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);

// =====================================================================
// Helper functions
// =====================================================================
static bool RegisterPanelClass(const wchar_t* className, WNDPROC proc, HINSTANCE hInst) {
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = className;
    return RegisterClassEx(&wc) != 0;
}

static HWND CreateCtrl(HWND hParent, const wchar_t* cls, const wchar_t* text,
                       int x, int y, int w, int h, int id, DWORD extraStyle = 0) {
    return CreateWindowEx(0, cls, text, WS_CHILD | WS_VISIBLE | extraStyle,
                          x, y, w, h, hParent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
}

static HWND CreateButton(HWND hParent, const wchar_t* text, int x, int y, int w, int h, int id) {
    return CreateCtrl(hParent, L"BUTTON", text, x, y, w, h, id, BS_PUSHBUTTON);
}

static HWND CreateLabel(HWND hParent, const wchar_t* text, int x, int y, int w, int h, int id) {
    return CreateCtrl(hParent, L"STATIC", text, x, y, w, h, id, SS_LEFT);
}

static HWND CreateListBox(HWND hParent, int x, int y, int w, int h, int id) {
    return CreateCtrl(hParent, L"LISTBOX", L"", x, y, w, h, id,
                      WS_BORDER | WS_VSCROLL | LBS_NOTIFY);
}

// =====================================================================
// Sound Manager (Fully Functional Dynamic File Browser)
// =====================================================================
static const int ID_SOUND_LIST    = 101;
static const int ID_SOUND_REFRESH = 102;
static const int ID_SOUND_CLOSE   = 103;
static const int ID_SOUND_PLAY    = 104;
static const int ID_SOUND_STOP    = 105;

void ShowSoundManager(bool show) {
    g_editorPanels.showSoundMgr = show;
    if (g_editorPanels.hSoundMgr)
        ShowWindow((HWND)g_editorPanels.hSoundMgr, show ? SW_SHOW : SW_HIDE);
}

void ScanSoundBrowserFiles() {
    ScanFilesAndPackages("", { ".wav", ".ogg", ".mp3" }, g_soundFiles);
    if (g_editorPanels.hSoundMgr) {
        SendMessage((HWND)g_editorPanels.hSoundMgr, WM_USER + 50, 0, 0);
    }
}

static LRESULT CALLBACK SoundMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList;
    switch (msg) {
    case WM_CREATE: {
        CreateLabel(hwnd, L"Available Audio Files (GameData/Audio):", 10, 10, 320, 20, 1);
        hList = CreateListBox(hwnd, 10, 35, 440, 180, ID_SOUND_LIST);
        CreateButton(hwnd, L"Refresh", 10, 225, 90, 28, ID_SOUND_REFRESH);
        CreateButton(hwnd, L"Play", 110, 225, 90, 28, ID_SOUND_PLAY);
        CreateButton(hwnd, L"Stop", 210, 225, 90, 28, ID_SOUND_STOP);
        CreateButton(hwnd, L"Close", 350, 225, 100, 28, ID_SOUND_CLOSE);
        ScanSoundBrowserFiles();
        break;
    }
    case WM_USER + 50: {
        SendMessage(hList, LB_RESETCONTENT, 0, 0);
        for (const auto& snd : g_soundFiles) {
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)snd.name.c_str());
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_SOUND_CLOSE) {
            ShowSoundManager(false);
        } else if (id == ID_SOUND_REFRESH) {
            ScanSoundBrowserFiles();
        } else if (id == ID_SOUND_STOP) {
            g_editorPanels.actionStopSoundPreview = true;
        } else if (id == ID_SOUND_PLAY || (id == ID_SOUND_LIST && HIWORD(w) == LBN_DBLCLK)) {
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_soundFiles.size()) {
                g_editorPanels.actionPreviewSoundPath = g_soundFiles[sel].path;
            }
        }
        break;
    }
    case WM_CLOSE:
        g_editorPanels.actionStopSoundPreview = true;
        ShowSoundManager(false);
        break;
    case WM_DESTROY:
        g_editorPanels.hSoundMgr = nullptr;
        break;
    default:
        return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

// =====================================================================
// Texture Manager (Fully Functional Dynamic File Browser)
// =====================================================================
static const int ID_TEX_LIST    = 101;
static const int ID_TEX_REFRESH = 102;
static const int ID_TEX_CLOSE   = 103;
static const int ID_TEX_TARGET  = 104;
static const int ID_TEX_APPLY   = 105;

void ShowTextureManager(bool show) {
    g_editorPanels.showTextureMgr = show;
    if (g_editorPanels.hTextureMgr)
        ShowWindow((HWND)g_editorPanels.hTextureMgr, show ? SW_SHOW : SW_HIDE);
}

void ScanTextureBrowserFiles() {
    ScanFilesAndPackages("", { ".png", ".tga", ".bmp", ".jpg", ".jpeg", ".gif" }, g_textureFiles);
    if (g_editorPanels.hTextureMgr) {
        SendMessage((HWND)g_editorPanels.hTextureMgr, WM_USER + 50, 0, 0);
    }
}

void UpdateTextureManagerList() {
    ScanTextureBrowserFiles();
}

void SetTextureTargetNames(const std::vector<std::string>& names) {
    g_textureTargetNames = names;
    if (g_editorPanels.hTextureMgr) {
        SendMessage((HWND)g_editorPanels.hTextureMgr, WM_USER + 51, 0, 0);
    }
}

static LRESULT CALLBACK TextureMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList;
    static HWND hTarget;
    switch (msg) {
    case WM_CREATE: {
        CreateLabel(hwnd, L"Available Textures (GameData/Textures):", 10, 10, 320, 20, 1);
        hList = CreateListBox(hwnd, 10, 35, 440, 190, ID_TEX_LIST);
        CreateLabel(hwnd, L"Target:", 10, 237, 55, 20, 2);
        hTarget = CreateWindowEx(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                 65, 233, 180, 300, hwnd, (HMENU)(INT_PTR)ID_TEX_TARGET, g_hInst, nullptr);
        // Populate from dynamically loaded model names (fallback to "Model N" if empty)
        if (g_textureTargetNames.empty()) {
            for (int model = 1; model <= 20; model++) {
                wchar_t label[32];
                swprintf(label, 32, L"Model %d", model);
                SendMessage(hTarget, CB_ADDSTRING, 0, (LPARAM)label);
            }
        } else {
            for (const auto& name : g_textureTargetNames) {
                std::wstring wname(name.begin(), name.end());
                SendMessage(hTarget, CB_ADDSTRING, 0, (LPARAM)wname.c_str());
            }
        }
        SendMessage(hTarget, CB_SETCURSEL, 0, 0);
        CreateButton(hwnd, L"Refresh", 10, 280, 90, 28, ID_TEX_REFRESH);
        CreateButton(hwnd, L"Apply", 110, 280, 90, 28, ID_TEX_APPLY);
        CreateButton(hwnd, L"Close", 350, 280, 100, 28, ID_TEX_CLOSE);
        ScanTextureBrowserFiles();
        break;
    }
    case WM_USER + 50: {
        SendMessage(hList, LB_RESETCONTENT, 0, 0);
        for (const auto& tex : g_textureFiles) {
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)tex.name.c_str());
        }
        break;
    }
    case WM_USER + 51: {
        // Repopulate target combo from updated model names
        HWND hTarg = GetDlgItem(hwnd, ID_TEX_TARGET);
        if (hTarg) {
            SendMessage(hTarg, CB_RESETCONTENT, 0, 0);
            if (g_textureTargetNames.empty()) {
                for (int model = 1; model <= 20; model++) {
                    wchar_t label[32];
                    swprintf(label, 32, L"Model %d", model);
                    SendMessage(hTarg, CB_ADDSTRING, 0, (LPARAM)label);
                }
            } else {
                for (const auto& name : g_textureTargetNames) {
                    std::wstring wname(name.begin(), name.end());
                    SendMessage(hTarg, CB_ADDSTRING, 0, (LPARAM)wname.c_str());
                }
            }
            SendMessage(hTarg, CB_SETCURSEL, 0, 0);
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_TEX_CLOSE) {
            ShowTextureManager(false);
        } else if (id == ID_TEX_REFRESH) {
            ScanTextureBrowserFiles();
        } else if (id == ID_TEX_APPLY || (id == ID_TEX_LIST && HIWORD(w) == LBN_DBLCLK)) {
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_textureFiles.size()) {
                int target = (int)SendMessage(hTarget, CB_GETCURSEL, 0, 0);
                if (target >= 0) {
                    g_editorPanels.actionTexturePath = g_textureFiles[sel].path;
                    g_editorPanels.actionTextureTarget = target + 1;
                }
            }
        }
        break;
    }
    case WM_CLOSE:
        ShowTextureManager(false);
        break;
    case WM_DESTROY:
        g_editorPanels.hTextureMgr = nullptr;
        break;
    default:
        return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

static bool ChooseWorldFile(bool save, std::string& outPath) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_hRaylibWnd;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = save ? L"WDL World (*.wdl)\0*.wdl\0All Files (*.*)\0*.*\0\0"
                              : L"World Files (*.wdl;*.ozone)\0*.wdl;*.ozone\0WDL World (*.wdl)\0*.wdl\0OZONE World (*.ozone)\0*.ozone\0\0";
    dialog.lpstrDefExt = save ? L"wdl" : nullptr;
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) return false;

    int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return false;
    std::vector<char> utf8((size_t)size);
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), size, nullptr, nullptr);
    outPath.assign(utf8.data());
    return true;
}

bool ChooseOpenWorldFile(std::string& outPath) { return ChooseWorldFile(false, outPath); }
bool ChooseSaveWorldFile(std::string& outPath) { return ChooseWorldFile(true, outPath); }

// =====================================================================
// Pawn Manager
// =====================================================================
static const int ID_PAWN_CLOSE   = 100;
static const int ID_PAWN_ADD     = 101;
static const int ID_PAWN_LIST    = 102;
static const int ID_PAWN_SPAWN   = 103;
static const int ID_PAWN_REFRESH = 104;

void ShowPawnManager(bool show) {
    g_editorPanels.showPawnMgr = show;
    if (g_editorPanels.hPawnMgr)
        ShowWindow((HWND)g_editorPanels.hPawnMgr, show ? SW_SHOW : SW_HIDE);
}

void PawnManagerAddPawn(const char* name, const char* meshPath) {
    g_pawns.push_back({name, meshPath});
    if (g_editorPanels.hPawnMgr) {
        SendMessage((HWND)g_editorPanels.hPawnMgr, WM_USER + 50, 0, 0);
    }
}

int GetPawnCount() { return (int)g_pawns.size(); }
const char* GetPawnName(int index) {
    if (index < 0 || index >= (int)g_pawns.size()) return nullptr;
    return g_pawns[index].name.c_str();
}

static void PopulatePawnListFromSystem(HWND hList) {
    g_pawns.clear();
    // Pull from PawnSystem registered definitions
    const auto& defs = PawnSystem::Instance().GetDefs();
    for (const auto& def : defs) {
        std::string meshPath = std::string("GameData/Models/") + def.name + ".obj";
        g_pawns.push_back({ def.name, meshPath });
    }
    // Also scan for .obj files under GameData/ as potential pawn meshes
    std::vector<ResourceEntry> models;
    ScanFilesAndPackages("", { ".obj" }, models);
    for (const auto& m : models) {
        bool found = false;
        for (const auto& p : g_pawns) {
            if (p.name == m.name) { found = true; break; }
        }
        if (!found) {
            g_pawns.push_back({ m.name, m.path });
        }
    }
}

static LRESULT CALLBACK PawnMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList;
    switch (msg) {
    case WM_CREATE: {
        CreateLabel(hwnd, L"Pawn Definitions (from PawnSystem + .obj files):", 10, 10, 350, 20, 1);
        hList = CreateListBox(hwnd, 10, 35, 360, 120, ID_PAWN_LIST);
        CreateButton(hwnd, L"Spawn in World", 10, 165, 120, 28, ID_PAWN_SPAWN);
        CreateButton(hwnd, L"Refresh", 140, 165, 90, 28, ID_PAWN_REFRESH);
        CreateButton(hwnd, L"Close", 240, 165, 120, 28, ID_PAWN_CLOSE);
        SendMessage(hwnd, WM_USER + 50, 0, 0);
        break;
    }
    case WM_USER + 50: {
        PopulatePawnListFromSystem(hList);
        SendMessage(hList, LB_RESETCONTENT, 0, 0);
        for (auto& p : g_pawns)
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)p.name.c_str());
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_PAWN_CLOSE) ShowPawnManager(false);
        else if (id == ID_PAWN_REFRESH) {
            SendMessage(hwnd, WM_USER + 50, 0, 0);
        } else if (id == ID_PAWN_SPAWN) {
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_pawns.size()) {
                g_editorPanels.actionSpawnPawn = sel;
            }
        } else if (id == ID_PAWN_LIST && HIWORD(w) == LBN_DBLCLK) {
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_pawns.size()) {
                char msgBuf[256];
                snprintf(msgBuf, sizeof(msgBuf), "Selected Pawn: %s\nMesh: %s",
                         g_pawns[sel].name.c_str(), g_pawns[sel].meshPath.c_str());
                MessageBoxA(hwnd, msgBuf, "Pawn Info", MB_OK);
            }
        }
        break;
    }
    case WM_CLOSE:
        ShowPawnManager(false);
        break;
    case WM_DESTROY:
        g_editorPanels.hPawnMgr = nullptr;
        break;
    default:
        return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

// =====================================================================
// Script Manager — Dynamic file scanning (GameData/ + packages)
// =====================================================================
static const int ID_SCRIPT_CLOSE = 100;
static const int ID_SCRIPT_LIST  = 101;
static const int ID_SCRIPT_REFRESH = 102;

static std::vector<ResourceEntry> g_scriptFiles;

void ScanScriptFiles() {
    ScanFilesAndPackages("", { ".ps", ".wdl", ".ozone" }, g_scriptFiles);
    if (g_editorPanels.hScriptMgr) {
        SendMessage((HWND)g_editorPanels.hScriptMgr, WM_USER + 50, 0, 0);
    }
}

void ShowScriptManager(bool show) {
    g_editorPanels.showScriptMgr = show;
    if (g_editorPanels.hScriptMgr)
        ShowWindow((HWND)g_editorPanels.hScriptMgr, show ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK ScriptMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList;
    switch (msg) {
    case WM_CREATE: {
        CreateLabel(hwnd, L"Available Scripts (GameData/.ps .wdl .ozone):", 10, 10, 380, 20, 1);
        hList = CreateListBox(hwnd, 10, 35, 380, 220, ID_SCRIPT_LIST);
        CreateButton(hwnd, L"Refresh", 10, 265, 90, 28, ID_SCRIPT_REFRESH);
        CreateButton(hwnd, L"Close", 300, 265, 100, 28, ID_SCRIPT_CLOSE);
        ScanScriptFiles();
        break;
    }
    case WM_USER + 50: {
        SendMessage(hList, LB_RESETCONTENT, 0, 0);
        for (const auto& scr : g_scriptFiles) {
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)scr.name.c_str());
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_SCRIPT_CLOSE) {
            ShowScriptManager(false);
        } else if (id == ID_SCRIPT_REFRESH) {
            ScanScriptFiles();
        } else if (id == ID_SCRIPT_LIST && HIWORD(w) == LBN_DBLCLK) {
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_scriptFiles.size()) {
                char msgBuf[512];
                snprintf(msgBuf, sizeof(msgBuf), "Script: %s\nPath: %s",
                         g_scriptFiles[sel].name.c_str(), g_scriptFiles[sel].path.c_str());
                MessageBoxA(hwnd, msgBuf, "Script Info", MB_OK);
            }
        }
        break;
    }
    case WM_CLOSE:
        ShowScriptManager(false);
        break;
    case WM_DESTROY:
        g_editorPanels.hScriptMgr = nullptr;
        break;
    default:
        return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

// =====================================================================
// Model / Mesh Browser
// =====================================================================
static const int ID_MDL_LIST    = 100;
static const int ID_MDL_REFRESH = 101;
static const int ID_MDL_PLACE   = 102;
static const int ID_MDL_CLOSE   = 103;
static const int ID_MDL_PREVIEW = 104;

static const UINT WM_MODEL_SELECTED = WM_USER + 100;
static const UINT WM_PREVIEW_READY  = WM_USER + 101;

void ShowModelBrowser(bool show) {
    g_editorPanels.showModelBrowser = show;
    if (g_editorPanels.hModelBrowser)
        ShowWindow((HWND)g_editorPanels.hModelBrowser, show ? SW_SHOW : SW_HIDE);
}

void ScanModelBrowserFiles() {
    g_editorPanels.modelEntries.clear();
    g_editorPanels.selectedModel = -1;

    // Filesystem scan
    fs::path base = fs::current_path() / "GameData";
    try {
        if (fs::exists(base)) {
            for (auto& entry : fs::recursive_directory_iterator(base)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".obj") {
                        ModelBrowserEntry mbe;
                        mbe.name = entry.path().stem().string();
                        mbe.path = entry.path().string();
                        g_editorPanels.modelEntries.push_back(mbe);
                    }
                }
            }
        }
    } catch (...) {}

    // Package scan for .obj files
    std::vector<std::string> pkgFiles;
    PackageAssetLoader::Instance().ListAllFiles(pkgFiles);
    for (const auto& pkgPath : pkgFiles) {
        std::string ext = pkgPath.substr(pkgPath.rfind('.'));
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".obj") {
            ModelBrowserEntry mbe;
            std::string name = pkgPath;
            size_t slash = name.rfind('/');
            if (slash != std::string::npos) name = name.substr(slash + 1);
            size_t dot = name.rfind('.');
            if (dot != std::string::npos) name = name.substr(0, dot);
            mbe.name = name;
            mbe.path = pkgPath;
            g_editorPanels.modelEntries.push_back(mbe);
        }
    }
    
    std::sort(g_editorPanels.modelEntries.begin(), g_editorPanels.modelEntries.end(),
        [](auto& a, auto& b) { return a.name < b.name; });

    if (g_editorPanels.hModelBrowser)
        SendMessage((HWND)g_editorPanels.hModelBrowser, WM_USER + 50, 0, 0);
}

void UpdateModelPreview(void* hBmp, int w, int h) {
    if (g_editorPanels.hPreviewBitmap)
        DeleteObject((HGDIOBJ)g_editorPanels.hPreviewBitmap);
    g_editorPanels.hPreviewBitmap = hBmp;
    g_editorPanels.previewW = w;
    g_editorPanels.previewH = h;
    if (g_editorPanels.hModelBrowser) {
        PostMessage((HWND)g_editorPanels.hModelBrowser, WM_PREVIEW_READY,
                    (WPARAM)hBmp, MAKELPARAM(w, h));
    }
}

static LRESULT CALLBACK ModelBrwProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList, hPreview;
    int id;
    switch (msg) {
    case WM_CREATE: {
        int PW = 540, PH = 500;
        hList = CreateListBox(hwnd, 8, 32, 230, PH - 100, ID_MDL_LIST);
        hPreview = CreateWindowEx(WS_EX_STATICEDGE, L"STATIC", L"",
             WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
             248, 32, 260, 220, hwnd, (HMENU)(INT_PTR)ID_MDL_PREVIEW,
             g_hInst, nullptr);
        CreateLabel(hwnd, L"Select a model from the list", 248, 260, 260, 60, 2);
        CreateButton(hwnd, L"Refresh", 8, 4, 80, 22, ID_MDL_REFRESH);
        CreateButton(hwnd, L"Place in World", 248, 340, 120, 24, ID_MDL_PLACE);
        CreateButton(hwnd, L"Close", PW - 72, PH - 28, 64, 22, ID_MDL_CLOSE);
        break;
    }
    case WM_USER + 50: {
        SendMessage(hList, LB_RESETCONTENT, 0, 0);
        for (auto& e : g_editorPanels.modelEntries)
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)e.name.c_str());
        break;
    }
    case WM_PREVIEW_READY: {
        HBITMAP hBmp = (HBITMAP)w;
        if (hBmp) {
            SendMessage(hPreview, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmp);
        }
        break;
    }
    case WM_COMMAND: {
        id = LOWORD(w);
        if (id == ID_MDL_CLOSE) {
            ShowModelBrowser(false);
        } else if (id == ID_MDL_REFRESH) {
            g_editorPanels.actionRefreshBrowser = true;
        } else if (id == ID_MDL_PLACE) {
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_editorPanels.modelEntries.size()) {
                g_editorPanels.selectedModel = sel;
                g_editorPanels.actionPlaceModel = sel;
            }
        } else if (id == ID_MDL_LIST && HIWORD(w) == LBN_SELCHANGE) {
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_editorPanels.modelEntries.size()) {
                g_editorPanels.selectedModel = sel;
            }
        }
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)l;
        if (dis->CtlID == ID_MDL_PREVIEW && g_editorPanels.hPreviewBitmap) {
            HDC hdc = dis->hDC;
            RECT r = dis->rcItem;
            BITMAP bm;
            GetObject((HGDIOBJ)g_editorPanels.hPreviewBitmap, sizeof(bm), &bm);
            HDC hdcMem = CreateCompatibleDC(hdc);
            SelectObject(hdcMem, (HGDIOBJ)g_editorPanels.hPreviewBitmap);
            StretchBlt(hdc, r.left, r.top, r.right - r.left, r.bottom - r.top,
                       hdcMem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
            DeleteDC(hdcMem);
            return TRUE;
        }
        return DefWindowProc(hwnd, msg, w, l);
    }
    case WM_CLOSE:
        ShowModelBrowser(false);
        break;
    case WM_DESTROY:
        g_editorPanels.hModelBrowser = nullptr;
        if (g_editorPanels.hPreviewBitmap) {
            DeleteObject((HGDIOBJ)g_editorPanels.hPreviewBitmap);
            g_editorPanels.hPreviewBitmap = nullptr;
        }
        break;
    default:
        return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

// =====================================================================
// Environment Settings
// =====================================================================
static const int ID_ENV_CLOSE     = 100;
static const int ID_ENV_APPLY_FOG = 101;
static const int ID_ENV_APPLY_AMB = 102;
static const int ID_SB_FOG_R = 110, ID_SB_FOG_G = 111, ID_SB_FOG_B = 112;
static const int ID_SB_FOG_DENSITY = 113;
static const int ID_SB_AMB_R = 114, ID_SB_AMB_G = 115, ID_SB_AMB_B = 116;
static const int ID_SB_AMB_INT = 117;

void ShowEnvPanel(bool show) {
    g_editorPanels.showEnvPanel = show;
    if (g_editorPanels.hEnvPanel)
        ShowWindow((HWND)g_editorPanels.hEnvPanel, show ? SW_SHOW : SW_HIDE);
}

EnvSettings GetEnvSettings() {
    return g_env;
}

void ClearEnvApplyFlags() {
    g_env.applyFog = false;
    g_env.applyAmbient = false;
}

static void UpdateEnvFromScrollbars(HWND hwnd) {
    auto getPos = [hwnd](int id) -> int {
        return (int)SendDlgItemMessage(hwnd, id, SBM_GETPOS, 0, 0);
    };
    g_env.fogR = getPos(ID_SB_FOG_R);
    g_env.fogG = getPos(ID_SB_FOG_G);
    g_env.fogB = getPos(ID_SB_FOG_B);
    g_env.fogDensity = getPos(ID_SB_FOG_DENSITY) / 1000.0f;
    g_env.ambR = getPos(ID_SB_AMB_R);
    g_env.ambG = getPos(ID_SB_AMB_G);
    g_env.ambB = getPos(ID_SB_AMB_B);
    g_env.ambIntensity = getPos(ID_SB_AMB_INT) / 100.0f;
}

static LRESULT CALLBACK EnvPanelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        int x = 10, y = 10, sw = 250, sh = 20, gap = 25;
        auto addSlider = [&](int id, const wchar_t* label, int minv, int maxv, int def, int xoff) {
            CreateLabel(hwnd, label, x + xoff, y, 80, 20, id + 1000);
            CreateWindowEx(0, L"SCROLLBAR", L"",
                WS_CHILD | WS_VISIBLE | SBS_HORZ,
                x + xoff + 85, y, sw, sh,
                hwnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
            SetScrollRange(GetDlgItem(hwnd, id), SB_CTL, minv, maxv, TRUE);
            SetScrollPos(GetDlgItem(hwnd, id), SB_CTL, def, TRUE);
        };
        CreateLabel(hwnd, L"--- Fog Settings ---", x, y, 200, 20, 1);
        y += gap;
        addSlider(ID_SB_FOG_R, L"Fog R:", 0, 255, 200, 0); y += gap;
        addSlider(ID_SB_FOG_G, L"Fog G:", 0, 255, 200, 0); y += gap;
        addSlider(ID_SB_FOG_B, L"Fog B:", 0, 255, 210, 0); y += gap;
        addSlider(ID_SB_FOG_DENSITY, L"Density:", 0, 200, 20, 0); y += gap;
        CreateButton(hwnd, L"Apply Fog", x, y, 100, 28, ID_ENV_APPLY_FOG); y += 35;

        y += 5;
        CreateLabel(hwnd, L"--- Ambient Settings ---", x, y, 200, 20, 2); y += gap;
        addSlider(ID_SB_AMB_R, L"Amb R:", 0, 255, 180, 0); y += gap;
        addSlider(ID_SB_AMB_G, L"Amb G:", 0, 255, 180, 0); y += gap;
        addSlider(ID_SB_AMB_B, L"Amb B:", 0, 255, 200, 0); y += gap;
        addSlider(ID_SB_AMB_INT, L"Intensity:", 0, 100, 40, 0); y += gap;
        CreateButton(hwnd, L"Apply Ambient", x, y, 120, 28, ID_ENV_APPLY_AMB); y += 35;
        CreateButton(hwnd, L"Close", 300, y, 100, 28, ID_ENV_CLOSE);
        break;
    }
    case WM_HSCROLL: {
        UpdateEnvFromScrollbars(hwnd);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_ENV_CLOSE) ShowEnvPanel(false);
        else if (id == ID_ENV_APPLY_FOG) {
            UpdateEnvFromScrollbars(hwnd);
            g_env.applyFog = true;
        } else if (id == ID_ENV_APPLY_AMB) {
            UpdateEnvFromScrollbars(hwnd);
            g_env.applyAmbient = true;
        }
        break;
    }
    case WM_CLOSE:
        ShowEnvPanel(false);
        break;
    case WM_DESTROY:
        g_editorPanels.hEnvPanel = nullptr;
        break;
    default:
        return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

// =====================================================================
// Pickup Panel
// =====================================================================
static const int ID_PICK_CLOSE   = 100;
static const int ID_PICK_HEALTH  = 101;
static const int ID_PICK_MANA    = 102;
static const int ID_PICK_ENERGY  = 103;
static const int ID_PICK_KEY     = 104;
static const int ID_PICK_COIN    = 105;
static const int ID_PICK_POWERUP = 106;

static int g_lastPickupType = 0;

void ShowPickupPanel(bool show) {
    g_editorPanels.showPickupPanel = show;
    if (g_editorPanels.hPickupPanel)
        ShowWindow((HWND)g_editorPanels.hPickupPanel, show ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK PickupPanelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        CreateLabel(hwnd, L"Pickups", 10, 10, 100, 20, 1);
        CreateButton(hwnd, L"Health Vial", 10, 35, 160, 24, ID_PICK_HEALTH);
        CreateButton(hwnd, L"Mana Vial", 10, 63, 160, 24, ID_PICK_MANA);
        CreateButton(hwnd, L"Energy Crystal", 10, 91, 160, 24, ID_PICK_ENERGY);
        CreateButton(hwnd, L"Key", 10, 119, 160, 24, ID_PICK_KEY);
        CreateButton(hwnd, L"Coin", 10, 147, 160, 24, ID_PICK_COIN);
        CreateButton(hwnd, L"Powerup", 10, 175, 160, 24, ID_PICK_POWERUP);
        CreateButton(hwnd, L"Close", 70, 215, 100, 28, ID_PICK_CLOSE);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_PICK_CLOSE) ShowPickupPanel(false);
        else {
            int type = -1;
            if (id == ID_PICK_HEALTH) type = 0;
            else if (id == ID_PICK_MANA) type = 1;
            else if (id == ID_PICK_ENERGY) type = 2;
            else if (id == ID_PICK_KEY) type = 3;
            else if (id == ID_PICK_COIN) type = 4;
            else if (id == ID_PICK_POWERUP) type = 5;
            if (type >= 0) {
                g_lastPickupType = type;
                g_editorPanels.actionPickupType = type;
            }
        }
        break;
    }
    case WM_CLOSE:
        ShowPickupPanel(false);
        break;
    case WM_DESTROY:
        g_editorPanels.hPickupPanel = nullptr;
        break;
    default:
        return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

// =====================================================================
// Node Panel
// =====================================================================
static const int ID_NODE_CLOSE = 100;
static const int ID_NODE_SPAWN = 101;
static const int ID_NODE_NPC   = 102;
static const int ID_NODE_LIGHT = 103;
static const int ID_NODE_ZONE  = 104;

void ShowNodePanel(bool show) {
    g_editorPanels.showNodePanel = show;
    if (g_editorPanels.hNodePanel)
        ShowWindow((HWND)g_editorPanels.hNodePanel, show ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK NodePanelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        CreateLabel(hwnd, L"Nodes", 10, 10, 100, 20, 1);
        CreateButton(hwnd, L"Player Spawn", 10, 35, 160, 24, ID_NODE_SPAWN);
        CreateButton(hwnd, L"NPC Spawn", 10, 63, 160, 24, ID_NODE_NPC);
        CreateButton(hwnd, L"Point Light", 10, 91, 160, 24, ID_NODE_LIGHT);
        CreateButton(hwnd, L"Zone Volume", 10, 119, 160, 24, ID_NODE_ZONE);
        CreateButton(hwnd, L"Close", 50, 151, 100, 28, ID_NODE_CLOSE);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_NODE_CLOSE) ShowNodePanel(false);
        else {
            int type = -1;
            if (id == ID_NODE_SPAWN) type = 0;
            else if (id == ID_NODE_NPC) type = 1;
            else if (id == ID_NODE_LIGHT) type = 2;
            else if (id == ID_NODE_ZONE) type = 3;
            if (type >= 0) g_editorPanels.actionNodeType = type;
        }
        break;
    }
    case WM_CLOSE:
        ShowNodePanel(false);
        break;
    case WM_DESTROY:
        g_editorPanels.hNodePanel = nullptr;
        break;
    default:
        return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

// =====================================================================
// Heightmap Editor — Load, configure, and preview terrain heightmaps
// =====================================================================
static const int ID_HM_IMAGE      = 210;
static const int ID_HM_TEX        = 211;
static const int ID_HM_BROWSE_IMG = 212;
static const int ID_HM_BROWSE_TEX = 213;
static const int ID_HM_POSX       = 214;
static const int ID_HM_POSY       = 215;
static const int ID_HM_POSZ       = 216;
static const int ID_HM_SIZEX      = 217;
static const int ID_HM_SIZEY      = 218;
static const int ID_HM_SIZEZ      = 219;
static const int ID_HM_SCALE      = 220;
static const int ID_HM_GENERATE   = 221;
static const int ID_HM_CLOSE      = 222;

void ShowHeightmapEditor(bool show) {
    g_editorPanels.showHeightmapEditor = show;
    if (g_editorPanels.hHeightmapEditor)
        ShowWindow((HWND)g_editorPanels.hHeightmapEditor, show ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK HmEditorProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static wchar_t g_imgPath[512] = L"";
    static wchar_t g_texPath[512] = L"";
    switch (msg) {
    case WM_CREATE: {
        int x = 10, y = 10, lw = 100, ew = 180, bw = 80, rowH = 26, gap = 4;

        CreateLabel(hwnd, L"Heightmap Editor", x, y, 200, 20, 1); y += 24;

        CreateLabel(hwnd, L"Image Path:", x, y, lw, rowH, 2);
        CreateCtrl(hwnd, L"EDIT", L"", x + lw, y, ew, rowH, ID_HM_IMAGE, WS_BORDER | ES_AUTOHSCROLL);
        CreateButton(hwnd, L"Browse...", x + lw + ew + gap, y, bw, rowH, ID_HM_BROWSE_IMG);
        y += rowH + gap;

        CreateLabel(hwnd, L"Texture Path:", x, y, lw, rowH, 3);
        CreateCtrl(hwnd, L"EDIT", L"", x + lw, y, ew, rowH, ID_HM_TEX, WS_BORDER | ES_AUTOHSCROLL);
        CreateButton(hwnd, L"Browse...", x + lw + ew + gap, y, bw, rowH, ID_HM_BROWSE_TEX);
        y += rowH + gap + 6;

        CreateLabel(hwnd, L"Position (X Y Z):", x, y, lw + 40, rowH, 4);
        CreateCtrl(hwnd, L"EDIT", L"0", x + lw + 40, y, 50, rowH, ID_HM_POSX, WS_BORDER | ES_NUMBER);
        CreateCtrl(hwnd, L"EDIT", L"0", x + lw + 96, y, 50, rowH, ID_HM_POSY, WS_BORDER | ES_NUMBER);
        CreateCtrl(hwnd, L"EDIT", L"0", x + lw + 152, y, 50, rowH, ID_HM_POSZ, WS_BORDER | ES_NUMBER);
        y += rowH + gap;

        CreateLabel(hwnd, L"Size (W H D):", x, y, lw + 20, rowH, 5);
        CreateCtrl(hwnd, L"EDIT", L"100", x + lw + 20, y, 50, rowH, ID_HM_SIZEX, WS_BORDER | ES_NUMBER);
        CreateCtrl(hwnd, L"EDIT", L"50",  x + lw + 76, y, 50, rowH, ID_HM_SIZEY, WS_BORDER | ES_NUMBER);
        CreateCtrl(hwnd, L"EDIT", L"100", x + lw + 132, y, 50, rowH, ID_HM_SIZEZ, WS_BORDER | ES_NUMBER);
        y += rowH + gap;

        CreateLabel(hwnd, L"Scale:", x, y, 50, rowH, 6);
        CreateCtrl(hwnd, L"EDIT", L"1.0", x + 55, y, 60, rowH, ID_HM_SCALE, WS_BORDER);
        y += rowH + gap + 6;

        CreateButton(hwnd, L"Generate", x, y, 100, 30, ID_HM_GENERATE);
        CreateButton(hwnd, L"Close", x + 110, y, 100, 30, ID_HM_CLOSE);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_HM_CLOSE) {
            ShowHeightmapEditor(false);
        } else if (id == ID_HM_BROWSE_IMG || id == ID_HM_BROWSE_TEX) {
            wchar_t path[512] = L"";
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFile = path;
            ofn.nMaxFile = 512;
            ofn.lpstrFilter = L"PNG Files\0*.png\0All Files\0*.*\0";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn)) {
                HWND hEdit = GetDlgItem(hwnd, (id == ID_HM_BROWSE_IMG) ? ID_HM_IMAGE : ID_HM_TEX);
                if (hEdit) SetWindowTextW(hEdit, path);
            }
        } else if (id == ID_HM_GENERATE) {
            // Read current values from edit controls
            wchar_t buf[256];
            HWND hImg = GetDlgItem(hwnd, ID_HM_IMAGE);
            HWND hTex = GetDlgItem(hwnd, ID_HM_TEX);
            HWND hPX  = GetDlgItem(hwnd, ID_HM_POSX);
            HWND hPY  = GetDlgItem(hwnd, ID_HM_POSY);
            HWND hPZ  = GetDlgItem(hwnd, ID_HM_POSZ);
            HWND hSX  = GetDlgItem(hwnd, ID_HM_SIZEX);
            HWND hSY  = GetDlgItem(hwnd, ID_HM_SIZEY);
            HWND hSZ  = GetDlgItem(hwnd, ID_HM_SIZEZ);
            HWND hSC  = GetDlgItem(hwnd, ID_HM_SCALE);
            if (hImg) GetWindowTextW(hImg, g_imgPath, 512);
            if (hTex) GetWindowTextW(hTex, g_texPath, 512);
            double px=0,py=0,pz=0,sx=100,sy=50,sz=100,sc=1.0;
            if (hPX) { GetWindowTextW(hPX, buf, 256); px = wcstod(buf, nullptr); }
            if (hPY) { GetWindowTextW(hPY, buf, 256); py = wcstod(buf, nullptr); }
            if (hPZ) { GetWindowTextW(hPZ, buf, 256); pz = wcstod(buf, nullptr); }
            if (hSX) { GetWindowTextW(hSX, buf, 256); sx = wcstod(buf, nullptr); }
            if (hSY) { GetWindowTextW(hSY, buf, 256); sy = wcstod(buf, nullptr); }
            if (hSZ) { GetWindowTextW(hSZ, buf, 256); sz = wcstod(buf, nullptr); }
            if (hSC) { GetWindowTextW(hSC, buf, 256); sc = wcstod(buf, nullptr); }
            // Store to a shared state that main loop can read
            FILE* log = fopen("System/AngelEd.log", "a");
            if (log) {
                fprintf(log, "[HmEditor] img=%ls tex=%ls pos=(%.1f,%.1f,%.1f) size=(%.1f,%.1f,%.1f) scale=%.2f\n",
                        g_imgPath, g_texPath, px, py, pz, sx, sy, sz, sc);
                fclose(log);
            }
        }
        break;
    }
    case WM_CLOSE:
        ShowHeightmapEditor(false);
        break;
    case WM_DESTROY:
        g_editorPanels.hHeightmapEditor = nullptr;
        break;
    default:
        return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

// =====================================================================
// CSG Sidebar — left-side brush tool window (like UnrealEd)
// =====================================================================
static const int ID_CSG_BOX    = 301;
static const int ID_CSG_CYL    = 302;
static const int ID_CSG_SPH    = 303;
static const int ID_CSG_PYR    = 304;
static const int ID_CSG_PLN    = 305;
static const int ID_CSG_ADD    = 310;
static const int ID_CSG_SUB    = 311;
static const int ID_CSG_INTERSECT = 312;
static const int ID_CSG_DERESC = 313;
static const int ID_CSG_PLACE  = 320;
static const int ID_CSG_COLLISION = 321;
static const int ID_CSG_POSX   = 330;
static const int ID_CSG_POSY   = 331;
static const int ID_CSG_POSZ   = 332;
static const int ID_CSG_WIDTH  = 333;
static const int ID_CSG_HEIGHT = 334;
static const int ID_CSG_DEPTH  = 335;
static const int ID_CSG_ROT    = 336;
static const int ID_CSG_SCALE  = 337;
static const int ID_CSG_CLOSE  = 399;

void ShowCsgSidebar(bool show) {
    g_editorPanels.showCsgSidebar = show;
    if (g_editorPanels.hCsgSidebar)
        ShowWindow((HWND)g_editorPanels.hCsgSidebar, show ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK CsgSidebarProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        int x = 8, y = 8, bw = 180, rowH = 24, gap = 4;

        CreateLabel(hwnd, L"CSG Brushes", x, y, bw, 20, 1); y += 26;

        // Primitive type buttons
        CreateButton(hwnd, L"Box",     x, y, 80, rowH, ID_CSG_BOX);
        CreateButton(hwnd, L"Cylinder", x + 84, y, 88, rowH, ID_CSG_CYL);
        y += rowH + gap;
        CreateButton(hwnd, L"Sphere",  x, y, 80, rowH, ID_CSG_SPH);
        CreateButton(hwnd, L"Pyramid", x + 84, y, 88, rowH, ID_CSG_PYR);
        y += rowH + gap;
        CreateButton(hwnd, L"Plane",   x, y, 80, rowH, ID_CSG_PLN);
        y += rowH + 8;

        // CSG operation buttons
        CreateLabel(hwnd, L"Operation:", x, y, bw, 18, 2); y += 20;
        CreateButton(hwnd, L"Add",        x, y, 80, rowH, ID_CSG_ADD);
        CreateButton(hwnd, L"Sub",        x + 84, y, 88, rowH, ID_CSG_SUB);
        y += rowH + gap;
        CreateButton(hwnd, L"Intersect",  x, y, 80, rowH, ID_CSG_INTERSECT);
        CreateButton(hwnd, L"De-Resc",    x + 84, y, 88, rowH, ID_CSG_DERESC);
        y += rowH + 8;

        // Dimensions
        CreateLabel(hwnd, L"Position:", x, y, 60, rowH, 3);
        CreateCtrl(hwnd, L"EDIT", L"0", x + 60, y, 35, 20, ID_CSG_POSX, WS_BORDER);
        CreateCtrl(hwnd, L"EDIT", L"0", x + 100, y, 35, 20, ID_CSG_POSY, WS_BORDER);
        CreateCtrl(hwnd, L"EDIT", L"0", x + 140, y, 35, 20, ID_CSG_POSZ, WS_BORDER);
        y += 22;
        CreateLabel(hwnd, L"Size:", x, y, 40, rowH, 4);
        CreateCtrl(hwnd, L"EDIT", L"4", x + 40, y, 45, 20, ID_CSG_WIDTH, WS_BORDER);
        CreateCtrl(hwnd, L"EDIT", L"4", x + 90, y, 45, 20, ID_CSG_HEIGHT, WS_BORDER);
        CreateCtrl(hwnd, L"EDIT", L"4", x + 140, y, 35, 20, ID_CSG_DEPTH, WS_BORDER);
        y += 22;
        CreateLabel(hwnd, L"Rot:", x, y, 30, rowH, 5);
        CreateCtrl(hwnd, L"EDIT", L"0", x + 30, y, 50, 20, ID_CSG_ROT, WS_BORDER);
        CreateLabel(hwnd, L"Scale:", x + 85, y, 40, rowH, 6);
        CreateCtrl(hwnd, L"EDIT", L"1", x + 125, y, 50, 20, ID_CSG_SCALE, WS_BORDER);
        y += 28;

        CreateButton(hwnd, L"Place Brush", x, y, bw, 30, ID_CSG_PLACE);
        y += 36;
        CreateCtrl(hwnd, L"BUTTON", L"Enable Collision", x, y, bw, rowH, ID_CSG_COLLISION, BS_AUTOCHECKBOX);

        CreateButton(hwnd, L"Close", x, y + 40, bw, 28, ID_CSG_CLOSE);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_CSG_CLOSE) { ShowCsgSidebar(false); break; }

        int primType = -1;
        if (id == ID_CSG_BOX) primType = 0;
        else if (id == ID_CSG_CYL) primType = 1;
        else if (id == ID_CSG_SPH) primType = 2;
        else if (id == ID_CSG_PYR) primType = 3;
        else if (id == ID_CSG_PLN) primType = 4;

        if (primType >= 0) {
            g_editorPanels.actionCsgPlace = primType;
            break;
        }

        int csgOp = -1;
        if (id == ID_CSG_ADD) csgOp = 1;
        else if (id == ID_CSG_SUB) csgOp = 2;
        else if (id == ID_CSG_INTERSECT) csgOp = 3;
        else if (id == ID_CSG_DERESC) csgOp = 4;

        if (csgOp >= 0) {
            // Store CSG operation for next place
            // (read by main loop via actionCsgPlace processing)
            extern int g_csgOpFromSidebar;
            g_csgOpFromSidebar = csgOp;
            break;
        }

        if (id == ID_CSG_PLACE) {
            // Read position/dimension values from edit controls
            wchar_t buf[64];
            float px = 0, py = 0, pz = 0;
            float w = 4, h = 4, d = 4;
            float rot = 0, scale = 1.0f;
            HWND hPX = GetDlgItem(hwnd, ID_CSG_POSX);
            HWND hPY = GetDlgItem(hwnd, ID_CSG_POSY);
            HWND hPZ = GetDlgItem(hwnd, ID_CSG_POSZ);
            HWND hW  = GetDlgItem(hwnd, ID_CSG_WIDTH);
            HWND hH  = GetDlgItem(hwnd, ID_CSG_HEIGHT);
            HWND hD  = GetDlgItem(hwnd, ID_CSG_DEPTH);
            HWND hR  = GetDlgItem(hwnd, ID_CSG_ROT);
            HWND hSC = GetDlgItem(hwnd, ID_CSG_SCALE);
            if (hPX) { GetWindowTextW(hPX, buf, 64); px = (float)wcstod(buf, nullptr); }
            if (hPY) { GetWindowTextW(hPY, buf, 64); py = (float)wcstod(buf, nullptr); }
            if (hPZ) { GetWindowTextW(hPZ, buf, 64); pz = (float)wcstod(buf, nullptr); }
            if (hW)  { GetWindowTextW(hW, buf, 64);  w  = (float)wcstod(buf, nullptr); }
            if (hH)  { GetWindowTextW(hH, buf, 64);  h  = (float)wcstod(buf, nullptr); }
            if (hD)  { GetWindowTextW(hD, buf, 64);  d  = (float)wcstod(buf, nullptr); }
            if (hR)  { GetWindowTextW(hR, buf, 64);  rot = (float)wcstod(buf, nullptr); }
            if (hSC) { GetWindowTextW(hSC, buf, 64); scale = (float)wcstod(buf, nullptr); }
            // Store placement params via editor state
            extern void CsgSidebarPlace(float x, float y, float z, float w, float h, float d, float rot, float scale);
            CsgSidebarPlace(px, py, pz, w, h, d, rot, scale);
            // Trigger placement
            if (g_editorPanels.actionCsgPlace < 0)
                g_editorPanels.actionCsgPlace = 0; // default: box
        }

        if (id == ID_CSG_COLLISION) {
            extern bool g_csgCollisionFromSidebar;
            g_csgCollisionFromSidebar = IsDlgButtonChecked(hwnd, ID_CSG_COLLISION) == BST_CHECKED;
        }
        break;
    }
    case WM_CLOSE:
        ShowCsgSidebar(false);
        break;
    case WM_DESTROY:
        g_editorPanels.hCsgSidebar = nullptr;
        break;
    default:
        return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

// =====================================================================
// Public API — Create / Destroy
// =====================================================================
void CreateAllEditorWindows(void* hInst, void* hRaylibWnd) {
    g_hInst = (HINSTANCE)hInst;
    g_hRaylibWnd = (HWND)hRaylibWnd;

    RegisterPanelClass(CLASS_SOUNDMGR, SoundMgrProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_TEXTUREMGR, TextureMgrProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_PAWNNMGR, PawnMgrProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_SCRIPTMGR, ScriptMgrProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_MODELBRW, ModelBrwProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_ENVPANEL, EnvPanelProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_PICKUPPANEL, PickupPanelProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_NODEPANEL, NodePanelProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_HMEDITOR, HmEditorProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_CSGSIDEBAR, CsgSidebarProc, (HINSTANCE)hInst);

    auto create = [&](const wchar_t* cls, const wchar_t* title,
                      EditorPanelState::WinPos& pos, void*& out) {
        HWND hwnd = CreateWindowEx(WS_EX_TOOLWINDOW,
              cls, title,
              WS_OVERLAPPEDWINDOW,
              pos.x, pos.y, pos.w, pos.h,
              nullptr, nullptr, (HINSTANCE)hInst, nullptr);
        out = hwnd;
        if (hwnd) ShowWindow(hwnd, SW_HIDE);
    };

    create(CLASS_SOUNDMGR,    L"Sound Manager",       g_editorPanels.soundMgrPos,   g_editorPanels.hSoundMgr);
    create(CLASS_TEXTUREMGR,  L"Texture Manager",     g_editorPanels.textureMgrPos, g_editorPanels.hTextureMgr);
    create(CLASS_PAWNNMGR,    L"Pawn Manager",        g_editorPanels.pawnMgrPos,    g_editorPanels.hPawnMgr);
    create(CLASS_SCRIPTMGR,   L"Script Manager",      g_editorPanels.scriptMgrPos,  g_editorPanels.hScriptMgr);
    create(CLASS_MODELBRW,    L"Model / Mesh Browser",g_editorPanels.modelBrwPos,   g_editorPanels.hModelBrowser);
    create(CLASS_ENVPANEL,    L"Environment Settings",g_editorPanels.envPanelPos,   g_editorPanels.hEnvPanel);
    create(CLASS_PICKUPPANEL, L"Pickups",             g_editorPanels.pickPanelPos,  g_editorPanels.hPickupPanel);
    create(CLASS_NODEPANEL,   L"Nodes",               g_editorPanels.nodePanelPos,  g_editorPanels.hNodePanel);
    create(CLASS_HMEDITOR,    L"Heightmap Editor",     g_editorPanels.heightmapEditorPos, g_editorPanels.hHeightmapEditor);
    create(CLASS_CSGSIDEBAR,  L"CSG Brushes",          g_editorPanels.csgSidebarPos,      g_editorPanels.hCsgSidebar);

    ScanTextureBrowserFiles();
    ScanSoundBrowserFiles();
    ScanModelBrowserFiles();
    ScanScriptFiles();
}

void DestroyAllEditorWindows() {
    auto destroy = [](void*& hwnd) {
        if (hwnd) { DestroyWindow((HWND)hwnd); hwnd = nullptr; }
    };
    destroy(g_editorPanels.hSoundMgr);
    destroy(g_editorPanels.hTextureMgr);
    destroy(g_editorPanels.hPawnMgr);
    destroy(g_editorPanels.hScriptMgr);
    destroy(g_editorPanels.hModelBrowser);
    destroy(g_editorPanels.hEnvPanel);
    destroy(g_editorPanels.hPickupPanel);
    destroy(g_editorPanels.hNodePanel);
    destroy(g_editorPanels.hHeightmapEditor);
    destroy(g_editorPanels.hCsgSidebar);
    if (g_editorPanels.hPreviewBitmap) {
        DeleteObject((HGDIOBJ)g_editorPanels.hPreviewBitmap);
        g_editorPanels.hPreviewBitmap = nullptr;
    }
}
