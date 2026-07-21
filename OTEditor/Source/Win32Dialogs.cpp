#define WIN32_LEAN_AND_MEAN
#include "Win32Dialogs.hpp"
#include <windows.h>
#include <commctrl.h>
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

// Environment settings (read by editor rendering loop)
static EnvSettings g_env;

// Pawn data
struct PawnDef {
    std::string name;
    std::string meshPath;
};
static std::vector<PawnDef> g_pawns;

// Model preview sequence number (for refresh)
static int g_previewSeq = 0;

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

// =====================================================================
// Window class registration
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

// =====================================================================
// Helper: create a basic child control
// =====================================================================
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
                      WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOCOLUMNHEADER);
}

// =====================================================================
// Sound Manager
// =====================================================================
static const int ID_SOUND_CLOSE = 100;

void ShowSoundManager(bool show) {
    g_editorPanels.showSoundMgr = show;
    if (g_editorPanels.hSoundMgr)
        ShowWindow(g_editorPanels.hSoundMgr, show ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK SoundMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        CreateLabel(hwnd, L"Sound Manager — Placeholder", 10, 10, 360, 20, 1);
        CreateLabel(hwnd, L"Sound list and preview coming soon.", 10, 40, 360, 40, 2);
        CreateButton(hwnd, L"Close", 280, 220, 100, 28, ID_SOUND_CLOSE);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_SOUND_CLOSE) ShowSoundManager(false);
        break;
    }
    case WM_CLOSE:
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
// Texture Manager
// =====================================================================
static const int ID_TEX_CLOSE = 100;

void ShowTextureManager(bool show) {
    g_editorPanels.showTextureMgr = show;
    if (g_editorPanels.hTextureMgr)
        ShowWindow(g_editorPanels.hTextureMgr, show ? SW_SHOW : SW_HIDE);
}

void UpdateTextureManagerList() {
    if (!g_editorPanels.hTextureMgr) return;
    // Send a custom message to rebuild texture list
    SendMessage(g_editorPanels.hTextureMgr, WM_USER + 50, 0, 0);
}

static LRESULT CALLBACK TextureMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList;
    switch (msg) {
    case WM_CREATE: {
        CreateLabel(hwnd, L"Loaded Textures:", 10, 10, 200, 20, 1);
        hList = CreateListBox(hwnd, 10, 35, 440, 240, 2);
        CreateButton(hwnd, L"Close", 340, 285, 120, 28, ID_TEX_CLOSE);
        break;
    }
    case WM_USER + 50: {
        // Rebuild list — in future this reads from shared texture state
        SendMessage(hList, LB_RESETCONTENT, 0, 0);
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)"Model1Texture");
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)"Model2Texture");
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)"Model3Texture");
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)"Model4Texture");
        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)"HeightMapTexture");
        break;
    }
    case WM_COMMAND:
        if (LOWORD(w) == ID_TEX_CLOSE) ShowTextureManager(false);
        break;
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

// =====================================================================
// Pawn Manager
// =====================================================================
static const int ID_PAWN_CLOSE = 100;
static const int ID_PAWN_ADD = 101;
static const int ID_PAWN_LIST = 102;
static const int ID_PAWN_SPAWN = 103; // spawn selected pawn in world

void ShowPawnManager(bool show) {
    g_editorPanels.showPawnMgr = show;
    if (g_editorPanels.hPawnMgr)
        ShowWindow(g_editorPanels.hPawnMgr, show ? SW_SHOW : SW_HIDE);
}

void PawnManagerAddPawn(const char* name, const char* meshPath) {
    g_pawns.push_back({name, meshPath});
    if (g_editorPanels.hPawnMgr) {
        SendMessage(g_editorPanels.hPawnMgr, WM_USER + 50, 0, 0);
    }
}

int GetPawnCount() { return (int)g_pawns.size(); }
const char* GetPawnName(int index) {
    if (index < 0 || index >= (int)g_pawns.size()) return nullptr;
    return g_pawns[index].name.c_str();
}

static LRESULT CALLBACK PawnMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList;
    switch (msg) {
    case WM_CREATE: {
        CreateLabel(hwnd, L"Pawn Definitions:", 10, 10, 200, 20, 1);
        hList = CreateListBox(hwnd, 10, 35, 320, 100, ID_PAWN_LIST);
        CreateButton(hwnd, L"Spawn in World", 10, 140, 120, 28, ID_PAWN_SPAWN);
        CreateButton(hwnd, L"Add Pawn...", 10, 172, 100, 28, ID_PAWN_ADD);
        CreateButton(hwnd, L"Close", 220, 172, 120, 28, ID_PAWN_CLOSE);
        // Add default pawns
        PawnManagerAddPawn("Default Player", "GameData/Models/Player.obj");
        PawnManagerAddPawn("Skaarj Trooper", "GameData/Models/Skaarj.obj");
        break;
    }
    case WM_USER + 50: {
        // Refresh list from g_pawns
        SendMessage(hList, LB_RESETCONTENT, 0, 0);
        for (auto& p : g_pawns)
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)p.name.c_str());
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_PAWN_CLOSE) ShowPawnManager(false);
        else if (id == ID_PAWN_SPAWN) {
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_pawns.size()) {
                g_editorPanels.actionSpawnPawn = sel;
            }
        } else if (id == ID_PAWN_ADD) {
            // In future: open a dialog to input name + mesh path
            MessageBoxA(hwnd, "Pawn addition dialog coming soon.\n"
                       "Use PawnManagerAddPawn() from code for now.",
                       "Add Pawn", MB_OK);
        } else if (id == ID_PAWN_LIST && HIWORD(w) == LBN_DBLCLK) {
            int sel = (int)SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_pawns.size()) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Selected: %s\nMesh: %s",
                         g_pawns[sel].name.c_str(), g_pawns[sel].meshPath.c_str());
                MessageBoxA(hwnd, msg, "Pawn Info", MB_OK);
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
// Script Manager
// =====================================================================
static const int ID_SCRIPT_CLOSE = 100;

void ShowScriptManager(bool show) {
    g_editorPanels.showScriptMgr = show;
    if (g_editorPanels.hScriptMgr)
        ShowWindow(g_editorPanels.hScriptMgr, show ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK ScriptMgrProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList;
    switch (msg) {
    case WM_CREATE: {
        CreateLabel(hwnd, L"Available Scripts (WDL Script1-10):", 10, 10, 380, 20, 1);
        hList = CreateListBox(hwnd, 10, 35, 380, 220, 2);
        CreateButton(hwnd, L"Close", 300, 265, 100, 28, ID_SCRIPT_CLOSE);
        // Populate script list
        for (int i = 1; i <= 10; i++) {
            char label[64];
            snprintf(label, sizeof(label), "Script%d.ps", i);
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)label);
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(w) == ID_SCRIPT_CLOSE) ShowScriptManager(false);
        break;
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
static const int ID_MDL_LIST = 100;
static const int ID_MDL_REFRESH = 101;
static const int ID_MDL_PLACE = 102;
static const int ID_MDL_CLOSE = 103;
static const int ID_MDL_PREVIEW = 104; // static control for preview bitmap

// Custom messages
static const UINT WM_MODEL_SELECTED = WM_USER + 100; // sent to raylib wnd
static const UINT WM_PREVIEW_READY  = WM_USER + 101; // sent to dialog wnd
// wParam = (WPARAM)HBITMAP, lParam = MAKELPARAM(w, h)

void ShowModelBrowser(bool show) {
    g_editorPanels.showModelBrowser = show;
    if (g_editorPanels.hModelBrowser)
        ShowWindow(g_editorPanels.hModelBrowser, show ? SW_SHOW : SW_HIDE);
}

void ScanModelBrowserFiles() {
    g_editorPanels.modelEntries.clear();
    g_editorPanels.selectedModel = -1;
    fs::path base = fs::current_path() / "GameData";
    try {
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
    } catch (...) {}
    // Sort by name
    std::sort(g_editorPanels.modelEntries.begin(), g_editorPanels.modelEntries.end(),
        [](auto& a, auto& b) { return a.name < b.name; });
    // Refresh listbox
    if (g_editorPanels.hModelBrowser)
        SendMessage(g_editorPanels.hModelBrowser, WM_USER + 50, 0, 0);
}

void UpdateModelPreview(HBITMAP hBmp, int w, int h) {
    if (g_editorPanels.hPreviewBitmap)
        DeleteObject(g_editorPanels.hPreviewBitmap);
    g_editorPanels.hPreviewBitmap = hBmp;
    g_editorPanels.previewW = w;
    g_editorPanels.previewH = h;
    if (g_editorPanels.hModelBrowser) {
        // WM_PREVIEW_READY: wParam=HBITMAP, lParam=MAKELPARAM(w,h)
        PostMessage(g_editorPanels.hModelBrowser, WM_PREVIEW_READY,
                    (WPARAM)hBmp, MAKELPARAM(w, h));
    }
}

static LRESULT CALLBACK ModelBrwProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static HWND hList, hPreview;
    int id;
    switch (msg) {
    case WM_CREATE: {
        int PW = 540, PH = 500;
        // Left: listbox
        hList = CreateListBox(hwnd, 8, 32, 230, PH - 100, ID_MDL_LIST);
        // Right: preview area
        hPreview = CreateWindowEx(WS_EX_STATICEDGE, L"STATIC", L"",
             WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
             248, 32, 260, 220, hwnd, (HMENU)(INT_PTR)ID_MDL_PREVIEW,
             g_hInst, nullptr);
        // Info text
        CreateLabel(hwnd, L"Select a model from the list", 248, 260, 260, 60, 2);
        // Buttons
        CreateButton(hwnd, L"Refresh", 8, 4, 80, 22, ID_MDL_REFRESH);
        CreateButton(hwnd, L"Place in World", 248, 340, 120, 24, ID_MDL_PLACE);
        CreateButton(hwnd, L"Close", PW - 72, PH - 28, 64, 22, ID_MDL_CLOSE);
        break;
    }
    case WM_USER + 50: {
        // Refresh list from g_editorPanels.modelEntries
        SendMessage(hList, LB_RESETCONTENT, 0, 0);
        for (auto& e : g_editorPanels.modelEntries)
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)e.name.c_str());
        break;
    }
    case WM_PREVIEW_READY: {
        // Receives HBITMAP from raylib main loop
        HBITMAP hBmp = (HBITMAP)w;
        int bw = LOWORD(l), bh = HIWORD(l);
        if (hBmp) {
            // Set as preview static control image
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
        // Owner-draw for preview static
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)l;
        if (dis->CtlID == ID_MDL_PREVIEW && g_editorPanels.hPreviewBitmap) {
            HDC hdc = dis->hDC;
            RECT r = dis->rcItem;
            BITMAP bm;
            GetObject(g_editorPanels.hPreviewBitmap, sizeof(bm), &bm);
            HDC hdcMem = CreateCompatibleDC(hdc);
            SelectObject(hdcMem, g_editorPanels.hPreviewBitmap);
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
            DeleteObject(g_editorPanels.hPreviewBitmap);
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
static const int ID_ENV_CLOSE = 100;
static const int ID_ENV_APPLY_FOG = 101;
static const int ID_ENV_APPLY_AMB = 102;
// Scrollbar IDs for sliders
static const int ID_SB_FOG_R = 110, ID_SB_FOG_G = 111, ID_SB_FOG_B = 112;
static const int ID_SB_FOG_DENSITY = 113;
static const int ID_SB_AMB_R = 114, ID_SB_AMB_G = 115, ID_SB_AMB_B = 116;
static const int ID_SB_AMB_INT = 117;

void ShowEnvPanel(bool show) {
    g_editorPanels.showEnvPanel = show;
    if (g_editorPanels.hEnvPanel)
        ShowWindow(g_editorPanels.hEnvPanel, show ? SW_SHOW : SW_HIDE);
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
        // Fog section
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
        // Update env state when any scrollbar changes
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
static const int ID_PICK_CLOSE = 100;
static const int ID_PICK_HEALTH = 101;
static const int ID_PICK_MANA = 102;
static const int ID_PICK_ENERGY = 103;
static const int ID_PICK_KEY = 104;
static const int ID_PICK_COIN = 105;
static const int ID_PICK_POWERUP = 106;

static int g_lastPickupType = 0; // 0=Health, 1=Mana, 2=Energy, etc.

void ShowPickupPanel(bool show) {
    g_editorPanels.showPickupPanel = show;
    if (g_editorPanels.hPickupPanel)
        ShowWindow(g_editorPanels.hPickupPanel, show ? SW_SHOW : SW_HIDE);
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
static const int ID_NODE_NPC = 102;
static const int ID_NODE_LIGHT = 103;

void ShowNodePanel(bool show) {
    g_editorPanels.showNodePanel = show;
    if (g_editorPanels.hNodePanel)
        ShowWindow(g_editorPanels.hNodePanel, show ? SW_SHOW : SW_HIDE);
}

static LRESULT CALLBACK NodePanelProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        CreateLabel(hwnd, L"Nodes", 10, 10, 100, 20, 1);
        CreateButton(hwnd, L"Player Spawn", 10, 35, 160, 24, ID_NODE_SPAWN);
        CreateButton(hwnd, L"NPC Spawn", 10, 63, 160, 24, ID_NODE_NPC);
        CreateButton(hwnd, L"Point Light", 10, 91, 160, 24, ID_NODE_LIGHT);
        CreateButton(hwnd, L"Close", 50, 135, 100, 28, ID_NODE_CLOSE);
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
// Public API — Create / Destroy
// =====================================================================
void CreateAllEditorWindows(HINSTANCE hInst, HWND hRaylibWnd) {
    g_hInst = hInst;
    g_hRaylibWnd = hRaylibWnd;

    // Register ALL window classes
    RegisterPanelClass(CLASS_SOUNDMGR, SoundMgrProc, hInst);
    RegisterPanelClass(CLASS_TEXTUREMGR, TextureMgrProc, hInst);
    RegisterPanelClass(CLASS_PAWNNMGR, PawnMgrProc, hInst);
    RegisterPanelClass(CLASS_SCRIPTMGR, ScriptMgrProc, hInst);
    RegisterPanelClass(CLASS_MODELBRW, ModelBrwProc, hInst);
    RegisterPanelClass(CLASS_ENVPANEL, EnvPanelProc, hInst);
    RegisterPanelClass(CLASS_PICKUPPANEL, PickupPanelProc, hInst);
    RegisterPanelClass(CLASS_NODEPANEL, NodePanelProc, hInst);

    auto create = [&](const wchar_t* cls, const wchar_t* title,
                      EditorPanelState::WinPos& pos, HWND& out) {
        out = CreateWindowEx(WS_EX_DLGMODALFRAME,
              cls, title,
              WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
              pos.x, pos.y, pos.w, pos.h,
              hRaylibWnd, nullptr, hInst, nullptr);
        // Hide initially
        if (out) ShowWindow(out, SW_HIDE);
    };

    create(CLASS_SOUNDMGR,    L"Sound Manager",       g_editorPanels.soundMgrPos,   g_editorPanels.hSoundMgr);
    create(CLASS_TEXTUREMGR,  L"Texture Manager",     g_editorPanels.textureMgrPos, g_editorPanels.hTextureMgr);
    create(CLASS_PAWNNMGR,    L"Pawn Manager",        g_editorPanels.pawnMgrPos,    g_editorPanels.hPawnMgr);
    create(CLASS_SCRIPTMGR,   L"Script Manager",      g_editorPanels.scriptMgrPos,  g_editorPanels.hScriptMgr);
    create(CLASS_MODELBRW,    L"Model / Mesh Browser", g_editorPanels.modelBrwPos,  g_editorPanels.hModelBrowser);
    create(CLASS_ENVPANEL,    L"Environment Settings", g_editorPanels.envPanelPos,  g_editorPanels.hEnvPanel);
    create(CLASS_PICKUPPANEL, L"Pickups",             g_editorPanels.pickPanelPos,  g_editorPanels.hPickupPanel);
    create(CLASS_NODEPANEL,   L"Nodes",               g_editorPanels.nodePanelPos,  g_editorPanels.hNodePanel);

    // Initial texture list population
    if (g_editorPanels.hTextureMgr)
        SendMessage(g_editorPanels.hTextureMgr, WM_USER + 50, 0, 0);

    // Scan model files
    ScanModelBrowserFiles();
}

void DestroyAllEditorWindows() {
    auto destroy = [](HWND& hwnd) {
        if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }
    };
    destroy(g_editorPanels.hSoundMgr);
    destroy(g_editorPanels.hTextureMgr);
    destroy(g_editorPanels.hPawnMgr);
    destroy(g_editorPanels.hScriptMgr);
    destroy(g_editorPanels.hModelBrowser);
    destroy(g_editorPanels.hEnvPanel);
    destroy(g_editorPanels.hPickupPanel);
    destroy(g_editorPanels.hNodePanel);
    if (g_editorPanels.hPreviewBitmap) {
        DeleteObject(g_editorPanels.hPreviewBitmap);
        g_editorPanels.hPreviewBitmap = nullptr;
    }
}
