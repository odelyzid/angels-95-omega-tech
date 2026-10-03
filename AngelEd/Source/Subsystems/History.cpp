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

// Write the SelType::MAP property rows back into LevelMetadata.
//
// Only pushes an undo entry when something actually changed. Every other Apply
// path calls HistoryPush() unconditionally before dispatching, which is fine for
// them because their apply always mutates something — but a level's 16 metadata
// fields are edited in one screen, so re-opening and pressing Apply without
// touching anything would otherwise stack a no-op snapshot per click and make
// Ctrl+Z appear to do nothing but burn steps.
static void ApplyMapProperties() {
    const auto& P = g_editorPanels;
    LevelMetadata before = GetLevelMetadata();

    LevelMetadata meta = before;
    meta.gameType          = static_cast<GameType>(P.propMapGameType);
    meta.maxPlayers        = P.propMapMaxPlayers;
    meta.respawnTime       = P.propMapRespawnTime;
    meta.timeLimitEnabled  = P.propMapTimeLimitEnabled;
    meta.timeLimitMinutes  = P.propMapTimeLimitMinutes;
    meta.scoreLimit        = P.propMapScoreLimit;
    meta.friendlyFire      = P.propMapFriendlyFire;
    // The skybox path is also applied immediately by the Browse / Use-Active-Tex
    // handlers (the viewport reads it live), so `before` can already equal the new
    // value here; that is fine, it simply means the diff below reports no change
    // for that one field.
    meta.skyboxTexturePath = P.propMapSkybox;
    meta.skyboxSidePath    = P.propMapSkyboxSide;
    meta.particleType      = static_cast<ParticleType>(P.propMapParticleType);
    meta.particleDensity   = P.propMapParticleDensity;
    meta.particleSpeed     = P.propMapParticleSpeed;
    meta.particleColorR    = P.propMapParticleR;
    meta.particleColorG    = P.propMapParticleG;
    meta.particleColorB    = P.propMapParticleB;
    meta.particleWindX     = P.propMapParticleWindX;
    meta.particleWindZ     = P.propMapParticleWindZ;

    auto differs = [](const LevelMetadata& a, const LevelMetadata& b) {
        return a.gameType != b.gameType || a.maxPlayers != b.maxPlayers ||
               a.respawnTime != b.respawnTime ||
               a.timeLimitEnabled != b.timeLimitEnabled ||
               a.timeLimitMinutes != b.timeLimitMinutes ||
               a.scoreLimit != b.scoreLimit ||
               a.friendlyFire != b.friendlyFire ||
               a.skyboxTexturePath != b.skyboxTexturePath ||
               a.skyboxSidePath != b.skyboxSidePath ||
               a.particleType != b.particleType ||
               a.particleDensity != b.particleDensity ||
               a.particleSpeed != b.particleSpeed ||
               a.particleColorR != b.particleColorR ||
               a.particleColorG != b.particleColorG ||
               a.particleColorB != b.particleColorB ||
               a.particleWindX != b.particleWindX ||
               a.particleWindZ != b.particleWindZ;
    };

    if (!differs(before, meta)) {
        EditorLog("Map properties: no changes");
        return;
    }

    HistoryPush();
    SetLevelMetadata(meta);
    EditorLog("Applied map properties: mode=%d maxPlayers=%d respawn=%.1f "
              "timeLimit=%s scoreLimit=%d FF=%s skybox='%s' weather=%d",
              (int)meta.gameType, meta.maxPlayers, meta.respawnTime,
              meta.timeLimitEnabled ? "on" : "off", meta.scoreLimit,
              meta.friendlyFire ? "on" : "off", meta.skyboxTexturePath.c_str(),
              (int)meta.particleType);

    // RefreshWorldGraph had no callers at all, so nothing repainted the WorldGraph
    // name after an edit.
    RefreshWorldGraph();
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
