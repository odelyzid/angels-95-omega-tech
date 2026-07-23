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
static const wchar_t* CLASS_TEXTURE_GRID = L"OzTextureGrid";
static const wchar_t* CLASS_PAWNNMGR    = L"OzPawnMgr";
static const wchar_t* CLASS_SCRIPTMGR   = L"OzScriptMgr";
static const wchar_t* CLASS_MODELBRW    = L"OzModelBrw";
static const wchar_t* CLASS_ENVPANEL    = L"OzZoneProperties";
static const wchar_t* CLASS_PICKUPPANEL = L"OzPickupPanel";
static const wchar_t* CLASS_NODEPANEL   = L"OzNodePanel";
static const wchar_t* CLASS_HMEDITOR   = L"OzHmEditor";
static const wchar_t* CLASS_CSGSIDEBAR = L"OzCsgSidebar";
static const wchar_t* CLASS_LIGHTPROPS = L"OzLightProps";

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

    std::sort(out.begin(), out.end(),
        [](const ResourceEntry& a, const ResourceEntry& b) { return a.name < b.name; });
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
static LRESULT CALLBACK CsgSidebarProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);
static LRESULT CALLBACK LightPropsProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l);

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
    // Only scan packages, not filesystem
    std::vector<std::string> pkgFiles;
    PackageAssetLoader::Instance().ListAllFiles(pkgFiles);
    const std::vector<std::string> exts = { ".png", ".tga", ".bmp", ".jpg", ".jpeg", ".gif" };
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
    std::sort(g_textureFiles.begin(), g_textureFiles.end(),
        [](const ResourceEntry& a, const ResourceEntry& b) { return a.name < b.name; });
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

        // Toolbar: Add Package + Refresh + Close
        CreateButton(hwnd, L"Add Package", x, y, 100, 24, ID_TEX_ADDPKG);
        CreateButton(hwnd, L"Refresh", x + 106, y, 70, 24, ID_TEX_REFRESH);
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
        SetWindowPos(GetDlgItem(hwnd, ID_TEX_REFRESH), NULL, x + 106, 10, 70, 24, SWP_NOZORDER);
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
            Image img = LoadImageWithFallback(tex.path.c_str());
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
        } else if (id == ID_TEX_LIST && notify == 1) {
            // Grid selection changed — update preview
            if (hGrid) {
                auto* gs = (TextureGridState*)GetWindowLongPtr(hGrid, GWLP_USERDATA);
                int sel = gs ? gs->selectedIdx : -1;
                if (sel >= 0 && sel < (int)g_textureFiles.size()) {
                    std::string& p = g_textureFiles[sel].path;
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
    dialog.lpstrFilter = save ? L"World Files (*.wdl;*.ozone)\0*.wdl;*.ozone\0WDL World (*.wdl)\0*.wdl\0OZONE World (*.ozone)\0*.ozone\0\0"
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
    omegaPlayer.label = "OmegaPlayer";
    omegaPlayer.defName = "OmegaPlayer";  // Special: not in defs
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

    // InventoryPawn branch — pickups
    PawnTreeNode invBranch;
    invBranch.label = "InventoryPawn";
    invBranch.isExpanded = false;
    invBranch.typeTag = "category";

    PawnTreeNode pickupBranch;
    pickupBranch.label = "Pickups";
    pickupBranch.isExpanded = false;
    pickupBranch.typeTag = "category";
    const char* pickupNames[] = {"HealthVial", "ManaVial", "EnergyCrystal", "Key", "Coin", "Powerup"};
    for (auto& pn : pickupNames) {
        PawnTreeNode leaf;
        leaf.label = pn;
        leaf.defName = pn;
        leaf.typeTag = "pickup";
        pickupBranch.children.push_back(leaf);
    }
    invBranch.children.push_back(pickupBranch);

    // Weapon stubs
    PawnTreeNode weaponBranch;
    weaponBranch.label = "Weapons";
    weaponBranch.isExpanded = false;
    weaponBranch.typeTag = "category";
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

    pawnBranch.children.push_back(volBranch);
    root.children.push_back(pawnBranch);
    return root;
}

static LRESULT CALLBACK PawnMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hTree;
    switch (msg) {
    case WM_CREATE: {
        CreateLabel(hwnd, L"Actor Hierarchy (right-click to spawn):", 10, 10, 350, 20, 1);
        hTree = CreateWindowEx(WS_EX_CLIENTEDGE, WC_TREEVIEW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            10, 35, 360, 120, hwnd, (HMENU)(INT_PTR)ID_PAWN_TREE, g_hInst, nullptr);
        CreateButton(hwnd, L"Spawn Selected", 10, 165, 100, 28, ID_PAWN_SPAWN);
        CreateButton(hwnd, L"Refresh", 120, 165, 80, 28, ID_PAWN_REFRESH);
        CreateButton(hwnd, L"Close", 210, 165, 160, 28, ID_PAWN_CLOSE);
        SendMessage(hwnd, WM_USER + 50, 0, 0);
        break;
    }
    case WM_USER + 50: {
        PopulateTreeView(hTree);
        break;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)l;
        if (nm->idFrom == ID_PAWN_TREE && nm->code == NM_DBLCLK) {
            // Double-click on leaf — show info
            TVITEMW item;
            item.hItem = (HTREEITEM)SendMessage(hTree, TVM_GETNEXTITEM, TVGN_CARET, 0);
            item.mask = TVIF_PARAM;
            if (item.hItem && SendMessage(hTree, TVM_GETITEMW, 0, (LPARAM)&item)) {
                char* paramStr = (char*)item.lParam;
                if (paramStr) {
                    char* pipe = strchr(paramStr, '|');
                    if (pipe) {
                        *pipe = '\0';
                        const char* typeTag = paramStr;
                        const char* defName = pipe + 1;
                        if (strlen(defName) > 0) {
                            char msgBuf[256];
                            snprintf(msgBuf, sizeof(msgBuf), "Type: %s\nDefinition: %s", typeTag, defName);
                            MessageBoxA(hwnd, msgBuf, "Entity Info", MB_OK);
                        }
                    }
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
            // Spawn selected leaf node into world
            TVITEMW item;
            item.hItem = (HTREEITEM)SendMessage(hTree, TVM_GETNEXTITEM, TVGN_CARET, 0);
            item.mask = TVIF_PARAM;
            if (item.hItem && SendMessage(hTree, TVM_GETITEMW, 0, (LPARAM)&item)) {
                char* paramStr = (char*)item.lParam;
                if (paramStr) {
                    char* pipe = strchr(paramStr, '|');
                    if (pipe) {
                        *pipe = '\0';
                        const char* defName = pipe + 1;
                        if (strlen(defName) > 0) {
                            // Find index in g_pawns list
                            for (int i = 0; i < (int)g_pawns.size(); i++) {
                                if (g_pawns[i].name == defName) {
                                    g_editorPanels.actionSpawnPawn = i;
                                    break;
                                }
                            }
                        }
                    }
                }
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
// Script Manager Ã¢â‚¬â€ Dynamic file scanning (GameData/ + packages)
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
}

// Tab IDs
static const int ID_ZONE_TAB_FOG = 190;
static const int ID_ZONE_TAB_AMB = 191;
static const int ID_ZONE_TAB_GT  = 192;
static const int ID_ZONE_TAB_PAR = 193;
static const int ID_ZONE_CLOSE   = 199;

// Fog controls
static const int ID_SB_FOG_R = 110, ID_SB_FOG_G = 111, ID_SB_FOG_B = 112;
static const int ID_SB_FOG_DENSITY = 113;
static const int ID_SF_FOG_START = 114, ID_SF_FOG_END = 115;
static const int ID_ZONE_APPLY_FOG = 140;

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

// Tab control groups: 0=Fog, 1=Ambient, 2=GameType, 3=Particles
static std::vector<HWND> g_zoneControlGroups[4];

static void ShowZoneTab(HWND hwnd, int tab) {
    if (tab < 0 || tab > 3) return;
    g_zoneTab = tab;
    for (int t = 0; t < 4; t++) {
        for (auto& h : g_zoneControlGroups[t]) {
            ShowWindow(h, (t == tab) ? SW_SHOW : SW_HIDE);
        }
    }
}

static LRESULT CALLBACK ZonePropertiesProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        int x = 8, y = 8, gap = 26;

        // Clear previous control groups
        for (int t = 0; t < 4; t++) g_zoneControlGroups[t].clear();

        // Tab buttons
        CreateButton(hwnd, L"Fog",       x, y, 70, 24, ID_ZONE_TAB_FOG);
        CreateButton(hwnd, L"Ambient",   x + 74, y, 70, 24, ID_ZONE_TAB_AMB);
        CreateButton(hwnd, L"GameType",  x + 148, y, 80, 24, ID_ZONE_TAB_GT);
        CreateButton(hwnd, L"Particles", x + 232, y, 80, 24, ID_ZONE_TAB_PAR);
        y += 30;

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

        // --- Ambient tab (1) ---
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

        // --- Particles tab (3) ---
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

        // Close button (bottom of panel)
        CreateButton(hwnd, L"Close", 300, y, 90, 26, ID_ZONE_CLOSE);

        // Show first tab only
        ShowZoneTab(hwnd, 0);
        break;
    }
    case WM_HSCROLL: {
        auto getPos = [hwnd](int id) -> int {
            return (int)SendDlgItemMessage(hwnd, id, SBM_GETPOS, 0, 0);
        };
        g_zoneProps.fogR = getPos(ID_SB_FOG_R);
        g_zoneProps.fogG = getPos(ID_SB_FOG_G);
        g_zoneProps.fogB = getPos(ID_SB_FOG_B);
        g_zoneProps.fogDensity = getPos(ID_SB_FOG_DENSITY) / 1000.0f;
        g_zoneProps.ambR = getPos(ID_SB_AMB_R);
        g_zoneProps.ambG = getPos(ID_SB_AMB_G);
        g_zoneProps.ambB = getPos(ID_SB_AMB_B);
        g_zoneProps.ambIntensity = getPos(ID_SB_AMB_INT) / 100.0f;
        g_zoneProps.particleDensity = (float)getPos(ID_SB_PAR_DENSITY);
        g_zoneProps.particleSpeed = getPos(ID_SB_PAR_SPEED) / 10.0f;
        g_zoneProps.particleColorR = getPos(ID_SB_PAR_R);
        g_zoneProps.particleColorG = getPos(ID_SB_PAR_G);
        g_zoneProps.particleColorB = getPos(ID_SB_PAR_B);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_ZONE_CLOSE) { ShowEnvPanel(false); break; }
        if (id == ID_ZONE_TAB_FOG) { ShowZoneTab(hwnd, 0); break; }
        if (id == ID_ZONE_TAB_AMB) { ShowZoneTab(hwnd, 1); break; }
        if (id == ID_ZONE_TAB_GT)  { ShowZoneTab(hwnd, 2); break; }
        if (id == ID_ZONE_TAB_PAR) { ShowZoneTab(hwnd, 3); break; }

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
        if (id == ID_ZONE_APPLY_AMB) {
            g_zoneProps.applyAmbient = true;
            break;
        }
        if (id == ID_ZONE_APPLY_PAR) {
            g_zoneProps.applyParticles = true;
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
// Heightmap Editor Ã¢â‚¬â€ Load, configure, and preview terrain heightmaps
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
// CSG Sidebar Ã¢â‚¬â€ left-side brush tool window (like UnrealEd)
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
    if (g_editorPanels.hLightProps)
        ShowWindow((HWND)g_editorPanels.hLightProps, show ? SW_SHOW : SW_HIDE);
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
// Public API — Create / Destroy
// =====================================================================
void CreateAllEditorWindows(void* hInst, void* hRaylibWnd) {
    g_hInst = (HINSTANCE)hInst;
    g_hRaylibWnd = (HWND)hRaylibWnd;

    // Initialize common controls for TreeView, etc.
    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_TREEVIEW_CLASSES };
    InitCommonControlsEx(&icex);

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
    RegisterPanelClass(CLASS_CSGSIDEBAR, CsgSidebarProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_LIGHTPROPS, LightPropsProc, (HINSTANCE)hInst);

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
    create(CLASS_CSGSIDEBAR,  L"CSG Brushes",          g_editorPanels.csgSidebarPos,      g_editorPanels.hCsgSidebar);
    create(CLASS_LIGHTPROPS,  L"Light Properties",     g_editorPanels.lightPropsPos,       g_editorPanels.hLightProps);

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
    destroy(g_editorPanels.hLightProps);
    if (g_editorPanels.hPreviewBitmap) {
        DeleteObject((HGDIOBJ)g_editorPanels.hPreviewBitmap);
        g_editorPanels.hPreviewBitmap = nullptr;
    }
}
