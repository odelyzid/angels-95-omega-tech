#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include "../../Source/WindowsCompat.hpp"
#include "../../Source/Package/PackageAssetLoader.hpp"
#include "../../Source/Pawn/OzPawnSystem.hpp"
#include "../../Source/Script/LightningEntityRegistry.hpp"
#include "../../Source/World/OzOzoneLoader.hpp"
#include "Win32Dialogs.hpp"
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commdlg.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

// =====================================================================
// Globals
// =====================================================================
EditorPanelState g_editorPanels;

// Extern accessors from Main.cpp for WorldGraph model data
extern int WorldGraph_GetModelCount();
extern void WorldGraph_GetModelData(int index, float& outX, float& outY, float& outZ, float& outR, float& outS);
extern const char* WorldGraph_GetModelName(int index);
// Selection accessors
extern int Editor_GetSelectedType();
extern int Editor_GetSelectedIndex();
// Editor state accessors for toolbox sidebar
extern int Editor_GetCsgOperation();
extern void Editor_SetCsgOperation(int op);
extern int Editor_GetPlaceMode();
extern void Editor_SetPlaceMode(int mode);
// Current world directory (absolute) — used by the Texture Manager import feature
extern std::string Editor_GetCurrentWorldDir();
extern std::string Editor_GetCurrentWorldName();

static HINSTANCE g_hInst = nullptr;
static HWND g_hRaylibWnd = nullptr;
static HWND g_hMainWnd = nullptr; // alias for g_hRaylibWnd in message handling

static const wchar_t* CLASS_SOUNDMGR    = L"OzSoundMgr";
static const wchar_t* CLASS_TEXTUREMGR  = L"OzTextureMgr";
static const wchar_t* CLASS_TEXTURE_GRID = L"OzTextureGrid";
static const wchar_t* CLASS_PAWNNMGR    = L"OzPawnMgr";
static const wchar_t* CLASS_SCRIPTMGR   = L"OzScriptMgr";
static const wchar_t* CLASS_MODELBRW    = L"OzModelBrw";
static const wchar_t* CLASS_ENVPANEL    = L"OzZoneProperties";
static const wchar_t* CLASS_PICKUPPANEL = L"OzPickupPanel";
static const wchar_t* CLASS_NODEPANEL   = L"OzNodePanel";
static const wchar_t* CLASS_HMEDITOR   = L"OzHmEditor";

static const wchar_t* CLASS_LIGHTPROPS = L"OzLightProps";
static const wchar_t* CLASS_WORLDGRAPH = L"OzWorldGraph";
static const wchar_t* CLASS_PROPSPANEL = L"OzPropsPanel";
static const wchar_t* CLASS_STATSSIDEBAR = L"OzStatsSidebar";
static const wchar_t* CLASS_LEVELLIST = L"OzLevelList";
static const wchar_t* CLASS_ANIMPANEL  = L"OzAnimPanel";
static const int STATS_SIDEBAR_W = 200;

// Zone properties (read by editor rendering loop)
// g_zoneProps is defined in the ZoneProperties section below

// Entry structures for dynamic resource browsers
struct ResourceEntry {
    std::string name;
    std::string path;
    HBITMAP thumbnail = nullptr; // cached 64x64 preview
};

static std::vector<ResourceEntry> g_textureFiles;
static std::vector<ResourceEntry> g_soundFiles;

// Texture target model names (set from Main.cpp after model loading)
static std::vector<std::string> g_textureTargetNames;

// Pawn defs managed by PawnSystem (shared with runtime)

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
    } catch (const std::exception& e) {
            fprintf(stderr, "WARN: Exception during file scan: %s\n", e.what());
        } catch (...) {
            fprintf(stderr, "WARN: Unknown exception during file scan\n");
        }

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

    // Deduplicate by display name, preferring real files over package entries.
    auto isPkgRes = [](const ResourceEntry& e) { return !IsPathFile(e.path.c_str()); };
    std::stable_sort(out.begin(), out.end(),
        [&](const ResourceEntry& a, const ResourceEntry& b) {
            if (a.name != b.name) return a.name < b.name;
            return (isPkgRes(a) ? 1 : 0) < (isPkgRes(b) ? 1 : 0);
        });
    auto last = std::unique(out.begin(), out.end(),
        [](const ResourceEntry& a, const ResourceEntry& b) { return a.name == b.name; });
    out.erase(last, out.end());
}

// =====================================================================
// Forward declarations
// =====================================================================
static LRESULT CALLBACK SoundMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK TextureMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK TextureGridProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK PawnMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK ScriptMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK ModelBrwProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK ZonePropertiesProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK PickupPanelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK NodePanelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK HmEditorProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK LightPropsProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK WorldGraphProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK LevelListProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK PropsPanelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK StatsSidebarProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);

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

// --- Icon buttons (owner-draw) for the native stats sidebar -----------------
static std::unordered_map<std::string, HBITMAP> g_uiBitmaps;
static std::unordered_map<int, HBITMAP> g_uiBtnIcon;

// Load a UI .bmp as an HBITMAP (cached). Searches AngelEd/UI then the packed
// GameData/Global/Engine/UI library ("effect-effect-" names).
static HBITMAP LoadUiBitmap(const std::string& name) {
    auto it = g_uiBitmaps.find(name);
    if (it != g_uiBitmaps.end()) return it->second;
    const char* prefixes[] = {
        "AngelEd/UI/",
        "../AngelEd/UI/",
        "GameData/Global/Engine/UI/effect-effect-",
        "../GameData/Global/Engine/UI/effect-effect-",
    };
    HBITMAP bm = nullptr;
    for (auto* p : prefixes) {
        std::string full = std::string(p) + name + ".bmp";
        std::wstring w(full.begin(), full.end());
        bm = (HBITMAP)LoadImageW(nullptr, w.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
        if (bm) break;
    }
    g_uiBitmaps[name] = bm;
    return bm;
}

static HWND CreateIconButton(HWND hParent, const wchar_t* text, int x, int y,
                             int w, int h, int id, const std::string& icon) {
    HWND b = CreateWindowEx(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                            x, y, w, h, hParent, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    g_uiBtnIcon[id] = LoadUiBitmap(icon);
    return b;
}

// Draw an owner-draw icon button (bitmap on the left, label after it).
static LRESULT DrawIconButton(LPDRAWITEMSTRUCT dis) {
    HBRUSH bg = CreateSolidBrush((dis->itemState & ODS_SELECTED) ? RGB(70, 90, 120) : RGB(45, 45, 50));
    FillRect(dis->hDC, &dis->rcItem, bg);
    DeleteObject(bg);
    FrameRect(dis->hDC, &dis->rcItem, (HBRUSH)GetStockObject(GRAY_BRUSH));

    int x = dis->rcItem.left + 4;
    int cy = (dis->rcItem.top + dis->rcItem.bottom) / 2;
    auto it = g_uiBtnIcon.find((int)dis->CtlID);
    if (it != g_uiBtnIcon.end() && it->second) {
        BITMAP b;
        GetObject(it->second, sizeof(b), &b);
        int ih = (dis->rcItem.bottom - dis->rcItem.top) - 6;
        if (ih > 20) ih = 20;
        if (ih < 8) ih = 8;
        int iw = (b.bmHeight > 0) ? (int)((float)b.bmWidth * ih / b.bmHeight) : ih;
        if (iw > ih) iw = ih;
        HDC mem = CreateCompatibleDC(dis->hDC);
        HGDIOBJ old = SelectObject(mem, it->second);
        SetStretchBltMode(dis->hDC, COLORONCOLOR);
        StretchBlt(dis->hDC, x, cy - ih / 2, iw, ih, mem, 0, 0, b.bmWidth, b.bmHeight, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
        x += iw + 5;
    }
    wchar_t txt[64] = {0};
    GetWindowTextW(dis->hwndItem, txt, 64);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, RGB(215, 225, 240));
    RECT tr = dis->rcItem;
    tr.left = x;
    tr.right -= 3;
    DrawTextW(dis->hDC, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    return TRUE;
}

// =====================================================================
// Sound Manager v2 Ã¢â‚¬â€ Category tabs, volume, loop, source info
// =====================================================================
static const int ID_SOUND_LIST     = 101;
static const int ID_SOUND_REFRESH  = 102;
static const int ID_SOUND_CLOSE    = 103;
static const int ID_SOUND_PLAY     = 104;
static const int ID_SOUND_STOP     = 105;
static const int ID_SOUND_CAT_SFX  = 106;
static const int ID_SOUND_CAT_MUS  = 107;
static const int ID_SOUND_CAT_AMB  = 108;
static const int ID_SOUND_VOLUME   = 109;
static const int ID_SOUND_LOOP     = 110;
static const int ID_SOUND_SRC_LABEL= 111;

static int g_soundCategory = 0; // 0=SFX, 1=Music, 2=Ambience
static std::vector<ResourceEntry> g_sfxFiles;
static std::vector<ResourceEntry> g_musicFiles;
static std::vector<ResourceEntry> g_ambFiles;

void ShowSoundManager(bool show) {
    g_editorPanels.showSoundMgr = show;
    if (g_editorPanels.hSoundMgr)
        ShowWindow((HWND)g_editorPanels.hSoundMgr, show ? SW_SHOW : SW_HIDE);
}

void ScanSoundBrowserFiles() {
    ScanFilesAndPackages("Global/Sounds", { ".wav", ".ogg", ".mp3" }, g_sfxFiles);
    ScanFilesAndPackages("Global/Sounds/Ambience", { ".wav", ".ogg", ".mp3" }, g_ambFiles);
    ScanFilesAndPackages("", { ".wav", ".ogg", ".mp3" }, g_musicFiles);
    if (g_editorPanels.hSoundMgr)
        SendMessage((HWND)g_editorPanels.hSoundMgr, WM_USER + 50, 0, 0);
}

static void SoundMgrPopulateList(HWND hList) {
    SendMessage(hList, LB_RESETCONTENT, 0, 0);
    const auto& files = (g_soundCategory == 0) ? g_sfxFiles :
                        (g_soundCategory == 1) ? g_musicFiles : g_ambFiles;
    for (const auto& snd : files)
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)snd.name.c_str());
}

static LRESULT CALLBACK SoundMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList, hLoopBtn, hVolTrack, hSrcLabel;
    switch (msg) {
    case WM_CREATE: {
        int x = 10, y = 10, bw = 80;

        // Category tabs
        CreateButton(hwnd, L"SFX",      x, y, bw, 24, ID_SOUND_CAT_SFX);
        CreateButton(hwnd, L"Music",    x + bw + 4, y, bw, 24, ID_SOUND_CAT_MUS);
        CreateButton(hwnd, L"Ambience", x + (bw + 4) * 2, y, bw + 10, 24, ID_SOUND_CAT_AMB);
        y += 30;

        // Source label
        hSrcLabel = CreateLabel(hwnd, L"Source: scanning...", x, y, 300, 16, ID_SOUND_SRC_LABEL);
        y += 20;

        // Sound list
        hList = CreateListBox(hwnd, x, y, 380, 160, ID_SOUND_LIST);
        y += 166;

        // Volume slider
        CreateLabel(hwnd, L"Volume:", x, y, 50, 20, 20);
        hVolTrack = CreateWindowEx(0, TRACKBAR_CLASS, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ,
                                   x + 55, y, 180, 24, hwnd, (HMENU)ID_SOUND_VOLUME, g_hInst, nullptr);
        SendMessage(hVolTrack, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessage(hVolTrack, TBM_SETPOS, TRUE, 80);
        y += 30;

        // Loop checkbox
        hLoopBtn = CreateCtrl(hwnd, L"BUTTON", L"Loop", x, y, 100, 22, ID_SOUND_LOOP, BS_AUTOCHECKBOX);
        y += 28;

        // Action buttons
        CreateButton(hwnd, L"Play",    x, y, 70, 26, ID_SOUND_PLAY);
        CreateButton(hwnd, L"Stop",    x + 76, y, 70, 26, ID_SOUND_STOP);
        CreateButton(hwnd, L"Refresh", x + 152, y, 70, 26, ID_SOUND_REFRESH);
        CreateButton(hwnd, L"Close",   x + 300, y, 90, 26, ID_SOUND_CLOSE);

        ScanSoundBrowserFiles();
        break;
    }
    case WM_USER + 50: {
        SoundMgrPopulateList(hList);
        // Update source label
        const char* cats[] = {"Global/Sounds/ (SFX)", "Global/ (Music)", "Global/Sounds/Ambience/"};
        SetWindowTextA(hSrcLabel, TextFormat("Source: GameData/%s", cats[g_soundCategory]));
        break;
    }
    case WM_HSCROLL: {
        if ((HWND)l == hVolTrack) {
            int vol = (int)SendMessage(hVolTrack, TBM_GETPOS, 0, 0);
            g_editorPanels.actionSoundVolume = vol;
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
            const auto& files = (g_soundCategory == 0) ? g_sfxFiles :
                                (g_soundCategory == 1) ? g_musicFiles : g_ambFiles;
            if (sel >= 0 && sel < (int)files.size()) {
                g_editorPanels.actionPreviewSoundPath = files[sel].path;
                g_editorPanels.actionSoundCategory = g_soundCategory;
                g_editorPanels.actionSoundLoop = (int)SendMessage(hLoopBtn, BM_GETCHECK, 0, 0);
            }
        } else if (id == ID_SOUND_CAT_SFX || id == ID_SOUND_CAT_MUS || id == ID_SOUND_CAT_AMB) {
            g_soundCategory = (id == ID_SOUND_CAT_SFX) ? 0 : (id == ID_SOUND_CAT_MUS) ? 1 : 2;
            SoundMgrPopulateList(hList);
            SetWindowTextA(hSrcLabel, TextFormat("Source: %s",
                (g_soundCategory == 0) ? "GameData/Global/Sounds/" :
                (g_soundCategory == 1) ? "GameData/ (Music)" : "GameData/Global/Sounds/Ambience/"));
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
// Texture Manager v2 Ã¢â‚¬â€ oztex integration, preview, source info
// =====================================================================
static const int ID_TEX_LIST       = 101;
static const int ID_TEX_REFRESH    = 102;
static const int ID_TEX_CLOSE      = 103;
static const int ID_TEX_TARGET     = 104;
static const int ID_TEX_APPLY      = 105;
static const int ID_TEX_PREVIEW    = 106;
static const int ID_TEX_DIMS_LABEL = 107;
static const int ID_TEX_SRC_LABEL  = 108;
static const int ID_TEX_APPLY_ALL  = 109;
static const int ID_TEX_ADDPKG     = 110;
static const int ID_TEX_IMPORT     = 111;

// Texture grid view constants
static const int TEX_THUMB_SIZE = 64;
static const int TEX_CELL_W     = TEX_THUMB_SIZE + 12;
static const int TEX_CELL_H     = TEX_THUMB_SIZE + 22;
static const int TEX_GRID_GAP   = 4;

void ShowTextureManager(bool show) {
    g_editorPanels.showTextureMgr = show;
    if (g_editorPanels.hTextureMgr)
        ShowWindow((HWND)g_editorPanels.hTextureMgr, show ? SW_SHOW : SW_HIDE);
}

void ScanTextureBrowserFiles() {
    g_textureFiles.clear();
    // .dds included so the Global/sky skybox library is selectable
    const std::vector<std::string> exts = { ".png", ".tga", ".bmp", ".jpg", ".jpeg", ".dds" };

    // Scan filesystem under GameData/
    fs::path base = fs::current_path() / "GameData";
    try {
        if (fs::exists(base)) {
            for (auto& entry : fs::recursive_directory_iterator(base)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    for (const auto& e : exts) {
                        if (ext == e) {
                            g_textureFiles.push_back({ entry.path().stem().string(), entry.path().string(), nullptr });
                            break;
                        }
                    }
                }
            }
        }
    } catch (...) {}

    // Scan packages
    std::vector<std::string> pkgFiles;
    PackageAssetLoader::Instance().ListAllFiles(pkgFiles);
    for (const auto& pkgPath : pkgFiles) {
        std::string ext = pkgPath.substr(pkgPath.rfind('.'));
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        for (const auto& e : exts) {
            if (ext == e) {
                std::string name = pkgPath;
                size_t slash = name.rfind('/');
                if (slash != std::string::npos) name = name.substr(slash + 1);
                size_t dot = name.rfind('.');
                if (dot != std::string::npos) name = name.substr(0, dot);
                g_textureFiles.push_back({ name, pkgPath, nullptr });
                break;
            }
        }
    }

    // Deduplicate by display name, preferring real files over package entries
    // (a package copy like "Models/Skybox.png" would otherwise shadow the world's
    // actual file and be unusable as an editable source).
    auto isPkg = [](const ResourceEntry& e) { return !IsPathFile(e.path.c_str()); };
    std::stable_sort(g_textureFiles.begin(), g_textureFiles.end(),
        [&](const ResourceEntry& a, const ResourceEntry& b) {
            if (a.name != b.name) return a.name < b.name;
            return (isPkg(a) ? 1 : 0) < (isPkg(b) ? 1 : 0);
        });
    auto last = std::unique(g_textureFiles.begin(), g_textureFiles.end(),
        [](const ResourceEntry& a, const ResourceEntry& b) { return a.name == b.name; });
    g_textureFiles.erase(last, g_textureFiles.end());

    if (g_editorPanels.hTextureMgr)
        SendMessage((HWND)g_editorPanels.hTextureMgr, WM_USER + 50, 0, 0);
}

void UpdateTextureManagerList() {
    ScanTextureBrowserFiles();
}

void SetTextureTargetNames(const std::vector<std::string>& names) {
    g_textureTargetNames = names;
    if (g_editorPanels.hTextureMgr)
        SendMessage((HWND)g_editorPanels.hTextureMgr, WM_USER + 51, 0, 0);
}

// ---------------------------------------------------------------------
// Import Textures — copy image file(s) into the open world's tileset so
// they become selectable in the browser and usable as texSlot indices.
// ---------------------------------------------------------------------
static void ImportTexturesIntoWorld(HWND hwnd) {
    std::vector<wchar_t> buf(32768, 0);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.dds)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.dds\0All Files (*.*)\0*.*\0";
    ofn.lpstrTitle = L"Import Textures";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;

    // Multi-select buffer layout: "dir\0file1\0file2\0\0", or a single
    // selection which is a direct "fullpath\0".
    std::vector<std::wstring> files;
    std::wstring first(buf.data());
    if (first.empty()) return;
    const wchar_t* p = buf.data() + first.size() + 1;
    if (*p == L'\0') {
        files.push_back(first);
    } else {
        fs::path dir(first);
        while (*p) {
            std::wstring name(p);
            files.push_back((dir / name).wstring());
            p += name.size() + 1;
        }
    }
    if (files.empty()) return;

    // Destination: <world>/oztex/tileset (1-based, sorted → new tile slots).
    // With no world open, fall back to GameData/Textures.
    fs::path dest;
    std::string worldDir = Editor_GetCurrentWorldDir();
    if (!worldDir.empty())
        dest = fs::path(worldDir) / "oztex" / "tileset";
    else
        dest = fs::current_path() / "GameData" / "Textures";

    std::error_code ec;
    fs::create_directories(dest, ec);

    int copied = 0, failed = 0;
    for (const auto& f : files) {
        fs::path src(f);
        fs::path out = dest / src.filename();
        std::error_code cec;
        fs::copy_file(src, out, fs::copy_options::overwrite_existing, cec);
        if (cec) failed++; else copied++;
    }

    ScanTextureBrowserFiles();

    std::string msg = "Imported " + std::to_string(copied) + " texture(s) into:\n" +
                      dest.string();
    if (failed) msg += "\n" + std::to_string(failed) + " file(s) failed.";
    msg += "\n\nReopen the world (or Refresh) to use them as tileset slots.";
    MessageBoxA(hwnd, msg.c_str(), "Import Textures", MB_OK | MB_ICONINFORMATION);
}

// =====================================================================
// Texture Grid View — custom control drawing thumbnails in a responsive grid
// =====================================================================
struct TextureGridState {
    int selectedIdx = -1;
    int columns = 1;
    int totalHeight = 0;
};

static LRESULT CALLBACK TextureGridProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    auto* state = (TextureGridState*)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        state = new TextureGridState();
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)state);
        break;
    }
    case WM_NCDESTROY: {
        delete state;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        break;
    }
    case WM_SIZE: {
        if (!state) break;
        int cw = LOWORD(l);
        int ch = HIWORD(l);
        if (cw < 1) cw = 1;
        if (ch < 1) ch = 1;
        state->columns = (cw - 8) / (TEX_CELL_W + TEX_GRID_GAP);
        if (state->columns < 1) state->columns = 1;
        int rows = ((int)g_textureFiles.size() + state->columns - 1) / state->columns;
        state->totalHeight = rows * (TEX_CELL_H + TEX_GRID_GAP) + 8;
        SCROLLINFO si = { sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE, 0, state->totalHeight, ch, 0 };
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        InvalidateRect(hwnd, NULL, TRUE);
        break;
    }
    case WM_PAINT: {
        if (!state) break;
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int scrollPos = GetScrollPos(hwnd, SB_VERT);
        HDC hdcMem = CreateCompatibleDC(hdc);

        FillRect(hdc, &ps.rcPaint, GetSysColorBrush(COLOR_WINDOW));

        for (size_t i = 0; i < g_textureFiles.size(); i++) {
            int col = (int)(i % state->columns);
            int row = (int)(i / state->columns);
            int x = 4 + col * (TEX_CELL_W + TEX_GRID_GAP);
            int y = 4 + row * (TEX_CELL_H + TEX_GRID_GAP) - scrollPos;

            if (y + TEX_CELL_H < 0 || y > rc.bottom) continue;

            // Selection highlight
            if ((int)i == state->selectedIdx) {
                RECT sel = { x - 2, y - 2, x + TEX_CELL_W + 2, y + TEX_CELL_H + 2 };
                HBRUSH hBrush = CreateSolidBrush(RGB(60, 80, 120));
                FillRect(hdc, &sel, hBrush);
                DeleteObject(hBrush);
            }

            // Thumbnail
            if (g_textureFiles[i].thumbnail) {
                SelectObject(hdcMem, g_textureFiles[i].thumbnail);
                StretchBlt(hdc, x + (TEX_CELL_W - TEX_THUMB_SIZE) / 2, y + 2,
                           TEX_THUMB_SIZE, TEX_THUMB_SIZE, hdcMem, 0, 0, TEX_THUMB_SIZE, TEX_THUMB_SIZE, SRCCOPY);
            }

            // Name below thumbnail
            RECT tr = { x, y + TEX_THUMB_SIZE + 4, x + TEX_CELL_W, y + TEX_CELL_H };
            SetTextColor(hdc, RGB(200, 200, 200));
            SetBkMode(hdc, TRANSPARENT);
            DrawTextA(hdc, g_textureFiles[i].name.c_str(), -1, &tr, DT_CENTER | DT_SINGLELINE | DT_WORD_ELLIPSIS);
        }
        DeleteDC(hdcMem);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_LBUTTONDOWN: {
        if (!state) break;
        int mx = LOWORD(l), my = HIWORD(l);
        int scrollPos = GetScrollPos(hwnd, SB_VERT);
        int col = (mx - 4) / (TEX_CELL_W + TEX_GRID_GAP);
        int row = (my + scrollPos - 4) / (TEX_CELL_H + TEX_GRID_GAP);
        int idx = row * state->columns + col;
        if (idx >= 0 && idx < (int)g_textureFiles.size() && col < state->columns && mx >= 4) {
            state->selectedIdx = idx;
            InvalidateRect(hwnd, NULL, TRUE);
            PostMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(ID_TEX_LIST, 1), 0);
        }
        break;
    }
    case WM_LBUTTONDBLCLK: {
        if (!state) break;
        int mx = LOWORD(l), my = HIWORD(l);
        int scrollPos = GetScrollPos(hwnd, SB_VERT);
        int col = (mx - 4) / (TEX_CELL_W + TEX_GRID_GAP);
        int row = (my + scrollPos - 4) / (TEX_CELL_H + TEX_GRID_GAP);
        int idx = row * state->columns + col;
        if (idx >= 0 && idx < (int)g_textureFiles.size() && col < state->columns && mx >= 4) {
            state->selectedIdx = idx;
            InvalidateRect(hwnd, NULL, TRUE);
            PostMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(ID_TEX_LIST, 2), 0);
        }
        break;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(w);
        int scrollPos = GetScrollPos(hwnd, SB_VERT);
        scrollPos -= delta / 120 * (TEX_CELL_H + TEX_GRID_GAP);
        if (scrollPos < 0) scrollPos = 0;
        SCROLLINFO si = { sizeof(SCROLLINFO), SIF_RANGE | SIF_PAGE };
        GetScrollInfo(hwnd, SB_VERT, &si);
        int maxPos = si.nMax - (int)si.nPage;
        if (maxPos < 0) maxPos = 0;
        if (scrollPos > maxPos) scrollPos = maxPos;
        SetScrollPos(hwnd, SB_VERT, scrollPos, TRUE);
        InvalidateRect(hwnd, NULL, TRUE);
        break;
    }
    case WM_VSCROLL: {
        SCROLLINFO si = { sizeof(SCROLLINFO), SIF_ALL };
        GetScrollInfo(hwnd, SB_VERT, &si);
        int pos = si.nPos;
        switch (LOWORD(w)) {
        case SB_LINEUP:        pos -= TEX_CELL_H / 4; break;
        case SB_LINEDOWN:      pos += TEX_CELL_H / 4; break;
        case SB_PAGEUP:        pos -= (int)si.nPage; break;
        case SB_PAGEDOWN:      pos += (int)si.nPage; break;
        case SB_THUMBTRACK:    pos = si.nTrackPos; break;
        }
        if (pos < 0) pos = 0;
        int maxPos = si.nMax - (int)si.nPage;
        if (maxPos < 0) maxPos = 0;
        if (pos > maxPos) pos = maxPos;
        si.nPos = pos;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        InvalidateRect(hwnd, NULL, TRUE);
        break;
    }
    case WM_GETDLGCODE: {
        // Let the grid receive arrow keys if needed
        return DLGC_WANTARROWS;
    }
    default:
        return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

// =====================================================================
// Texture Manager v3 — Grid-based texture browser, dynamic resize, package-only
// =====================================================================
static LRESULT CALLBACK TextureMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hGrid = nullptr, hTarget = nullptr, hPreview = nullptr, hDims = nullptr, hSrc = nullptr;
    switch (msg) {
    case WM_CREATE: {
        int x = 10, y = 10, bw = 500;

        // Toolbar: Add Package + Import Textures + Refresh + Close
        CreateButton(hwnd, L"Add Package", x, y, 100, 24, ID_TEX_ADDPKG);
        CreateButton(hwnd, L"Import Textures", x + 106, y, 116, 24, ID_TEX_IMPORT);
        CreateButton(hwnd, L"Refresh", x + 228, y, 70, 24, ID_TEX_REFRESH);
        CreateButton(hwnd, L"Close", x + bw - 80, y, 80, 24, ID_TEX_CLOSE);
        y += 30;

        // Grid view (replaces old owner-drawn listbox)
        hGrid = CreateWindowEx(WS_EX_CLIENTEDGE, CLASS_TEXTURE_GRID, L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL,
            x, y, bw, 200, hwnd, (HMENU)(INT_PTR)ID_TEX_LIST, g_hInst, nullptr);
        y += 206;

        // Source + dimensions info
        hSrc = CreateLabel(hwnd, L"Source: packages", x, y, bw, 16, ID_TEX_SRC_LABEL);
        y += 18;
        hDims = CreateLabel(hwnd, L"Select a texture to preview", x, y, bw, 16, ID_TEX_DIMS_LABEL);
        y += 22;

        // Preview area (static bitmap control)
        hPreview = CreateWindowEx(WS_EX_STATICEDGE, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_BITMAP,
            x + 140, y, 220, 120, hwnd, (HMENU)(INT_PTR)ID_TEX_PREVIEW, g_hInst, nullptr);
        y += 126;

        // Target combo
        CreateLabel(hwnd, L"Target:", x, y, 55, 20, 20);
        hTarget = CreateWindowEx(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                 x + 55, y, 180, 200, hwnd, (HMENU)(INT_PTR)ID_TEX_TARGET, g_hInst, nullptr);
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
        y += 30;

        // Apply to all checkbox
        CreateCtrl(hwnd, L"BUTTON", L"Apply to All", x, y, 120, 22, ID_TEX_APPLY_ALL, BS_AUTOCHECKBOX);
        y += 28;

        CreateButton(hwnd, L"Apply", x, y, 80, 26, ID_TEX_APPLY);

        ScanTextureBrowserFiles();
        break;
    }
    case WM_SIZE: {
        int winW = LOWORD(l), winH = HIWORD(l);
        int x = 10;
        int bw = winW - 20;
        if (bw < 100) bw = 100;

        // Toolbar
        SetWindowPos(GetDlgItem(hwnd, ID_TEX_ADDPKG), NULL, x, 10, 100, 24, SWP_NOZORDER);
        SetWindowPos(GetDlgItem(hwnd, ID_TEX_IMPORT), NULL, x + 106, 10, 116, 24, SWP_NOZORDER);
        SetWindowPos(GetDlgItem(hwnd, ID_TEX_REFRESH), NULL, x + 228, 10, 70, 24, SWP_NOZORDER);
        SetWindowPos(GetDlgItem(hwnd, ID_TEX_CLOSE), NULL, x + bw - 80, 10, 80, 24, SWP_NOZORDER);

        // Grid — fill most of the window
        int gridBot = winH - 280;
        if (gridBot < 60) gridBot = 60;
        int gridH = gridBot - 40;
        if (gridH < 20) gridH = 20;
        if (hGrid) {
            SetWindowPos(hGrid, NULL, x, 40, bw, gridH, SWP_NOZORDER);
            SendMessage(hGrid, WM_SIZE, 0, MAKELPARAM(bw, gridH));
        }

        // Info labels
        int iy = gridBot + 6;
        if (hSrc) SetWindowPos(hSrc, NULL, x, iy, bw, 16, SWP_NOZORDER);
        iy += 18;
        if (hDims) SetWindowPos(hDims, NULL, x, iy, bw, 16, SWP_NOZORDER);
        iy += 22;

        // Preview centered
        int prevW = bw - 40;
        if (prevW > 220) prevW = 220;
        if (prevW < 80) prevW = 80;
        int prevH = 120;
        int prevX = x + (bw - prevW) / 2;
        if (hPreview) SetWindowPos(hPreview, NULL, prevX, iy, prevW, prevH, SWP_NOZORDER);
        iy += prevH + 6;

        // Target combo + buttons
        int comboW = bw - 65;
        if (comboW > 180) comboW = 180;
        if (comboW < 60) comboW = 60;
        SetWindowPos(GetDlgItem(hwnd, 20), NULL, x, iy, 55, 20, SWP_NOZORDER);
        if (hTarget) SetWindowPos(hTarget, NULL, x + 55, iy, comboW, 200, SWP_NOZORDER);
        iy += 26;
        SetWindowPos(GetDlgItem(hwnd, ID_TEX_APPLY_ALL), NULL, x, iy, 120, 22, SWP_NOZORDER);
        iy += 28;
        SetWindowPos(GetDlgItem(hwnd, ID_TEX_APPLY), NULL, x, iy, 80, 26, SWP_NOZORDER);
        break;
    }
    case WM_USER + 50: {
        // Free old thumbnails
        for (auto& tex : g_textureFiles) {
            if (tex.thumbnail) { DeleteObject(tex.thumbnail); tex.thumbnail = nullptr; }
        }
        // Reload all thumbnails
        for (auto& tex : g_textureFiles) {
            // Determine extension (dds needs the texture loader, not the image one)
            std::string ext = tex.path.substr(tex.path.rfind('.'));
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            Image img = {0};
            if (ext == ".dds") {
                // raylib loads DDS via LoadTexture; resolve package entries by
                // caching the bytes to a real file first
                std::string filePath = IsPathFile(tex.path.c_str())
                    ? tex.path
                    : PackageAssetLoader::Instance().CacheModelFile(tex.path.c_str());
                if (!filePath.empty() && IsPathFile(filePath.c_str())) {
                    Texture2D t = LoadTexture(filePath.c_str());
                    if (t.id > 0) {
                        img = LoadImageFromTexture(t);
                        UnloadTexture(t);
                    }
                }
            } else {
                img = LoadImageWithFallback(tex.path.c_str());
            }

            if (img.data) {
                ImageResize(&img, TEX_THUMB_SIZE, TEX_THUMB_SIZE);
                ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
                unsigned char* px = (unsigned char*)img.data;
                for (int i = 0; i < img.width * img.height; i++) {
                    unsigned char tmp = px[i*4];
                    px[i*4] = px[i*4+2];
                    px[i*4+2] = tmp;
                }
                HDC hdc = GetDC(hwnd);
                BITMAPINFO bmi = {};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = img.width;
                bmi.bmiHeader.biHeight = -img.height;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                void* bits = nullptr;
                tex.thumbnail = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
                if (bits && img.data) memcpy(bits, img.data, img.width * img.height * 4);
                ReleaseDC(hwnd, hdc);
                UnloadImage(img);
            }
        }
        // Refresh grid
        if (hGrid) {
            RECT rc;
            GetClientRect(hGrid, &rc);
            SendMessage(hGrid, WM_SIZE, 0, MAKELPARAM(rc.right - rc.left, rc.bottom - rc.top));
            InvalidateRect(hGrid, NULL, TRUE);
        }
        break;
    }
    case WM_USER + 51: {
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
        int notify = HIWORD(w);
        if (id == ID_TEX_CLOSE) {
            ShowTextureManager(false);
        } else if (id == ID_TEX_REFRESH) {
            ScanTextureBrowserFiles();
        } else if (id == ID_TEX_ADDPKG) {
            // File dialog to add a texture package
            wchar_t path[512] = L"";
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFile = path;
            ofn.nMaxFile = 512;
            ofn.lpstrFilter = L"Texture Packages (*.oztex;*.ozpak)\0*.oztex;*.ozpak\0Oz Packages (*.oz*)\0*.oz*\0";
            ofn.lpstrInitialDir = L"System\\Data";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                char utf8Path[512];
                WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8Path, 512, nullptr, nullptr);
                if (PackageAssetLoader::Instance().LoadPackageFile(utf8Path)) {
                    ScanTextureBrowserFiles();
                } else {
                    MessageBoxA(hwnd, "Failed to load package file.\nThe file may be corrupt or not a valid OzPackage.", "Error", MB_OK | MB_ICONERROR);
                }
            }
        } else if (id == ID_TEX_IMPORT) {
            ImportTexturesIntoWorld(hwnd);
        } else if (id == ID_TEX_LIST && notify == 1) {
            // Grid selection changed — update preview
            if (hGrid) {
                auto* gs = (TextureGridState*)GetWindowLongPtr(hGrid, GWLP_USERDATA);
                int sel = gs ? gs->selectedIdx : -1;
                if (sel >= 0 && sel < (int)g_textureFiles.size()) {
                    std::string& p = g_textureFiles[sel].path;
                    g_editorPanels.activeTexturePath = p;
                    SetWindowTextA(hSrc, TextFormat("Source: %s", p.c_str()));
                    Image tmp = LoadImageWithFallback(p.c_str());
                    if (tmp.data) {
                        SetWindowTextA(hDims, TextFormat("%dx%d package", tmp.width, tmp.height));
                        RECT pr; GetWindowRect(hPreview, &pr);
                        int pw = pr.right - pr.left - 4, ph = pr.bottom - pr.top - 4;
                        if (pw < 1) pw = 100;
                        if (ph < 1) ph = 60;
                        Image rs = ImageCopy(tmp);
                        ImageResize(&rs, pw, ph);
                        ImageFormat(&rs, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
                        unsigned char* px = (unsigned char*)rs.data;
                        for (int i = 0; i < rs.width * rs.height; i++) {
                            unsigned char t = px[i*4]; px[i*4] = px[i*4+2]; px[i*4+2] = t;
                        }
                        HDC hdc = GetDC(hwnd);
                        BITMAPINFO bmi = {}; bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bmi.bmiHeader.biWidth = rs.width; bmi.bmiHeader.biHeight = -rs.height;
                        bmi.bmiHeader.biPlanes = 1; bmi.bmiHeader.biBitCount = 32;
                        bmi.bmiHeader.biCompression = BI_RGB;
                        void* bits = nullptr;
                        HBITMAP hBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
                        if (bits && rs.data) memcpy(bits, rs.data, rs.width * rs.height * 4);
                        ReleaseDC(hwnd, hdc);
                        if (hBmp) {
                            HBITMAP oldBmp = (HBITMAP)SendMessage(hPreview, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmp);
                            if (oldBmp) DeleteObject(oldBmp);
                        }
                        UnloadImage(tmp);
                        UnloadImage(rs);
                    }
                }
            }
        } else if (id == ID_TEX_APPLY || (id == ID_TEX_LIST && notify == 2)) {
            // Apply selected texture to target model
            int sel = -1;
            if (hGrid) {
                auto* gs = (TextureGridState*)GetWindowLongPtr(hGrid, GWLP_USERDATA);
                sel = gs ? gs->selectedIdx : -1;
            }
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
    dialog.lpstrFilter = save ? L"OZONE World (*.ozone)\0*.ozone\0\0"
                              : L"OZONE World (*.ozone)\0*.ozone\0\0";
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
// Pawn Manager — Hierarchical Tree View
// =====================================================================
static const int ID_PAWN_CLOSE     = 100;
static const int ID_PAWN_SPAWN     = 103;
static const int ID_PAWN_REFRESH   = 104;
static const int ID_PAWN_TREE      = 105;

static HTREEITEM AddTreeItem(HWND hTree, HTREEITEM hParent, const wchar_t* text, LPARAM lParam) {
    TVINSERTSTRUCTW tvis = {};
    tvis.hParent = hParent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.itemex.mask = TVIF_TEXT | TVIF_PARAM;
    tvis.itemex.pszText = const_cast<wchar_t*>(text);
    tvis.itemex.lParam = lParam;
    return (HTREEITEM)SendMessage(hTree, TVM_INSERTITEMW, 0, (LPARAM)&tvis);
}

static void PopulateTreeView(HWND hTree) {
    SendMessage(hTree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
    PawnTreeNode root = BuildPawnTree();

    // Recursive helper
    struct Recursor {
        static void AddChildren(HWND hTree, HTREEITEM hParent, const PawnTreeNode& node) {
            for (const auto& child : node.children) {
                std::wstring wlabel(child.label.begin(), child.label.end());
                // lParam encodes type + defName: "type|defName" or just "leaf|defName" or "category|"
                std::string paramStr = child.typeTag + "|" + child.defName;
                LPARAM lParam = (LPARAM)_strdup(paramStr.c_str());
                HTREEITEM hItem = AddTreeItem(hTree, hParent, wlabel.c_str(), lParam);
                if (child.isExpanded)
                    SendMessage(hTree, TVM_EXPAND, TVE_EXPAND, (LPARAM)hItem);
                AddChildren(hTree, hItem, child);
            }
        }
    };
    Recursor::AddChildren(hTree, TVI_ROOT, root);
    // Expand root
    HTREEITEM hRoot = (HTREEITEM)SendMessage(hTree, TVM_GETNEXTITEM, TVGN_ROOT, 0);
    if (hRoot) SendMessage(hTree, TVM_EXPAND, TVE_EXPAND, (LPARAM)hRoot);
}

void ShowPawnManager(bool show) {
    g_editorPanels.showPawnMgr = show;
    if (g_editorPanels.hPawnMgr)
        ShowWindow((HWND)g_editorPanels.hPawnMgr, show ? SW_SHOW : SW_HIDE);
}

void RefreshPawnManager() {
    if (g_editorPanels.hPawnMgr)
        SendMessage((HWND)g_editorPanels.hPawnMgr, WM_USER + 50, 0, 0);
}

// Split a tree item's "typeTag|defName" lParam into its two parts WITHOUT
// mutating the stored buffer (mutating it broke every spawn after the first).
static void SplitTreeParam(const char* paramStr, std::string& typeTag, std::string& defName) {
    typeTag.clear();
    defName.clear();
    if (!paramStr) return;
    const char* pipe = strchr(paramStr, '|');
    if (!pipe) { typeTag = paramStr; return; }
    typeTag.assign(paramStr, pipe - paramStr);
    defName = pipe + 1;
}

void PawnManagerAddPawn(const char*, const char*) {
    // Legacy no-op — defs managed by PawnSystem
}

PawnTreeNode BuildPawnTree() {
    PawnTreeNode root;
    root.label = "Actor";
    root.isExpanded = true;

    // Pawn branch
    PawnTreeNode pawnBranch;
    pawnBranch.label = "Pawn";
    pawnBranch.isExpanded = true;
    pawnBranch.typeTag = "category";

    // PlayerPawn sub-branch
    PawnTreeNode playerBranch;
    playerBranch.label = "PlayerPawn";
    playerBranch.isExpanded = false;
    playerBranch.typeTag = "category";
    PawnTreeNode omegaPlayer;
    omegaPlayer.label = "AngelPlayer";
    omegaPlayer.defName = "AngelPlayer";  // Special: not in defs
    omegaPlayer.typeTag = "playerstart";
    playerBranch.children.push_back(omegaPlayer);
    pawnBranch.children.push_back(playerBranch);

    // EnemyPawn branch — populated from PawnSystem registered defs
    PawnTreeNode enemyBranch;
    enemyBranch.label = "EnemyPawn";
    enemyBranch.isExpanded = true;
    enemyBranch.typeTag = "category";
    const auto& defs = PawnSystem::Instance().GetDefs();
    for (const auto& def : defs) {
        PawnTreeNode leaf;
        leaf.label = def.name;
        leaf.defName = def.name;
        leaf.typeTag = "enemy";
        enemyBranch.children.push_back(leaf);
    }
    pawnBranch.children.push_back(enemyBranch);

    // InventoryPawn branch — pickups (data-driven from LightningScript entity registry)
    PawnTreeNode invBranch;
    invBranch.label = "InventoryPawn";
    invBranch.isExpanded = false;
    invBranch.typeTag = "category";

    PawnTreeNode pickupBranch;
    pickupBranch.label = "Pickups";
    pickupBranch.isExpanded = false;
    pickupBranch.typeTag = "category";
    {
        std::vector<const EntityDef*> pickupDefs;
        LightningEntityRegistry::Instance().FindByType(EntityType::PICKUP, pickupDefs);
        for (auto* def : pickupDefs) {
            PawnTreeNode leaf;
            leaf.label = def->name;
            leaf.defName = def->name;
            leaf.typeTag = "pickup";
            pickupBranch.children.push_back(leaf);
        }
    }
    invBranch.children.push_back(pickupBranch);

    // Weapons branch — weapon .ozls defs; placing one creates a weapon pickup
    // (world files represent weapons as `pickup <weaponDefName>`).
    PawnTreeNode weaponBranch;
    weaponBranch.label = "Weapons";
    weaponBranch.isExpanded = false;
    weaponBranch.typeTag = "category";
    {
        std::vector<const EntityDef*> weaponDefs;
        LightningEntityRegistry::Instance().FindByType(EntityType::WEAPON, weaponDefs);
        for (auto* def : weaponDefs) {
            PawnTreeNode leaf;
            leaf.label = def->name;
            leaf.defName = def->name;
            leaf.typeTag = "pickup";
            weaponBranch.children.push_back(leaf);
        }
    }
    pawnBranch.children.push_back(invBranch);
    pawnBranch.children.push_back(weaponBranch);

    // Volume & Node Markers branch
    PawnTreeNode volBranch;
    volBranch.label = "Volume & Node Markers";
    volBranch.isExpanded = false;
    volBranch.typeTag = "category";

    PawnTreeNode psNode;
    psNode.label = "PlayerStartNode";
    psNode.defName = "PlayerStartNode";
    psNode.typeTag = "playerstart";
    volBranch.children.push_back(psNode);

    PawnTreeNode emitBranch;
    emitBranch.label = "EmitterNode";
    emitBranch.isExpanded = false;
    emitBranch.typeTag = "category";
    const char* emitTypes[] = {"SoundEmitter", "MusicEmitter"};
    for (auto& et : emitTypes) {
        PawnTreeNode leaf;
        leaf.label = et;
        leaf.defName = et;
        leaf.typeTag = "emitter";
        emitBranch.children.push_back(leaf);
    }
    volBranch.children.push_back(emitBranch);

    PawnTreeNode zoneBranch;
    zoneBranch.label = "ZoneVolumeNode";
    zoneBranch.isExpanded = false;
    zoneBranch.typeTag = "category";
    const char* zoneTypes[] = {"ZONE_WATER", "ZONE_LADDER", "ZONE_SKY", "ZONE_REVERB", "ZONE_GAMEPLAY_SOUND"};
    for (auto& zt : zoneTypes) {
        PawnTreeNode leaf;
        leaf.label = zt;
        leaf.defName = zt;
        leaf.typeTag = "zone";
        zoneBranch.children.push_back(leaf);
    }
    volBranch.children.push_back(zoneBranch);

    // GameEngine.Mesh branch — places the model currently selected in the
    // Model Browser as a Mesh.Static / Mesh.Skeletal world object.
    PawnTreeNode meshBranch;
    meshBranch.label = "GameEngine.Mesh";
    meshBranch.isExpanded = false;
    meshBranch.typeTag = "category";
    {
        PawnTreeNode s;
        s.label = "Mesh.Static";
        s.defName = "Mesh.Static";
        s.typeTag = "mesh_static";
        meshBranch.children.push_back(s);
        PawnTreeNode k;
        k.label = "Mesh.Skeletal";
        k.defName = "Mesh.Skeletal";
        k.typeTag = "mesh_skeletal";
        meshBranch.children.push_back(k);
    }
    volBranch.children.push_back(meshBranch);

    // GameEngine.ParticleEmitter — local 3D particles (fire/sparks/smoke)
    PawnTreeNode particleLeaf;
    particleLeaf.label = "ParticleEmitter";
    particleLeaf.defName = "ParticleEmitter";
    particleLeaf.typeTag = "particle";
    volBranch.children.push_back(particleLeaf);

    // GameEngine.PathNode — NPC patrol waypoint
    PawnTreeNode pathLeaf;
    pathLeaf.label = "PathNode";
    pathLeaf.defName = "PathNode";
    pathLeaf.typeTag = "pathnode";
    volBranch.children.push_back(pathLeaf);

    // WindZone — foliage sway region
    PawnTreeNode windLeaf;
    windLeaf.label = "WindZone";
    windLeaf.defName = "WindZone";
    windLeaf.typeTag = "windzone";
    volBranch.children.push_back(windLeaf);

    pawnBranch.children.push_back(volBranch);
    root.children.push_back(pawnBranch);
    return root;
}

// Spawn the currently selected Actor-Hierarchy leaf DIRECTLY into PawnSystem.
// Done synchronously inside the panel's window proc (rather than via main-loop
// action flags) so it works regardless of frame/flag timing. Returns false when
// nothing could be spawned (e.g. a category is selected, or no model chosen).
static bool SpawnSelectedPawnTreeItem(HWND hTree) {
    TVITEMW item;
    item.hItem = (HTREEITEM)SendMessage(hTree, TVM_GETNEXTITEM, TVGN_CARET, 0);
    item.mask = TVIF_PARAM;
    if (!item.hItem || !SendMessage(hTree, TVM_GETITEMW, 0, (LPARAM)&item))
        return false;

    std::string typeTag, defName;
    SplitTreeParam((const char*)item.lParam, typeTag, defName);
    if (defName.empty()) return false;

    auto& ps = PawnSystem::Instance();
    Vector3 pos = { g_editorPanels.spawnPos[0], g_editorPanels.spawnPos[1], g_editorPanels.spawnPos[2] };

    if (typeTag == "enemy") {
        ps.Spawn(pos, defName.c_str());
    } else if (typeTag == "pickup") {
        PickupNode n; n.position = pos; n.typeName = defName; ps.AddPickup(n);
    } else if (typeTag == "playerstart") {
        PlayerStartNode n; n.position = pos; n.yaw = 0.0f; ps.AddPlayerStart(n);
    } else if (typeTag == "emitter") {
        EmitterNode n;
        n.type = (defName == "MusicEmitter") ? EmitterType::MUSIC : EmitterType::SOUND;
        n.position = pos; ps.AddEmitter(n);
    } else if (typeTag == "zone") {
        ZoneVolumeNode n;
        n.bounds.min = {pos.x - 4.0f, pos.y - 2.0f, pos.z - 4.0f};
        n.bounds.max = {pos.x + 4.0f, pos.y + 2.0f, pos.z + 4.0f};
        if      (defName == "ZONE_LADDER")         n.zoneType = ZoneType::ZONE_LADDER;
        else if (defName == "ZONE_SKY")            n.zoneType = ZoneType::ZONE_SKY;
        else if (defName == "ZONE_REVERB")         n.zoneType = ZoneType::ZONE_REVERB;
        else if (defName == "ZONE_GAMEPLAY_SOUND") n.zoneType = ZoneType::ZONE_GAMEPLAY_SOUND;
        else                                        n.zoneType = ZoneType::ZONE_WATER;
        ps.AddZone(n);
    } else if (typeTag == "mesh_static" || typeTag == "mesh_skeletal") {
        int idx = g_editorPanels.selectedModel;
        if (idx < 0 || idx >= (int)g_editorPanels.modelEntries.size()) return false;
        MeshObjectNode n;
        n.meshPath = g_editorPanels.modelEntries[idx].path;
        n.skeletal = (typeTag == "mesh_skeletal");
        n.position = pos; n.yaw = 0.0f; n.scale = 1.0f;
        ps.AddMeshObject(n);
    } else if (typeTag == "particle") {
        ParticleEmitterNode n;
        n.type = "fire"; n.position = pos; n.direction = {0, 1, 0};
        n.rate = 30.0f; n.lifetime = 0.9f; n.speed = 2.0f; n.spread = 0.5f;
        n.sizeStart = 0.5f; n.sizeEnd = 0.0f;
        n.colorStart = {255, 170, 60, 255}; n.colorEnd = {80, 20, 10, 0}; n.radius = 0.2f;
        ps.AddParticleEmitter(n);
    } else if (typeTag == "pathnode") {
        static int s_pathCounter = 0;
        PathNode n; n.name = "path_" + std::to_string(s_pathCounter++);
        n.position = pos; n.radius = 1.0f;
        ps.AddPathNode(n);
    } else if (typeTag == "windzone") {
        WindZoneNode n;
        n.bounds.min = {pos.x - 5.0f, pos.y - 5.0f, pos.z - 5.0f};
        n.bounds.max = {pos.x + 5.0f, pos.y + 5.0f, pos.z + 5.0f};
        n.direction = {1, 0, 0}; n.strength = 1.0f; n.frequency = 1.0f;
        ps.AddWindZone(n);
    } else {
        ps.Spawn(pos, defName.c_str());
    }
    return true;
}

// Lay the Actor-Hierarchy controls out to fill the current client area so the
// tree (and buttons) track window resizes.
static void LayoutPawnMgr(HWND hwnd, HWND hLabel, HWND hTree,
                          HWND hSpawn, HWND hRefresh, HWND hClose) {
    if (!hwnd) return;
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    if (hLabel) MoveWindow(hLabel, 10, 10, W - 20, 20, TRUE);
    int top = 35, btnH = 26, pad = 10;
    int treeH = H - top - (btnH + 12) - pad;
    if (treeH < 60) treeH = 60;
    if (hTree) MoveWindow(hTree, 10, top, W - 28, treeH, TRUE);
    int by = top + treeH + 6;
    if (hSpawn)   MoveWindow(hSpawn,   10, by, 110, btnH, TRUE);
    if (hRefresh) MoveWindow(hRefresh, 128, by, 80, btnH, TRUE);
    if (hClose)   MoveWindow(hClose, (W - 10 - 80 < 216) ? 216 : (W - 10 - 80), by, 80, btnH, TRUE);
}

static LRESULT CALLBACK PawnMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hTree, hLabel, hSpawn, hRefresh, hClose;
    switch (msg) {
    case WM_CREATE: {
        hLabel = CreateLabel(hwnd, L"Actor Hierarchy (select a leaf, then Spawn Selected):", 10, 10, 360, 20, 1);
        hTree = CreateWindowEx(WS_EX_CLIENTEDGE, WC_TREEVIEW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            10, 35, 370, 185, hwnd, (HMENU)(INT_PTR)ID_PAWN_TREE, g_hInst, nullptr);
        hSpawn   = CreateButton(hwnd, L"Spawn Selected", 10, 228, 110, 28, ID_PAWN_SPAWN);
        hRefresh = CreateButton(hwnd, L"Refresh", 128, 228, 80, 28, ID_PAWN_REFRESH);
        hClose   = CreateButton(hwnd, L"Close", 216, 228, 80, 28, ID_PAWN_CLOSE);
        LayoutPawnMgr(hwnd, hLabel, hTree, hSpawn, hRefresh, hClose);
        SendMessage(hwnd, WM_USER + 50, 0, 0);
        break;
    }
    case WM_SIZE:
        LayoutPawnMgr(hwnd, hLabel, hTree, hSpawn, hRefresh, hClose);
        break;
    case WM_USER + 50: {
        PopulateTreeView(hTree);
        break;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)l;
        if (nm->idFrom == ID_PAWN_TREE && nm->code == NM_DBLCLK) {
            // Informational only — spawning is done via the "Spawn Selected"
            // button so a context-menu right-click never adds duplicates.
            TVITEMW item;
            item.hItem = (HTREEITEM)SendMessage(hTree, TVM_GETNEXTITEM, TVGN_CARET, 0);
            item.mask = TVIF_PARAM;
            if (item.hItem && SendMessage(hTree, TVM_GETITEMW, 0, (LPARAM)&item)) {
                std::string typeTag, defName;
                SplitTreeParam((const char*)item.lParam, typeTag, defName);
                if (!defName.empty()) {
                    char msgBuf[256];
                    snprintf(msgBuf, sizeof(msgBuf),
                             "Type: %s\nDefinition: %s\n\nUse \"Spawn Selected\" to place it.",
                             typeTag.c_str(), defName.c_str());
                    MessageBoxA(hwnd, msgBuf, "Entity Info", MB_OK);
                }
            }
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_PAWN_CLOSE) ShowPawnManager(false);
        else if (id == ID_PAWN_REFRESH) {
            SendMessage(hwnd, WM_USER + 50, 0, 0);
        } else if (id == ID_PAWN_SPAWN) {
            if (!SpawnSelectedPawnTreeItem(hTree))
                MessageBoxA(hwnd, "Select a leaf (and, for meshes, a model in the\n"
                                  "Model Browser) before spawning.", "Spawn", MB_OK | MB_ICONINFORMATION);
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
// Script Manager — LightningScript (.ozls) browser + editor launcher
// =====================================================================
static const int ID_SCRIPT_CLOSE  = 100;
static const int ID_SCRIPT_LIST   = 101;
static const int ID_SCRIPT_REFRESH = 102;
static const int ID_SCRIPT_EDIT   = 103;
static const int ID_SCRIPT_NEW    = 104;
static const int ID_SCRIPT_DELETE = 105;
static const int ID_SCRIPT_RELOAD = 106;
static const int ID_SCRIPT_DETAIL = 107;
static const int ID_SCRIPT_NEWNAME = 108;
static const int ID_SCRIPT_NEWTYPE = 109;
static const int ID_SCRIPT_NEWCREATE = 110;
static const int ID_SCRIPT_NEWCANCEL = 111;

struct ScriptListEntry {
    std::string name;
    std::string typeName;   // EntityTypeName() or "?" for unparsed files
    int actionCount = 0;
    std::string path;
    int defIndex = -1;      // index into LightningEntityRegistry::GetAllDefs(), -1 = parse error
};

static std::vector<ScriptListEntry> g_scripts;
static bool s_scriptNewMode = false;

// Folder for newly created defs, by entity type
static const char* ScriptFolderForType(const std::string& type) {
    if (type == "weapon") return "gun";
    if (type == "pawn") return "Pawns";
    if (type == "skyzone") return "Zones";
    return "Items"; // pickup / consumable / upgrade / armor
}

// Minimal .ozls skeleton per type
static std::string ScriptTemplate(const std::string& name, const std::string& type) {
    if (type == "weapon")
        return "entity \"" + name + "\" : weapon {\n"
               "    stats {\n        damage = 10\n        fire_rate = 0.4\n"
               "        projectile_speed = 20\n        projectile_lifetime = 2.0\n"
               "        magazine = 12\n        reload_time = 2.0\n    }\n"
               "    actions {\n        on_fire {\n            say \"Bang!\"\n        }\n"
               "        on_reload {\n            say \"Reloading...\"\n        }\n    }\n}\n";
    if (type == "pickup")
        return "entity \"" + name + "\" : pickup {\n"
               "    stats {\n        item_id = 0\n        respawn_time = 30\n    }\n"
               "    actions {\n        on_collect {\n            msg \"Picked up\"\n        }\n    }\n}\n";
    if (type == "pawn")
        return "entity \"" + name + "\" : pawn {\n"
               "    actions {\n        on_death {\n            msg \"Enemy down.\"\n        }\n    }\n}\n";
    if (type == "skyzone")
        return "entity \"" + name + "\" : skyzone {\n"
               "    actions {\n        on_enter {\n            msg \"Entered zone.\"\n        }\n"
               "        on_exit {\n            msg \"Left zone.\"\n        }\n    }\n}\n";
    if (type == "consumable")
        return "entity \"" + name + "\" : consumable {\n"
               "    stats {\n        value = 25\n    }\n"
               "    actions {\n        on_use {\n            playerstat health += 25\n            consume\n        }\n    }\n}\n";
    return "entity \"" + name + "\" : upgrade {\n    stats {\n    }\n    actions {\n    }\n}\n";
}

// Registry-driven .ozls list: parsed defs first, then files that failed to parse
static void ScanScriptFiles() {
    g_scripts.clear();
    auto& registry = LightningEntityRegistry::Instance();
    const auto& defs = registry.GetAllDefs();
    for (size_t i = 0; i < defs.size(); i++) {
        const EntityDef& def = defs[i];
        ScriptListEntry e;
        e.name = def.name;
        e.typeName = EntityTypeName(def.type);
        e.actionCount = (int)def.actions.size();
        e.path = def.sourcePath;
        e.defIndex = (int)i;
        g_scripts.push_back(e);
    }
    // Filesystem fallback: .ozls files that produced no def (parse errors)
    fs::path gd = fs::current_path() / "GameData";
    if (fs::exists(gd)) {
        for (auto& entry : fs::recursive_directory_iterator(gd)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext != ".ozls") continue;
            std::string p = entry.path().string();
            bool known = false;
            for (auto& s : g_scripts) if (s.path == p) { known = true; break; }
            if (!known) {
                ScriptListEntry e;
                e.name = entry.path().stem().string();
                e.typeName = "?";
                e.path = p;
                g_scripts.push_back(e);
            }
        }
    }
    std::sort(g_scripts.begin(), g_scripts.end(),
              [](const ScriptListEntry& a, const ScriptListEntry& b) { return a.name < b.name; });
    if (g_editorPanels.hScriptMgr)
        SendMessage((HWND)g_editorPanels.hScriptMgr, WM_USER + 50, 0, 0);
}

// Read-only detail text for the selected script (def stats + actions)
static void BuildDefSummary(const EntityDef& def, const std::string& sourcePath,
                            int actionCountHint, std::string& out);
static void BuildScriptDetail(int sel, std::string& out) {
    out.clear();
    if (sel < 0 || sel >= (int)g_scripts.size()) return;
    const ScriptListEntry& e = g_scripts[sel];
    if (e.defIndex < 0) {
        out += e.name + "  [" + e.typeName + "]\n";
        out += "source: " + e.path + "\n";
        out += "\n(parse error — file did not produce an entity definition)\n";
        return;
    }
    auto& defs = LightningEntityRegistry::Instance().GetAllDefs();
    if (e.defIndex >= (int)defs.size()) return;
    BuildDefSummary(defs[e.defIndex], e.path, -1, out);
}

void ShowScriptManager(bool show) {
    g_editorPanels.showScriptMgr = show;
    if (g_editorPanels.hScriptMgr) {
        if (show) SendMessage((HWND)g_editorPanels.hScriptMgr, WM_USER + 50, 0, 0);
        ShowWindow((HWND)g_editorPanels.hScriptMgr, show ? SW_SHOW : SW_HIDE);
    }
}

static LRESULT CALLBACK ScriptMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList = nullptr;
    static HWND hDetail = nullptr;

    // Builds the panel contents for the current mode (normal / new-script form).
    // Destroy-and-rebuild follows the house PopulatePropertiesPanel pattern.
    auto buildPanel = [&]() {
        HWND child = GetWindow(hwnd, GW_CHILD);
        while (child) {
            HWND next = GetWindow(child, GW_HWNDNEXT);
            DestroyWindow(child);
            child = next;
        }
        hList = nullptr;
        hDetail = nullptr;
        SendMessage(hwnd, WM_SETREDRAW, FALSE, 0);
        if (s_scriptNewMode) {
            CreateLabel(hwnd, L"New Script Name:", 10, 14, 120, 20, 0);
            CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"my_script",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                130, 10, 180, 22, hwnd, (HMENU)(INT_PTR)ID_SCRIPT_NEWNAME, g_hInst, nullptr);
            CreateLabel(hwnd, L"Type:", 10, 42, 120, 20, 0);
            HWND hCombo = CreateWindowEx(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                130, 38, 180, 200, hwnd, (HMENU)(INT_PTR)ID_SCRIPT_NEWTYPE, g_hInst, nullptr);
            const wchar_t* types[] = { L"weapon", L"pickup", L"pawn", L"skyzone", L"consumable", L"upgrade" };
            for (auto* t : types) SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)t);
            SendMessage(hCombo, CB_SETCURSEL, 0, 0);
            CreateButton(hwnd, L"Create", 320, 10, 90, 26, ID_SCRIPT_NEWCREATE);
            CreateButton(hwnd, L"Cancel", 320, 40, 90, 26, ID_SCRIPT_NEWCANCEL);
            CreateLabel(hwnd, L"Folder: GameData/Global/<type>  —  opened in your editor after creation.",
                        10, 72, 500, 20, 0);
        } else {
            CreateLabel(hwnd, L"LightningScript files (.ozls — GameData/ + packages):", 10, 10, 520, 20, 1);
            hList = CreateListBox(hwnd, 10, 32, 520, 170, ID_SCRIPT_LIST);
            hDetail = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                10, 210, 520, 160, hwnd, (HMENU)(INT_PTR)ID_SCRIPT_DETAIL, g_hInst, nullptr);
            CreateButton(hwnd, L"Edit", 10, 378, 90, 28, ID_SCRIPT_EDIT);
            CreateButton(hwnd, L"New Script...", 106, 378, 110, 28, ID_SCRIPT_NEW);
            CreateButton(hwnd, L"Delete", 222, 378, 90, 28, ID_SCRIPT_DELETE);
            CreateButton(hwnd, L"Reload", 318, 378, 90, 28, ID_SCRIPT_RELOAD);
            CreateButton(hwnd, L"Close", 440, 378, 90, 28, ID_SCRIPT_CLOSE);
        }
        SendMessage(hwnd, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(hwnd, nullptr, TRUE);
    };

    switch (msg) {
    case WM_CREATE:
        buildPanel();
        if (!s_scriptNewMode) {
            ScanScriptFiles();
            SendMessage(hwnd, WM_USER + 50, 0, 0); // fill list (hScriptMgr not set yet during WM_CREATE)
        }
        break;
    case WM_USER + 50: {
        if (s_scriptNewMode || !hList) break;
        SendMessage(hList, LB_RESETCONTENT, 0, 0);
        for (const auto& scr : g_scripts) {
            char line[320];
            snprintf(line, sizeof(line), "%s  [%s]%s", scr.name.c_str(), scr.typeName.c_str(),
                     scr.defIndex < 0 ? "  (parse error)" :
                     (scr.actionCount > 0 ? "" : "  (no actions)"));
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)line);
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_SCRIPT_CLOSE) {
            ShowScriptManager(false);
        } else if (id == ID_SCRIPT_NEW) {
            s_scriptNewMode = true;
            buildPanel();
        } else if (id == ID_SCRIPT_NEWCANCEL) {
            s_scriptNewMode = false;
            buildPanel();
            ScanScriptFiles();
        } else if (id == ID_SCRIPT_NEWCREATE) {
            wchar_t nameBuf[128] = {0};
            GetWindowTextW(GetDlgItem(hwnd, ID_SCRIPT_NEWNAME), nameBuf, 128);
            char nameA[128] = {0};
            WideCharToMultiByte(CP_UTF8, 0, nameBuf, -1, nameA, 128, nullptr, nullptr);
            int typeSel = (int)SendMessage(GetDlgItem(hwnd, ID_SCRIPT_NEWTYPE), CB_GETCURSEL, 0, 0);
            static const char* types[] = { "weapon", "pickup", "pawn", "skyzone", "consumable", "upgrade" };
            std::string type = (typeSel >= 0 && typeSel < 6) ? types[typeSel] : "pickup";
            std::string name = nameA;
            while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(0, 1);
            while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
            for (auto& c : name) if (c == ' ' || c == '\\' || c == '/' || c == ':') c = '_';
            if (name.empty()) {
                MessageBoxA(hwnd, "Script name must not be empty.", "New Script", MB_OK);
                break;
            }
            fs::path dir = fs::current_path() / "GameData" / "Global" / ScriptFolderForType(type);
            std::error_code ec;
            fs::create_directories(dir, ec);
            fs::path out = dir / (name + ".ozls");
            if (fs::exists(out)) {
                MessageBoxA(hwnd, ("Already exists: " + out.string()).c_str(), "New Script", MB_OK);
                break;
            }
            std::ofstream ofs(out, std::ios::binary);
            if (!ofs) {
                MessageBoxA(hwnd, ("Could not write: " + out.string()).c_str(), "New Script", MB_OK);
                break;
            }
            ofs << ScriptTemplate(name, type);
            ofs.close();
            LightningEntityRegistry::Instance().Init();
            if (g_editorPanels.hPawnMgr)
                SendMessage((HWND)g_editorPanels.hPawnMgr, WM_USER + 50, 0, 0);
            ShellExecuteA(hwnd, "open", out.string().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            s_scriptNewMode = false;
            buildPanel();
            ScanScriptFiles();
        } else if (id == ID_SCRIPT_REFRESH) {
            ScanScriptFiles();
        } else if (id == ID_SCRIPT_RELOAD) {
            LightningEntityRegistry::Instance().Init();
            ScanScriptFiles();
            if (g_editorPanels.hPawnMgr)
                SendMessage((HWND)g_editorPanels.hPawnMgr, WM_USER + 50, 0, 0);
            OZ_INFO("Script registry reloaded (%zu defs)", g_scripts.size());
        } else if (id == ID_SCRIPT_DELETE) {
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_scripts.size()) {
                const ScriptListEntry& e = g_scripts[sel];
                if (!fs::exists(e.path)) {
                    MessageBoxA(hwnd,
                        "This script lives inside a package.\nEdit the source .ozls and repack to change it.",
                        "Delete Script", MB_OK | MB_ICONINFORMATION);
                    break;
                }
                std::string q = "Delete '" + e.name + "'?\n" + e.path;
                if (MessageBoxA(hwnd, q.c_str(), "Delete Script", MB_YESNO | MB_ICONWARNING) == IDYES) {
                    std::error_code ec;
                    fs::remove(e.path, ec);
                    LightningEntityRegistry::Instance().Init();
                    ScanScriptFiles();
                    if (g_editorPanels.hPawnMgr)
                        SendMessage((HWND)g_editorPanels.hPawnMgr, WM_USER + 50, 0, 0);
                }
            }
        } else if (id == ID_SCRIPT_EDIT ||
                   (id == ID_SCRIPT_LIST && HIWORD(w) == LBN_DBLCLK)) {
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_scripts.size()) {
                const std::string& p = g_scripts[sel].path;
                if (!fs::exists(p)) {
                    std::string msg = "Script is packaged (no editable source file):\n" + p +
                                      "\n\nEdit the GameData source .ozls and repack.";
                    MessageBoxA(hwnd, msg.c_str(), "Edit Script", MB_OK | MB_ICONINFORMATION);
                } else {
                    ShellExecuteA(hwnd, "open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            }
        } else if (id == ID_SCRIPT_LIST && HIWORD(w) == LBN_SELCHANGE) {
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            std::string detail;
            BuildScriptDetail(sel, detail);
            SetWindowTextA(hDetail, detail.c_str());
        }
        break;
    }
    case WM_CLOSE:
        s_scriptNewMode = false;
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
static const int ID_MDL_IMPORT  = 105;
static const int ID_MDL_EXPORT  = 106;

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
                    if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".iqm" || ext == ".vox" || ext == ".m3d") {
                        ModelBrowserEntry mbe;
                        mbe.name = entry.path().stem().string();
                        mbe.path = entry.path().string();
                        g_editorPanels.modelEntries.push_back(mbe);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "WARN: Exception during model scan: %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "WARN: Unknown exception during model scan\n");
    }

    // Package scan for .obj files
    std::vector<std::string> pkgFiles;
    PackageAssetLoader::Instance().ListAllFiles(pkgFiles);
    for (const auto& pkgPath : pkgFiles) {
        std::string ext = pkgPath.substr(pkgPath.rfind('.'));
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".iqm" || ext == ".vox" || ext == ".m3d") {
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
    
    // Deduplicate by name, preferring real files over package copies.
    auto isPkgModel = [](const ModelBrowserEntry& e) { return !IsPathFile(e.path.c_str()); };
    std::stable_sort(g_editorPanels.modelEntries.begin(), g_editorPanels.modelEntries.end(),
        [&](const ModelBrowserEntry& a, const ModelBrowserEntry& b) {
            if (a.name != b.name) return a.name < b.name;
            return (isPkgModel(a) ? 1 : 0) < (isPkgModel(b) ? 1 : 0);
        });
    auto last = std::unique(g_editorPanels.modelEntries.begin(), g_editorPanels.modelEntries.end(),
        [](const ModelBrowserEntry& a, const ModelBrowserEntry& b) { return a.name == b.name; });
    g_editorPanels.modelEntries.erase(last, g_editorPanels.modelEntries.end());

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

static void LayoutModelBrowser(HWND hwnd, HWND hList, HWND hPreview, HWND hInfo,
                               HWND hRefresh, HWND hPlace, HWND hClose) {
    if (!hwnd) return;
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    int top = 32, bottomPad = 34;
    int listW = (int)(W * 0.42f); if (listW < 160) listW = 160;
    int listH = H - top - bottomPad; if (listH < 80) listH = 80;
    int rx = listW + 18;
    int rw = W - rx - 8; if (rw < 120) rw = 120;
    int previewH = (H - top - bottomPad) / 2; if (previewH < 120) previewH = 120;
    if (hList)    MoveWindow(hList, 8, top, listW, listH, TRUE);
    if (hPreview) MoveWindow(hPreview, rx, top, rw, previewH, TRUE);
    if (hInfo)    MoveWindow(hInfo, rx, top + previewH + 6, rw, 56, TRUE);
    if (hRefresh) MoveWindow(hRefresh, 8, 4, 80, 22, TRUE);
    if (hPlace)   MoveWindow(hPlace, rx, top + previewH + 68, 130, 24, TRUE);
    if (hClose)   MoveWindow(hClose, W - 8 - 72, H - 8 - 24, 72, 24, TRUE);
}

static LRESULT CALLBACK ModelBrwProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList, hPreview, hInfo, hRefreshBtn, hPlaceBtn, hCloseBtn;
    int id;
    switch (msg) {
    case WM_CREATE: {
        int PW = 540, PH = 500;
        hList = CreateListBox(hwnd, 8, 32, 230, PH - 100, ID_MDL_LIST);
        hPreview = CreateWindowEx(WS_EX_STATICEDGE, L"STATIC", L"",
             WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
             248, 32, 260, 220, hwnd, (HMENU)(INT_PTR)ID_MDL_PREVIEW,
             g_hInst, nullptr);
        hInfo    = CreateLabel(hwnd, L"Select a model from the list", 248, 260, 260, 60, 2);
        hRefreshBtn = CreateButton(hwnd, L"Refresh", 8, 4, 80, 22, ID_MDL_REFRESH);
        CreateIconButton(hwnd, L"Import", 94, 4, 74, 22, ID_MDL_IMPORT, "BBGeneric");
        CreateIconButton(hwnd, L"Export", 172, 4, 74, 22, ID_MDL_EXPORT, "BBSheet");
        hPlaceBtn   = CreateButton(hwnd, L"Place in World", 248, 340, 120, 24, ID_MDL_PLACE);
        hCloseBtn   = CreateButton(hwnd, L"Close", PW - 72, PH - 28, 64, 22, ID_MDL_CLOSE);
        LayoutModelBrowser(hwnd, hList, hPreview, hInfo, hRefreshBtn, hPlaceBtn, hCloseBtn);
        break;
    }
    case WM_SIZE:
        LayoutModelBrowser(hwnd, hList, hPreview, hInfo, hRefreshBtn, hPlaceBtn, hCloseBtn);
        break;
    case WM_USER + 50: {
        SendMessage(hList, LB_RESETCONTENT, 0, 0);
        for (auto& e : g_editorPanels.modelEntries)
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)e.name.c_str());
        SetWindowTextA(hInfo, "Select a model from the list");
        break;
    }
    case WM_PREVIEW_READY: {
        HBITMAP hBmp = (HBITMAP)w;
        if (hBmp) {
            SendMessage(hPreview, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmp);
            InvalidateRect(hPreview, nullptr, TRUE);
            UpdateWindow(hPreview);
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
                const auto& e = g_editorPanels.modelEntries[sel];
                char buf[512];
                snprintf(buf, sizeof(buf), "%s   (%d verts, %d tris)\n%s",
                         e.name.c_str(), e.vertices, e.triangles, e.path.c_str());
                SetWindowTextA(hInfo, buf);
            }
        } else if (id == ID_MDL_IMPORT) {
            // Copy an external model (and its companion texture) into GameData.
            wchar_t path[MAX_PATH] = L"";
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrFilter = L"Meshes (*.obj;*.glb;*.gltf;*.iqm;*.vox;*.m3d)\0*.obj;*.glb;*.gltf;*.iqm;*.vox;*.m3d\0All Files (*.*)\0*.*\0\0";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (GetOpenFileNameW(&ofn)) {
                std::error_code ec;
                fs::path src(path);
                fs::path destDir = fs::current_path() / "GameData" / "Global" / "Models";
                fs::create_directories(destDir, ec);
                fs::path dest = destDir / src.filename();
                fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
                std::string stem = src.stem().string();
                for (const char* suf : {"_texture.png", ".png", "_texture.tga", ".tga", "_texture.bmp", ".bmp"}) {
                    fs::path tp = src.parent_path() / (stem + suf);
                    if (fs::exists(tp)) {
                        fs::copy_file(tp, destDir / tp.filename(), fs::copy_options::overwrite_existing, ec);
                        break;
                    }
                }
                ScanModelBrowserFiles();
                fprintf(stdout, "Imported mesh '%s' -> %s\n", src.filename().string().c_str(), dest.string().c_str());
            }
        } else if (id == ID_MDL_EXPORT) {
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)g_editorPanels.modelEntries.size()) {
                MessageBoxA(hwnd, "Select a model first.", "Export Mesh", MB_OK | MB_ICONINFORMATION);
            } else {
                const auto& e = g_editorPanels.modelEntries[sel];
                std::string ext = ".obj";
                size_t dot = e.path.rfind('.');
                if (dot != std::string::npos) ext = e.path.substr(dot);
                std::string defName = e.name + ext;
                wchar_t path[MAX_PATH] = L"";
                std::wstring wdef(defName.begin(), defName.end());
                wcsncpy(path, wdef.c_str(), MAX_PATH - 1);
                OPENFILENAMEW sfn = {};
                sfn.lStructSize = sizeof(sfn);
                sfn.hwndOwner = hwnd;
                sfn.lpstrFile = path;
                sfn.nMaxFile = MAX_PATH;
                sfn.lpstrFilter = L"Meshes (*.obj;*.glb;*.gltf;*.iqm;*.vox;*.m3d)\0*.obj;*.glb;*.gltf;*.iqm;*.vox;*.m3d\0All Files (*.*)\0*.*\0\0";
                sfn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetSaveFileNameW(&sfn)) {
                    std::error_code ec;
                    fs::path dest(path);
                    if (IsPathFile(e.path.c_str())) {
                        fs::copy_file(e.path, dest, fs::copy_options::overwrite_existing, ec);
                    } else {
                        size_t sz = 0;
                        const uint8_t* data =
                            PackageAssetLoader::Instance().ResolvePackageKey(e.path.c_str(), sz);
                        if (data && sz > 0) {
                            std::ofstream out(dest, std::ios::binary);
                            if (out.is_open()) out.write((const char*)data, (std::streamsize)sz);
                        }
                    }
                    fprintf(stdout, "Exported mesh '%s' -> %s\n", e.name.c_str(), dest.string().c_str());
                }
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
        if (dis->CtlType == ODT_BUTTON) return DrawIconButton(dis);
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
// =====================================================================
// ZoneProperties Ã¢â‚¬â€ replaces EnvPanel with Fog/Ambient/GameType/Particles
// =====================================================================
static int g_zoneTab = 0; // 0=Fog, 1=Ambient, 2=GameType, 3=Particles

// Zone properties state
static ZoneProperties g_zoneProps;

void ShowEnvPanel(bool show) {
    g_editorPanels.showEnvPanel = show;
    if (g_editorPanels.hEnvPanel)
        ShowWindow((HWND)g_editorPanels.hEnvPanel, show ? SW_SHOW : SW_HIDE);
}

ZoneProperties GetZoneProperties() { return g_zoneProps; }
void ClearZoneApplyFlags() {
    g_zoneProps.applyFog = false;
    g_zoneProps.applyAmbient = false;
    g_zoneProps.applyParticles = false;
    g_zoneProps.applyGameType = false;
    g_zoneProps.applySkybox = false;
}

// --- Level metadata (persisted via LevelInfo/Particles instructions) ---
static LevelMetadata g_levelMeta;

LevelMetadata GetLevelMetadata() { return g_levelMeta; }

void SetLevelMetadata(const LevelMetadata& meta) {
    g_levelMeta = meta;
    // Mirror into ZoneProperties so the dialog shows current values
    g_zoneProps.gameType = meta.gameType;
    g_zoneProps.maxPlayers = meta.maxPlayers;
    g_zoneProps.respawnTime = meta.respawnTime;
    g_zoneProps.timeLimitEnabled = meta.timeLimitEnabled;
    g_zoneProps.timeLimitMinutes = meta.timeLimitMinutes;
    g_zoneProps.scoreLimit = meta.scoreLimit;
    g_zoneProps.friendlyFire = meta.friendlyFire;
    g_zoneProps.skyboxTexturePath = meta.skyboxTexturePath;
    g_zoneProps.particleType = meta.particleType;
    g_zoneProps.particleDensity = meta.particleDensity;
    g_zoneProps.particleSpeed = meta.particleSpeed;
    g_zoneProps.particleColorR = meta.particleColorR;
    g_zoneProps.particleColorG = meta.particleColorG;
    g_zoneProps.particleColorB = meta.particleColorB;
    g_zoneProps.particleWindX = meta.particleWindX;
    g_zoneProps.particleWindZ = meta.particleWindZ;
}

// --- Available worlds scan (for portal targets + LevelList) ---
static std::vector<std::string> g_availableWorlds;

static void ScanAvailableWorlds() {
    g_availableWorlds.clear();
    const char* roots[] = {"GameData/Worlds", "../GameData/Worlds"};
    for (const char* root : roots) {
        fs::path base(root);
        if (!fs::exists(base)) continue;
        for (auto& entry : fs::directory_iterator(base)) {
            if (!entry.is_directory()) continue;
            std::string name = entry.path().filename().string();
            if (name == "Legacy") continue; // nested legacy world dump
            if (fs::exists(entry.path() / "World.ozone")) {
                // avoid duplicates when both roots resolve to the same tree
                bool dup = false;
                for (const auto& w : g_availableWorlds) if (w == name) { dup = true; break; }
                if (!dup) g_availableWorlds.push_back(name);
            }
        }
    }
    std::sort(g_availableWorlds.begin(), g_availableWorlds.end());
}

// Tab IDs
static const int ID_ZONE_TAB_FOG = 190;
static const int ID_ZONE_TAB_AMB = 191;
static const int ID_ZONE_TAB_GT  = 192;
static const int ID_ZONE_TAB_PAR = 193;
static const int ID_ZONE_TAB_POR = 194;
static const int ID_ZONE_CLOSE   = 199;

// Fog controls
static const int ID_SB_FOG_R = 110, ID_SB_FOG_G = 111, ID_SB_FOG_B = 112;
static const int ID_SB_FOG_DENSITY = 113;
static const int ID_SF_FOG_START = 114, ID_SF_FOG_END = 115;
static const int ID_SF_SKYBOX_PATH = 116;
static const int ID_ZONE_APPLY_FOG = 140;
// Fog tab — skybox pickers (browse file / use Texture Manager selection / apply)
static const int ID_SF_SKYBOX_BROWSE = 117;
static const int ID_SF_SKYBOX_ACTIVE = 118;
static const int ID_ZONE_APPLY_SKY   = 119;

// Ambient controls
static const int ID_SB_AMB_R = 120, ID_SB_AMB_G = 121, ID_SB_AMB_B = 122;
static const int ID_SB_AMB_INT = 123;
static const int ID_ZONE_APPLY_AMB = 141;

// GameType controls
static const int ID_CMB_GAMETYPE   = 150;
static const int ID_SF_MAXPLAYERS  = 151;
static const int ID_SF_RESPAWN     = 152;
static const int ID_CHK_TIMELIMIT  = 153;
static const int ID_SF_TIMELIMIT   = 154;
static const int ID_SF_SCORELIMIT  = 155;
static const int ID_CHK_FRIENDLY   = 156;
static const int ID_ZONE_APPLY_GT  = 157;

// Particle controls
static const int ID_CMB_PARTICLETYPE = 160;
static const int ID_SB_PAR_DENSITY   = 161;
static const int ID_SB_PAR_SPEED     = 162;
static const int ID_SB_PAR_R         = 163;
static const int ID_SB_PAR_G         = 164;
static const int ID_SB_PAR_B         = 165;
static const int ID_SF_PAR_WINDX     = 166;
static const int ID_SF_PAR_WINDZ     = 167;
static const int ID_ZONE_APPLY_PAR   = 168;

// Portal tab controls
static const int ID_CMB_PORTAL_LIST   = 170;
static const int ID_CMB_PORTAL_WORLD  = 171;
static const int ID_SF_PORTAL_SX      = 172;
static const int ID_SF_PORTAL_SY      = 173;
static const int ID_SF_PORTAL_SZ      = 174;
static const int ID_CHK_PORTAL_BIDIR  = 175;
static const int ID_ZONE_APPLY_PORTAL = 176;
static const int ID_ZONE_DEL_PORTAL   = 177;
static const int ID_BTN_PORTAL_REFRESH= 178;

// Tab control groups: 0=Fog, 1=Ambient, 2=GameType, 3=Particles, 4=Portal
static std::vector<HWND> g_zoneControlGroups[5];

// Portal editing state (shared with Main.cpp via accessors)
static PortalEditState g_portalEdit;

static void SyncScrollPos(HWND hwnd, int id, int value) {
    HWND hSB = GetDlgItem(hwnd, id);
    if (hSB) SetScrollPos(hSB, SB_CTL, value, TRUE);
}

static void ShowZoneTab(HWND hwnd, int tab) {
    if (tab < 0 || tab > 4) return;
    g_zoneTab = tab;
    for (int t = 0; t < 5; t++) {
        for (auto& h : g_zoneControlGroups[t]) {
            ShowWindow(h, (t == tab) ? SW_SHOW : SW_HIDE);
        }
    }
    // Sync scrollbar positions from current g_zoneProps values
    if (tab == 0) {
        SyncScrollPos(hwnd, ID_SB_FOG_R, g_zoneProps.fogR);
        SyncScrollPos(hwnd, ID_SB_FOG_G, g_zoneProps.fogG);
        SyncScrollPos(hwnd, ID_SB_FOG_B, g_zoneProps.fogB);
        SyncScrollPos(hwnd, ID_SB_FOG_DENSITY, (int)(g_zoneProps.fogDensity * 1000));
    } else if (tab == 1) {
        SyncScrollPos(hwnd, ID_SB_AMB_R, g_zoneProps.ambR);
        SyncScrollPos(hwnd, ID_SB_AMB_G, g_zoneProps.ambG);
        SyncScrollPos(hwnd, ID_SB_AMB_B, g_zoneProps.ambB);
        SyncScrollPos(hwnd, ID_SB_AMB_INT, (int)(g_zoneProps.ambIntensity * 100));
    } else if (tab == 3) {
        SyncScrollPos(hwnd, ID_SB_PAR_DENSITY, (int)g_zoneProps.particleDensity);
        SyncScrollPos(hwnd, ID_SB_PAR_SPEED, (int)(g_zoneProps.particleSpeed * 10));
        SyncScrollPos(hwnd, ID_SB_PAR_R, g_zoneProps.particleColorR);
        SyncScrollPos(hwnd, ID_SB_PAR_G, g_zoneProps.particleColorG);
        SyncScrollPos(hwnd, ID_SB_PAR_B, g_zoneProps.particleColorB);
    } else if (tab == 4) {
        RefreshPortalList();
    }
}

// --- Portal tab data plumbing ---
int GetPortalCount() {
    return (int)PawnSystem::Instance().GetPortals().size();
}

const char* GetPortalTargetWorld(int index) {
    auto& portals = PawnSystem::Instance().GetPortals();
    if (index < 0 || index >= (int)portals.size()) return nullptr;
    return portals[index].targetWorld.c_str();
}

void RefreshPortalList() {
    // Called on portal tab open and after world changes; rebuilds combo contents
    // (deferred until controls exist — guarded by panel handle)
    if (!g_editorPanels.hEnvPanel) return;
    HWND hList = GetDlgItem((HWND)g_editorPanels.hEnvPanel, ID_CMB_PORTAL_LIST);
    if (!hList) return;
    SendMessage(hList, CB_RESETCONTENT, 0, 0);
    auto& portals = PawnSystem::Instance().GetPortals();
    for (size_t p = 0; p < portals.size(); p++) {
        wchar_t label[300];
        std::wstring tgt(portals[p].targetWorld.begin(), portals[p].targetWorld.end());
        if (tgt.empty()) tgt = L"<unassigned>";
        _snwprintf(label, 299, L"Portal %zu -> %s", p, tgt.c_str());
        label[299] = 0;
        SendMessage(hList, CB_ADDSTRING, 0, (LPARAM)label);
    }
    if (g_portalEdit.selectedIndex >= 0 &&
        g_portalEdit.selectedIndex < (int)portals.size())
        SendMessage(hList, CB_SETCURSEL, g_portalEdit.selectedIndex, 0);
}

static void LoadPortalIntoEditor(int index) {
    auto& portals = PawnSystem::Instance().GetPortals();
    g_portalEdit.selectedIndex = index;
    if (index < 0 || index >= (int)portals.size()) {
        g_portalEdit.targetWorld[0] = 0;
        g_portalEdit.spawnX = g_portalEdit.spawnY = g_portalEdit.spawnZ = 0;
        g_portalEdit.bidirectional = true;
        return;
    }
    const ZonePortal& p = portals[index];
    size_t n = p.targetWorld.copy(g_portalEdit.targetWorld, 255);
    g_portalEdit.targetWorld[n] = 0;
    g_portalEdit.spawnX = p.targetSpawn.x;
    g_portalEdit.spawnY = p.targetSpawn.y;
    g_portalEdit.spawnZ = p.targetSpawn.z;
    g_portalEdit.bidirectional = p.bidirectional;
}

void SetPortalSelection(int index) {
    LoadPortalIntoEditor(index);
    // Push values into controls if the panel exists
    if (!g_editorPanels.hEnvPanel) return;
    HWND hwnd = (HWND)g_editorPanels.hEnvPanel;
    ShowZoneTab(hwnd, 4);
    RefreshPortalList();
    ScanAvailableWorlds();
    HWND hWorld = GetDlgItem(hwnd, ID_CMB_PORTAL_WORLD);
    if (hWorld) {
        std::wstring cur(g_portalEdit.targetWorld,
                         g_portalEdit.targetWorld + strlen(g_portalEdit.targetWorld));
        int sel = (int)SendMessage(hWorld, CB_FINDSTRINGEXACT, -1, (LPARAM)cur.c_str());
        SendMessage(hWorld, CB_SETCURSEL, sel, 0);
    }
    auto setFloat = [&](int id, float v) {
        wchar_t buf[32];
        _snwprintf(buf, 31, L"%.2f", v); buf[31] = 0;
        SetWindowTextW(GetDlgItem(hwnd, id), buf);
    };
    setFloat(ID_SF_PORTAL_SX, g_portalEdit.spawnX);
    setFloat(ID_SF_PORTAL_SY, g_portalEdit.spawnY);
    setFloat(ID_SF_PORTAL_SZ, g_portalEdit.spawnZ);
    SendMessage(GetDlgItem(hwnd, ID_CHK_PORTAL_BIDIR), BM_SETCHECK,
                g_portalEdit.bidirectional ? BST_CHECKED : BST_UNCHECKED, 0);
}

PortalEditValues GetPortalEditValues() {
    PortalEditValues out;
    out.targetWorld = g_portalEdit.targetWorld;
    out.spawnX = g_portalEdit.spawnX;
    out.spawnY = g_portalEdit.spawnY;
    out.spawnZ = g_portalEdit.spawnZ;
    out.bidirectional = g_portalEdit.bidirectional;
    // Live-read controls if the panel exists (captures unsaved edits)
    if (g_editorPanels.hEnvPanel) {
        HWND hwnd = (HWND)g_editorPanels.hEnvPanel;
        int wsel = (int)SendMessage(GetDlgItem(hwnd, ID_CMB_PORTAL_WORLD), CB_GETCURSEL, 0, 0);
        if (wsel >= 0 && wsel < (int)g_availableWorlds.size())
            out.targetWorld = g_availableWorlds[wsel];
        auto readF = [hwnd](int fid, float def) -> float {
            wchar_t b[64] = {0};
            HWND h = GetDlgItem(hwnd, fid);
            if (!h) return def;
            GetWindowTextW(h, b, 64);
            return (float)wcstod(b, nullptr);
        };
        out.spawnX = readF(ID_SF_PORTAL_SX, out.spawnX);
        out.spawnY = readF(ID_SF_PORTAL_SY, out.spawnY);
        out.spawnZ = readF(ID_SF_PORTAL_SZ, out.spawnZ);
        out.bidirectional =
            (int)SendMessage(GetDlgItem(hwnd, ID_CHK_PORTAL_BIDIR), BM_GETCHECK, 0, 0) != 0;
    }
    return out;
}

// Generic image picker. Prefers a repo-relative GameData/ path so saved worlds
// stay portable. Used by the entity/model properties texture fields and the
// Zone Properties skybox.
static bool ChooseImageFile(std::string& outPath) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_hRaylibWnd;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.dds)\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.dds\0All Files (*.*)\0*.*\0\0";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) return false;

    int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return false;
    std::vector<char> utf8((size_t)size);
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), size, nullptr, nullptr);
    outPath.assign(utf8.data());

    std::string s = outPath;
    for (auto& c : s) if (c == '\\') c = '/';
    size_t gd = s.find("GameData/");
    if (gd != std::string::npos) outPath = s.substr(gd);
    return true;
}

// Skybox texture picker for the Zone Properties Fog tab.
static bool ChooseSkyboxFile(std::string& outPath) { return ChooseImageFile(outPath); }

// `.ozanim` vertex-keyframe picker (normalized to a GameData-relative path).
static bool ChooseAnimFile(std::string& outPath) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW d = {};
    d.lStructSize = sizeof(d);
    d.hwndOwner = g_hRaylibWnd;
    d.lpstrFile = path;
    d.nMaxFile = MAX_PATH;
    d.lpstrFilter = L"OzAnim (*.ozanim)\0*.ozanim\0All Files (*.*)\0*.*\0\0";
    d.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameW(&d)) return false;

    int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return false;
    std::vector<char> utf8((size_t)size);
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), size, nullptr, nullptr);
    outPath.assign(utf8.data());

    std::string s = outPath;
    for (auto& c : s) if (c == '\\') c = '/';
    size_t gd = s.find("GameData/");
    if (gd != std::string::npos) outPath = s.substr(gd);
    return true;
}

static void SetSkyboxField(HWND hwnd, const std::string& path) {
    std::wstring w(path.begin(), path.end());
    SetWindowTextW(GetDlgItem(hwnd, ID_SF_SKYBOX_PATH), w.c_str());
    g_zoneProps.skyboxTexturePath = path;
}

static LRESULT CALLBACK ZonePropertiesProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        int x = 8, y = 8, gap = 26;

        // Clear previous control groups
        for (int t = 0; t < 5; t++) g_zoneControlGroups[t].clear();

        // Tab buttons
        CreateButton(hwnd, L"Fog",       x, y, 70, 24, ID_ZONE_TAB_FOG);
        CreateButton(hwnd, L"Ambient",   x + 74, y, 70, 24, ID_ZONE_TAB_AMB);
        CreateButton(hwnd, L"GameType",  x + 148, y, 80, 24, ID_ZONE_TAB_GT);
        CreateButton(hwnd, L"Particles", x + 232, y, 80, 24, ID_ZONE_TAB_PAR);
        CreateButton(hwnd, L"Portals",   x + 316, y, 80, 24, ID_ZONE_TAB_POR);
        y += 30;
        // Every tab lays its controls out from the same origin; ShowZoneTab()
        // toggles visibility, so tabs would otherwise render below the window
        // (that is what made Particles/Portals look unimplemented).
        const int tabTop = y;

        auto addSliderToGroup = [&](int tabIdx, int id, const wchar_t* label, int minv, int maxv, int def) {
            CreateLabel(hwnd, label, x, y, 55, 20, id + 1000);
            HWND hSB = CreateWindowEx(0, L"SCROLLBAR", L"", WS_CHILD | WS_VISIBLE | SBS_HORZ,
                           x + 60, y, 200, 18, hwnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
            SetScrollRange(hSB, SB_CTL, minv, maxv, TRUE);
            SetScrollPos(hSB, SB_CTL, def, TRUE);
            g_zoneControlGroups[tabIdx].push_back(GetDlgItem(hwnd, id + 1000)); // label
            g_zoneControlGroups[tabIdx].push_back(hSB); // scrollbar
            y += gap;
        };

        // --- Fog tab (0) ---
        CreateLabel(hwnd, L"Fog Color:", x, y, 100, 18, 10);
        g_zoneControlGroups[0].push_back(GetDlgItem(hwnd, 10));
        y += 20;
        addSliderToGroup(0, ID_SB_FOG_R, L"R:", 0, 255, g_zoneProps.fogR);
        addSliderToGroup(0, ID_SB_FOG_G, L"G:", 0, 255, g_zoneProps.fogG);
        addSliderToGroup(0, ID_SB_FOG_B, L"B:", 0, 255, g_zoneProps.fogB);
        addSliderToGroup(0, ID_SB_FOG_DENSITY, L"Density:", 0, 200, (int)(g_zoneProps.fogDensity * 1000));
        y += 4;
        HWND hFogApply = CreateButton(hwnd, L"Apply Fog", x, y, 120, 26, ID_ZONE_APPLY_FOG);
        g_zoneControlGroups[0].push_back(hFogApply);
        y += 32;
        // Add fog start/end input fields that were missing
        CreateLabel(hwnd, L"Fog Start:", x, y, 70, 20, 11);
        HWND hFogStart = CreateCtrl(hwnd, L"EDIT", L"10", x + 75, y, 50, 20, ID_SF_FOG_START, WS_BORDER | ES_NUMBER);
        CreateLabel(hwnd, L"Fog End:", x + 135, y, 55, 20, 12);
        HWND hFogEnd = CreateCtrl(hwnd, L"EDIT", L"100", x + 195, y, 50, 20, ID_SF_FOG_END, WS_BORDER | ES_NUMBER);
        g_zoneControlGroups[0].push_back(GetDlgItem(hwnd, 11));
        g_zoneControlGroups[0].push_back(hFogStart);
        g_zoneControlGroups[0].push_back(GetDlgItem(hwnd, 12));
        g_zoneControlGroups[0].push_back(hFogEnd);
        y += 26;

        // Skybox texture path (on Fog tab for visual co-location)
        CreateLabel(hwnd, L"Skybox Tex:", x, y, 75, 20, 13);
        HWND hSkyboxPath = CreateCtrl(hwnd, L"EDIT",
            std::wstring(g_zoneProps.skyboxTexturePath.begin(), g_zoneProps.skyboxTexturePath.end()).c_str(),
            x + 78, y, 180, 20, ID_SF_SKYBOX_PATH, WS_BORDER);
        g_zoneControlGroups[0].push_back(GetDlgItem(hwnd, 13));
        g_zoneControlGroups[0].push_back(hSkyboxPath);
        y += 24;
        {
            HWND hBrowse = CreateButton(hwnd, L"Browse...", x, y, 90, 24, ID_SF_SKYBOX_BROWSE);
            HWND hActive = CreateButton(hwnd, L"Use Active Tex", x + 96, y, 110, 24, ID_SF_SKYBOX_ACTIVE);
            HWND hApplySky = CreateButton(hwnd, L"Apply Skybox", x + 212, y, 110, 24, ID_ZONE_APPLY_SKY);
            g_zoneControlGroups[0].push_back(hBrowse);
            g_zoneControlGroups[0].push_back(hActive);
            g_zoneControlGroups[0].push_back(hApplySky);
            y += 30;
        }

        // --- Ambient tab (1) ---
        y = tabTop;
        CreateLabel(hwnd, L"Ambient Color:", x, y, 100, 18, 20);
        g_zoneControlGroups[1].push_back(GetDlgItem(hwnd, 20));
        y += 20;
        addSliderToGroup(1, ID_SB_AMB_R, L"R:", 0, 255, g_zoneProps.ambR);
        addSliderToGroup(1, ID_SB_AMB_G, L"G:", 0, 255, g_zoneProps.ambG);
        addSliderToGroup(1, ID_SB_AMB_B, L"B:", 0, 255, g_zoneProps.ambB);
        addSliderToGroup(1, ID_SB_AMB_INT, L"Intensity:", 0, 100, (int)(g_zoneProps.ambIntensity * 100));
        y += 4;
        HWND hAmbApply = CreateButton(hwnd, L"Apply Ambient", x, y, 130, 26, ID_ZONE_APPLY_AMB);
        g_zoneControlGroups[1].push_back(hAmbApply);
        y += 32;

        // --- GameType tab (2) ---
        y = tabTop;
        CreateLabel(hwnd, L"Game Mode:", x, y, 80, 20, 30);
        g_zoneControlGroups[2].push_back(GetDlgItem(hwnd, 30));
        HWND hGT = CreateWindowEx(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                  x + 85, y, 180, 200, hwnd, (HMENU)(INT_PTR)ID_CMB_GAMETYPE, g_hInst, nullptr);
        const wchar_t* gameTypes[] = {
            L"Single Player", L"Coop", L"Etheral Match (DM)",
            L"Angel Team Game (TDM)", L"Angel Run", L"Capture The Orb", L"Time Shift"
        };
        for (auto& gt : gameTypes) SendMessage(hGT, CB_ADDSTRING, 0, (LPARAM)gt);
        SendMessage(hGT, CB_SETCURSEL, (int)g_zoneProps.gameType, 0);
        g_zoneControlGroups[2].push_back(hGT);
        y += 28;

        auto addInputToGroup = [&](int tabIdx, int id, const wchar_t* label, const wchar_t* def, int iw) {
            CreateLabel(hwnd, label, x, y, 80, 20, id + 2000);
            HWND hEdit = CreateCtrl(hwnd, L"EDIT", def, x + 85, y, iw, 20, id, WS_BORDER | ES_NUMBER);
            g_zoneControlGroups[tabIdx].push_back(GetDlgItem(hwnd, id + 2000));
            g_zoneControlGroups[tabIdx].push_back(hEdit);
            y += 26;
        };
        addInputToGroup(2, ID_SF_MAXPLAYERS, L"Max Players:", L"8", 40);
        addInputToGroup(2, ID_SF_RESPAWN, L"Respawn (s):", L"5", 40);
        addInputToGroup(2, ID_SF_TIMELIMIT, L"Time Limit:", L"10", 40);
        addInputToGroup(2, ID_SF_SCORELIMIT, L"Score Limit:", L"50", 40);
        HWND hTimeLimit = CreateCtrl(hwnd, L"BUTTON", L"Time Limit Enabled", x, y, 150, 22, ID_CHK_TIMELIMIT, BS_AUTOCHECKBOX);
        g_zoneControlGroups[2].push_back(hTimeLimit);
        y += 26;
        HWND hFriendly = CreateCtrl(hwnd, L"BUTTON", L"Friendly Fire", x, y, 120, 22, ID_CHK_FRIENDLY, BS_AUTOCHECKBOX);
        g_zoneControlGroups[2].push_back(hFriendly);
        y += 30;
        HWND hGTApply = CreateButton(hwnd, L"Apply GameType", x, y, 140, 26, ID_ZONE_APPLY_GT);
        g_zoneControlGroups[2].push_back(hGTApply);
        y += 32;

        // --- Particles tab (3) ---
        y = tabTop;
        CreateLabel(hwnd, L"Particle Type:", x, y, 85, 22, 40);
        g_zoneControlGroups[3].push_back(GetDlgItem(hwnd, 40));
        HWND hPT = CreateWindowEx(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                  x + 90, y, 170, 120, hwnd, (HMENU)(INT_PTR)ID_CMB_PARTICLETYPE, g_hInst, nullptr);
        const wchar_t* pTypes[] = { L"None", L"Snow", L"Rain", L"Void Realm", L"Psychic Realm" };
        for (auto& pt : pTypes) SendMessage(hPT, CB_ADDSTRING, 0, (LPARAM)pt);
        SendMessage(hPT, CB_SETCURSEL, (int)g_zoneProps.particleType, 0);
        g_zoneControlGroups[3].push_back(hPT);
        y += 28;

        addSliderToGroup(3, ID_SB_PAR_DENSITY, L"Density:", 0, 200, (int)g_zoneProps.particleDensity);
        addSliderToGroup(3, ID_SB_PAR_SPEED, L"Speed:", 0, 100, (int)(g_zoneProps.particleSpeed * 10));
        addSliderToGroup(3, ID_SB_PAR_R, L"Color R:", 0, 255, g_zoneProps.particleColorR);
        addSliderToGroup(3, ID_SB_PAR_G, L"Color G:", 0, 255, g_zoneProps.particleColorG);
        addSliderToGroup(3, ID_SB_PAR_B, L"Color B:", 0, 255, g_zoneProps.particleColorB);
        y += 4;
        HWND hParApply = CreateButton(hwnd, L"Apply Particles", x, y, 140, 26, ID_ZONE_APPLY_PAR);
        g_zoneControlGroups[3].push_back(hParApply);
        y += 32;

        // --- Portal tab (4) ---
        {
            y = tabTop;
            // Portal selector (existing portals in this world)
            CreateLabel(hwnd, L"Portal:", x, y, 55, 20, 50);
            g_zoneControlGroups[4].push_back(GetDlgItem(hwnd, 50));
            HWND hList = CreateWindowEx(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST,
                                        x + 60, y, 220, 200, hwnd, (HMENU)(INT_PTR)ID_CMB_PORTAL_LIST, g_hInst, nullptr);
            g_zoneControlGroups[4].push_back(hList);
            y += 28;

            // Target world dropdown
            CreateLabel(hwnd, L"To World:", x, y, 70, 20, 51);
            g_zoneControlGroups[4].push_back(GetDlgItem(hwnd, 51));
            HWND hWorld = CreateWindowEx(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST,
                                         x + 75, y, 205, 300, hwnd, (HMENU)(INT_PTR)ID_CMB_PORTAL_WORLD, g_hInst, nullptr);
            ScanAvailableWorlds();
            for (const auto& wname : g_availableWorlds) {
                std::wstring w(wname.begin(), wname.end());
                SendMessage(hWorld, CB_ADDSTRING, 0, (LPARAM)w.c_str());
            }
            if (!g_portalEdit.targetWorld[0] && !g_availableWorlds.empty()) {
                strncpy(g_portalEdit.targetWorld, g_availableWorlds[0].c_str(), 255);
                g_portalEdit.targetWorld[255] = 0;
                SendMessage(hWorld, CB_SETCURSEL, 0, 0);
            } else {
                std::wstring cur(g_portalEdit.targetWorld,
                                 g_portalEdit.targetWorld + strlen(g_portalEdit.targetWorld));
                int sel = (int)SendMessage(hWorld, CB_FINDSTRINGEXACT, -1, (LPARAM)cur.c_str());
                SendMessage(hWorld, CB_SETCURSEL, sel >= 0 ? sel : 0, 0);
            }
            g_zoneControlGroups[4].push_back(hWorld);
            y += 28;

            // Spawn point at destination
            auto addFloatInput = [&](int id, const wchar_t* label, float def, int lx, int ix) {
                CreateLabel(hwnd, label, x + lx, y, 20, 20, id + 3000);
                wchar_t buf[32];
                _snwprintf(buf, 31, L"%.2f", def); buf[31] = 0;
                HWND hEdit = CreateCtrl(hwnd, L"EDIT", buf, x + ix, y, 55, 20, id, WS_BORDER);
                g_zoneControlGroups[4].push_back(GetDlgItem(hwnd, id + 3000));
                g_zoneControlGroups[4].push_back(hEdit);
            };
            CreateLabel(hwnd, L"Spawn At:", x, y, 65, 20, 52);
            g_zoneControlGroups[4].push_back(GetDlgItem(hwnd, 52));
            addFloatInput(ID_SF_PORTAL_SX, L"X", g_portalEdit.spawnX, 68, 90);
            addFloatInput(ID_SF_PORTAL_SY, L"Y", g_portalEdit.spawnY, 152, 174);
            addFloatInput(ID_SF_PORTAL_SZ, L"Z", g_portalEdit.spawnZ, 236, 258);
            y += 26;

            // Bidirectional checkbox
            HWND hBidir = CreateCtrl(hwnd, L"BUTTON", L"Bidirectional", x, y, 120, 22,
                                     ID_CHK_PORTAL_BIDIR, BS_AUTOCHECKBOX);
            SendMessage(hBidir, BM_SETCHECK, g_portalEdit.bidirectional ? BST_CHECKED : BST_UNCHECKED, 0);
            g_zoneControlGroups[4].push_back(hBidir);
            y += 30;

            // Apply / Delete / Refresh row
            HWND hPorApply = CreateButton(hwnd, L"Apply Portal", x, y, 110, 26, ID_ZONE_APPLY_PORTAL);
            HWND hPorDel   = CreateButton(hwnd, L"Delete", x + 116, y, 70, 26, ID_ZONE_DEL_PORTAL);
            HWND hPorRef   = CreateButton(hwnd, L"Refresh", x + 192, y, 80, 26, ID_BTN_PORTAL_REFRESH);
            g_zoneControlGroups[4].push_back(hPorApply);
            g_zoneControlGroups[4].push_back(hPorDel);
            g_zoneControlGroups[4].push_back(hPorRef);
            y += 32;
        }

        // Close button (bottom of panel)
        CreateButton(hwnd, L"Close", 300, y, 90, 26, ID_ZONE_CLOSE);

        // Show first tab only
        ShowZoneTab(hwnd, 0);
        break;
    }
    case WM_HSCROLL: {
        // Only read sliders from the active tab to avoid cross-tab contamination
        auto getPos = [hwnd](int id) -> int {
            return (int)SendDlgItemMessage(hwnd, id, SBM_GETPOS, 0, 0);
        };
        if (g_zoneTab == 0) {
            g_zoneProps.fogR = getPos(ID_SB_FOG_R);
            g_zoneProps.fogG = getPos(ID_SB_FOG_G);
            g_zoneProps.fogB = getPos(ID_SB_FOG_B);
            g_zoneProps.fogDensity = getPos(ID_SB_FOG_DENSITY) / 1000.0f;
        } else if (g_zoneTab == 1) {
            g_zoneProps.ambR = getPos(ID_SB_AMB_R);
            g_zoneProps.ambG = getPos(ID_SB_AMB_G);
            g_zoneProps.ambB = getPos(ID_SB_AMB_B);
            g_zoneProps.ambIntensity = getPos(ID_SB_AMB_INT) / 100.0f;
        } else if (g_zoneTab == 3) {
            g_zoneProps.particleDensity = (float)getPos(ID_SB_PAR_DENSITY);
            g_zoneProps.particleSpeed = getPos(ID_SB_PAR_SPEED) / 10.0f;
            g_zoneProps.particleColorR = getPos(ID_SB_PAR_R);
            g_zoneProps.particleColorG = getPos(ID_SB_PAR_G);
            g_zoneProps.particleColorB = getPos(ID_SB_PAR_B);
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_ZONE_CLOSE) { ShowEnvPanel(false); break; }
        if (id == ID_ZONE_TAB_FOG) { ShowZoneTab(hwnd, 0); break; }
        if (id == ID_ZONE_TAB_AMB) { ShowZoneTab(hwnd, 1); break; }
        if (id == ID_ZONE_TAB_GT)  { ShowZoneTab(hwnd, 2); break; }
        if (id == ID_ZONE_TAB_PAR) { ShowZoneTab(hwnd, 3); break; }
        if (id == ID_ZONE_TAB_POR) { ShowZoneTab(hwnd, 4); break; }

        if (id == ID_ZONE_APPLY_FOG) {
            // Read fog start/end from edit fields
            auto readFloat = [hwnd](int id, float def) -> float {
                wchar_t buf[64];
                HWND h = GetDlgItem(hwnd, id);
                if (!h) return def;
                GetWindowTextW(h, buf, 64);
                return (float)wcstod(buf, nullptr);
            };
            g_zoneProps.fogStart = readFloat(ID_SF_FOG_START, 10.0f);
            g_zoneProps.fogEnd = readFloat(ID_SF_FOG_END, 100.0f);
            g_zoneProps.applyFog = true;
            break;
        }
        if (id == ID_SF_SKYBOX_BROWSE) {
            std::string p;
            if (ChooseSkyboxFile(p)) {
                SetSkyboxField(hwnd, p);
                g_zoneProps.applySkybox = true; // take effect immediately
            }
            break;
        }
        if (id == ID_SF_SKYBOX_ACTIVE) {
            if (g_editorPanels.activeTexturePath.empty()) {
                OZ_WARN("Zone Properties: no active texture in the Texture Manager");
                break;
            }
            std::string s = g_editorPanels.activeTexturePath;
            for (auto& c : s) if (c == '\\') c = '/';
            size_t gd = s.find("GameData/");
            if (gd != std::string::npos) s = s.substr(gd);
            SetSkyboxField(hwnd, s);
            g_zoneProps.applySkybox = true;
            break;
        }
        if (id == ID_ZONE_APPLY_SKY) {
            // Fall through to the generic field reader below so the typed path
            // is captured, then take effect via applySkybox.
            g_zoneProps.applySkybox = true;
        }
        if (id == ID_ZONE_APPLY_AMB) {
            g_zoneProps.applyAmbient = true;
            break;
        }
        if (id == ID_ZONE_APPLY_PAR) {
            g_zoneProps.applyParticles = true;
            break;
        }
        if (id == ID_ZONE_APPLY_GT) {
            g_zoneProps.applyGameType = true;
            break;
        }
        if (id == ID_CMB_PORTAL_LIST && HIWORD(w) == CBN_SELCHANGE) {
            int sel = (int)SendMessage(GetDlgItem(hwnd, ID_CMB_PORTAL_LIST), CB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                LoadPortalIntoEditor(sel);
                // Refresh field values
                ScanAvailableWorlds();
                HWND hWorld = GetDlgItem(hwnd, ID_CMB_PORTAL_WORLD);
                std::wstring cur(g_portalEdit.targetWorld,
                                 g_portalEdit.targetWorld + strlen(g_portalEdit.targetWorld));
                int wsel = (int)SendMessage(hWorld, CB_FINDSTRINGEXACT, -1, (LPARAM)cur.c_str());
                SendMessage(hWorld, CB_SETCURSEL, wsel >= 0 ? wsel : 0, 0);
                auto setFloat = [&](int fid, float v) {
                    wchar_t b[32];
                    _snwprintf(b, 31, L"%.2f", v); b[31] = 0;
                    SetWindowTextW(GetDlgItem(hwnd, fid), b);
                };
                setFloat(ID_SF_PORTAL_SX, g_portalEdit.spawnX);
                setFloat(ID_SF_PORTAL_SY, g_portalEdit.spawnY);
                setFloat(ID_SF_PORTAL_SZ, g_portalEdit.spawnZ);
                SendMessage(GetDlgItem(hwnd, ID_CHK_PORTAL_BIDIR), BM_SETCHECK,
                            g_portalEdit.bidirectional ? BST_CHECKED : BST_UNCHECKED, 0);
                g_editorPanels.actionSelectPortal = sel; // Main.cpp syncs viewport selection
            }
            break;
        }
        if (id == ID_CMB_PORTAL_WORLD && HIWORD(w) == CBN_SELCHANGE) {
            int sel = (int)SendMessage(GetDlgItem(hwnd, ID_CMB_PORTAL_WORLD), CB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_availableWorlds.size()) {
                strncpy(g_portalEdit.targetWorld, g_availableWorlds[sel].c_str(), 255);
                g_portalEdit.targetWorld[255] = 0;
            }
            break;
        }
        if (id == ID_CHK_PORTAL_BIDIR) {
            g_portalEdit.bidirectional =
                (int)SendMessage(GetDlgItem(hwnd, ID_CHK_PORTAL_BIDIR), BM_GETCHECK, 0, 0) != 0;
            break;
        }
        if (id == ID_BTN_PORTAL_REFRESH) {
            RefreshPortalList();
            break;
        }
        if (id == ID_ZONE_APPLY_PORTAL) {
            // Read spawn fields + world selection into edit state, flag apply
            auto readF = [hwnd](int fid, float def) -> float {
                wchar_t b[64] = {0};
                HWND h = GetDlgItem(hwnd, fid);
                if (!h) return def;
                GetWindowTextW(h, b, 64);
                return (float)wcstod(b, nullptr);
            };
            g_portalEdit.spawnX = readF(ID_SF_PORTAL_SX, 0);
            g_portalEdit.spawnY = readF(ID_SF_PORTAL_SY, 20);
            g_portalEdit.spawnZ = readF(ID_SF_PORTAL_SZ, 0);
            int wsel = (int)SendMessage(GetDlgItem(hwnd, ID_CMB_PORTAL_WORLD), CB_GETCURSEL, 0, 0);
            if (wsel >= 0 && wsel < (int)g_availableWorlds.size()) {
                strncpy(g_portalEdit.targetWorld, g_availableWorlds[wsel].c_str(), 255);
                g_portalEdit.targetWorld[255] = 0;
            }
            g_editorPanels.actionApplyPortal = g_portalEdit.selectedIndex;
            break;
        }
        if (id == ID_ZONE_DEL_PORTAL) {
            if (g_portalEdit.selectedIndex >= 0)
                g_editorPanels.actionDeletePortal = g_portalEdit.selectedIndex;
            break;
        }
        if (id == ID_CMB_GAMETYPE) {
            int sel = (int)SendMessage(GetDlgItem(hwnd, ID_CMB_GAMETYPE), CB_GETCURSEL, 0, 0);
            if (sel >= 0) g_zoneProps.gameType = (GameType)sel;
            break;
        }
        if (id == ID_CMB_PARTICLETYPE) {
            int sel = (int)SendMessage(GetDlgItem(hwnd, ID_CMB_PARTICLETYPE), CB_GETCURSEL, 0, 0);
            if (sel >= 0) g_zoneProps.particleType = (ParticleType)sel;
            break;
        }
        if (id == ID_CHK_TIMELIMIT) {
            g_zoneProps.timeLimitEnabled = (int)SendMessage(GetDlgItem(hwnd, ID_CHK_TIMELIMIT), BM_GETCHECK, 0, 0) != 0;
            break;
        }
        if (id == ID_CHK_FRIENDLY) {
            g_zoneProps.friendlyFire = (int)SendMessage(GetDlgItem(hwnd, ID_CHK_FRIENDLY), BM_GETCHECK, 0, 0) != 0;
            break;
        }
        // Read input fields
        auto readFloat = [hwnd](int id, float def) -> float {
            wchar_t buf[64];
            HWND h = GetDlgItem(hwnd, id);
            if (!h) return def;
            GetWindowTextW(h, buf, 64);
            return (float)wcstod(buf, nullptr);
        };
        g_zoneProps.maxPlayers = (int)readFloat(ID_SF_MAXPLAYERS, 8);
        g_zoneProps.respawnTime = readFloat(ID_SF_RESPAWN, 5);
        g_zoneProps.timeLimitMinutes = readFloat(ID_SF_TIMELIMIT, 10);
        g_zoneProps.scoreLimit = (int)readFloat(ID_SF_SCORELIMIT, 50);

        // Read skybox texture path
        {
            wchar_t wbuf[512] = {0};
            HWND hSky = GetDlgItem(hwnd, ID_SF_SKYBOX_PATH);
            if (hSky) {
                GetWindowTextW(hSky, wbuf, 512);
                int len = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, nullptr, 0, nullptr, nullptr);
                if (len > 0) {
                    std::string mbuf((size_t)len, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, &mbuf[0], len, nullptr, nullptr);
                    g_zoneProps.skyboxTexturePath = mbuf.c_str();
                }
            }
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
// Pickup Panel — dynamically generated from LightningScript entity registry
// =====================================================================
static const int ID_PICK_CLOSE   = 100;
static const int ID_PICKUP_BASE  = 101;

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
        std::vector<const EntityDef*> pickupDefs;
        LightningEntityRegistry::Instance().FindByType(EntityType::PICKUP, pickupDefs);
        int y = 35;
        for (size_t i = 0; i < pickupDefs.size(); i++, y += 28) {
            std::wstring label(pickupDefs[i]->name.begin(), pickupDefs[i]->name.end());
            CreateButton(hwnd, label.c_str(), 10, y, 160, 24, ID_PICKUP_BASE + (int)i);
        }
        CreateButton(hwnd, L"Close", 70, y + 8, 100, 28, ID_PICK_CLOSE);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_PICK_CLOSE) ShowPickupPanel(false);
        else if (id >= ID_PICKUP_BASE) {
            int type = id - ID_PICKUP_BASE;
            g_lastPickupType = type;
            g_editorPanels.actionPickupType = type;
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
static const int ID_NODE_PORTAL= 105;

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
        CreateButton(hwnd, L"Level Portal", 10, 147, 160, 24, ID_NODE_PORTAL);
        CreateButton(hwnd, L"Close", 50, 179, 100, 28, ID_NODE_CLOSE);
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
            else if (id == ID_NODE_PORTAL) type = 4;
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
// Heightmap Editor Load, configure, and preview terrain heightmaps
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
        CreateIconButton(hwnd, L"Browse", x + lw + ew + gap, y, bw, rowH, ID_HM_BROWSE_IMG, "BBSheet");
        y += rowH + gap;

        CreateLabel(hwnd, L"Texture Path:", x, y, lw, rowH, 3);
        CreateCtrl(hwnd, L"EDIT", L"", x + lw, y, ew, rowH, ID_HM_TEX, WS_BORDER | ES_AUTOHSCROLL);
        CreateIconButton(hwnd, L"Browse", x + lw + ew + gap, y, bw, rowH, ID_HM_BROWSE_TEX, "BBSheet");
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

        CreateIconButton(hwnd, L"Generate", x, y, 120, 30, ID_HM_GENERATE, "BBTerrain");
        CreateButton(hwnd, L"Close", x + 130, y, 100, 30, ID_HM_CLOSE);
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)l;
        if (dis && dis->CtlType == ODT_BUTTON) return DrawIconButton(dis);
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
            char imgPathA[512], texPathA[512];
            if (hImg) { GetWindowTextW(hImg, g_imgPath, 512); WideCharToMultiByte(CP_UTF8, 0, g_imgPath, -1, imgPathA, 512, 0, 0); }
            if (hTex) { GetWindowTextW(hTex, g_texPath, 512); WideCharToMultiByte(CP_UTF8, 0, g_texPath, -1, texPathA, 512, 0, 0); }
            double px=0,py=0,pz=0,sx=100,sy=50,sz=100,sc=1.0;
            if (hPX) { GetWindowTextW(hPX, buf, 256); px = wcstod(buf, nullptr); }
            if (hPY) { GetWindowTextW(hPY, buf, 256); py = wcstod(buf, nullptr); }
            if (hPZ) { GetWindowTextW(hPZ, buf, 256); pz = wcstod(buf, nullptr); }
            if (hSX) { GetWindowTextW(hSX, buf, 256); sx = wcstod(buf, nullptr); }
            if (hSY) { GetWindowTextW(hSY, buf, 256); sy = wcstod(buf, nullptr); }
            if (hSZ) { GetWindowTextW(hSZ, buf, 256); sz = wcstod(buf, nullptr); }
            if (hSC) { GetWindowTextW(hSC, buf, 256); sc = wcstod(buf, nullptr); }
            // Store to state that main loop reads to call BuildHeightmap
            g_editorPanels.actionHeightmapImage = imgPathA;
            g_editorPanels.actionHeightmapTexture = texPathA;
            g_editorPanels.actionHmPosX = (float)px;
            g_editorPanels.actionHmPosY = (float)py;
            g_editorPanels.actionHmPosZ = (float)pz;
            g_editorPanels.actionHmSx = (float)sx;
            g_editorPanels.actionHmSy = (float)sy;
            g_editorPanels.actionHmSz = (float)sz;
            g_editorPanels.actionHmScale = (float)sc;
            g_editorPanels.actionGenerateHeightmap = true;
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
// Light Properties panel — color, type, effect, flare, corona
// =====================================================================
static const int ID_LP_CLOSE  = 400;
static const int ID_LP_APPLY  = 401;
static const int ID_LP_R      = 402;
static const int ID_LP_G      = 403;
static const int ID_LP_B      = 404;
static const int ID_LP_INTENS = 405;
static const int ID_LP_RADIUS = 406;
static const int ID_LP_TYPE   = 407;
static const int ID_LP_EFFECT = 408;
static const int ID_LP_FLARE  = 409;
static const int ID_LP_CORONA = 410;
static const int ID_LP_INNER  = 411;
static const int ID_LP_OUTER  = 412;

void ShowLightProps(bool show) {
    g_editorPanels.showLightProps = show;
    if (show && g_editorPanels.hLightProps) {
        // Set target from current selection if a light is selected
        int selType = Editor_GetSelectedType();
        int selIdx  = Editor_GetSelectedIndex();
        if (selType == 5 && selIdx >= 0) { // SelType::LIGHT = 5
            g_editorPanels.lightPropTarget = selIdx;
        }
        // Populate controls from the target light
        SendMessage((HWND)g_editorPanels.hLightProps, WM_USER + 50, 0, 0);
        ShowWindow((HWND)g_editorPanels.hLightProps, SW_SHOW);
        SetForegroundWindow((HWND)g_editorPanels.hLightProps);
    } else if (g_editorPanels.hLightProps) {
        ShowWindow((HWND)g_editorPanels.hLightProps, SW_HIDE);
    }
}

static LRESULT CALLBACK LightPropsProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        int x = 10, y = 10, gap = 26;
        CreateLabel(hwnd, L"Light Properties", x, y, 200, 20, 1); y += 26;

        auto addSlider = [&](int id, const wchar_t* label, int minv, int maxv, int def) {
            CreateLabel(hwnd, label, x, y, 70, 20, id + 1000);
            CreateWindowEx(0, L"SCROLLBAR", L"", WS_CHILD | WS_VISIBLE | SBS_HORZ,
                x + 75, y, 180, 18, hwnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
            SetScrollRange(GetDlgItem(hwnd, id), SB_CTL, minv, maxv, TRUE);
            SetScrollPos(GetDlgItem(hwnd, id), SB_CTL, def, TRUE);
            y += gap;
        };

        addSlider(ID_LP_R, L"Red:", 0, 255, 255);
        addSlider(ID_LP_G, L"Green:", 0, 255, 255);
        addSlider(ID_LP_B, L"Blue:", 0, 255, 255);
        addSlider(ID_LP_INTENS, L"Intensity:", 0, 100, 100);
        addSlider(ID_LP_RADIUS, L"Radius:", 1, 200, 50);
        y += 4;

        // Light type combo
        CreateLabel(hwnd, L"Type:", x, y, 50, 20, 100);
        HWND hType = CreateWindowEx(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            x + 55, y, 150, 100, hwnd, (HMENU)(INT_PTR)ID_LP_TYPE, g_hInst, nullptr);
        SendMessage(hType, CB_ADDSTRING, 0, (LPARAM)L"Directional");
        SendMessage(hType, CB_ADDSTRING, 0, (LPARAM)L"Point");
        SendMessage(hType, CB_ADDSTRING, 0, (LPARAM)L"Spot");
        SendMessage(hType, CB_SETCURSEL, 1, 0);
        y += 28;

        // Effect combo
        CreateLabel(hwnd, L"Effect:", x, y, 50, 20, 101);
        HWND hEff = CreateWindowEx(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
            x + 55, y, 150, 100, hwnd, (HMENU)(INT_PTR)ID_LP_EFFECT, g_hInst, nullptr);
        const wchar_t* effects[] = { L"None", L"Watery", L"Torch", L"Fire", L"Lamp" };
        for (auto& e : effects) SendMessage(hEff, CB_ADDSTRING, 0, (LPARAM)e);
        SendMessage(hEff, CB_SETCURSEL, 0, 0);
        y += 28;

        // Spot light angles
        CreateLabel(hwnd, L"Inner Angle:", x, y, 80, 20, 102);
        CreateCtrl(hwnd, L"EDIT", L"15", x + 85, y, 50, 20, ID_LP_INNER, WS_BORDER);
        y += 26;
        CreateLabel(hwnd, L"Outer Angle:", x, y, 80, 20, 103);
        CreateCtrl(hwnd, L"EDIT", L"45", x + 85, y, 50, 20, ID_LP_OUTER, WS_BORDER);
        y += 30;

        // Flare/Corona checkboxes
        CreateCtrl(hwnd, L"BUTTON", L"Lens Flare", x, y, 120, 22, ID_LP_FLARE, BS_AUTOCHECKBOX);
        y += 26;
        CreateCtrl(hwnd, L"BUTTON", L"Corona", x, y, 100, 22, ID_LP_CORONA, BS_AUTOCHECKBOX);
        y += 34;

        CreateButton(hwnd, L"Apply", x, y, 80, 26, ID_LP_APPLY);
        CreateButton(hwnd, L"Close", x + 90, y, 80, 26, ID_LP_CLOSE);
        break;
    }
    case WM_USER + 50: {
        // Populate controls from the target light node
        auto& lights = PawnSystem::Instance().GetLights();
        int idx = g_editorPanels.lightPropTarget;
        if (idx >= 0 && idx < (int)lights.size()) {
            LightNode& ln = lights[idx];
            SetScrollPos(GetDlgItem(hwnd, ID_LP_R), SB_CTL, ln.color.r, TRUE);
            SetScrollPos(GetDlgItem(hwnd, ID_LP_G), SB_CTL, ln.color.g, TRUE);
            SetScrollPos(GetDlgItem(hwnd, ID_LP_B), SB_CTL, ln.color.b, TRUE);
            SetScrollPos(GetDlgItem(hwnd, ID_LP_INTENS), SB_CTL, (int)(ln.intensity * 100), TRUE);
            SetScrollPos(GetDlgItem(hwnd, ID_LP_RADIUS), SB_CTL, (int)ln.radius, TRUE);
            SendMessage(GetDlgItem(hwnd, ID_LP_TYPE), CB_SETCURSEL, (int)ln.type, 0);
            SendMessage(GetDlgItem(hwnd, ID_LP_EFFECT), CB_SETCURSEL, (int)ln.effect, 0);
            // Recompute angle from cosine
            float innerDeg = acosf(ln.innerCone) * RAD2DEG;
            float outerDeg = acosf(ln.outerCone) * RAD2DEG;
            SetWindowTextW(GetDlgItem(hwnd, ID_LP_INNER), std::to_wstring(innerDeg).c_str());
            SetWindowTextW(GetDlgItem(hwnd, ID_LP_OUTER), std::to_wstring(outerDeg).c_str());
            SendMessage(GetDlgItem(hwnd, ID_LP_FLARE), BM_SETCHECK, ln.flare ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessage(GetDlgItem(hwnd, ID_LP_CORONA), BM_SETCHECK, 0, 0); // reserved
        }
        g_editorPanels.lightColorR = (float)SendDlgItemMessage(hwnd, ID_LP_R, SBM_GETPOS, 0, 0);
        g_editorPanels.lightColorG = (float)SendDlgItemMessage(hwnd, ID_LP_G, SBM_GETPOS, 0, 0);
        g_editorPanels.lightColorB = (float)SendDlgItemMessage(hwnd, ID_LP_B, SBM_GETPOS, 0, 0);
        g_editorPanels.lightIntensity = SendDlgItemMessage(hwnd, ID_LP_INTENS, SBM_GETPOS, 0, 0) / 100.0f;
        g_editorPanels.lightRadius = (float)SendDlgItemMessage(hwnd, ID_LP_RADIUS, SBM_GETPOS, 0, 0);
        break;
    }
    case WM_HSCROLL: {
        g_editorPanels.lightColorR = (float)SendDlgItemMessage(hwnd, ID_LP_R, SBM_GETPOS, 0, 0);
        g_editorPanels.lightColorG = (float)SendDlgItemMessage(hwnd, ID_LP_G, SBM_GETPOS, 0, 0);
        g_editorPanels.lightColorB = (float)SendDlgItemMessage(hwnd, ID_LP_B, SBM_GETPOS, 0, 0);
        g_editorPanels.lightIntensity = SendDlgItemMessage(hwnd, ID_LP_INTENS, SBM_GETPOS, 0, 0) / 100.0f;
        g_editorPanels.lightRadius = (float)SendDlgItemMessage(hwnd, ID_LP_RADIUS, SBM_GETPOS, 0, 0);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_LP_CLOSE) { ShowLightProps(false); break; }
        if (id == ID_LP_APPLY) {
            g_editorPanels.lightType = (int)SendMessage(GetDlgItem(hwnd, ID_LP_TYPE), CB_GETCURSEL, 0, 0);
            g_editorPanels.lightEffect = (int)SendMessage(GetDlgItem(hwnd, ID_LP_EFFECT), CB_GETCURSEL, 0, 0);
            g_editorPanels.lightFlare = SendMessage(GetDlgItem(hwnd, ID_LP_FLARE), BM_GETCHECK, 0, 0) != 0;
            g_editorPanels.lightCorona = SendMessage(GetDlgItem(hwnd, ID_LP_CORONA), BM_GETCHECK, 0, 0) != 0;
            // Read spot angles
            auto readEditFloat = [hwnd](int id, float def) -> float {
                wchar_t buf[64];
                HWND h = GetDlgItem(hwnd, id);
                if (!h) return def;
                GetWindowTextW(h, buf, 64);
                return (float)wcstod(buf, nullptr);
            };
            g_editorPanels.lightInnerAngle = readEditFloat(ID_LP_INNER, 15.0f);
            g_editorPanels.lightOuterAngle = readEditFloat(ID_LP_OUTER, 45.0f);
            g_editorPanels.actionApplyLight = true;
        }
        break;
    }
    case WM_CLOSE: ShowLightProps(false); break;
    case WM_DESTROY: g_editorPanels.hLightProps = nullptr; break;
    default: return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

// =====================================================================
// WorldGraph Explorer - ListView of all world entities
// =====================================================================
static const int ID_WG_LIST    = 301;
static const int ID_WG_REFRESH = 302;
static const int ID_WG_CLOSE   = 303;

struct WorldGraphEntry {
    std::string typeLabel;
    std::string name;
    float posX, posY, posZ;
    float rotation;
    int selType;   // SelType encoded as int
    int selIndex;
};

static std::vector<WorldGraphEntry> g_worldGraphEntries;

static void BuildWorldGraphEntries() {
    g_worldGraphEntries.clear();

    // Brushes from OzoneLoader collision volumes
    {
        auto& vols = OzoneLoader::Instance().GetCollisionVolumes();
        for (size_t i = 0; i < vols.size(); i++) {
            WorldGraphEntry e;
            e.typeLabel = "Brush";
            e.name = TextFormat("Brush %zu", i);
            e.posX = (vols[i].aabb.min.x + vols[i].aabb.max.x) * 0.5f;
            e.posY = (vols[i].aabb.min.y + vols[i].aabb.max.y) * 0.5f;
            e.posZ = (vols[i].aabb.min.z + vols[i].aabb.max.z) * 0.5f;
            e.rotation = 0;
            e.selType = 1; // SelType::BRUSH
            e.selIndex = (int)i;
            g_worldGraphEntries.push_back(e);
        }
    }

    // WDL Models
    {
        int count = WorldGraph_GetModelCount();
        for (int i = 0; i < count; i++) {
            const char* name = WorldGraph_GetModelName(i);
            float x, y, z, r, s;
            WorldGraph_GetModelData(i, x, y, z, r, s);
            WorldGraphEntry e;
            e.typeLabel = "Model";
            e.name = name ? name : "Model";
            e.posX = x; e.posY = y; e.posZ = z;
            e.rotation = r;
            e.selType = 2; // SelType::MODEL
            e.selIndex = i;
            g_worldGraphEntries.push_back(e);
        }
    }

    // Pawns (NPCs)
    {
        auto& pawns = PawnSystem::Instance().GetPawns();
        for (auto& p : pawns) {
            if (!p.active) continue;
            WorldGraphEntry e;
            e.typeLabel = "NPC";
            e.name = p.defName;
            e.posX = p.position.x; e.posY = p.position.y; e.posZ = p.position.z;
            e.rotation = p.yaw;
            e.selType = 3; // SelType::NPC
            e.selIndex = (int)p.id;
            g_worldGraphEntries.push_back(e);
        }
    }

    // Pickups
    {
        auto& pickups = PawnSystem::Instance().GetPickups();
        for (auto& pk : pickups) {
            if (!pk.active) continue;
            WorldGraphEntry e;
            e.typeLabel = "Pickup";
            e.name = pk.typeName;
            e.posX = pk.position.x; e.posY = pk.position.y; e.posZ = pk.position.z;
            e.rotation = 0;
            e.selType = 4; // SelType::PICKUP
            e.selIndex = (int)pk.id;
            g_worldGraphEntries.push_back(e);
        }
    }

    // Lights
    {
        auto& lights = PawnSystem::Instance().GetLights();
        for (auto& l : lights) {
            if (!l.active) continue;
            WorldGraphEntry e;
            e.typeLabel = "Light";
            e.name = l.name.empty() ? "Light" : l.name;
            e.posX = l.position.x; e.posY = l.position.y; e.posZ = l.position.z;
            e.rotation = 0;
            e.selType = 5; // SelType::LIGHT
            e.selIndex = (int)l.id;
            g_worldGraphEntries.push_back(e);
        }
    }

    // Zones
    {
        auto& zones = PawnSystem::Instance().GetZones();
        for (auto& z : zones) {
            WorldGraphEntry e;
            e.typeLabel = "Zone";
            e.name = z.name.empty() ? "ZoneVolume" : z.name;
            e.posX = (z.bounds.min.x + z.bounds.max.x) * 0.5f;
            e.posY = (z.bounds.min.y + z.bounds.max.y) * 0.5f;
            e.posZ = (z.bounds.min.z + z.bounds.max.z) * 0.5f;
            e.rotation = 0;
            e.selType = 6; // SelType::ZONE
            e.selIndex = (int)z.id;
            g_worldGraphEntries.push_back(e);
        }
    }

    // Portals (level connections)
    {
        auto& portals = PawnSystem::Instance().GetPortals();
        for (size_t p = 0; p < portals.size(); p++) {
            auto& portal = portals[p];
            WorldGraphEntry e;
            e.typeLabel = "Portal";
            e.name = portal.targetWorld.empty() ? "<unassigned>" : portal.targetWorld;
            e.posX = (portal.bounds.min.x + portal.bounds.max.x) * 0.5f;
            e.posY = (portal.bounds.min.y + portal.bounds.max.y) * 0.5f;
            e.posZ = (portal.bounds.min.z + portal.bounds.max.z) * 0.5f;
            e.rotation = 0;
            e.selType = 8; // SelType::PORTAL
            e.selIndex = (int)p;
            g_worldGraphEntries.push_back(e);
        }
    }

    // Player Starts
    {
        auto& starts = PawnSystem::Instance().GetPlayerStarts();
        for (auto& s : starts) {
            WorldGraphEntry e;
            e.typeLabel = "Spawn";
            e.name = "PlayerStart";
            e.posX = s.position.x; e.posY = s.position.y; e.posZ = s.position.z;
            e.rotation = s.yaw;
            e.selType = 7; // SelType::SPAWN
            e.selIndex = (int)s.id;
            g_worldGraphEntries.push_back(e);
        }
    }

    // Emitters
    {
        auto& emitters = PawnSystem::Instance().GetEmitters();
        for (auto& em : emitters) {
            WorldGraphEntry e;
            e.typeLabel = (em.type == EmitterType::SOUND) ? "SoundEmitter" : "MusicEmitter";
            e.name = "Emitter";
            e.posX = em.position.x; e.posY = em.position.y; e.posZ = em.position.z;
            e.rotation = 0;
            e.selType = 0; // SelType::NONE - emitters not directly selectable
            e.selIndex = (int)em.id;
            g_worldGraphEntries.push_back(e);
        }
    }
}

static void PopulateWorldGraphList(HWND hList) {
    ListView_DeleteAllItems(hList);
    for (size_t i = 0; i < g_worldGraphEntries.size(); i++) {
        auto& e = g_worldGraphEntries[i];

        std::wstring wtype(e.typeLabel.begin(), e.typeLabel.end());
        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT | LVIF_PARAM;
        lvi.iItem = (int)i;
        lvi.lParam = i;
        lvi.pszText = const_cast<wchar_t*>(wtype.c_str());
        ListView_InsertItem(hList, &lvi);

        std::wstring wname(e.name.begin(), e.name.end());
        ListView_SetItemText(hList, (int)i, 1, const_cast<wchar_t*>(wname.c_str()));

        wchar_t wbuf[32];
        swprintf(wbuf, 32, L"%.1f", e.posX);
        ListView_SetItemText(hList, (int)i, 2, wbuf);
        swprintf(wbuf, 32, L"%.1f", e.posY);
        ListView_SetItemText(hList, (int)i, 3, wbuf);
        swprintf(wbuf, 32, L"%.1f", e.posZ);
        ListView_SetItemText(hList, (int)i, 4, wbuf);
        swprintf(wbuf, 32, L"%.1f", e.rotation);
        ListView_SetItemText(hList, (int)i, 5, wbuf);
    }
}

static LRESULT CALLBACK WorldGraphProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList;
    switch (msg) {
    case WM_CREATE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int bw = 80, margin = 6;
        int listH = rc.bottom - bw - margin * 3;

        hList = CreateWindowEx(0, WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
            margin, margin, rc.right - margin * 2, listH,
            hwnd, (HMENU)ID_WG_LIST, g_hInst, nullptr);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        // Columns: Type, Name, PosX, PosY, PosZ, Rot
        LVCOLUMNW lvc = {};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt = LVCFMT_LEFT;
        const wchar_t* headers[] = {L"Type", L"Name", L"PosX", L"PosY", L"PosZ", L"Rot"};
        int widths[] = {80, 140, 70, 70, 70, 60};
        for (int i = 0; i < 6; i++) {
            lvc.cx = widths[i];
            lvc.pszText = const_cast<wchar_t*>(headers[i]);
            ListView_InsertColumn(hList, i, &lvc);
        }

        CreateButton(hwnd, L"Refresh", margin, listH + margin * 2, bw, 26, ID_WG_REFRESH);
        CreateButton(hwnd, L"Close", rc.right - bw - margin, listH + margin * 2, bw, 26, ID_WG_CLOSE);

        BuildWorldGraphEntries();
        PopulateWorldGraphList(hList);
        break;
    }
    case WM_USER + 50: {
        BuildWorldGraphEntries();
        PopulateWorldGraphList(hList);
        break;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)l;
        if (nm->idFrom == ID_WG_LIST && nm->code == NM_DBLCLK) {
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < (int)g_worldGraphEntries.size()) {
                auto& e = g_worldGraphEntries[sel];
                g_editorPanels.actionSelectFromGraph = e.selIndex;
                g_editorPanels.actionSelectFromGraphType = e.selType;
                g_editorPanels.actionSelectFromGraphName = e.name;
                g_editorPanels.actionSelectFromGraphPos[0] = e.posX;
                g_editorPanels.actionSelectFromGraphPos[1] = e.posY;
                g_editorPanels.actionSelectFromGraphPos[2] = e.posZ;
            }
        }
        if (nm->idFrom == ID_WG_LIST && nm->code == NM_RCLICK) {
            // Right-click context menu
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < (int)g_worldGraphEntries.size()) {
                auto& e = g_worldGraphEntries[sel];
                // First select the entity (same as double-click)
                g_editorPanels.actionSelectFromGraph = e.selIndex;
                g_editorPanels.actionSelectFromGraphType = e.selType;
                g_editorPanels.actionSelectFromGraphName = e.name;
                g_editorPanels.actionSelectFromGraphPos[0] = e.posX;
                g_editorPanels.actionSelectFromGraphPos[1] = e.posY;
                g_editorPanels.actionSelectFromGraphPos[2] = e.posZ;
                // Then show popup menu
                HMENU hMenu = CreatePopupMenu();
                AppendMenuA(hMenu, MF_STRING, 1001, "Properties");
                AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenuA(hMenu, MF_STRING, 1002, "Delete");
                AppendMenuA(hMenu, MF_STRING, 1003, "Duplicate");
                POINT pt;
                GetCursorPos(&pt);
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
                if (cmd == 1001) {
                    g_editorPanels.actionWorldGraphProperties = sel;
                } else if (cmd == 1002) {
                    g_editorPanels.actionWorldGraphDelete = e.selIndex;
                } else if (cmd == 1003) {
                    g_editorPanels.actionWorldGraphDup = e.selIndex;
                }
            }
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_WG_CLOSE) ShowWorldGraph(false);
        if (id == ID_WG_REFRESH) {
            BuildWorldGraphEntries();
            PopulateWorldGraphList(hList);
        }
        break;
    }
    case WM_CLOSE: ShowWorldGraph(false); break;
    case WM_DESTROY: g_editorPanels.hWorldGraph = nullptr; break;
    default: return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

void ShowWorldGraph(bool show) {
    g_editorPanels.showWorldGraph = show;
    if (g_editorPanels.hWorldGraph)
        ShowWindow((HWND)g_editorPanels.hWorldGraph, show ? SW_SHOW : SW_HIDE);
}

void RefreshWorldGraph() {
    if (g_editorPanels.hWorldGraph)
        SendMessage((HWND)g_editorPanels.hWorldGraph, WM_USER + 50, 0, 0);
}

// =====================================================================
// LevelList / Campaign panel — worlds + portal connections
// =====================================================================
static const int ID_LL_LIST   = 500;
static const int ID_LL_OPEN   = 501;
static const int ID_LL_LINK   = 502;
static const int ID_LL_REFRESH= 503;
static const int ID_LL_CLOSE  = 504;

struct LevelListEntry {
    std::string world;
    std::string format;      // "WDL" or "OZONE"
    int portalsOut = 0;
    int portalsIn = 0;
    bool isCurrent = false;
};

static std::vector<LevelListEntry> g_levelList;

// Extract portal target-world names from a world file (light text scan)
static void ScanPortalTargets(const fs::path& worldFile, std::vector<std::string>& out) {
    out.clear();
    std::ifstream f(worldFile);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        // trim leading whitespace
        size_t s = line.find_first_not_of(" \t\r\n");
        if (s == std::string::npos) continue;
        line = line.substr(s);
        if (line.rfind("Portal:", 0) == 0) {                    // WDL
            size_t c1 = line.find(':', 7);
            size_t c2 = (c1 != std::string::npos) ? line.find(':', c1 + 1) : std::string::npos;
            if (c1 != std::string::npos && c2 != std::string::npos)
                out.push_back(line.substr(c1 + 1, c2 - c1 - 1));
        } else if (line.rfind("portal ", 0) == 0) {             // OZONE
            size_t sp = line.find(' ', 7);
            if (sp != std::string::npos)
                out.push_back(line.substr(7, sp - 7));
        }
    }
}

static void BuildLevelList() {
    g_levelList.clear();
    ScanAvailableWorlds();
    extern std::string Editor_GetCurrentWorldName();
    std::string current = Editor_GetCurrentWorldName();

    // First pass: outgoing portal counts
    std::vector<std::vector<std::string>> targets(g_availableWorlds.size());
    for (size_t i = 0; i < g_availableWorlds.size(); i++) {
        LevelListEntry e;
        e.world = g_availableWorlds[i];
        e.isCurrent = (e.world == current);
        fs::path ozone = fs::path("GameData/Worlds") / e.world / "World.ozone";
        if (!fs::exists(ozone)) ozone = fs::path("../GameData/Worlds") / e.world / "World.ozone";
        if (fs::exists(ozone)) { e.format = "OZONE"; ScanPortalTargets(ozone, targets[i]); }
        e.portalsOut = (int)targets[i].size();
        g_levelList.push_back(e);
    }
    // Second pass: incoming counts
    for (auto& e : g_levelList) {
        e.portalsIn = 0;
        for (size_t i = 0; i < g_levelList.size(); i++) {
            if (&g_levelList[i] == &e) continue;
            for (const auto& t : targets[i])
                if (t == e.world) { e.portalsIn++; break; }
        }
    }
}

static void PopulateLevelList(HWND hList) {
    ListView_DeleteAllItems(hList);
    for (size_t i = 0; i < g_levelList.size(); i++) {
        auto& e = g_levelList[i];
        wchar_t buf[64];
        auto setItem = [&](int col, const wchar_t* txt) {
            LVITEMW lvi = {};
            lvi.mask = LVIF_TEXT;
            lvi.iItem = (int)i;
            lvi.iSubItem = col;
            lvi.pszText = const_cast<wchar_t*>(txt);
            if (col == 0) ListView_InsertItem(hList, &lvi);
            else ListView_SetItem(hList, &lvi);
        };
        std::wstring wname(e.world.begin(), e.world.end());
        std::wstring wfmt(e.format.begin(), e.format.end());
        _snwprintf(buf, 63, L"%d", e.portalsOut); buf[63] = 0;
        std::wstring wout(buf);
        _snwprintf(buf, 63, L"%d", e.portalsIn); buf[63] = 0;
        std::wstring win(buf);
        setItem(0, wname.c_str());
        setItem(1, wfmt.c_str());
        setItem(2, wout.c_str());
        setItem(3, win.c_str());
        setItem(4, e.isCurrent ? L"< current >" : L"");
    }
}

static LRESULT CALLBACK LevelListProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList;
    switch (msg) {
    case WM_CREATE: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int bw = 110, margin = 6;
        int listH = rc.bottom - bw - margin * 3;

        hList = CreateWindowEx(0, WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
            margin, margin, rc.right - margin * 2, listH,
            hwnd, (HMENU)ID_LL_LIST, g_hInst, nullptr);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        LVCOLUMNW lvc = {};
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        lvc.fmt = LVCFMT_LEFT;
        const wchar_t* headers[] = {L"World", L"Format", L"Links Out", L"Links In", L""};
        int widths[] = {160, 70, 80, 80, 90};
        for (int i = 0; i < 5; i++) {
            lvc.cx = widths[i];
            lvc.pszText = const_cast<wchar_t*>(headers[i]);
            ListView_InsertColumn(hList, i, &lvc);
        }

        CreateButton(hwnd, L"Open World",   margin, listH + margin * 2, bw, 26, ID_LL_OPEN);
        CreateButton(hwnd, L"Add Link ->",  margin + bw + 6, listH + margin * 2, bw, 26, ID_LL_LINK);
        CreateButton(hwnd, L"Refresh",      margin + (bw + 6) * 2, listH + margin * 2, bw, 26, ID_LL_REFRESH);
        CreateButton(hwnd, L"Close",        rc.right - bw - margin, listH + margin * 2, bw, 26, ID_LL_CLOSE);

        BuildLevelList();
        PopulateLevelList(hList);
        break;
    }
    case WM_USER + 51:
        BuildLevelList();
        PopulateLevelList(hList);
        break;
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)l;
        if (nm->idFrom == ID_LL_LIST && nm->code == NM_DBLCLK) {
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < (int)g_levelList.size())
                g_editorPanels.actionLevelListOpen = g_levelList[sel].world;
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
        if (id == ID_LL_CLOSE) { ShowLevelList(false); break; }
        if (id == ID_LL_REFRESH) {
            BuildLevelList();
            PopulateLevelList(hList);
            break;
        }
        if (sel < 0 || sel >= (int)g_levelList.size()) break;
        if (id == ID_LL_OPEN)
            g_editorPanels.actionLevelListOpen = g_levelList[sel].world;
        if (id == ID_LL_LINK && !g_levelList[sel].isCurrent)
            g_editorPanels.actionLevelListLink = g_levelList[sel].world;
        break;
    }
    case WM_CLOSE: ShowLevelList(false); break;
    case WM_DESTROY: g_editorPanels.hLevelList = nullptr; break;
    default: return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

void ShowLevelList(bool show) {
    g_editorPanels.showLevelList = show;
    if (g_editorPanels.hLevelList)
        ShowWindow((HWND)g_editorPanels.hLevelList, show ? SW_SHOW : SW_HIDE);
}

void RefreshLevelList() {
    if (g_editorPanels.hLevelList)
        SendMessage((HWND)g_editorPanels.hLevelList, WM_USER + 51, 0, 0);
}

// =====================================================================
// Properties Panel - context-sensitive, dynamic controls
// =====================================================================
static const int ID_PP_POSX  = 401;
static const int ID_PP_POSY  = 402;
static const int ID_PP_POSZ  = 403;
static const int ID_PP_ROT   = 404;
static const int ID_PP_SX    = 405;
static const int ID_PP_SY    = 406;
static const int ID_PP_SZ    = 407;
static const int ID_PP_APPLY = 408;
static const int ID_PP_CLOSE = 409;
static const int ID_PP_LABEL = 410;
static const int ID_PP_TEX_SCALE_U = 411;
static const int ID_PP_TEX_SCALE_V = 412;
static const int ID_PP_TEX_OFF_U = 413;
static const int ID_PP_TEX_OFF_V = 414;
// Def-aligned sections
static const int ID_PP_DEFBLOCK = 415;   // read-only def summary
static const int ID_PP_EDITDEF  = 416;   // "Edit .ozls" button
static const int ID_PP_HEALTH   = 417;   // NPC instance
static const int ID_PP_SPEED    = 418;   // NPC instance
static const int ID_PP_RESPAWN  = 419;   // pickup instance
static const int ID_PP_ZONETYPE = 420;   // zone combo
static const int ID_PP_ZONEINT  = 421;   // zone intensity
static const int ID_PP_ZONENAME = 422;   // zone script-hook name
static const int ID_PP_ZONEGRAV    = 423; // zone gravity (PhysicsInfo)
static const int ID_PP_ZONEJUMP    = 424; // zone jump speed
static const int ID_PP_ZONETERM    = 425; // zone terminal velocity
static const int ID_PP_ZONEWGRAV   = 426; // zone water gravity
static const int ID_PP_ZONEWDRAG   = 427; // zone water drag
static const int ID_PP_ZONESWIM    = 428; // zone swim-up speed
static const int ID_PP_ZONELADDER  = 429; // zone ladder speed
static const int ID_PP_ZONEFLYMULT = 430; // zone fly/noclip speed multiplier
static const int ID_PP_PORTALWORLD = 423;
static const int ID_PP_PSPAWNX  = 424;
static const int ID_PP_PSPAWNY  = 425;
static const int ID_PP_PSPAWNZ  = 426;
static const int ID_PP_PBIDIR   = 427;
static const int ID_PP_PORTALBROWSE = 428; // browse target world
static const int ID_PP_SCALE    = 429;    // GameEngine.Mesh uniform scale
static const int ID_PP_MESHPATH = 430;    // GameEngine.Mesh model path
static const int ID_PP_MESHTEX  = 431;    // GameEngine.Mesh texture path
static const int ID_PP_ANIMCLIP = 432;    // GameEngine.Mesh.Skeletal clip
static const int ID_PP_MESHRELOAD = 433;  // force mesh reload button
static const int ID_PP_EMITTER_TYPE   = 434;
static const int ID_PP_EMITTER_TEX    = 435;
static const int ID_PP_EMITTER_RATE   = 436;
static const int ID_PP_EMITTER_LIFE   = 437;
static const int ID_PP_EMITTER_SPEED  = 438;
static const int ID_PP_EMITTER_SIZE   = 439;
static const int ID_PP_EMITTER_SPREAD = 440;
static const int ID_PP_EMITTER_R      = 441;
static const int ID_PP_EMITTER_G      = 442;
static const int ID_PP_EMITTER_B      = 443;
static const int ID_PP_PATHNAME       = 444;
static const int ID_PP_PATHRADIUS     = 445;
static const int ID_PP_PATHNEXT       = 446;
static const int ID_PP_PATHLOOP       = 447;
static const int ID_PP_MESHWIND       = 448;
static const int ID_PP_WIND_SX        = 449;
static const int ID_PP_WIND_SY        = 450;
static const int ID_PP_WIND_SZ        = 451;
static const int ID_PP_WIND_DIRX      = 452;
static const int ID_PP_WIND_DIRY      = 453;
static const int ID_PP_WIND_DIRZ      = 454;
static const int ID_PP_WIND_STRENGTH  = 455;
static const int ID_PP_WIND_FREQ      = 456;
static const int ID_PP_MESHTEX_BROWSE   = 457;
static const int ID_PP_MESHTEX_ACTIVE   = 458;
static const int ID_PP_EMITTERTEX_BROWSE = 459;
static const int ID_PP_EMITTERTEX_ACTIVE = 460;
static const int ID_PP_MESHANIMFILE       = 461;
static const int ID_PP_MESHANIMFILE_BROWSE = 462;
static const int ID_PP_MESHANIMSPEED      = 463;
static const int ID_PP_CONVERT_ANIMATED   = 464;

// Build a read-only def summary (stats + actions + PawnDef block). Shared by the
// Script Manager detail pane and the Properties panel def section.
static void BuildDefSummary(const EntityDef& def, const std::string& sourcePath,
                            int actionCountHint, std::string& out) {
    out.clear();
    out += def.name + "  [" + EntityTypeName(def.type) + "]\n";
    if (!sourcePath.empty()) out += "source: " + sourcePath + "\n";
    if (!def.mesh.empty())    out += "mesh: " + def.mesh + "\n";
    if (!def.texture.empty()) out += "texture: " + def.texture + "\n";
    if (!def.icon.empty())    out += "icon: " + def.icon + "\n";
    if (!def.skybox.empty())  out += "skybox: " + def.skybox + "\n";
    if (!def.music.empty())   out += "music: " + def.music + "\n";

    if (def.type == EntityType::PAWN) {
        for (const auto& pd : PawnSystem::Instance().GetDefs()) {
            if (pd.name != def.name) continue;
            out += "\npawn stats (PawnDefs/*.cfg):\n";
            out += "  speed = " + std::to_string(pd.speed) + "\n";
            out += "  aggroRange = " + std::to_string(pd.aggroRange) + "\n";
            out += "  attackRange = " + std::to_string(pd.attackRange) + "\n";
            out += "  damage = " + std::to_string(pd.damage) + "\n";
            out += "  maxHealth = " + std::to_string(pd.maxHealth) + "\n";
            if (!pd.sprite_path.empty()) out += "  sprite = " + pd.sprite_path + "\n";
            if (!pd.scream_path.empty()) out += "  scream = " + pd.scream_path + "\n";
            break;
        }
    }
    if (!def.stats.floats.empty()) {
        out += "\nstats:\n";
        std::vector<std::pair<std::string, float>> sv(def.stats.floats.begin(), def.stats.floats.end());
        std::sort(sv.begin(), sv.end());
        for (auto& [k, v] : sv) out += "  " + k + " = " + std::to_string(v) + "\n";
    }
    if (!def.stats.strings.empty()) {
        out += "\nstrings:\n";
        std::vector<std::pair<std::string, std::string>> ss(def.stats.strings.begin(), def.stats.strings.end());
        std::sort(ss.begin(), ss.end());
        for (auto& [k, v] : ss) out += "  " + k + " = " + v + "\n";
    }
    int actions = actionCountHint >= 0 ? actionCountHint : (int)def.actions.size();
    if (actions > 0) {
        out += "\nactions:\n";
        if (!def.actions.empty()) {
            for (const auto& a : def.actions)
                out += "  " + a.name + " (" + std::to_string(a.scriptLines.size()) + " lines)\n";
        } else {
            out += "  (" + std::to_string(actions) + ")\n";
        }
    }
}

// Compact number formatting for read-only def value rows (1 not 1.000000)
static std::string FormatStat(float v) {
    char buf[32];
    if (std::fabs(v - std::round(v)) < 0.0001f)
        snprintf(buf, sizeof(buf), "%d", (int)std::round(v));
    else
        snprintf(buf, sizeof(buf), "%g", v);
    return std::string(buf);
}

static void PopulatePropertiesPanel(HWND hwnd) {
    // Destroy existing controls
    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child) {
        HWND next = GetWindow(child, GW_HWNDNEXT);
        DestroyWindow(child);
        child = next;
    }

    int selType = g_editorPanels.propsTargetType;
    int selIdx  = g_editorPanels.propsTargetIndex;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int x = 10, y = 10, lw = 74, ew = 100, bw = 80, rowH = 24;

    // Title label
    {
        char title[128];
        snprintf(title, sizeof(title), "Properties: %s",
                 g_editorPanels.propsTargetName.c_str());
        std::wstring wtitle(title, title + strlen(title));
        CreateLabel(hwnd, wtitle.c_str(), x, y, rc.right - 20, 20, ID_PP_LABEL);
        y += 26;
    }

    // Common position fields
    auto addField = [&](const wchar_t* label, int id, float val) {
        CreateLabel(hwnd, label, x, y, lw, 20, 0);
        std::wstring wval = std::to_wstring(val);
        HWND hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", wval.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            x + lw, y, ew, 22, hwnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
        y += rowH;
        return hEdit;
    };

    addField(L"Pos X:", ID_PP_POSX, g_editorPanels.propPosX);
    addField(L"Pos Y:", ID_PP_POSY, g_editorPanels.propPosY);
    addField(L"Pos Z:", ID_PP_POSZ, g_editorPanels.propPosZ);

    // Type-specific fields
    if (selType == 1 || selType == 6 || selType == 8) { // Brush, Zone, or Portal — add size fields
        addField(L"Size X:", ID_PP_SX, g_editorPanels.propSizeX);
        addField(L"Size Y:", ID_PP_SY, g_editorPanels.propSizeY);
        addField(L"Size Z:", ID_PP_SZ, g_editorPanels.propSizeZ);
    }

    // Texture scale/offset for brushes
    if (selType == 1) {
        addField(L"Tex U Scale:", ID_PP_TEX_SCALE_U, g_editorPanels.propTexScaleU);
        addField(L"Tex V Scale:", ID_PP_TEX_SCALE_V, g_editorPanels.propTexScaleV);
        addField(L"Tex U Off:", ID_PP_TEX_OFF_U, g_editorPanels.propTexOffsetU);
        addField(L"Tex V Off:", ID_PP_TEX_OFF_V, g_editorPanels.propTexOffsetV);
    }

    // Rotation
    addField(L"Rot:", ID_PP_ROT, g_editorPanels.propRotation);

    // ---- Def-aligned sections -------------------------------------------------
    // Section header + read-only key/value row helpers
    auto addSection = [&](const char* title) {
        std::wstring wt(title, title + strlen(title));
        CreateLabel(hwnd, wt.c_str(), x, y, rc.right - 20, 18, 1);
        y += 20;
    };
    const int defLabelW = 96;
    auto addReadOnlyRow = [&](const std::string& key, const std::string& val) {
        std::wstring wk(key.begin(), key.end());
        CreateLabel(hwnd, wk.c_str(), x, y, defLabelW, 20, 0);
        std::wstring wv(val.begin(), val.end());
        CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", wv.c_str(),
            WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL,
            x + defLabelW, y, rc.right - (x + defLabelW) - 20, 22,
            hwnd, nullptr, g_hInst, nullptr);
        y += rowH;
    };
    auto addTextField = [&](const wchar_t* label, int id, const std::string& val) {
        CreateLabel(hwnd, label, x, y, defLabelW, 20, 0);
        std::wstring wval(val.begin(), val.end());
        HWND h = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", wval.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            x + defLabelW, y, rc.right - (x + defLabelW) - 20, 22, hwnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
        y += rowH;
        return h;
    };

    // Definition block (read-only, per-key rows) — shown for NPC/pickup/zone
    if (!g_editorPanels.propDefTitle.empty()) {
        addSection("Definition (read-only)");
        addReadOnlyRow("type", g_editorPanels.propDefTitle);
        if (!g_editorPanels.propDefSource.empty())
            addReadOnlyRow("source", g_editorPanels.propDefSource);
        if (!g_editorPanels.propDefPawnFields.empty()) {
            addSection("PawnDefs stats");
            for (auto& f : g_editorPanels.propDefPawnFields) addReadOnlyRow(f.key, f.value);
        }
        if (!g_editorPanels.propDefFields.empty()) {
            addSection(".ozls stats");
            for (auto& f : g_editorPanels.propDefFields) addReadOnlyRow(f.key, f.value);
        }
        if (!g_editorPanels.propDefActions.empty()) {
            addSection("Actions");
            for (auto& f : g_editorPanels.propDefActions) addReadOnlyRow(f.key, f.value);
        }
        if (!g_editorPanels.propDefPath.empty()) {
            CreateButton(hwnd, L"Edit .ozls", x, y, 100, 24, ID_PP_EDITDEF);
            y += 30;
        }
    }

    if (selType == 3) { // NPC — instance overrides
        addSection("Instance overrides");
        addField(L"Health:", ID_PP_HEALTH, g_editorPanels.propHealth);
        addField(L"Speed:", ID_PP_SPEED, g_editorPanels.propSpeed);
    } else if (selType == 4) { // PICKUP / weapon — instance overrides
        addSection("Instance overrides");
        addField(L"Respawn:", ID_PP_RESPAWN, g_editorPanels.propRespawnTime);
    } else if (selType == 6) { // ZONE
        addSection("Zone");
        CreateLabel(hwnd, L"Type:", x, y, lw, 20, 0);
        {
            HWND hCombo = CreateWindowEx(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                x + lw, y, ew, 200, hwnd, (HMENU)(INT_PTR)ID_PP_ZONETYPE, g_hInst, nullptr);
            const wchar_t* zt[] = { L"water", L"ladder", L"sky", L"reverb", L"sound" };
            for (auto* t : zt) SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)t);
            int zsel = g_editorPanels.propZoneType;
            if (zsel < 0 || zsel > 4) zsel = 0;
            SendMessage(hCombo, CB_SETCURSEL, zsel, 0);
        }
        y += rowH;
        addField(L"Intensity:", ID_PP_ZONEINT, g_editorPanels.propZoneIntensity);
        addTextField(L"Name:", ID_PP_ZONENAME, g_editorPanels.propZoneName);
        // Per-zone physics overrides (round-trip through the OZONE export)
        addSection("Physics");
        addField(L"Gravity:", ID_PP_ZONEGRAV, g_editorPanels.propZoneGravity);
        addField(L"Jump:", ID_PP_ZONEJUMP, g_editorPanels.propZoneJump);
        addField(L"Terminal:", ID_PP_ZONETERM, g_editorPanels.propZoneTerminal);
        addField(L"Water Gravity:", ID_PP_ZONEWGRAV, g_editorPanels.propZoneWaterGravity);
        addField(L"Water Drag:", ID_PP_ZONEWDRAG, g_editorPanels.propZoneWaterDrag);
        addField(L"Swim Up:", ID_PP_ZONESWIM, g_editorPanels.propZoneSwimUp);
        addField(L"Ladder Speed:", ID_PP_ZONELADDER, g_editorPanels.propZoneLadderSpeed);
        addField(L"Fly Mult:", ID_PP_ZONEFLYMULT, g_editorPanels.propZoneFlyMult);
    } else if (selType == 8) { // PORTAL
        addSection("Destination");
        addTextField(L"Target World:", ID_PP_PORTALWORLD, g_editorPanels.propPortalWorld);
        addField(L"Spawn X:", ID_PP_PSPAWNX, g_editorPanels.propPortalSpawn[0]);
        addField(L"Spawn Y:", ID_PP_PSPAWNY, g_editorPanels.propPortalSpawn[1]);
        addField(L"Spawn Z:", ID_PP_PSPAWNZ, g_editorPanels.propPortalSpawn[2]);
        {
            HWND hCheck = CreateWindowEx(0, L"BUTTON", L"Bidirectional",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                x, y, 200, 22, hwnd, (HMENU)(INT_PTR)ID_PP_PBIDIR, g_hInst, nullptr);
            SendMessage(hCheck, BM_SETCHECK, g_editorPanels.propPortalBidir ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        y += rowH;
    } else if (selType == 9) { // GameEngine.Mesh.Static / Mesh.Skeletal
        addSection("Mesh");
        addTextField(L"Mesh Path:", ID_PP_MESHPATH, g_editorPanels.propMeshPath);
        addTextField(L"Texture:", ID_PP_MESHTEX, g_editorPanels.propMeshTex);
        // Pick a texture from the Texture Manager ("Use Active Tex") or the OS
        // file dialog ("Browse...") instead of typing/copying a path.
        CreateButton(hwnd, L"Browse Tex...", x, y, 90, 22, ID_PP_MESHTEX_BROWSE);
        CreateButton(hwnd, L"Use Active Tex", x + 96, y, 110, 22, ID_PP_MESHTEX_ACTIVE);
        y += rowH;
        addTextField(L"Anim Clip:", ID_PP_ANIMCLIP, g_editorPanels.propAnimClip);
        addTextField(L"Anim File:", ID_PP_MESHANIMFILE, g_editorPanels.propMeshAnimFile);
        CreateButton(hwnd, L"Browse Anim...", x, y, 110, 22, ID_PP_MESHANIMFILE_BROWSE);
        CreateButton(hwnd, L"Convert to Animated", x + 116, y, 150, 22, ID_PP_CONVERT_ANIMATED);
        y += rowH;
        addField(L"Anim Speed:", ID_PP_MESHANIMSPEED, g_editorPanels.propMeshAnimSpeed);
        addField(L"Scale:", ID_PP_SCALE, g_editorPanels.propScale);
        {
            HWND hCheck = CreateWindowEx(0, L"BUTTON", L"Wind Affected",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                x, y, 200, 22, hwnd, (HMENU)(INT_PTR)ID_PP_MESHWIND, g_hInst, nullptr);
            SendMessage(hCheck, BM_SETCHECK, g_editorPanels.propMeshWind ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        y += rowH;
        CreateButton(hwnd, L"Reload Mesh", x, y, 110, 24, ID_PP_MESHRELOAD);
        y += 30;
    } else if (selType == 10) { // GameEngine.ParticleEmitter
        addSection("Particle Emitter");
        addTextField(L"Type:", ID_PP_EMITTER_TYPE, g_editorPanels.propEmitterType);
        addTextField(L"Texture:", ID_PP_EMITTER_TEX, g_editorPanels.propEmitterTex);
        CreateButton(hwnd, L"Browse Tex...", x, y, 90, 22, ID_PP_EMITTERTEX_BROWSE);
        CreateButton(hwnd, L"Use Active Tex", x + 96, y, 110, 22, ID_PP_EMITTERTEX_ACTIVE);
        y += rowH;
        addField(L"Rate:", ID_PP_EMITTER_RATE, g_editorPanels.propEmitterRate);
        addField(L"Lifetime:", ID_PP_EMITTER_LIFE, g_editorPanels.propEmitterLife);
        addField(L"Speed:", ID_PP_EMITTER_SPEED, g_editorPanels.propEmitterSpeed);
        addField(L"Size:", ID_PP_EMITTER_SIZE, g_editorPanels.propEmitterSize);
        addField(L"Spread:", ID_PP_EMITTER_SPREAD, g_editorPanels.propEmitterSpread);
        addField(L"Color R:", ID_PP_EMITTER_R, (float)g_editorPanels.propEmitterR);
        addField(L"Color G:", ID_PP_EMITTER_G, (float)g_editorPanels.propEmitterG);
        addField(L"Color B:", ID_PP_EMITTER_B, (float)g_editorPanels.propEmitterB);
    } else if (selType == 11) { // GameEngine.PathNode
        addSection("Path Node");
        addTextField(L"Name:", ID_PP_PATHNAME, g_editorPanels.propPathName);
        addField(L"Radius:", ID_PP_PATHRADIUS, g_editorPanels.propPathRadius);
        addTextField(L"Next (a,b):", ID_PP_PATHNEXT, g_editorPanels.propPathNext);
        {
            HWND hCheck = CreateWindowEx(0, L"BUTTON", L"Loop",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                x, y, 200, 22, hwnd, (HMENU)(INT_PTR)ID_PP_PATHLOOP, g_hInst, nullptr);
            SendMessage(hCheck, BM_SETCHECK, g_editorPanels.propPathLoop ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        y += rowH;
    } else if (selType == 12) { // WindZone
        addSection("Wind Zone");
        addField(L"Size X:", ID_PP_WIND_SX, g_editorPanels.propWindSizeX);
        addField(L"Size Y:", ID_PP_WIND_SY, g_editorPanels.propWindSizeY);
        addField(L"Size Z:", ID_PP_WIND_SZ, g_editorPanels.propWindSizeZ);
        addField(L"Dir X:", ID_PP_WIND_DIRX, g_editorPanels.propWindDirX);
        addField(L"Dir Y:", ID_PP_WIND_DIRY, g_editorPanels.propWindDirY);
        addField(L"Dir Z:", ID_PP_WIND_DIRZ, g_editorPanels.propWindDirZ);
        addField(L"Strength:", ID_PP_WIND_STRENGTH, g_editorPanels.propWindStrength);
        addField(L"Frequency:", ID_PP_WIND_FREQ, g_editorPanels.propWindFrequency);
    }
    // ---------------------------------------------------------------------------

    y += 8;
    CreateButton(hwnd, L"Apply", x, y, bw, 26, ID_PP_APPLY);
    CreateButton(hwnd, L"Close", x + bw + 6, y, bw, 26, ID_PP_CLOSE);

    // Grow the window (never shrink) so every generated row is visible
    {
        int needed = y + 26 + 24; // buttons + margin
        RECT wr;
        GetWindowRect(hwnd, &wr);
        int curH = wr.bottom - wr.top;
        if (needed > curH) {
            SetWindowPos(hwnd, nullptr, 0, 0, wr.right - wr.left, needed,
                         SWP_NOMOVE | SWP_NOZORDER);
        }
    }
}

static LRESULT CALLBACK PropsPanelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        PopulatePropertiesPanel(hwnd);
        break;
    }
    case WM_USER + 50: {
        PopulatePropertiesPanel(hwnd);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_PP_CLOSE) { ShowPropertiesPanel(false); break; }
        if (id == ID_PP_EDITDEF) {
            // Hand off to the OS editor (defs stay read-only in the panel)
            if (!g_editorPanels.propDefPath.empty())
                ShellExecuteA(hwnd, "open", g_editorPanels.propDefPath.c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        if (id == ID_PP_MESHRELOAD) {
            g_editorPanels.actionReloadMesh = true;
            break;
        }
        // Texture pickers for mesh / particle-emitter properties — pick from the
        // Texture Manager ("Use Active Tex") or the OS dialog ("Browse Tex...")
        // instead of hand-copying a path, then apply immediately.
        if (id == ID_PP_MESHANIMFILE_BROWSE) {
            std::string path;
            if (ChooseAnimFile(path)) {
                std::wstring w(path.begin(), path.end());
                SetWindowTextW(GetDlgItem(hwnd, ID_PP_MESHANIMFILE), w.c_str());
                g_editorPanels.propMeshAnimFile = path;
                g_editorPanels.actionApplyProperties = true;
            }
            break;
        }
        if (id == ID_PP_CONVERT_ANIMATED) {
            g_editorPanels.actionConvertToAnimated = true;
            break;
        }
        if (id == ID_PP_MESHTEX_BROWSE || id == ID_PP_EMITTERTEX_BROWSE ||
            id == ID_PP_MESHTEX_ACTIVE || id == ID_PP_EMITTERTEX_ACTIVE) {
            bool mesh = (id == ID_PP_MESHTEX_BROWSE || id == ID_PP_MESHTEX_ACTIVE);
            bool active = (id == ID_PP_MESHTEX_ACTIVE || id == ID_PP_EMITTERTEX_ACTIVE);
            std::string path = active ? g_editorPanels.activeTexturePath : std::string();
            if (!active && !ChooseImageFile(path)) break;
            if (path.empty()) break;
            int field = mesh ? ID_PP_MESHTEX : ID_PP_EMITTER_TEX;
            std::wstring w(path.begin(), path.end());
            SetWindowTextW(GetDlgItem(hwnd, field), w.c_str());
            if (mesh) g_editorPanels.propMeshTex = path;
            else      g_editorPanels.propEmitterTex = path;
            g_editorPanels.actionApplyProperties = true;
            break;
        }
        if (id == ID_PP_APPLY) {
            // Read all edit fields and set action flags
            auto readFloat = [hwnd](int id, float def) -> float {
                HWND hCtrl = GetDlgItem(hwnd, id);
                if (!hCtrl) return def;
                wchar_t buf[64];
                GetWindowTextW(hCtrl, buf, 64);
                return (float)wcstod(buf, nullptr);
            };
            auto readString = [hwnd](int id, const std::string& def) -> std::string {
                HWND hCtrl = GetDlgItem(hwnd, id);
                if (!hCtrl) return def;
                wchar_t buf[256];
                GetWindowTextW(hCtrl, buf, 256);
                char out[256] = {0};
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, 256, nullptr, nullptr);
                return std::string(out);
            };
            g_editorPanels.propPosX = readFloat(ID_PP_POSX, 0);
            g_editorPanels.propPosY = readFloat(ID_PP_POSY, 0);
            g_editorPanels.propPosZ = readFloat(ID_PP_POSZ, 0);
            g_editorPanels.propRotation = readFloat(ID_PP_ROT, 0);
            g_editorPanels.propSizeX = readFloat(ID_PP_SX, 1);
            g_editorPanels.propSizeY = readFloat(ID_PP_SY, 1);
            g_editorPanels.propSizeZ = readFloat(ID_PP_SZ, 1);
            g_editorPanels.propTexScaleU = readFloat(ID_PP_TEX_SCALE_U, 1.0f);
            g_editorPanels.propTexScaleV = readFloat(ID_PP_TEX_SCALE_V, 1.0f);
            g_editorPanels.propTexOffsetU = readFloat(ID_PP_TEX_OFF_U, 0.0f);
            g_editorPanels.propTexOffsetV = readFloat(ID_PP_TEX_OFF_V, 0.0f);
            // Def-aligned instance overrides
            g_editorPanels.propHealth = readFloat(ID_PP_HEALTH, g_editorPanels.propHealth);
            g_editorPanels.propSpeed = readFloat(ID_PP_SPEED, g_editorPanels.propSpeed);
            g_editorPanels.propRespawnTime = readFloat(ID_PP_RESPAWN, g_editorPanels.propRespawnTime);
    g_editorPanels.propZoneIntensity = readFloat(ID_PP_ZONEINT, g_editorPanels.propZoneIntensity);
    g_editorPanels.propZoneName = readString(ID_PP_ZONENAME, g_editorPanels.propZoneName);
    g_editorPanels.propZoneGravity = readFloat(ID_PP_ZONEGRAV, g_editorPanels.propZoneGravity);
    g_editorPanels.propZoneJump = readFloat(ID_PP_ZONEJUMP, g_editorPanels.propZoneJump);
    g_editorPanels.propZoneTerminal = readFloat(ID_PP_ZONETERM, g_editorPanels.propZoneTerminal);
    g_editorPanels.propZoneWaterGravity = readFloat(ID_PP_ZONEWGRAV, g_editorPanels.propZoneWaterGravity);
    g_editorPanels.propZoneWaterDrag = readFloat(ID_PP_ZONEWDRAG, g_editorPanels.propZoneWaterDrag);
    g_editorPanels.propZoneSwimUp = readFloat(ID_PP_ZONESWIM, g_editorPanels.propZoneSwimUp);
    g_editorPanels.propZoneLadderSpeed = readFloat(ID_PP_ZONELADDER, g_editorPanels.propZoneLadderSpeed);
    g_editorPanels.propZoneFlyMult = readFloat(ID_PP_ZONEFLYMULT, g_editorPanels.propZoneFlyMult);
            g_editorPanels.propPortalWorld = readString(ID_PP_PORTALWORLD, g_editorPanels.propPortalWorld);
            g_editorPanels.propPortalSpawn[0] = readFloat(ID_PP_PSPAWNX, g_editorPanels.propPortalSpawn[0]);
            g_editorPanels.propPortalSpawn[1] = readFloat(ID_PP_PSPAWNY, g_editorPanels.propPortalSpawn[1]);
            g_editorPanels.propPortalSpawn[2] = readFloat(ID_PP_PSPAWNZ, g_editorPanels.propPortalSpawn[2]);
            if (HWND hz = GetDlgItem(hwnd, ID_PP_ZONETYPE)) {
                int s = (int)SendMessage(hz, CB_GETCURSEL, 0, 0);
                if (s >= 0 && s <= 4) g_editorPanels.propZoneType = s;
            }
            if (HWND hb = GetDlgItem(hwnd, ID_PP_PBIDIR))
                g_editorPanels.propPortalBidir =
                    SendMessage(hb, BM_GETCHECK, 0, 0) == BST_CHECKED;
            // GameEngine.Mesh object edits
            g_editorPanels.propScale = readFloat(ID_PP_SCALE, g_editorPanels.propScale);
            g_editorPanels.propMeshPath = readString(ID_PP_MESHPATH, g_editorPanels.propMeshPath);
            g_editorPanels.propMeshTex = readString(ID_PP_MESHTEX, g_editorPanels.propMeshTex);
            g_editorPanels.propAnimClip = readString(ID_PP_ANIMCLIP, g_editorPanels.propAnimClip);
            g_editorPanels.propMeshAnimFile = readString(ID_PP_MESHANIMFILE, g_editorPanels.propMeshAnimFile);
            g_editorPanels.propMeshAnimSpeed = readFloat(ID_PP_MESHANIMSPEED, g_editorPanels.propMeshAnimSpeed);
            // GameEngine.ParticleEmitter edits
            g_editorPanels.propEmitterType = readString(ID_PP_EMITTER_TYPE, g_editorPanels.propEmitterType);
            g_editorPanels.propEmitterTex = readString(ID_PP_EMITTER_TEX, g_editorPanels.propEmitterTex);
            g_editorPanels.propEmitterRate = readFloat(ID_PP_EMITTER_RATE, g_editorPanels.propEmitterRate);
            g_editorPanels.propEmitterLife = readFloat(ID_PP_EMITTER_LIFE, g_editorPanels.propEmitterLife);
            g_editorPanels.propEmitterSpeed = readFloat(ID_PP_EMITTER_SPEED, g_editorPanels.propEmitterSpeed);
            g_editorPanels.propEmitterSize = readFloat(ID_PP_EMITTER_SIZE, g_editorPanels.propEmitterSize);
            g_editorPanels.propEmitterSpread = readFloat(ID_PP_EMITTER_SPREAD, g_editorPanels.propEmitterSpread);
            g_editorPanels.propEmitterR = (int)readFloat(ID_PP_EMITTER_R, (float)g_editorPanels.propEmitterR);
            g_editorPanels.propEmitterG = (int)readFloat(ID_PP_EMITTER_G, (float)g_editorPanels.propEmitterG);
            g_editorPanels.propEmitterB = (int)readFloat(ID_PP_EMITTER_B, (float)g_editorPanels.propEmitterB);
            // GameEngine.PathNode edits
            g_editorPanels.propPathName = readString(ID_PP_PATHNAME, g_editorPanels.propPathName);
            g_editorPanels.propPathRadius = readFloat(ID_PP_PATHRADIUS, g_editorPanels.propPathRadius);
            g_editorPanels.propPathNext = readString(ID_PP_PATHNEXT, g_editorPanels.propPathNext);
            if (HWND hb = GetDlgItem(hwnd, ID_PP_PATHLOOP))
                g_editorPanels.propPathLoop =
                    SendMessage(hb, BM_GETCHECK, 0, 0) == BST_CHECKED;
            // GameEngine.Mesh wind flag
            if (HWND hb = GetDlgItem(hwnd, ID_PP_MESHWIND))
                g_editorPanels.propMeshWind =
                    SendMessage(hb, BM_GETCHECK, 0, 0) == BST_CHECKED;
            // WindZone edits
            g_editorPanels.propWindSizeX = readFloat(ID_PP_WIND_SX, g_editorPanels.propWindSizeX);
            g_editorPanels.propWindSizeY = readFloat(ID_PP_WIND_SY, g_editorPanels.propWindSizeY);
            g_editorPanels.propWindSizeZ = readFloat(ID_PP_WIND_SZ, g_editorPanels.propWindSizeZ);
            g_editorPanels.propWindDirX = readFloat(ID_PP_WIND_DIRX, g_editorPanels.propWindDirX);
            g_editorPanels.propWindDirY = readFloat(ID_PP_WIND_DIRY, g_editorPanels.propWindDirY);
            g_editorPanels.propWindDirZ = readFloat(ID_PP_WIND_DIRZ, g_editorPanels.propWindDirZ);
            g_editorPanels.propWindStrength = readFloat(ID_PP_WIND_STRENGTH, g_editorPanels.propWindStrength);
            g_editorPanels.propWindFrequency = readFloat(ID_PP_WIND_FREQ, g_editorPanels.propWindFrequency);
            g_editorPanels.actionApplyProperties = true;
        }
        break;
    }
    case WM_CLOSE: ShowPropertiesPanel(false); break;
    case WM_DESTROY: g_editorPanels.hPropsPanel = nullptr; break;
    default: return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

void ShowPropertiesPanel(bool show) {
    g_editorPanels.showPropsPanel = show;
    if (show && g_editorPanels.hPropsPanel) {
        // Pre-populate apply values from the selection
        g_editorPanels.propPosX = g_editorPanels.propsTargetPos[0];
        g_editorPanels.propPosY = g_editorPanels.propsTargetPos[1];
        g_editorPanels.propPosZ = g_editorPanels.propsTargetPos[2];
        g_editorPanels.propRotation = g_editorPanels.propsTargetRotation;
        g_editorPanels.propScale = g_editorPanels.propsTargetScale;
        g_editorPanels.propDefPath.clear();
        g_editorPanels.propDefTitle.clear();
        g_editorPanels.propDefSource.clear();
        g_editorPanels.propDefPawnFields.clear();
        g_editorPanels.propDefFields.clear();
        g_editorPanels.propDefActions.clear();

        // Helper: fill the structured def rows from the registry by def name
        // (+ PawnDefs/*.cfg fallback for pawns without an .ozls def).
        auto fillDefBlock = [](const std::string& defName, const std::string& fallbackPath) {
            using DefField = EditorPanelState::DefField;
            auto& P = g_editorPanels;
            P.propDefPath.clear(); P.propDefTitle.clear(); P.propDefSource.clear();
            P.propDefPawnFields.clear(); P.propDefFields.clear(); P.propDefActions.clear();

            auto addPawnDefFields = [&](const std::string& name) {
                for (const auto& pd : PawnSystem::Instance().GetDefs()) {
                    if (pd.name != name) continue;
                    P.propDefPawnFields.push_back({"speed", FormatStat(pd.speed)});
                    P.propDefPawnFields.push_back({"aggroRange", FormatStat(pd.aggroRange)});
                    P.propDefPawnFields.push_back({"attackRange", FormatStat(pd.attackRange)});
                    P.propDefPawnFields.push_back({"damage", FormatStat(pd.damage)});
                    P.propDefPawnFields.push_back({"maxHealth", std::to_string(pd.maxHealth)});
                    if (!pd.sprite_path.empty()) P.propDefPawnFields.push_back({"sprite", pd.sprite_path});
                    if (!pd.scream_path.empty()) P.propDefPawnFields.push_back({"scream", pd.scream_path});
                    return true;
                }
                return false;
            };

            const EntityDef* def = LightningEntityRegistry::Instance().Find(defName);
            if (def) {
                P.propDefPath = def->sourcePath;
                P.propDefTitle = def->name + "  [" + EntityTypeName(def->type) + "]";
                P.propDefSource = def->sourcePath;
                if (!def->mesh.empty())    P.propDefFields.push_back({"mesh", def->mesh});
                if (!def->texture.empty()) P.propDefFields.push_back({"texture", def->texture});
                if (!def->icon.empty())    P.propDefFields.push_back({"icon", def->icon});
                if (!def->skybox.empty())  P.propDefFields.push_back({"skybox", def->skybox});
                if (!def->music.empty())   P.propDefFields.push_back({"music", def->music});
                std::vector<std::pair<std::string, float>> sv(def->stats.floats.begin(), def->stats.floats.end());
                std::sort(sv.begin(), sv.end());
                for (auto& [k, v] : sv) P.propDefFields.push_back({k, FormatStat(v)});
                std::vector<std::pair<std::string, std::string>> ss(def->stats.strings.begin(), def->stats.strings.end());
                std::sort(ss.begin(), ss.end());
                for (auto& [k, v] : ss) P.propDefFields.push_back({k, "\"" + v + "\""});
                for (const auto& a : def->actions)
                    P.propDefActions.push_back({a.name, std::to_string(a.scriptLines.size()) + " lines"});
                if (def->type == EntityType::PAWN) addPawnDefFields(def->name);
                return;
            }
            // No .ozls def — PawnDefs/*.cfg-only pawn fallback
            if (addPawnDefFields(defName)) {
                std::string path = fallbackPath.empty()
                    ? ("GameData/Global/PawnDefs/" + defName + ".cfg") : fallbackPath;
                if (!fs::exists(path)) path.clear();
                P.propDefPath = path;
                P.propDefTitle = defName + "  [pawn - PawnDefs only]";
                P.propDefSource = path;
            }
        };

        if (g_editorPanels.propsTargetType == 3) { // NPC
            if (Pawn* p = PawnSystem::Instance().Get(g_editorPanels.propsTargetIndex)) {
                g_editorPanels.propHealth = (float)p->health;
                g_editorPanels.propSpeed = p->speed;
                fillDefBlock(p->defName, "");
            }
        } else if (g_editorPanels.propsTargetType == 4) { // PICKUP / weapon
            for (auto& pk : PawnSystem::Instance().GetPickups()) {
                if ((int)pk.id == g_editorPanels.propsTargetIndex) {
                    g_editorPanels.propRespawnTime = pk.respawnTime;
                    fillDefBlock(pk.typeName, "");
                    break;
                }
            }
        } else if (g_editorPanels.propsTargetType == 6) { // ZONE
            for (auto& z : PawnSystem::Instance().GetZones()) {
                if ((int)z.id == g_editorPanels.propsTargetIndex) {
            g_editorPanels.propZoneType = (int)z.zoneType;
            if (g_editorPanels.propZoneType > 4) g_editorPanels.propZoneType = 0;
            g_editorPanels.propZoneIntensity = z.intensity;
            g_editorPanels.propZoneName = z.name;
            g_editorPanels.propZoneGravity = z.physics.gravity;
            g_editorPanels.propZoneJump = z.physics.jumpSpeed;
            g_editorPanels.propZoneTerminal = z.physics.terminalVelocity;
            g_editorPanels.propZoneWaterGravity = z.physics.waterGravity;
            g_editorPanels.propZoneWaterDrag = z.physics.waterDrag;
            g_editorPanels.propZoneSwimUp = z.physics.swimUpSpeed;
            g_editorPanels.propZoneLadderSpeed = z.physics.ladderSpeed;
            g_editorPanels.propZoneFlyMult = z.physics.flySpeedMult;
            if (!z.name.empty()) fillDefBlock(z.name, "");
                    break;
                }
            }
        } else if (g_editorPanels.propsTargetType == 8) { // PORTAL
            auto& portals = PawnSystem::Instance().GetPortals();
            if (g_editorPanels.propsTargetIndex >= 0 &&
                g_editorPanels.propsTargetIndex < (int)portals.size()) {
                auto& p = portals[g_editorPanels.propsTargetIndex];
                g_editorPanels.propPortalWorld = p.targetWorld;
                g_editorPanels.propPortalSpawn[0] = p.targetSpawn.x;
                g_editorPanels.propPortalSpawn[1] = p.targetSpawn.y;
                g_editorPanels.propPortalSpawn[2] = p.targetSpawn.z;
                g_editorPanels.propPortalBidir = p.bidirectional;
            }
        } else if (g_editorPanels.propsTargetType == 9) { // GameEngine.Mesh
            if (MeshObjectNode* m = PawnSystem::Instance().GetMeshObject(g_editorPanels.propsTargetIndex)) {
                g_editorPanels.propMeshPath = m->meshPath;
                g_editorPanels.propMeshTex = m->texturePath;
                g_editorPanels.propAnimClip = m->animClip;
                g_editorPanels.propMeshAnimFile = m->animFile;
                g_editorPanels.propMeshAnimSpeed = m->animSpeed;
                g_editorPanels.propScale = m->scale;
                g_editorPanels.propMeshWind = m->windAffected;
            }
        } else if (g_editorPanels.propsTargetType == 10) { // GameEngine.ParticleEmitter
            if (ParticleEmitterNode* e = PawnSystem::Instance().GetParticleEmitter(g_editorPanels.propsTargetIndex)) {
                g_editorPanels.propEmitterType = e->type;
                g_editorPanels.propEmitterTex = e->texturePath;
                g_editorPanels.propEmitterRate = e->rate;
                g_editorPanels.propEmitterLife = e->lifetime;
                g_editorPanels.propEmitterSpeed = e->speed;
                g_editorPanels.propEmitterSize = e->sizeStart;
                g_editorPanels.propEmitterSpread = e->spread;
                g_editorPanels.propEmitterR = e->colorStart.r;
                g_editorPanels.propEmitterG = e->colorStart.g;
                g_editorPanels.propEmitterB = e->colorStart.b;
            }
        } else if (g_editorPanels.propsTargetType == 11) { // GameEngine.PathNode
            if (PathNode* pn = PawnSystem::Instance().GetPathNode(g_editorPanels.propsTargetIndex)) {
                g_editorPanels.propPathName = pn->name;
                g_editorPanels.propPathRadius = pn->radius;
                g_editorPanels.propPathNext.clear();
                for (size_t i = 0; i < pn->next.size(); i++) {
                    if (i) g_editorPanels.propPathNext += ",";
                    g_editorPanels.propPathNext += pn->next[i];
                }
                g_editorPanels.propPathLoop = pn->loop;
            }
        } else if (g_editorPanels.propsTargetType == 12) { // WindZone
            if (WindZoneNode* z = PawnSystem::Instance().GetWindZone(g_editorPanels.propsTargetIndex)) {
                g_editorPanels.propWindSizeX = z->bounds.max.x - z->bounds.min.x;
                g_editorPanels.propWindSizeY = z->bounds.max.y - z->bounds.min.y;
                g_editorPanels.propWindSizeZ = z->bounds.max.z - z->bounds.min.z;
                g_editorPanels.propWindDirX = z->direction.x;
                g_editorPanels.propWindDirY = z->direction.y;
                g_editorPanels.propWindDirZ = z->direction.z;
                g_editorPanels.propWindStrength = z->strength;
                g_editorPanels.propWindFrequency = z->frequency;
            }
        }

        // For brush/zone, derive size from position data if needed
        if (g_editorPanels.propsTargetType == 1) { // BRUSH
            int idx = g_editorPanels.propsTargetIndex;
            // Prefer reading UV values from the renderable (source of truth)
            if (idx >= 0 && idx < OzoneLoader::Instance().Count()) {
                OzoneRenderable* r = OzoneLoader::Instance().Get(idx);
                if (r && r->loaded) {
                    BoundingBox mb = GetMeshBoundingBox(r->model.meshes[0]);
                    g_editorPanels.propSizeX = mb.max.x - mb.min.x;
                    g_editorPanels.propSizeY = mb.max.y - mb.min.y;
                    g_editorPanels.propSizeZ = mb.max.z - mb.min.z;
                    g_editorPanels.propTexScaleU = r->texScaleU;
                    g_editorPanels.propTexScaleV = r->texScaleV;
                    g_editorPanels.propTexOffsetU = r->texOffsetU;
                    g_editorPanels.propTexOffsetV = r->texOffsetV;
                }
            } else {
                auto& vols = OzoneLoader::Instance().GetCollisionVolumes();
                if (idx >= 0 && idx < (int)vols.size()) {
                    g_editorPanels.propSizeX = vols[idx].aabb.max.x - vols[idx].aabb.min.x;
                    g_editorPanels.propSizeY = vols[idx].aabb.max.y - vols[idx].aabb.min.y;
                    g_editorPanels.propSizeZ = vols[idx].aabb.max.z - vols[idx].aabb.min.z;
                    g_editorPanels.propTexScaleU = vols[idx].texScaleU;
                    g_editorPanels.propTexScaleV = vols[idx].texScaleV;
                    g_editorPanels.propTexOffsetU = vols[idx].texOffsetU;
                    g_editorPanels.propTexOffsetV = vols[idx].texOffsetV;
                }
            }
        } else if (g_editorPanels.propsTargetType == 6) { // ZONE
            auto& zones = PawnSystem::Instance().GetZones();
            for (auto& z : zones) {
                if ((int)z.id == g_editorPanels.propsTargetIndex) {
                    g_editorPanels.propSizeX = z.bounds.max.x - z.bounds.min.x;
                    g_editorPanels.propSizeY = z.bounds.max.y - z.bounds.min.y;
                    g_editorPanels.propSizeZ = z.bounds.max.z - z.bounds.min.z;
                    break;
                }
            }
        } else if (g_editorPanels.propsTargetType == 8) { // PORTAL
            auto& portals = PawnSystem::Instance().GetPortals();
            if (g_editorPanels.propsTargetIndex >= 0 &&
                g_editorPanels.propsTargetIndex < (int)portals.size()) {
                auto& p = portals[g_editorPanels.propsTargetIndex];
                g_editorPanels.propSizeX = p.bounds.max.x - p.bounds.min.x;
                g_editorPanels.propSizeY = p.bounds.max.y - p.bounds.min.y;
                g_editorPanels.propSizeZ = p.bounds.max.z - p.bounds.min.z;
            }
        }

        ShowWindow((HWND)g_editorPanels.hPropsPanel, SW_SHOW);
        SetForegroundWindow((HWND)g_editorPanels.hPropsPanel);
        // Rebuild controls for the current entity type
        SendMessage((HWND)g_editorPanels.hPropsPanel, WM_USER + 50, 0, 0);
    } else if (g_editorPanels.hPropsPanel) {
        ShowWindow((HWND)g_editorPanels.hPropsPanel, SW_HIDE);
    }
}

// =====================================================================
// Stats Sidebar â€” docked native left panel (stats + toolbox)
// =====================================================================
static const int ID_SB_TITLE   = 900;
static const int ID_SB_POS     = 901;
static const int ID_SB_SIZE    = 902;
static const int ID_SB_ROT     = 903;
static const int ID_SB_COLL    = 904;
static const int ID_SB_CHUNKS  = 905;
static const int ID_SB_MODE    = 906;
static const int ID_SB_CAM_L   = 907;
static const int ID_SB_CAM     = 908;
static const int ID_SB_SEP1    = 909;
static const int ID_SB_SEP2    = 910;
// Toolbox button IDs
static const int ID_TB_CSG_BOX    = 920;
static const int ID_TB_CSG_CYL    = 921;
static const int ID_TB_CSG_SPH    = 922;
static const int ID_TB_CSG_PYR    = 923;
static const int ID_TB_CSG_PLN    = 924;
static const int ID_TB_OP_SOLID   = 925;
static const int ID_TB_OP_ADD     = 926;
static const int ID_TB_OP_SUB     = 927;
static const int ID_TB_OP_INTER   = 928;
static const int ID_TB_MODE_CAM   = 929;
static const int ID_TB_MODE_MOVE  = 930;
static const int ID_TB_MODE_SCALE = 931;
static const int ID_TB_MODE_ROT   = 932;

static HWND g_sbPos = nullptr, g_sbSize = nullptr, g_sbRot = nullptr;
static HWND g_sbColl = nullptr, g_sbChunks = nullptr, g_sbMode = nullptr;
static HWND g_sbCam = nullptr;
static HBRUSH g_sbBgBrush = nullptr;

int GetStatsSidebarWidth() { return STATS_SIDEBAR_W; }

void LayoutStatsSidebar(int clientW, int clientH, int topOffset, int width) {
    (void)clientW;
    HWND hwnd = (HWND)g_editorPanels.hStatsSidebar;
    if (!hwnd) return;
    int w = width > 0 ? width : STATS_SIDEBAR_W;
    int h = clientH - topOffset;
    if (h < 1) h = 1;
    SetWindowPos(hwnd, HWND_TOP, 0, topOffset, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void UpdateStatsSidebar(float posX, float posY, float posZ,
                        float sizeX, float sizeY, float sizeZ,
                        float rot, float scale,
                        int collisionVols, int chunks,
                        const char* mode,
                        float camX, float camY, float camZ) {
    HWND hwnd = (HWND)g_editorPanels.hStatsSidebar;
    if (!hwnd) return;

    wchar_t buf[128];
    auto setA = [](HWND h, const wchar_t* t) {
        if (h) SetWindowTextW(h, t);
    };

    swprintf(buf, 128, L"Pos: %.1f %.1f %.1f", posX, posY, posZ);
    setA(g_sbPos, buf);
    swprintf(buf, 128, L"Size: %.1f x %.1f x %.1f", sizeX, sizeY, sizeZ);
    setA(g_sbSize, buf);
    swprintf(buf, 128, L"Rot: %.0f  Scale: %.1f", rot, scale);
    setA(g_sbRot, buf);
    swprintf(buf, 128, L"Collision: %d vols", collisionVols);
    setA(g_sbColl, buf);
    swprintf(buf, 128, L"Chunks: %d", chunks);
    setA(g_sbChunks, buf);
    {
        wchar_t modeW[32] = L"MODEL";
        if (mode && mode[0])
            MultiByteToWideChar(CP_UTF8, 0, mode, -1, modeW, 32);
        swprintf(buf, 128, L"Mode: %s", modeW);
        setA(g_sbMode, buf);
    }
    swprintf(buf, 128, L"%.1f %.1f %.1f", camX, camY, camZ);
    setA(g_sbCam, buf);
}

static LRESULT CALLBACK StatsSidebarProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        if (!g_sbBgBrush)
            g_sbBgBrush = CreateSolidBrush(RGB(25, 25, 30));

        int x = 10, y = 10, lw = STATS_SIDEBAR_W - 20, bh = 24, gap = 4, bw = (lw - gap) / 2;
        // --- Stats section ---
        CreateLabel(hwnd, L"Stats", x, y, lw, 20, ID_SB_TITLE); y += 28;
        g_sbPos    = CreateLabel(hwnd, L"Pos: 0 0 0", x, y, lw, 18, ID_SB_POS); y += 20;
        g_sbSize   = CreateLabel(hwnd, L"Size: 0 x 0 x 0", x, y, lw, 18, ID_SB_SIZE); y += 20;
        g_sbRot    = CreateLabel(hwnd, L"Rot: 0  Scale: 1.0", x, y, lw, 18, ID_SB_ROT); y += 26;
        CreateLabel(hwnd, L"---", x, y, lw, 16, ID_SB_SEP1); y += 20;
        g_sbColl   = CreateLabel(hwnd, L"Collision: 0 vols", x, y, lw, 18, ID_SB_COLL); y += 20;
        g_sbChunks = CreateLabel(hwnd, L"Chunks: 0", x, y, lw, 18, ID_SB_CHUNKS); y += 26;
        g_sbMode   = CreateLabel(hwnd, L"Mode: MODEL", x, y, lw, 18, ID_SB_MODE); y += 26;
        CreateLabel(hwnd, L"---", x, y, lw, 16, ID_SB_SEP2); y += 20;
        CreateLabel(hwnd, L"Camera:", x, y, lw, 18, ID_SB_CAM_L); y += 20;
        g_sbCam    = CreateLabel(hwnd, L"0.0 0.0 0.0", x, y, lw, 18, ID_SB_CAM);
        y += 10;

        // --- Primitives section (icons) ---
        CreateLabel(hwnd, L"Primitives", x, y, lw, 18, 950); y += 22;
        CreateIconButton(hwnd, L"Cube",     x, y, bw, bh, ID_TB_CSG_BOX, "BBCube");
        CreateIconButton(hwnd, L"Cylinder", x + bw + gap, y, bw, bh, ID_TB_CSG_CYL, "BBCylinder"); y += bh + gap;
        CreateIconButton(hwnd, L"Sphere",   x, y, bw, bh, ID_TB_CSG_SPH, "BBSphere");
        CreateIconButton(hwnd, L"Pyramid",  x + bw + gap, y, bw, bh, ID_TB_CSG_PYR, "BBGeneric"); y += bh + gap;
        CreateIconButton(hwnd, L"Plane",    x, y, bw, bh, ID_TB_CSG_PLN, "BBSheet"); y += bh + 10;

        // --- CSG Op section (icons) ---
        CreateLabel(hwnd, L"CSG Op", x, y, lw, 18, 951); y += 22;
        CreateIconButton(hwnd, L"Solid",    x, y, bw, bh, ID_TB_OP_SOLID, "BBCube");
        CreateIconButton(hwnd, L"Add",      x + bw + gap, y, bw, bh, ID_TB_OP_ADD, "ModeAdd"); y += bh + gap;
        CreateIconButton(hwnd, L"Sub",      x, y, bw, bh, ID_TB_OP_SUB, "ModeSubtract");
        CreateIconButton(hwnd, L"Inter",    x + bw + gap, y, bw, bh, ID_TB_OP_INTER, "ModeIntersect"); y += bh + 10;

        // --- Tool Mode section (icons) ---
        CreateLabel(hwnd, L"Tool", x, y, lw, 18, 952); y += 22;
        CreateIconButton(hwnd, L"Cam",      x, y, bw, bh, ID_TB_MODE_CAM, "ModeCamera");
        CreateIconButton(hwnd, L"Move",     x + bw + gap, y, bw, bh, ID_TB_MODE_MOVE, "ModeVertex"); y += bh + gap;
        CreateIconButton(hwnd, L"Scale",    x, y, bw, bh, ID_TB_MODE_SCALE, "ModeScale");
        CreateIconButton(hwnd, L"Rotate",   x + bw + gap, y, bw, bh, ID_TB_MODE_ROT, "ModeRotate");
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)l;
        if (dis && dis->CtlType == ODT_BUTTON) return DrawIconButton(dis);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        switch (id) {
            // --- Primitives ---
            case ID_TB_CSG_BOX:  g_editorPanels.actionCsgPlace = 0; break;
            case ID_TB_CSG_CYL:  g_editorPanels.actionCsgPlace = 1; break;
            case ID_TB_CSG_SPH:  g_editorPanels.actionCsgPlace = 2; break;
            case ID_TB_CSG_PYR:  g_editorPanels.actionCsgPlace = 3; break;
            case ID_TB_CSG_PLN:  g_editorPanels.actionCsgPlace = 4; break;
            // --- CSG Operations (commit immediately) ---
            case ID_TB_OP_SOLID: Editor_SetCsgOperation(0); Editor_SetPlaceMode(0);
                g_editorPanels.actionCsgCommitNow = 0; break;
            case ID_TB_OP_ADD:   Editor_SetCsgOperation(1); Editor_SetPlaceMode(0);
                g_editorPanels.actionCsgCommitNow = 1; break;
            case ID_TB_OP_SUB:   Editor_SetCsgOperation(2); Editor_SetPlaceMode(0);
                g_editorPanels.actionCsgCommitNow = 2; break;
            case ID_TB_OP_INTER: Editor_SetCsgOperation(3); Editor_SetPlaceMode(0);
                g_editorPanels.actionCsgCommitNow = 3; break;
            // --- Tool Modes ---
            case ID_TB_MODE_CAM:   g_editorPanels.currentToolMode = 0; break;
            case ID_TB_MODE_MOVE:  g_editorPanels.currentToolMode = 1; break;
            case ID_TB_MODE_SCALE: g_editorPanels.currentToolMode = 2; break;
            case ID_TB_MODE_ROT:   g_editorPanels.currentToolMode = 3; break;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)w;
        SetTextColor(hdc, RGB(180, 200, 220));
        SetBkColor(hdc, RGB(25, 25, 30));
        if (!g_sbBgBrush) g_sbBgBrush = CreateSolidBrush(RGB(25, 25, 30));
        return (LRESULT)g_sbBgBrush;
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        if (!g_sbBgBrush) g_sbBgBrush = CreateSolidBrush(RGB(25, 25, 30));
        FillRect((HDC)w, &rc, g_sbBgBrush);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        if (!g_sbBgBrush) g_sbBgBrush = CreateSolidBrush(RGB(25, 25, 30));
        FillRect(hdc, &rc, g_sbBgBrush);
        // Right edge line
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 70));
        HGDIOBJ old = SelectObject(hdc, pen);
        MoveToEx(hdc, rc.right - 1, 0, nullptr);
        LineTo(hdc, rc.right - 1, rc.bottom);
        SelectObject(hdc, old);
        DeleteObject(pen);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_DESTROY:
        g_editorPanels.hStatsSidebar = nullptr;
        g_sbPos = g_sbSize = g_sbRot = g_sbColl = g_sbChunks = g_sbMode = g_sbCam = nullptr;
        break;
    default:
        return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

// =====================================================================
// Animation / vertex-keyframe tool (Phase B: clips + playback + scrub)
// =====================================================================
static const int ID_AN_CLOSE  = 100;
static const int ID_AN_LIST   = 101;
static const int ID_AN_NEW    = 102;
static const int ID_AN_DELETE = 103;
static const int ID_AN_SAVE   = 104;
static const int ID_AN_PLAY   = 105;
static const int ID_AN_PAUSE  = 106;
static const int ID_AN_STOP   = 107;
static const int ID_AN_TIME   = 108;
static const int ID_AN_FPS    = 109;
static const int ID_AN_LOOP   = 110;
static const int ID_AN_STATUS = 111;
static const int ID_AN_ADDKEY = 112;
static const int ID_AN_DELKEY = 113;
static const int ID_AN_EDITVERTS = 114;
static const int ID_AN_UNDO = 115;
static const int ID_AN_REDO = 116;
static const int ID_AN_SELALL = 117;
static const int ID_AN_CLRSEL = 118;

// Returns the AnimatedMesh for the currently targeted mesh node, or nullptr.
static oz::AnimatedMesh* AnimTargetMesh() {
    MeshObjectNode* n = PawnSystem::Instance().GetMeshObject(g_editorPanels.animTargetMesh);
    if (!n || !n->mesh) return nullptr;
    return dynamic_cast<oz::AnimatedMesh*>(n->mesh.get());
}

static void PopulateAnimPanel(HWND hwnd, HWND hList, HWND hTime, HWND hFps, HWND hStatus, HWND hLoop) {
    SendMessage(hList, LB_RESETCONTENT, 0, 0);
    MeshObjectNode* n = PawnSystem::Instance().GetMeshObject(g_editorPanels.animTargetMesh);
    oz::AnimatedMesh* am = AnimTargetMesh();

    if (!n) {
        SetWindowTextA(hStatus, "No mesh selected. Select a GameEngine.Mesh object.");
        return;
    }
    if (!am) {
        SetWindowTextA(hStatus, n->animFile.empty()
            ? "Mesh has no anim file. Use \"Convert to Animated\" in Entity Properties."
            : "Anim file not loaded (check the path).");
        return;
    }

    // Clip list
    for (int i = 0; i < am->ClipCount(); i++)
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)am->ClipName(i));
    int cur = am->FindClip(g_editorPanels.animClipName);
    if (cur < 0 && am->ClipCount() > 0) cur = 0;
    if (cur >= 0) {
        SendMessage(hList, LB_SETCURSEL, cur, 0);
        g_editorPanels.animClipName = am->ClipName(cur);
    }

    const ozanim::Clip* clip = am->GetAnimation().FindClip(g_editorPanels.animClipName);
    float dur = clip ? clip->Duration() : 0.0f;
    wchar_t buf[128];
    if (clip) swprintf(buf, 128, L"%d clip(s) | %d verts | dur %.2fs", am->ClipCount(),
                       am->TotalVertexCount(), dur);
    else      swprintf(buf, 128, L"%d clip(s) | %d verts", am->ClipCount(), am->TotalVertexCount());
    SetWindowTextW(hStatus, buf);

    if (clip) {
        char fb[32]; snprintf(fb, sizeof(fb), "%g", clip->fps);
        SetWindowTextA(hFps, fb);
        SendMessage(hLoop, BM_SETCHECK, clip->loop ? BST_CHECKED : BST_UNCHECKED, 0);
        g_editorPanels.animFps = clip->fps;
        g_editorPanels.animLoop = clip->loop;
        int pos = (dur > 0.0f) ? (int)(g_editorPanels.animTime / dur * 1000.0f) : 0;
        if (pos < 0) pos = 0;
        if (pos > 1000) pos = 1000;
        SendMessage(hTime, SBM_SETPOS, pos, TRUE);
    }
}

static LRESULT CALLBACK AnimPanelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList, hTime, hFps, hStatus, hLoop, hEditBtn;
    switch (msg) {
    case WM_CREATE: {
        hList = CreateListBox(hwnd, 8, 8, 360, 130, ID_AN_LIST);
        CreateButton(hwnd, L"New Clip", 8, 144, 72, 26, ID_AN_NEW);
        CreateButton(hwnd, L"Delete",   84, 144, 60, 26, ID_AN_DELETE);
        CreateButton(hwnd, L"Save",    148, 144, 56, 26, ID_AN_SAVE);
        CreateButton(hwnd, L"Add Key", 208, 144, 68, 26, ID_AN_ADDKEY);
        CreateButton(hwnd, L"Del Key", 280, 144, 64, 26, ID_AN_DELKEY);

        CreateLabel(hwnd, L"FPS:", 8, 180, 32, 20, 0);
        hFps = CreateCtrl(hwnd, L"EDIT", L"30", 42, 178, 46, 22, ID_AN_FPS, WS_BORDER);
        hLoop = CreateWindowEx(0, L"BUTTON", L"Loop",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            96, 178, 64, 22, hwnd, (HMENU)(INT_PTR)ID_AN_LOOP, g_hInst, nullptr);
        SendMessage(hLoop, BM_SETCHECK, BST_CHECKED, 0);
        CreateButton(hwnd, L"Play",  166, 176, 62, 26, ID_AN_PLAY);
        CreateButton(hwnd, L"Pause", 232, 176, 62, 26, ID_AN_PAUSE);
        CreateButton(hwnd, L"Stop",  298, 176, 70, 26, ID_AN_STOP);

        hTime = CreateWindowEx(0, L"SCROLLBAR", L"",
            WS_CHILD | WS_VISIBLE | SBS_HORZ, 8, 210, 360, 18,
            hwnd, (HMENU)(INT_PTR)ID_AN_TIME, g_hInst, nullptr);
        SetScrollRange(hTime, SB_CTL, 0, 1000, TRUE);

        hStatus = CreateLabel(hwnd, L"", 8, 234, 360, 40, 0);

        hEditBtn = CreateWindowEx(0, L"BUTTON", L"Edit Verts",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            8, 276, 90, 24, hwnd, (HMENU)(INT_PTR)ID_AN_EDITVERTS, g_hInst, nullptr);
        CreateButton(hwnd, L"Undo", 104, 276, 56, 24, ID_AN_UNDO);
        CreateButton(hwnd, L"Redo", 164, 276, 56, 24, ID_AN_REDO);
        CreateButton(hwnd, L"Sel All", 224, 276, 66, 24, ID_AN_SELALL);
        CreateButton(hwnd, L"Clr Sel", 294, 276, 74, 24, ID_AN_CLRSEL);
        CreateButton(hwnd, L"Close", 8, 306, 80, 26, ID_AN_CLOSE);
        break;
    }
    case WM_USER + 50:
        PopulateAnimPanel(hwnd, hList, hTime, hFps, hStatus, hLoop);
        if (hEditBtn)
            SendMessage(hEditBtn, BM_SETCHECK, g_editorPanels.animEditVerts ? BST_CHECKED : BST_UNCHECKED, 0);
        break;
    case WM_HSCROLL: {
        if ((HWND)l == hTime) {
            int pos = (int)SendMessage(hTime, SBM_GETPOS, 0, 0);
            g_editorPanels.animTimeSlider = pos;
            g_editorPanels.actionAnimScrub = true;
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_AN_CLOSE) { ShowAnimPanel(false); break; }
        if (id == ID_AN_LIST && HIWORD(w) == LBN_SELCHANGE) {
            int s = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (s >= 0 && s < (int)SendMessage(hList, LB_GETCOUNT, 0, 0)) {
                char buf[128] = {0};
                SendMessageA(hList, LB_GETTEXT, s, (LPARAM)buf);
                g_editorPanels.animClipName = buf;
                g_editorPanels.animTime = 0.0f;
                g_editorPanels.actionAnimRefresh = true;
            }
            break;
        }
        if (id == ID_AN_NEW)    { g_editorPanels.actionAnimNewClip = true; break; }
        if (id == ID_AN_DELETE) { g_editorPanels.actionAnimDeleteClip = true; break; }
        if (id == ID_AN_SAVE)   { g_editorPanels.actionAnimSave = true; break; }
        if (id == ID_AN_ADDKEY) { g_editorPanels.actionAnimAddKey = true; break; }
        if (id == ID_AN_DELKEY) { g_editorPanels.actionAnimDeleteKey = true; break; }
        if (id == ID_AN_EDITVERTS) { g_editorPanels.actionAnimToggleEdit = true; break; }
        if (id == ID_AN_UNDO)   { g_editorPanels.actionAnimUndo = true; break; }
        if (id == ID_AN_REDO)   { g_editorPanels.actionAnimRedo = true; break; }
        if (id == ID_AN_SELALL) { g_editorPanels.actionAnimSelectAll = true; break; }
        if (id == ID_AN_CLRSEL) { g_editorPanels.actionAnimClearSel = true; break; }
        if (id == ID_AN_PLAY)   { g_editorPanels.animPlaying = true;  g_editorPanels.actionAnimRefresh = true; break; }
        if (id == ID_AN_PAUSE)  { g_editorPanels.animPlaying = false; break; }
        if (id == ID_AN_STOP)   { g_editorPanels.animPlaying = false; g_editorPanels.animTime = 0.0f; g_editorPanels.actionAnimRefresh = true; break; }
        if (id == ID_AN_FPS && HIWORD(w) == EN_CHANGE) {
            char b[32] = {0};
            GetWindowTextA(hFps, b, 32);
            float v = (float)atof(b);
            if (v > 0.0f) g_editorPanels.animFps = v;
            g_editorPanels.actionAnimApplyClipMeta = true;
            break;
        }
        if (id == ID_AN_LOOP && HIWORD(w) == BN_CLICKED) {
            g_editorPanels.animLoop =
                SendMessage(GetDlgItem(hwnd, ID_AN_LOOP), BM_GETCHECK, 0, 0) == BST_CHECKED;
            g_editorPanels.actionAnimApplyClipMeta = true;
            break;
        }
        break;
    }
    case WM_CLOSE: ShowAnimPanel(false); break;
    case WM_DESTROY: g_editorPanels.hAnimPanel = nullptr; break;
    default: return DefWindowProc(hwnd, msg, w, l);
    }
    return 0;
}

void ShowAnimPanel(bool show) {
    g_editorPanels.showAnimPanel = show;
    if (g_editorPanels.hAnimPanel) {
        ShowWindow((HWND)g_editorPanels.hAnimPanel, show ? SW_SHOW : SW_HIDE);
        if (show) SendMessage((HWND)g_editorPanels.hAnimPanel, WM_USER + 50, 0, 0);
    }
}

void RefreshAnimPanel() {
    if (g_editorPanels.hAnimPanel)
        SendMessage((HWND)g_editorPanels.hAnimPanel, WM_USER + 50, 0, 0);
}

// =====================================================================
// Public API — Create / Destroy
// =====================================================================
void CreateAllEditorWindows(void* hInst, void* hRaylibWnd) {
    g_hInst = (HINSTANCE)hInst;
    g_hRaylibWnd = (HWND)hRaylibWnd;

    // Initialize common controls for TreeView, etc.
    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_TREEVIEW_CLASSES };
    InitCommonControlsEx(&icex);

    // Initialize ListView common controls
    INITCOMMONCONTROLSEX icexLV = { sizeof(INITCOMMONCONTROLSEX), ICC_LISTVIEW_CLASSES };
    InitCommonControlsEx(&icexLV);

    RegisterPanelClass(CLASS_SOUNDMGR, SoundMgrProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_TEXTUREMGR, TextureMgrProc, (HINSTANCE)hInst);

    // Register texture grid custom control
    {
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = TextureGridProc;
        wc.hInstance = (HINSTANCE)hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = CLASS_TEXTURE_GRID;
        wc.cbWndExtra = sizeof(void*);
        RegisterClassEx(&wc);
    }

    RegisterPanelClass(CLASS_PAWNNMGR, PawnMgrProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_SCRIPTMGR, ScriptMgrProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_MODELBRW, ModelBrwProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_ENVPANEL, ZonePropertiesProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_PICKUPPANEL, PickupPanelProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_NODEPANEL, NodePanelProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_HMEDITOR, HmEditorProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_LIGHTPROPS, LightPropsProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_WORLDGRAPH, WorldGraphProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_PROPSPANEL, PropsPanelProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_ANIMPANEL, AnimPanelProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_LEVELLIST, LevelListProc, (HINSTANCE)hInst);

    // Stats sidebar uses dark background (override default COLOR_BTNFACE)
    {
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = StatsSidebarProc;
        wc.hInstance = (HINSTANCE)hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(25, 25, 30));
        wc.lpszClassName = CLASS_STATSSIDEBAR;
        RegisterClassEx(&wc);
    }

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
    create(CLASS_ENVPANEL,    L"Zone Properties",     g_editorPanels.envPanelPos,   g_editorPanels.hEnvPanel);
    create(CLASS_PICKUPPANEL, L"Pickups",             g_editorPanels.pickPanelPos,  g_editorPanels.hPickupPanel);
    create(CLASS_NODEPANEL,   L"Nodes",               g_editorPanels.nodePanelPos,  g_editorPanels.hNodePanel);
    create(CLASS_HMEDITOR,    L"Heightmap Editor",     g_editorPanels.heightmapEditorPos, g_editorPanels.hHeightmapEditor);
    create(CLASS_LIGHTPROPS,  L"Light Properties",     g_editorPanels.lightPropsPos,       g_editorPanels.hLightProps);
    create(CLASS_WORLDGRAPH,  L"World Graph Explorer", g_editorPanels.worldGraphPos,       g_editorPanels.hWorldGraph);
    create(CLASS_PROPSPANEL,  L"Entity Properties",    g_editorPanels.propsPanelPos,        g_editorPanels.hPropsPanel);
    create(CLASS_ANIMPANEL,   L"Animation",            g_editorPanels.animPanelPos,         g_editorPanels.hAnimPanel);
    create(CLASS_LEVELLIST,   L"Level List / Campaign",g_editorPanels.levelListPos,         g_editorPanels.hLevelList);

    // Docked native stats sidebar (child of raylib window)
    {
        // Prevent OpenGL/raylib from painting over child HWNDs
        if (g_hRaylibWnd) {
            LONG_PTR style = GetWindowLongPtr(g_hRaylibWnd, GWL_STYLE);
            SetWindowLongPtr(g_hRaylibWnd, GWL_STYLE, style | WS_CLIPCHILDREN);
        }
        RECT rc = {};
        if (g_hRaylibWnd) GetClientRect(g_hRaylibWnd, &rc);
        int top = 28;
        int h = (rc.bottom - rc.top) - top;
        if (h < 1) h = 400;
        HWND hSb = CreateWindowEx(0, CLASS_STATSSIDEBAR, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, top, STATS_SIDEBAR_W, h,
            g_hRaylibWnd, nullptr, (HINSTANCE)hInst, nullptr);
        g_editorPanels.hStatsSidebar = hSb;
        if (hSb) {
            ShowWindow(hSb, SW_SHOW);
            UpdateWindow(hSb);
        }
    }

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
    destroy(g_editorPanels.hAnimPanel);
    destroy(g_editorPanels.hScriptMgr);
    destroy(g_editorPanels.hModelBrowser);
    destroy(g_editorPanels.hEnvPanel);
    destroy(g_editorPanels.hPickupPanel);
    destroy(g_editorPanels.hNodePanel);
    destroy(g_editorPanels.hHeightmapEditor);
    destroy(g_editorPanels.hLightProps);
    destroy(g_editorPanels.hWorldGraph);
    destroy(g_editorPanels.hPropsPanel);
    destroy(g_editorPanels.hLevelList);
    destroy(g_editorPanels.hStatsSidebar);
    if (g_sbBgBrush) { DeleteObject(g_sbBgBrush); g_sbBgBrush = nullptr; }
    if (g_editorPanels.hPreviewBitmap) {
        DeleteObject((HGDIOBJ)g_editorPanels.hPreviewBitmap);
        g_editorPanels.hPreviewBitmap = nullptr;
    }
}
