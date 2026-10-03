// =============================================================================
// Subsystems/History.cpp
//
// Document undo/redo via ExportToOzone snapshots, plus ApplyMapProperties.
//
// FRAGMENT - not a standalone translation unit. Included by Main.cpp, which is the
// single TU for AngelEd's core layer. See Wiki/Editor-Architecture-Refactor.md.
// =============================================================================

static std::string HistoryCapture() {
    std::ostringstream oss;
    ExportToOzone(oss);
    return oss.str();
}

static void HistoryClear() {
    g_histUndo.clear();
    g_histRedo.clear();
}

// Call BEFORE a mutation: snapshots current state and invalidates redo.
static void HistoryPush() {
    g_histRedo.clear();
    g_histUndo.push_back(HistoryCapture());
    if (g_histUndo.size() > kHistMax) g_histUndo.erase(g_histUndo.begin());
}


static void HistoryRestore(const std::string& text) {
    ClearScene();
    OzoneLoader::Instance().LoadString(
        text.c_str(), OTEditor.Path[0] ? OTEditor.Path : nullptr);
    InjectOzoneEntities(OzoneLoader::Instance().GetEntities(),
                        PawnSystem::Instance());
    OzoneLoader::Instance().RebuildCollisionVolumes();
    g_sel = { SelType::NONE, -1, "", {0,0,0} };
    g_hoverSel = { SelType::NONE, -1, "", {0,0,0} };
    OmegaTechEditor.DrawModel = false;
}

static void HistoryUndo() {
    if (g_histUndo.empty()) return;
    g_histRedo.push_back(HistoryCapture());
    std::string snap = g_histUndo.back();
    g_histUndo.pop_back();
    HistoryRestore(snap);
    EditorLog("Undo (%zu undo / %zu redo)", g_histUndo.size(), g_histRedo.size());
}

static void HistoryRedo() {
    if (g_histRedo.empty()) return;
    g_histUndo.push_back(HistoryCapture());
    std::string snap = g_histRedo.back();
    g_histRedo.pop_back();
    HistoryRestore(snap);
    EditorLog("Redo (%zu undo / %zu redo)", g_histUndo.size(), g_histRedo.size());
}
