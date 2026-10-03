// =============================================================================
// Core/EditorShell.cpp
//
// Camera view presets, the panel toggle shims, world directory and scene reset.
//
// FRAGMENT - not a standalone translation unit. Included by Main.cpp, which is the
// single TU for AngelEd's core layer. See Wiki/Editor-Architecture-Refactor.md.
// =============================================================================

static void SetViewPreset(const Vector3& pos, const Vector3& target, const Vector3& up) {
    if (!g_orthoView) {
        g_perspectiveCamState = OTEditor.MainCamera;
        g_orthoView = true;
    }
    OTEditor.MainCamera.position = pos;
    OTEditor.MainCamera.target = target;
    OTEditor.MainCamera.up = up;
    OTEditor.MainCamera.projection = CAMERA_ORTHOGRAPHIC;
}

static void SetViewPerspective() {
    if (g_orthoView) {
        // Restore saved state
        OTEditor.MainCamera = g_perspectiveCamState;
        g_orthoView = false;
    }
    OTEditor.MainCamera.projection = CAMERA_PERSPECTIVE;
}

// Panel toggle helpers (keyboard-driven, no top menu bar)
static void ToggleSoundMgr()    { ShowSoundManager(!g_editorPanels.showSoundMgr); }
static void ToggleTextureMgr()  { ShowTextureManager(!g_editorPanels.showTextureMgr); }
static void TogglePawnMgr()     { ShowPawnManager(!g_editorPanels.showPawnMgr); }
static void ToggleScriptMgr()   { ShowScriptManager(!g_editorPanels.showScriptMgr); }
static void ToggleModelBrowser(){ ShowModelBrowser(!g_editorPanels.showModelBrowser); }
static void TogglePickupPanel() { ShowPickupPanel(!g_editorPanels.showPickupPanel); }
static void ToggleNodePanel()   { ShowNodePanel(!g_editorPanels.showNodePanel); }
// Zone placement mode. Used to be ToggleEnvPanel(), which toggled the standalone
// Zone Properties window AND set placement mode — two unrelated things behind one
// key. The window is gone (its per-zone rows live in Entity Properties and its
// level-state rows in the Map row), so this is only the placement half.
static void ToggleZonePlacement() { g_placeMode = PlaceMode::ENV; }
static void ToggleHeightmapEditor() { ShowHeightmapEditor(!g_editorPanels.showHeightmapEditor); }
static void ToggleWorldGraph() { ShowWorldGraph(!g_editorPanels.showWorldGraph); }
static void ToggleAnimPanel()   { ShowAnimPanel(!g_editorPanels.showAnimPanel); }
static void ResetCamera()       { OTEditor.MainCamera.position = {0, 10, 0}; OTEditor.MainCamera.target = {0, 0, 0}; OTEditor.MainCamera.up = {0, 1, 0}; }
static void CamUp()             { OTEditor.MainCamera.position.y += 2; }
static void CamDown()           { OTEditor.MainCamera.position.y -= 2; }

// Model preview for Win32 dialog (render-to-texture)
static RenderTexture2D g_previewRT = {0};
static int g_lastPreviewSel = -1;
static bool g_previewNeedsUpdate = false;

static void SetWorldDirectory(const fs::path& directory) {
    std::string path = directory.string();
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += fs::path::preferred_separator;
    std::strncpy(OTEditor.Path, path.c_str(), sizeof(OTEditor.Path) - 1);
    OTEditor.Path[sizeof(OTEditor.Path) - 1] = '\0';
}

static void StopSoundPreview() {
    if (!g_previewSoundLoaded) return;
    StopSound(g_previewSound);
    UnloadSound(g_previewSound);
    g_previewSound = {0};
    g_previewSoundLoaded = false;
}

static void ClearScene() {
    StopSoundPreview();
    OzoneLoader::Instance().Unload();
    auto& pawns = PawnSystem::Instance();
    auto& zones = ZoneManager::Instance();
    pawns.DespawnAll();
    pawns.ClearPlayerStarts();
    pawns.ClearPickups();
    zones.ClearZones();
    pawns.ClearLights();     // lights otherwise accumulate across loads
    zones.ClearPortals();
    pawns.ClearEmitters();
    pawns.ClearParticleEmitters();
    pawns.ClearPathNodes();
    pawns.ClearWindZones();
    pawns.ClearMeshObjects();
    pawns.ClearSkyZones();
    OmegaTechEditor.DrawModel = false;
}
