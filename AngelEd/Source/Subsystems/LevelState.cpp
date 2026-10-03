// ============================================================================
// Subsystems/LevelState.cpp
//
// The owner of LevelMetadata, plus the apply path that writes the Map row back into
// it. Both halves moved here in R6: the accessors came out of UI/Panels/LevelState.cpp
// (a subsystem state object implemented inside a UI panel, reachable only because the
// UI layer is one translation unit) and ApplyMapProperties came out of History.cpp
// (it is level state, not undo/redo - it just happened to sit next to it).
//
// FRAGMENT - not a standalone translation unit. Included by Main.cpp.
// ============================================================================

#ifdef _WIN32
// Real definitions. Guarded to match the _WIN32 / stub split in LevelState.hpp; the
// stubs there are what a non-Windows build would link against instead.
static LevelMetadata g_levelMeta;

LevelMetadata GetLevelMetadata() { return g_levelMeta; }

void SetLevelMetadata(const LevelMetadata& meta) {
    // No mirror into a dialog any more. This used to copy all 17 fields into
    // g_zoneProps so the deleted Zone window would show current values - one extra
    // copy of the level state that had to be kept in step by hand. The Map row reads
    // g_levelMeta directly.
    g_levelMeta = meta;
}
#endif

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
