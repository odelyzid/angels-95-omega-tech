// =============================================================================
// Core/EditorMenus.cpp
//
// The native menu bar construction.
//
// FRAGMENT - not a standalone translation unit. Included by Main.cpp, which is the
// single TU for AngelEd's core layer. See Wiki/Editor-Architecture-Refactor.md.
// =============================================================================

static void CreateEditorMenuBar() {
#ifdef _WIN32
    HWND hWnd = (HWND)GetWindowHandle();
    if (!hWnd) return;

    // Subclass the raylib/GLFW window so we can intercept menu WM_COMMAND
    g_originalWndProc = (WNDPROC)SetWindowLongPtr(hWnd, GWLP_WNDPROC, (LONG_PTR)EditorWndProc);

    HMENU hMenu = CreateMenu();
    HMENU hFile = CreatePopupMenu();
    AppendMenuA(hFile, MF_STRING, IDM_NEW, "&New\tN");
    AppendMenuA(hFile, MF_STRING, IDM_OPEN, "&Open...\tO");
    AppendMenuA(hFile, MF_STRING, IDM_SAVE, "&Save\tS");
    AppendMenuA(hFile, MF_STRING, IDM_SAVE_AS, "Save &As...");
    AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFile, MF_STRING, IDM_PLAY_TEST, "&Play Test\tP");
    AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFile, MF_STRING, IDM_EXIT, "E&xit\tQ");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFile, "&File");

    HMENU hEdit = CreatePopupMenu();
    AppendMenuA(hEdit, MF_STRING, IDM_UNDO, "&Undo\tCtrl+Z");
    AppendMenuA(hEdit, MF_STRING, IDM_REDO, "&Redo\tCtrl+Y");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hEdit, "&Edit");

    HMENU hView = CreatePopupMenu();
    AppendMenuA(hView, MF_STRING, IDM_MODEL_BRW, "Model &Browser\tF5");
    AppendMenuA(hView, MF_STRING, IDM_SOUND_MGR, "&Sound Manager\tF6");
    AppendMenuA(hView, MF_STRING, IDM_TEXTURE_MGR, "&Texture Manager\tF7");
    AppendMenuA(hView, MF_STRING, IDM_PAWN_MGR, "&Pawn Manager\tF8");
    AppendMenuA(hView, MF_STRING, IDM_SCRIPT_MGR, "&Script Manager\tF9");
    AppendMenuA(hView, MF_SEPARATOR, 0, NULL);
    // No Zone Properties / Light Properties entries. Their per-zone rows live in
    // Entity Properties (right-click a zone or light) and their level-state rows
    // in the WorldGraph "Map (level)" row. Both old windows duplicated that state
    // in a global that Apply could silently drop.
    AppendMenuA(hView, MF_STRING, IDM_NODE_PANEL, "&Node Panel");
    AppendMenuA(hView, MF_STRING, IDM_PICKUP_PANEL, "&Pickups\tF10");
    AppendMenuA(hView, MF_STRING, IDM_HEIGHTMAP, "&Heightmap Editor\tH");
    AppendMenuA(hView, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hView, MF_STRING, IDM_WORLD_GRAPH, "&World Graph Explorer");
    AppendMenuA(hView, MF_STRING, IDM_LEVEL_LIST, "Level &List / Campaign");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hView, "&View");

    HMENU hCam = CreatePopupMenu();
    AppendMenuA(hCam, MF_STRING, IDM_RESET_CAM, "&Reset Camera\tHome");
    AppendMenuA(hCam, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_TOP, "&Top\tNumpad 7");
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_BOTTOM, "&Bottom\tNumpad 1");
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_RIGHT, "&Right\tNumpad 3");
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_LEFT, "&Left\tNumpad 9");
    AppendMenuA(hCam, MF_STRING, IDM_VIEW_PERSPECTIVE, "&Perspective\tNumpad 5");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hCam, "&Camera");

    HMENU hSettings = CreatePopupMenu();
    AppendMenuA(hSettings, MF_STRING, IDM_FULLSCREEN, "Toggle &Fullscreen\tF11");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hSettings, "&Settings");

    HMENU hHelp = CreatePopupMenu();
    AppendMenuA(hHelp, MF_STRING, IDM_ABOUT, "&About AngelEd");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hHelp, "&Help");

    SetMenu(hWnd, hMenu);
#endif
}
