// ============================================================================
// UiShell.cpp - the single translation unit for AngelEd's UI layer.
//
// WHY A UNITY BUILD
//
// Splitting a 6,600-line .cpp into 16 files normally means 16 compile rules in
// AngelEd/Makefile AND 16 `g++ -c` lines plus 16 link entries in
// .github/workflows/ci.yml, because CI builds the editor by hand rather than
// through the Makefile. That is the "added in one place, CI breaks silently" trap
// from AGENTS.md multiplied by sixteen, and the two lists can drift without any
// signal.
//
// So the panels are FRAGMENTS: real, separately-editable files that are #included
// here. One object, one Makefile rule, one CI line.
//
// The cost is honest and worth stating: a fragment can rely on an include it does
// not state, because they all share this file scope. That is why every fragment
// starts with a banner saying it is not a standalone TU. Panel code is UI-facing
// and its dependencies are the panels, so in practice the coupling is along
// lines the layer rule already accepts (UI -> Subsystems/Resources, never back up).
//
// FRAGMENTS ARE NOT INCLUDED IN ANY ORDER-SENSITIVE WAY beyond needing the
// preamble: every declaration they share lives in UiCommon.hpp, which is included
// first.
// ============================================================================

#include "UiCommon.hpp"

// ---------------------------------------------------------------------------
// Panels. Each is one window: its control IDs, its window procedure, and its
// Show*/Refresh* entry points.
// ---------------------------------------------------------------------------
#include "Panels/SoundPanel.cpp"
#include "Panels/TexturePanel.cpp"
#include "Panels/LevelState.cpp"
#include "Panels/PickupPanel.cpp"
#include "Panels/NodePanel.cpp"
#include "Panels/HeightmapPanel.cpp"
#include "Panels/ModelPanel.cpp"
#include "Panels/PawnPanel.cpp"
#include "Panels/ScriptPanel.cpp"
#include "Panels/WorldGraphPanel.cpp"
#include "Panels/LevelListPanel.cpp"
#include "Panels/PropsPanel.cpp"
#include "Panels/StatsSidebar.cpp"
#include "Panels/AnimPanel.cpp"
#include "Panels/SurfacePropsPanel.cpp"

// ============================================================================
// CreateAllEditorWindows / DestroyAllEditorWindows
//
// These stay HERE rather than in a fragment because they name every panel class,
// which makes this the one place that shows the layer's full surface. If a panel is
// added without being registered here it will not appear, and this function is
// where you look.
// ============================================================================

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
    RegisterPanelClass(CLASS_PICKUPPANEL, PickupPanelProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_NODEPANEL, NodePanelProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_HMEDITOR, HmEditorProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_WORLDGRAPH, WorldGraphProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_PROPSPANEL, PropsPanelProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_ANIMPANEL, AnimPanelProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_LEVELLIST, LevelListProc, (HINSTANCE)hInst);
    RegisterPanelClass(CLASS_SURFACEPROPS, SurfacePropsProc, (HINSTANCE)hInst);

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
    create(CLASS_PICKUPPANEL, L"Pickups",             g_editorPanels.pickPanelPos,  g_editorPanels.hPickupPanel);
    create(CLASS_NODEPANEL,   L"Nodes",               g_editorPanels.nodePanelPos,  g_editorPanels.hNodePanel);
    create(CLASS_HMEDITOR,    L"Heightmap Editor",     g_editorPanels.heightmapEditorPos, g_editorPanels.hHeightmapEditor);
    create(CLASS_WORLDGRAPH,  L"World Graph Explorer", g_editorPanels.worldGraphPos,       g_editorPanels.hWorldGraph);
    create(CLASS_PROPSPANEL,  L"Entity Properties",    g_editorPanels.propsPanelPos,        g_editorPanels.hPropsPanel);
    create(CLASS_ANIMPANEL,   L"Animation",            g_editorPanels.animPanelPos,         g_editorPanels.hAnimPanel);
    create(CLASS_LEVELLIST,   L"Level List / Campaign",g_editorPanels.levelListPos,         g_editorPanels.hLevelList);
    create(CLASS_SURFACEPROPS,L"Surface Properties",    g_editorPanels.surfacePropsPos,     g_editorPanels.hSurfaceProps);

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
    destroy(g_editorPanels.hPickupPanel);
    destroy(g_editorPanels.hNodePanel);
    destroy(g_editorPanels.hHeightmapEditor);
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

