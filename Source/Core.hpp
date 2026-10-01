#include "Data.hpp"
#include "Log.hpp"
#include "Physics/PlayerPhysics.hpp"
#include "Client/JoinUri.hpp"
#include "Network/NetworkSession.hpp"
#include "PlayerProfile.hpp"
#include "Renderer/OzAssetMapper.hpp"
#include "Audio/SoundManager.hpp"
#include "World/OzOzoneLoader.hpp"
#include "Pawn/OzPawnSystem.hpp"
#include "Pawn/CombatMath.hpp"
#include "Package/PackageAssetLoader.hpp"
#include "Client/MasterList.hpp"
#include "Renderer/EngineBillboard.hpp"
#include "Renderer/ViewModel.hpp"
#include "Particle/OzParticleSimulationManager.hpp"
#include "Script/LightningEntityRegistry.hpp"
#include "Script/LightningEntityManager.hpp"

#include "raymath.h"
#include "Renderer/rlights/rlights.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "Screenshot.hpp"

// How long the damage vignette stays up after a hit, seconds.
static constexpr float kDamageFlashDuration = 0.45f;

bool FloorCollision = true;
bool ObjectCollision = false;
bool g_showCollisionDebug = false;

char g_world_to_load[256] = "EngineTest";
char g_world_dir_override[256] = "";
bool g_skipMenu = false;

// Portal transitions (campaign system) — set by the portal trigger in
// UpdateEntities, consumed right after LoadWorld() repositions the player.
bool g_portalTransitionPending = false;
Vector3 g_portalSpawnPos = {0, 20, 0};
float g_portalCooldown = 0.0f;

// Bidirectional portals: remember where we came from so a reverse portal can
// be spawned at the arrival point. g_returnPortalId indexes ZoneManager's
// portals (-1 = none); it starts disabled and arms once the player steps away.
std::string g_portalReturnWorld;
Vector3 g_portalReturnPos = {0, 0, 0};
bool g_portalHasReturn = false;
int g_returnPortalId = -1;

int ScriptTimer = 0;

// Cross-world state that must be reset on each LoadWorld()
float g_damageCooldown = 0.0f;
std::string g_activeEnvZone;
Texture2D g_skySideTex = {0};   // optional horizon/side skybox variant

// Distance from the camera to each skybox cube face. The faces are 2000 units
// square, so this sets the sky's angular coverage: atan(1000 / kSkyboxDist)
// either side of centre. The horizontal half-FOV of a 16:9 window at fovy 60 is
// about 47.5 degrees, so anything under ~840 leaves black corners (the original
// 1000 covered only 45 and did exactly that). 700 gives ~55 degrees, which
// covers the frustum without the side planes looming as huge flat slabs.
// Depth writes are disabled for the sky, so distance does not affect occlusion,
// and 700 stays well inside the 4000 far plane.
constexpr float kSkyboxDist = 700.0f;

// Set from PlayHomeScreen to request a server join
bool SetServerJoinFlag = false;
const char *SetServerJoinIP = nullptr;
int SetServerJoinPort = 27015;

void LoadSave();
void SaveGame();
void DrawRemotePlayers3D();

#include "Renderer/CombatFX.hpp"

// ---------------------------------------------------------------------------
// OzoneCollisionQuery — adapter that feeds OzoneLoader's chunked collision
// volumes / heightmap into the oz::physics::PlayerPhysics module, keeping the
// module independent of the world loader.
// ---------------------------------------------------------------------------
namespace oz {
namespace physics {

class OzoneCollisionQuery : public PlayerPhysics::CollisionQuery {
public:
    GroundSample SampleGround(float x, float z, float feetY) override {
        auto& ozLoader = OzoneLoader::Instance();
        // Support tolerance: a surface up to kStepHeight above the feet counts,
        // so walking into a step or stair climbs it. Everything higher is
        // ignored, which also keeps underground areas working — a tunnel at
        // feet=-6 must fall through to its own floor rather than being snapped
        // up to the heightmap surface far above.
        const float supportCeiling = feetY + kStepHeight;

        // Heightmap candidate (heightmap first, then brush tops, then the higher
        // valid one wins). BOTH are always evaluated: returning early on a valid
        // heightmap would hide every platform, stair and balcony authored above
        // a heightmap-covered map.
        float best = kNoGroundY;
        if (ozLoader.HasHeightmap()) {
            float hmY = ozLoader.SampleHeightmapY(x, z);
            if (hmY > kNoGroundY && hmY <= supportCeiling)
                best = hmY;
        }

        // Brush-top candidate (chunk-accelerated).
        auto& chunkMgr = ozLoader.GetChunkManager();
        std::vector<int> nearIndices;
        chunkMgr.GetVolumesNear(x, z, nearIndices);
        auto& vols = ozLoader.GetCollisionVolumes();
        for (int idx : nearIndices) {
            if (idx < 0 || idx >= (int)vols.size())
                continue;
            auto& vol = vols[idx];
            if (x >= vol.aabb.min.x && x <= vol.aabb.max.x &&
                z >= vol.aabb.min.z && z <= vol.aabb.max.z)
            {
                float top = vol.aabb.max.y;
                if (top > best && top <= supportCeiling)
                    best = top;
            }
        }
        if (best > kNoGroundY)
            return GroundSample{best, true};
        return GroundSample{0.0f, false};
    }

    bool OverlapsObstacle(const BoundingBox& player, float feetY) override {
        auto& ozLoader = OzoneLoader::Instance();
        auto& chunkMgr = ozLoader.GetChunkManager();
        std::vector<int> nearIndices;
        chunkMgr.GetVolumesNear(player.min.x, player.max.x, nearIndices);
        auto& vols = ozLoader.GetCollisionVolumes();
        for (int idx : nearIndices) {
            if (idx >= 0 && idx < (int)vols.size() &&
                CheckCollisionBoxes(player, vols[idx].aabb))
            {
                // Floors, steps and stairs are not obstacles: anything whose top
                // face is within the step-up tolerance of the feet is walked
                // over by ClampToGround. This is a hard threshold, so anything
                // taller still blocks and walls stay solid.
                if (vols[idx].aabb.max.y <= feetY + kStepHeight)
                    continue;
                // The heightmap is the ground: support comes from the ground
                // clamp, not the obstacle test.
                if (vols[idx].isHeightmap)
                    continue;
                return true;
            }
        }
        return false;
    }
};
} // namespace physics
} // namespace oz


class EngineData
{
public:
    int LevelIndex = 1;
    Camera MainCamera = {0};
    Shader PixelShader;
    Shader FogShader;
    Shader LineShader;
    Shader ToonShader;
    Shader SobelShader;
    Shader JitterShader;
    Shader Lights;
    Shader WindShader;   // Lighting.vs + wind displacement (foliage)
    Light GameLights[MAX_LIGHTS];

    Texture HomeScreen;
    Texture PauseHeading;
    Texture BtnNormal;
    Texture BtnHover;
    Texture BtnClicked;
    ray_video_t HomeScreenVideo;
    Music HomeScreenMusic;
    Model SkyboxFace[6];   // 0=top, 1=bottom, 2=+X, 3=-X, 4=+Z, 5=-Z

    bool FirstLoad = true;

    int Ticker = 0;
    int CameraSpeed = 1;
    int RenderRadius = 800;
    int Deaths;
    int BadPreformaceCounter = 0;
    bool SkyboxEnabled = false;
    int Ending = 0;

    // Damage-flash timer, seconds. Set by contact damage and by the scripted
    // `damage` opcode; drawn as a red screen vignette by the HUD pass. Was an
    // int saturating at 240 that nothing ever read.
    float DamageFlash = 0.0f;

    void InitCamera()
    {
        MainCamera.position = (Vector3){0.0f, 20.0f, 0.0f};
        MainCamera.target = (Vector3){0.0f, 20.0f, -10.0f};
        MainCamera.up = (Vector3){0.0f, 1.0f, 0.0f};
        MainCamera.fovy = 60.0f;
        MainCamera.projection = CAMERA_PERSPECTIVE;
        OZ_INFO("Camera initialized at (0, 20, 0)");
    }
};

EngineData OmegaTechData;

bool LoadFlag = false;

// STILL HARDCODED FILE PATH Loading for Worlds
// TODO: abreviate into handling by OzOzoneLoader and PawnBased/LightningScript Based object loading  // NOLINT

auto LoadWorld()
{
    OZ_INFO("LoadWorld: loading world %d (frame=%llu)", OmegaTechData.LevelIndex, (unsigned long long)OmegaTechData.Ticker);
    PlayFade();
    PawnSystem::Instance().ClearLights();

    // Reset movement state so player falls to new world's collision
    g_playerMovement.onGround = false;
    g_playerMovement.velocityY = 0.0f;

    // Reset cross-world state that would otherwise persist across LoadWorld calls
    g_damageCooldown = 0.0f;
    g_activeEnvZone.clear();
    // Audio (world music, zone ambience, reverb hook, script sound cache) owns
    // its own per-world reset.
    SoundManager::Instance().ResetWorldAudio();
    ScriptTimer = 0;

    {
        OmegaTechData.DamageFlash = 0.0f;

        LightningEntityManager::Instance().SetPlayerHealth(100.0f);

        OmegaTechData.SkyboxEnabled = false;

        // Resolve the asset directory prefix. g_world_to_load may be a world NAME
        // (assets live in GameData/Worlds/<name>/) or a direct file path (editor
        // playtest passes --world <path>; assets then live beside the file).
        std::string worldLoad = g_world_to_load;
        bool directWorldPath = worldLoad.find(".ozone") != std::string::npos;
        std::string assetPrefix;
        if (directWorldPath) {
            size_t slash = worldLoad.find_last_of("/\\");
            assetPrefix = (slash != std::string::npos) ? worldLoad.substr(0, slash + 1) : std::string();
        } else if (g_world_dir_override[0]) {
            assetPrefix = std::string("GameData/Worlds/") + g_world_dir_override + "/";
        } else {
            assetPrefix = std::string("GameData/Worlds/") + worldLoad + "/";
        }

        // World-scoped .ozls defs (every world ships a "zone_sky_0", so the
        // global registry collides by name). Make the by-name map point at THIS
        // world before anything resolves a zone name, otherwise on_enter never
        // fires and the level renders with the default black ambient.
        LightningEntityRegistry::Instance().LoadWorldOverrides(assetPrefix);

        // World skybox: filesystem first, then packages (resolves "Skybox.png"
        // inside the world's .ozone container in packaged builds)
        {
            if (WorldModels.Skybox.id > 0)
                UnloadTexture(WorldModels.Skybox);
            WorldModels.Skybox = LoadTextureWithFallback(TextFormat("%sModels/Skybox.png", assetPrefix.c_str()));
            OmegaTechData.SkyboxEnabled = (WorldModels.Skybox.id > 0);
            if (OmegaTechData.SkyboxEnabled)
                OZ_INFO("World skybox: loaded %sModels/Skybox.png", assetPrefix.c_str());
        }

        // Clear all existing entities before loading new world
        PawnSystem::Instance().ClearLights();
        PawnSystem::Instance().ClearPlayerStarts();
        PawnSystem::Instance().ClearPickups();
        ZoneManager::Instance().ClearZones();
        ZoneManager::Instance().ClearPortals();
        PawnSystem::Instance().ClearEmitters();
        PawnSystem::Instance().ClearParticleEmitters();
        PawnSystem::Instance().ClearPathNodes();
        PawnSystem::Instance().ClearWindZones();
        PawnSystem::Instance().DespawnAll();
        OzParticleSimulationManager::Instance().Clear();
        // Reset level metadata so stale settings never leak across worlds
        PawnSystem::Instance().GetWorldInfo() = WorldInfo{};
        ParticlesEnabled = false;
        CombatFX::Instance().ClearAll();
        if (g_skySideTex.id > 0) { UnloadTexture(g_skySideTex); g_skySideTex = {0}; }

        // The ONLY world source of truth: the OZONE container. A missing
        // World.ozone is a hard error (the legacy WDL fallback is gone).
        {
            char ozonePath[512];
            if (strstr(g_world_to_load, ".ozone") != nullptr)
                snprintf(ozonePath, sizeof(ozonePath), "%s", g_world_to_load); // direct path (editor playtest)
            else
                snprintf(ozonePath, sizeof(ozonePath), "%sWorld.ozone", assetPrefix.c_str());
            if (IsPathFile(ozonePath))
                OzoneLoader::Instance().LoadFile(ozonePath);
            else
                OZ_ERROR("LoadWorld: missing world '%s' (no World.ozone; WDL fallback removed)", ozonePath);

            // The loader only parses: it hands back the world's entities and
            // level metadata, and this orchestrator decides what to apply.
            InjectOzoneEntities(OzoneLoader::Instance().GetEntities(),
                                PawnSystem::Instance());

            // Re-assert the lit-fog shader on the freshly loaded world geometry.
            // The loader assigns it per-material when building brushes, but if
            // the shader was (re)loaded or the materials ended up with the
            // default shader the client rendered the world fullbright. The
            // editor already re-applies it after load; the client must too.
            if (OmegaTechData.Lights.id > 0)
                OzoneLoader::Instance().SetLitFogShader(OmegaTechData.Lights);
        }

        // Apply level metadata (LevelInfo/Particles) after entities are loaded
        {
            const LevelSettings& s = OzoneLoader::Instance().GetLevelSettings();
            if (!s.skyboxPath.empty())
            {
                Texture2D newSky = LoadTextureWithFallback(s.skyboxPath.c_str());
                if (newSky.id > 0)
                {
                    if (WorldModels.Skybox.id > 0) UnloadTexture(WorldModels.Skybox);
                    WorldModels.Skybox = newSky;
                    OmegaTechData.SkyboxEnabled = true;
                    OZ_INFO("LevelInfo: skybox '%s'", s.skyboxPath.c_str());
                }
                else
                {
                    OZ_WARN("LevelInfo: skybox '%s' not found", s.skyboxPath.c_str());
                }
            }
            ParticlesEnabled = (s.particleType != 0);
            if (ParticlesEnabled)
                OZ_INFO("LevelInfo: particles type=%d density=%.0f", s.particleType, s.particleDensity);
            if (!s.skyboxSidePath.empty())
            {
                Texture2D sideSky = LoadTextureWithFallback(s.skyboxSidePath.c_str());
                if (sideSky.id > 0)
                {
                    if (g_skySideTex.id > 0) UnloadTexture(g_skySideTex);
                    g_skySideTex = sideSky;
                    OZ_INFO("LevelInfo: skybox sides '%s'", s.skyboxSidePath.c_str());
                }
                else
                {
                    OZ_WARN("LevelInfo: skybox sides '%s' not found", s.skyboxSidePath.c_str());
                }
            }
        }

        // World music: <prefix>Music/Main.mp3, else the shared global ambience.
        SoundManager::Instance().PlayWorldMusic(assetPrefix);

        // Spawn at the world's playerstart unless resuming from a saved
        // position ("Continue" from the title menu sets SetCameraFlag).
        if (!SetCameraFlag)
            PawnSystem::Instance().RespawnPlayerAtStart(OmegaTechData.MainCamera);

        SaveGame();
    }
}

void LoadLaunchConfig()
{
    wstring Config = LoadFile("GameData/Launch.conf");

    wstring Resolution = WSplitValue(Config, 0);

    switch (Resolution[0])
    {
    case L'1':
        SetWindowSize(640, 480);
        break;
    case L'2':
        SetWindowSize(1280, 720);
        break;
    case L'3':
        SetWindowSize(1980, 1080);
        break;
    case L'4':
        SetWindowSize(2560, 1440);
        break;
    case L'5':
        SetWindowSize(3840, 2160);
        break;
    default:
        SetWindowSize(GetMonitorWidth(0), GetMonitorHeight(0));
        ToggleFullscreen();
        break;
    }
}

void UpdateLightSources()
{
    // Lighting now lives in Renderer/LitLightning (LitLightning_UpdateFrame).
    // Kept as a thin engine-side adapter that supplies the engine globals.
    LitLightning_UpdateFrame(PawnSystem::Instance().GetLights(), OmegaTechData.Lights,
                             OmegaTechData.MainCamera, OmegaTechData.GameLights[0],
                             GetFrameTime());
}

void DrawLights()
{
    const auto &lights = PawnSystem::Instance().GetLights();
    for (const auto &node : lights)
    {
        if (!node.active)
            continue;
        Color c = node.color;
        DrawSphereEx(node.position, 0.2f, 8, 8, c);
    }
}

// DrawLightFlares — additive billboard glows for lights with flare/corona.
// The glow texture is generated procedurally once (radial falloff) — no asset file.
inline Texture2D g_lightGlowTex = {0};
void DrawLightFlares(Camera3D& camera)
{
    const auto& lights = PawnSystem::Instance().GetLights();
    bool any = false;
    for (const auto& l : lights)
        if (l.active && (l.flare || l.corona)) { any = true; break; }
    if (!any) return;

    if (g_lightGlowTex.id == 0)
    {
        constexpr int S = 128;
        Image img = GenImageColor(S, S, BLANK);
        Color* px = (Color*)img.data;
        for (int y = 0; y < S; y++)
            for (int x = 0; x < S; x++) {
                float dx = (x - S/2 + 0.5f) / (S/2);
                float dy = (y - S/2 + 0.5f) / (S/2);
                float d = sqrtf(dx*dx + dy*dy);
                float a = (d < 1.0f) ? powf(1.0f - d, 2.5f) : 0.0f;
                px[y*S + x] = (Color){255, 255, 255, (unsigned char)(a * 255.0f)};
            }
        g_lightGlowTex = LoadTextureFromImage(img);
        UnloadImage(img);
    }

    BeginBlendMode(BLEND_ADDITIVE);
    for (const auto& l : lights)
    {
        if (!l.active || (!l.flare && !l.corona)) continue;
        // Size from light radius/intensity, clamped to sane screen presence
        float base = fminf(fmaxf(l.radius * 0.08f, 0.8f), 8.0f);
        if (l.corona)
            DrawBillboard(camera, g_lightGlowTex, l.position, base * 2.5f,
                          (Color){l.color.r, l.color.g, l.color.b, 70});
        if (l.flare)
            DrawBillboard(camera, g_lightGlowTex, l.position, base,
                          (Color){l.color.r, l.color.g, l.color.b, (unsigned char)(120 + l.intensity * 20)});
    }
    EndBlendMode();
}

void OmegaTechInit()
{
    OZ_INFO("=== OmegaTech Engine starting ===");
    OZ_INFO("CWD: %s", fs::current_path().string().c_str());
    OZ_INFO("GameData/Worlds exists: %d", (int)fs::exists("GameData/Worlds"));
    OZ_INFO("System/Data/Zones exists: %d", (int)fs::exists("System/Data/Zones"));
    // Launch.conf calls SetWindowSize after InitWindow, which would silently
    // override an explicit --shot-res. A capture run is meant to be exactly the
    // requested size, so skip it there.
    if (!g_shot.active)
        LoadLaunchConfig();
    else if (g_shot.resWidth > 0 && g_shot.resHeight > 0)
        SetWindowSize(g_shot.resWidth, g_shot.resHeight);

    GuiLoadStyleDark();

    // Player profile slots (System/PlayerProfiles.ini). Loaded once here rather
    // than per menu visit so reopening the Character pane cannot discard edits
    // by re-reading the file. The title menu only reads from here.
    PlayerProfileManager::Instance().Init();

    // Initialize package-based asset loading
    PackageAssetLoader::Instance().Init();

    // Initialize LightningScript entity system
    LightningEntityRegistry::Instance().Init();
    LightningEntityManager::Instance().Init();

    // Initialize engine/item texture mapper (must be before EngineBillboard::Init)
    AssetMapper::Instance().Init();

    // Initialize engine billboard system
    EngineBillboard::Init();

    // Initialize combat FX (procedural decal/particle textures)
    CombatFX::Instance().Init();

    // Initialize 3D skybox cube faces (6 planes with correct UV orientation per face)
    //
    // The cube is drawn at kSkyboxDist from the camera with depth writes off, so
    // its distance is purely a field-of-view decision: a plane of half-width
    // S/D covers atan(S/D) either side. At 2000/1000 that is only 45 degrees,
    // but the horizontal FOV at fovy=60 on a 16:9 window is ~47.5 degrees — the
    // corners fell outside the cube and rendered as black wedges, which is what
    // made outdoor levels look like they had a black sky. Halving the distance
    // to 500 gives atan(1000/500) = 63.4 degrees, comfortably past the corner
    // angles, and 500 is well inside RL_CULL_DISTANCE_FAR (4000).
    const float skySize = 2000.0f;
    for (int i = 0; i < 6; i++) {
        Mesh plane = GenMeshPlane(skySize, skySize, 1, 1);
        float* tc = (float*)plane.texcoords;
        int vcount = plane.vertexCount;
        if (tc) {
            switch (i) {
                case 0: for (int v = 0; v < vcount; v++) tc[v*2+1] = 1.0f - tc[v*2+1]; break;
                case 2: for (int v = 0; v < vcount; v++) tc[v*2] = 1.0f - tc[v*2]; break;
                case 5: for (int v = 0; v < vcount; v++) tc[v*2] = 1.0f - tc[v*2]; break;
            }
        }
        OmegaTechData.SkyboxFace[i] = LoadModelFromMesh(plane);
    }

    // Register pawn definitions from .cfg files (data-driven)
    {
        auto& ps = PawnSystem::Instance();
        const char* defsDir = "GameData/Global/PawnDefs";
        namespace fs = std::filesystem;
        if (fs::exists(defsDir)) {
            int loaded = 0;
            for (auto& entry : std::filesystem::directory_iterator(defsDir)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".cfg") continue;
                std::ifstream f(entry.path());
                if (!f.is_open()) continue;
                std::string name, sp, sc, modelPath, modelTex;
                std::string meshType, animIdle, animPatrol, animChase, animReturn, animDeath;
                float speed = 1.5f, aggroRange = 6.0f, attackRange = 1.5f, damage = 10.0f;
                float animSpeed = 1.0f, modelScale = 1.0f;
                int maxHealth = 100;
                std::string line;
                while (std::getline(f, line)) {
                    line.erase(0, line.find_first_not_of(" \t\r\n"));
                    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
                    size_t eq = line.find('=');
                    if (eq == std::string::npos) continue;
                    std::string key = line.substr(0, eq);
                    std::string val = line.substr(eq + 1);
                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);
                    val.erase(0, val.find_first_not_of(" \t"));
                    val.erase(val.find_last_not_of(" \t\r") + 1);
                    if (key == "name") name = val;
                    else if (key == "speed") speed = std::stof(val);
                    else if (key == "aggroRange") aggroRange = std::stof(val);
                    else if (key == "attackRange") attackRange = std::stof(val);
                    else if (key == "damage") damage = std::stof(val);
                    else if (key == "maxHealth") maxHealth = std::stoi(val);
                    else if (key == "sprite_path") sp = val;
                    else if (key == "scream_path") sc = val;
                    else if (key == "model_path") modelPath = val;
                    else if (key == "model_texture") modelTex = val;
                    else if (key == "mesh_type") meshType = val;
                    else if (key == "model_scale") modelScale = std::stof(val);
                    else if (key == "anim_idle") animIdle = val;
                    else if (key == "anim_patrol") animPatrol = val;
                    else if (key == "anim_chase") animChase = val;
                    else if (key == "anim_return") animReturn = val;
                    else if (key == "anim_death") animDeath = val;
                    else if (key == "anim_speed") animSpeed = std::stof(val);
                }
                if (!name.empty()) {
                    PawnDef def;
                    def.name = name;
                    def.speed = speed;
                    def.aggroRange = aggroRange;
                    def.attackRange = attackRange;
                    def.damage = damage;
                    def.maxHealth = maxHealth;
                    def.sprite_path = sp;
                    def.scream_path = sc;
                    def.model_path = modelPath;
                    def.model_texture = modelTex;
                    def.model_scale = modelScale;
                    def.mesh_type = meshType;
                    def.anim_idle = animIdle;
                    def.anim_patrol = animPatrol;
                    def.anim_chase = animChase;
                    def.anim_return = animReturn;
                    def.anim_death = animDeath;
                    def.anim_speed = animSpeed;
                    def.baseDir = entry.path().parent_path().string();
                    if (!def.baseDir.empty()) def.baseDir += "/";
                    ps.RegisterDef(def);
                    loaded++;
                }
            }
            OZ_INFO("Loaded %d pawn definitions from %s", loaded, defsDir);
        } else {
            // Fallback if no config directory exists
            OZ_INFO("PawnDefs dir not found, using hardcoded defaults");
            PawnDef d;
            d.name="Walker"; d.speed=1.5f; d.aggroRange=6.0f; d.attackRange=1.5f; d.damage=10.0f; d.maxHealth=100; ps.RegisterDef(d);
            d.name="Skaarj"; d.speed=2.5f; d.aggroRange=10.0f; d.attackRange=2.0f; d.damage=20.0f; d.maxHealth=150; ps.RegisterDef(d);
            d.name="Brute"; d.speed=1.0f; d.aggroRange=4.0f; d.attackRange=1.5f; d.damage=30.0f; d.maxHealth=250; ps.RegisterDef(d);
            d.name="Floater"; d.speed=1.2f; d.aggroRange=8.0f; d.attackRange=3.0f; d.damage=15.0f; d.maxHealth=80; ps.RegisterDef(d);
        }
    }

    OmegaTechData.InitCamera();

    OmegaTechData.PixelShader = LoadShaderWithFallback(0, "GameData/Shaders/Pixel.fs");
    // Legacy FogShader disabled - using LitFog material shader instead
    OmegaTechData.FogShader = {0};
    OmegaTechData.LineShader = LoadShaderWithFallback(0, "GameData/Shaders/Scanlines.fs");
    OmegaTechData.SobelShader = LoadShaderWithFallback(0, "GameData/Shaders/Sobel.fs");
    OmegaTechData.ToonShader = LoadShaderWithFallback(0, "GameData/Shaders/Toon.fs");
    OmegaTechData.JitterShader = LoadShaderWithFallback(0, "GameData/Shaders/Jitter.fs");
    OmegaTechData.Lights = LoadShaderWithFallback("GameData/Shaders/Lights/Lighting.vs", "GameData/Shaders/Lights/LitFog.fs");
    OmegaTechData.WindShader = LoadShaderWithFallback("GameData/Shaders/Lights/Wind.vs", "GameData/Shaders/Lights/LitFog.fs");
    if (OmegaTechData.WindShader.id == 0) {
        OmegaTechData.WindShader = OmegaTechData.Lights; // graceful fallback: no sway
        OZ_WARN("Wind.vs shader not found — foliage wind disabled");
    }
    OZ_INFO("Shaders loaded (Pixel=%d, Line=%d, Sobel=%d, Toon=%d, Jitter=%d, Lights=%d, Wind=%d)",
            OmegaTechData.PixelShader.id, OmegaTechData.LineShader.id,
            OmegaTechData.SobelShader.id, OmegaTechData.ToonShader.id,
            OmegaTechData.JitterShader.id, OmegaTechData.Lights.id, OmegaTechData.WindShader.id);
    OzoneLoader::Instance().SetLitFogShader(OmegaTechData.Lights);

    // Initialize all lights to disabled
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        OmegaTechData.GameLights[i] = {0};
    }

    OmegaTechData.Lights.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(OmegaTechData.Lights, "viewPos");
    int AmbientLoc = GetShaderLocation(OmegaTechData.Lights, "ambient");
    float ambient[4] = {0.1f, 0.1f, 0.1f, 1.0f};
    SetShaderValue(OmegaTechData.Lights, AmbientLoc, ambient, SHADER_UNIFORM_VEC4);

    // Initialize fog uniforms
    int fogStartLoc = GetShaderLocation(OmegaTechData.Lights, "fogStart");
    int fogEndLoc = GetShaderLocation(OmegaTechData.Lights, "fogEnd");
    int fogDensityLoc = GetShaderLocation(OmegaTechData.Lights, "fogDensity");
    int fogColorLoc = GetShaderLocation(OmegaTechData.Lights, "fogColor");
    int fogIntensityLoc = GetShaderLocation(OmegaTechData.Lights, "fogIntensity");

    float fogStart = 10.0f;
    float fogEnd = 100.0f;
    float fogDensity = 1.0f;
    float fogColor[3] = {0.7f, 0.7f, 0.8f};
    float fogIntensity = 1.0f;

    SetShaderValue(OmegaTechData.Lights, fogStartLoc, &fogStart, SHADER_UNIFORM_FLOAT);
    SetShaderValue(OmegaTechData.Lights, fogEndLoc, &fogEnd, SHADER_UNIFORM_FLOAT);
    SetShaderValue(OmegaTechData.Lights, fogDensityLoc, &fogDensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(OmegaTechData.Lights, fogColorLoc, fogColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(OmegaTechData.Lights, fogIntensityLoc, &fogIntensity, SHADER_UNIFORM_FLOAT);

    // uTime uniform for GPU light animation
    static int uTimeLoc = GetShaderLocation(OmegaTechData.Lights, "uTime");
    float timeVal = (float)GetTime();
    SetShaderValue(OmegaTechData.Lights, uTimeLoc, &timeVal, SHADER_UNIFORM_FLOAT);

    OmegaTechData.HomeScreen = LoadTextureWithFallback("GameData/Global/Title/Title.png");
    OmegaTechData.PauseHeading = LoadTexture("GameData/Global/Title/menu_heading.png");
    OmegaTechData.BtnNormal = LoadTexture("GameData/Global/Title/menu_button.png");
    OmegaTechData.BtnHover = LoadTexture("GameData/Global/Title/menu_button_hover.png");
    OmegaTechData.BtnClicked = LoadTexture("GameData/Global/Title/menu_button_clicked.png");
    if (IsPathFile("GameData/Global/Title/Title.mpg"))
        OmegaTechData.HomeScreenVideo = ray_video_open("GameData/Global/Title/Title.mpg");
    OmegaTechData.HomeScreenMusic = LoadMusicWithFallback("GameData/Global/Title/Title.mp3");

    OmegaTechTextSystem.Bar = LoadTextureWithFallback("GameData/Global/TextBar.png");
    OmegaTechTextSystem.BarFont = LoadFontWithFallback("GameData/Global/Font.ttf");
    // UI/gameplay one-shots + the typewriter blip (all owned by SoundManager).
    SoundManager::Instance().LoadCoreSounds();

    OmegaTechData.GameLights[0] = CreateLight(LIGHT_DIRECTIONAL, {OmegaTechData.MainCamera.position.x, OmegaTechData.MainCamera.position.y, OmegaTechData.MainCamera.position.z}, Vector3Zero(), WHITE, OmegaTechData.Lights);

    Target = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());

    // Pre-cache weapon object models from entity definitions (Object1-5)
    {
        static const char* objNames[] = {"Object1", "Object2", "Object3", "Object4", "Object5"};
        for (int o = 0; o < 5; o++) {
            int rIdx = LightningEntityManager::Instance().PrecacheModelForDef(objNames[o]);
            if (rIdx >= 0) {
                Model* m = LightningEntityManager::Instance().GetModelByResourceIdx(rIdx);
                if (m && m->meshes) {
                    WorldModels.objectModels[o] = *m;
                    WorldModels.objectModelsLoaded[o] = true;
                }
            }
        }
    }

    SoundManager::Instance().PlayStream(OmegaTechData.HomeScreenMusic);
}

void PlaySplashScreen()
{
    // Screenshot runs are automated: the 2.5s splash is pure latency and would
    // also leave the title logo in the first captured frame on slow machines.
    if (g_shot.active)
        return;

    // Reveal gate. The window is created hidden (FLAG_WINDOW_HIDDEN in
    // Main.cpp) so the engine can boot, resolve the asset pipeline and prime
    // the first frame without flashing an undecorated title bar or an empty
    // client area. This function is the one place allowed to reveal it, which
    // is why it must be called before anything interactive: a hidden Win32
    // window is not hit-tested, so the title menu would never see a click.
    ClearWindowState(FLAG_WINDOW_HIDDEN);
    #ifdef _WIN32
    // ClearWindowState clears WS_VISIBLE through raylib's platform layer, but
    // SetWindowLongPtr cannot reliably *show* a window on every Windows
    // version, so state the intent explicitly and keep the HWND in step with
    // raylib's flag.
    if (HWND hwnd = GetActiveWindow()) ShowWindow(hwnd, SW_SHOW);
    #endif

    Texture2D splash = LoadTexture("GameData/Global/Title/splash.png");
    double startTime = GetTime();

    while (!WindowShouldClose() && GetTime() - startTime < 2.5)
    {
        BeginDrawing();
        ClearBackground(BLACK);
        if (splash.id > 0)
            DrawTexturePro(splash,
                           (Rectangle){0, 0, (float)splash.width, (float)splash.height},
                           (Rectangle){0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()},
                           (Vector2){0, 0}, 0, WHITE);
        EndDrawing();
    }

    if (splash.id > 0)
        UnloadTexture(splash);
}

#include "Menu/TitleMenu.hpp"

void PlayHomeScreen()
{
    // Re-open the title video if a previous menu visit released it.
    if (!OmegaTechData.HomeScreenVideo.ok && IsPathFile("GameData/Global/Title/Title.mpg"))
        OmegaTechData.HomeScreenVideo = ray_video_open("GameData/Global/Title/Title.mpg");

    if (g_skipMenu)
    {
        g_skipMenu = false;
        UnloadRenderTexture(Target);
        Target = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
        OmegaTechData.Deaths = 1;
        return;
    }

    TitleMenu menu;

    while (!menu.Tick() && !WindowShouldClose())
    {
    }

    SoundManager::Instance().StopStream(OmegaTechData.HomeScreenMusic);

    // Release the title video (it will be re-opened on the next menu visit).
    if (OmegaTechData.HomeScreenVideo.ok)
    {
        ray_video_destroy(&OmegaTechData.HomeScreenVideo);
        OmegaTechData.HomeScreenVideo = ray_video_t{};
    }

    if (menu.ShouldLoadGame() || menu.GetSelectedWorld())
    {
        UnloadRenderTexture(Target);
        Target = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
        OmegaTechData.Deaths = 1;
    }

    if (menu.GetSelectedWorld())
    {
        strncpy(g_world_to_load, menu.GetSelectedWorld(), sizeof(g_world_to_load) - 1);
        g_world_to_load[sizeof(g_world_to_load) - 1] = '\0';
    }

    if (menu.ShouldJoinServer())
    {
        // Validate the typed/selected host so raygui textbox corruption can't
        // send a garbage address (e.g. a single stray char) into connect().
        std::string host;
        int port = menu.GetJoinPort();
        if (JoinUri::ParseHostPort(menu.GetJoinIP(), host, port, port > 0 ? port : 27015) &&
            !host.empty())
        {
            // SetServerJoinIP is consumed after PlayHomeScreen() returns, so it
            // must outlive the local menu (GetJoinIP() dangles once menu dies).
            static std::string s_menuJoinHost;
            s_menuJoinHost = host;
            SetServerJoinIP = s_menuJoinHost.c_str();
            SetServerJoinPort = port;
            SetServerJoinFlag = true;
        }
        else
        {
            OZ_WARN("Join: invalid address '%s' — not connecting", menu.GetJoinIP());
        }
    }

    if (menu.ShouldStartServer())
    {
        int port = menu.GetJoinPort();
        if (port <= 0 || port > 65535) port = 27015;
        SetServerJoinIP = "127.0.0.1";
        SetServerJoinPort = port;
        SetServerJoinFlag = true;
        // Launch dedicated server as a subprocess
        int serverPort = port;
        std::string cmd = "start /B \"\" System\\AngelServ.exe --port " +
                          std::to_string(serverPort) + " --dir GameData";
        // Announce the hosted server to the configured masters.
        for (const auto& m : master::LoadMasterUrls("System/Angels95.ini")) {
            if (m.rfind("http://", 0) == 0)
                cmd += " --master-http \"" + m + "\"";
            else if (m.rfind("https://", 0) == 0)
                OZ_WARN("Host: ignoring https master '%s' (plain http only)", m.c_str());
            else
                cmd += " --master " + m;
        }
        int result = std::system(cmd.c_str());
        if (result == 0) {
            OZ_INFO("Launched AngelServ.exe on port %d", serverPort);
        } else {
            OZ_ERROR("Failed to launch AngelServ.exe");
        }
    }

    OmegaTechData.Deaths = 1;
}

// ScriptTimer defined above in global section
float X = 0, Y = 0, Z = 0, S = 0, Rotation = 0, W = 0, H = 0, L = 0;
bool NextCollision = false;

float GetDistance(float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    float distance = std::sqrt(dx * dx + dy * dy);
    return distance;
}

int FlipNumber(int num)
{
    int i = 100;
    return i - num;
}


void UpdateEntitiesSim(float dt)
{
    Vector3 playerPos = OmegaTechData.MainCamera.position;

    // Single-pass zone scan for player â€” replaces 4 separate CheckZoneCollision calls
    ZoneManager::Instance().UpdatePlayerRegion(playerPos, g_playerMovement.PlayerBounds);

    // Portal trigger — level-to-level transitions (campaign system)
    if (g_portalCooldown > 0.0f)
        g_portalCooldown -= dt;
    else
    {
        ZonePortal* portal = ZoneManager::Instance().CheckPortalCollision(playerPos, g_playerMovement.PlayerBounds);
        if (portal && !SetSceneFlag)
        {
            OZ_INFO("Portal: entering level '%s'", portal->targetWorld.c_str());
            // Bidirectional portals record the origin so the destination gets a
            // reverse portal back to this exact spot. (g_world_to_load still
            // holds the CURRENT world name until the strncpy below.)
            if (portal->bidirectional) {
                g_portalReturnWorld = g_world_to_load;
                g_portalReturnPos = playerPos;
                g_portalHasReturn = true;
            } else {
                g_portalHasReturn = false;
            }
            strncpy(g_world_to_load, portal->targetWorld.c_str(), sizeof(g_world_to_load) - 1);
            g_world_to_load[sizeof(g_world_to_load) - 1] = '\0';
            g_portalSpawnPos = portal->targetSpawn;
            g_portalTransitionPending = true;
            SetSceneFlag = true;      // LoadWorld runs at end of frame
            g_portalCooldown = 3.0f;  // latch so the trigger can't re-fire mid-transition
        }
    }

    // Arm the transient return portal once the player has walked away from it.
    if (g_returnPortalId >= 0)
    {
        const auto& portals = ZoneManager::Instance().GetPortals();
        if (g_returnPortalId < (int)portals.size())
        {
            ZonePortal& rp = ZoneManager::Instance().GetPortals()[g_returnPortalId];
            Vector3 c = {(rp.bounds.min.x + rp.bounds.max.x) * 0.5f,
                         (rp.bounds.min.y + rp.bounds.max.y) * 0.5f,
                         (rp.bounds.min.z + rp.bounds.max.z) * 0.5f};
            float dx = playerPos.x - c.x, dy = playerPos.y - c.y, dz = playerPos.z - c.z;
            if (!rp.enabled && sqrtf(dx*dx + dy*dy + dz*dz) > 3.5f) {
                rp.enabled = true; // armed — walking back in now returns
                OZ_INFO("Portal: return portal armed");
            }
        }
        else g_returnPortalId = -1;
    }

    // Update all pawns via PawnSystem (FSM: IDLE/PATROL/CHASE/RETURN)
    PawnSystem::Instance().Update(playerPos, dt);

    // Update pickups (respawn timers, player collision).
    // Single-player only. In multiplayer the server owns pickup state and the
    // client additionally requests collects via PickupPawns; running this
    // unconditionally granted every pickup twice and ran two independent
    // respawn clocks that drifted apart.
    if (!g_network_enabled)
        PawnSystem::Instance().UpdatePickups(dt, playerPos, g_playerMovement.PlayerBounds);

    // Pickup collection feedback (console message + flash)
    {
        auto& fb = PawnSystem::Instance().m_pickupFeedback;
        if (fb.collected) {
            OmegaTechTextSystem.Write(TextFormat("Collected: %s", fb.typeName.c_str()));
            fb.collected = false;
        }
    }

    // NOTE: pawn/entity drawing moved out to DrawWorld (once per frame after
    // the fixed-step accumulator) so sim steps never double-draw.

    // Melee stamina regen. Runs every frame alongside the contact-damage
    // check so the pool refills whether or not an NPC is in range.
    LightningEntityManager::Instance().UpdateStamina(dt);

    // Check if any pawn is attacking the player (contact damage)
    {
        g_damageCooldown -= dt;
        float damage = 0;
        if (g_damageCooldown <= 0.0f && PawnSystem::Instance().IsPlayerAttacked(playerPos, damage))
        {
            // Equipped armor mitigates incoming damage (diminishing returns).
            // Shared with the server via oz::MitigateDamage so both ends agree.
            auto& lem = LightningEntityManager::Instance();
            float mitigated = oz::MitigateDamage(damage, lem.GetPlayerDefense());
            lem.SetPlayerHealth(std::max(0.0f, lem.GetPlayerHealth() - mitigated));
            g_damageCooldown = 1.0f;
            if (OmegaTechData.DamageFlash <= 0.0f)
                OmegaTechData.DamageFlash = kDamageFlashDuration;
            if (OmegaTechData.Ticker % 2 == 0)
            {
                SoundManager::Instance().PlayChasing();
            }
        }
    }

    // Death / respawn check — respawn is delayed by the world's
    // LevelSettings.respawnTime (default 5s, immediate = 0).
    {
        static float s_respawnTimer = 0.0f;
        if (LightningEntityManager::Instance().GetPlayerHealth() <= 0.0f)
        {
            if (s_respawnTimer <= 0.0f)
            {
                // First death frame: start the countdown, reset stats now.
                LightningEntityManager::Instance().SetPlayerHealth(100.0f);
                LightningEntityManager::Instance().SetPlayerMana(100.0f);
                OmegaTechData.Deaths++;
                OZ_INFO("Player died! Death #%d", OmegaTechData.Deaths);
                OmegaTechTextSystem.Write(TextFormat("You died! Death #%d", OmegaTechData.Deaths));
                float respawnTime = 5.0f;
                const auto& settings = PawnSystem::Instance().GetWorldInfo().settings;
                if (settings.respawnTime >= 0.0f) respawnTime = settings.respawnTime;
                s_respawnTimer = respawnTime;
            }
            else
            {
                s_respawnTimer -= dt;
                if (s_respawnTimer <= 0.0f)
                {
                    s_respawnTimer = 0.0f;
                    PawnSystem::Instance().RespawnPlayerAtStart(OmegaTechData.MainCamera);
                    // Refill items from save
                    LoadSave();
                }
            }
        }
        else s_respawnTimer = 0.0f;
    }
}

void UpdatePlayer()
{
    if (IsKeyDown(KEY_W) || GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y) != 0 && !Debug)
    {
        if (g_playerMovement.HeadBob)
        {
            if (OmegaTechData.Ticker % 4 == 0)
            {
                float bob = (g_playerMovement.HeadBobDirection == 1) ? 0.05f : -0.05f;
                OmegaTechData.MainCamera.target.y += bob;
                if (g_playerMovement.HeadBob >= 1)
                    g_playerMovement.HeadBobDirection = 0;
                else if (g_playerMovement.HeadBob <= -1)
                    g_playerMovement.HeadBobDirection = 1;
                g_playerMovement.HeadBob += (g_playerMovement.HeadBobDirection == 1) ? 1 : -1;
            }
        }
        SoundManager::Instance().StartWalkLoop();
    }
    else
    {
        SoundManager::Instance().StopWalkLoop();
    }

    g_playerMovement.UpdateBounds(OmegaTechData.MainCamera);
}

void SaveGame()
{
    wstring TFlags = L"";

    for (int i = 0; i <= 99; i++)
    {
        if (ToggleFlags[i].Value == 1)
        {
            TFlags += L'1';
        }
        if (ToggleFlags[i].Value == 0)
        {
            TFlags += L'0';
        }
    }

    TFlags += L':';

    // EntityManager hotbar+equipment state (replaces owned flags)
    {
        std::string emState = LightningEntityManager::Instance().SerializeState();
        TFlags += wstring(emState.begin(), emState.end());
    }
    TFlags += L':';
    // Backpack data
    TFlags += L':';
    for (int i = 0; i < BACKPACK_SLOTS; i++)
    {
        TFlags += to_wstring(gInventory.backpack[i].itemId) + L',' + to_wstring(gInventory.backpack[i].quantity) + L';';
    }
    TFlags += L':' + to_wstring(gInventory.coins);

    wofstream Outfile;
    Outfile.open("GameData/Saves/TF.sav");
    Outfile << TFlags;

    wstring Position = to_wstring(OmegaTechData.MainCamera.position.x) + L':' +
                       to_wstring(OmegaTechData.MainCamera.position.y) + L':' +
                       to_wstring(OmegaTechData.MainCamera.position.z) + L':' +
                       to_wstring(OmegaTechData.LevelIndex) + L':';

    wofstream Outfile1;
    Outfile1.open("GameData/Saves/POS.sav");
    Outfile1 << Position;
}

void LoadSave()
{
    wstring TFlags = LoadFile("GameData/Saves/TF.sav");
    size_t tfLen = TFlags.size();

    for (int i = 0; i <= 99 && i < (int)tfLen; i++)
    {
        if (TFlags[i] == L'1')
        {
            ToggleFlags[i].Value = 1;
        }
        if (TFlags[i] == L'0')
        {
            ToggleFlags[i].Value = 0;
        }
    }

    // Load EntityManager hotbar+equipment state (after first ':')
    size_t emSectionStart = TFlags.find(L':', 100);
    size_t emSectionEnd = string::npos;
    if (emSectionStart != string::npos && emSectionStart + 1 < TFlags.size()) {
        // EntityManager state ends at the next ':' (which separates from backpack data)
        emSectionEnd = TFlags.find(L':', emSectionStart + 1);
        if (emSectionEnd != string::npos) {
            std::string emState(TFlags.begin() + emSectionStart + 1, TFlags.begin() + emSectionEnd);
            LightningEntityManager::Instance().DeserializeState(emState);
        }
    }

    // Load backpack data (after EntityManager state section)
    size_t bpStart = emSectionEnd;
    if (bpStart != string::npos && bpStart + 1 < TFlags.size())
    {
        wstring bpData = TFlags.substr(bpStart + 1);
        size_t secondColon = bpData.find(L':');
        wstring slotData = (secondColon != string::npos) ? bpData.substr(0, secondColon) : bpData;
        // Parse slot data: "id,qty;id,qty;..."
        size_t pos = 0;
        int slotIdx = 0;
        while (pos < slotData.size() && slotIdx < BACKPACK_SLOTS)
        {
            size_t semi = slotData.find(L';', pos);
            if (semi == string::npos)
                break;
            wstring pair = slotData.substr(pos, semi - pos);
            size_t comma = pair.find(L',');
            if (comma != string::npos)
            {
                try {
                    int id = stoi(pair.substr(0, comma));
                    int qty = stoi(pair.substr(comma + 1));
                    gInventory.backpack[slotIdx].itemId = id;
                    gInventory.backpack[slotIdx].quantity = qty;
                } catch (...) { }
            }
            pos = semi + 1;
            slotIdx++;
        }
        // Coins
        if (secondColon != string::npos)
        {
            wstring coinStr = bpData.substr(secondColon + 1);
            if (!coinStr.empty()) {
                try { gInventory.coins = stoi(coinStr); } catch (...) { }
            }
        }
    }

    wstring Position = LoadFile("GameData/Saves/POS.sav");

    if (!Position.empty())
    {
        try {
            OmegaTechData.LevelIndex = int(ToFloat(WSplitValue(Position, 3)));
        } catch (...) {
            OmegaTechData.LevelIndex = 1;
        }

        SetCameraFlag = true;

        try {
            int X = ToFloat(WSplitValue(Position, 0));
            int Y = ToFloat(WSplitValue(Position, 1));
            int Z = ToFloat(WSplitValue(Position, 2));
            SetCameraPos = {float(X), float(Y), float(Z)};
        } catch (...) {
            SetCameraPos = {0.0f, 10.0f, 0.0f};
        }
    }
}

// Sweep every active projectile one frame-step ahead and test against OZONE
// brush collision volumes + heightmap terrain. On a hit the projectile is
// deactivated and an impact burst + decal are spawned at the contact point.
// Position integration itself stays in PawnSystem::UpdateProjectiles.
static void SweepProjectilesVsWorld(float dt)
{
    auto& loader = OzoneLoader::Instance();
    const auto& volumes = loader.GetCollisionVolumes();
    bool hasTerrain = loader.HasHeightmap();
    auto& projectiles = PawnSystem::Instance().GetProjectiles();

    for (auto& p : projectiles) {
        if (!p.active) continue;

        Vector3 next = {p.position.x + p.velocity.x * dt,
                        p.position.y + p.velocity.y * dt,
                        p.position.z + p.velocity.z * dt};

        float bestT = 1.0f;
        Vector3 bestNormal = {0, 1, 0};
        Vector3 hitPos = {0, 0, 0};
        bool hit = false;
        bool terrainHit = false;

        for (const auto& cv : volumes) {
            if (cv.isHeightmap) continue;
            float t;
            Vector3 n;
            if (SegmentVsAABB(p.position, next, cv.aabb, t, n) && t < bestT) {
                bestT = t;
                bestNormal = n;
                hit = true;
            }
        }

        if (!hit && hasTerrain) {
            float groundY = loader.SampleHeightmapY(next.x, next.z);
            if (next.y <= groundY) {
                hit = true;
                terrainHit = true;
                bestT = 1.0f;
                bestNormal = {0, 1, 0};
                hitPos = {next.x, groundY, next.z};
            }
        }

        if (hit) {
            if (!terrainHit)
                hitPos = Vector3Lerp(p.position, next, bestT);
            p.active = false;
            CombatFX::Instance().SpawnImpact(hitPos, bestNormal,
                                             Color{255, 180, 60, 255}, 12, 6.0f);
            CombatFX::Instance().AddDecal(hitPos, bestNormal, 0.35f);
        }
    }
}

void DrawWorld()
{
    BeginTextureMode(Target);
    ClearBackground(BLACK);

// Detect sky zone BEFORE 3D mode begins (needed for sky camera setup)
    PawnSystem::Instance().UpdateSkyZone(
        OmegaTechData.MainCamera.position,
        g_playerMovement.PlayerBounds);
    bool inSkyZone = PawnSystem::Instance().IsInSkyZone();

    BeginMode3D(OmegaTechData.MainCamera);

    // -----------------------------------------------------------------------
    // 3D Skybox Cube — drawn first with depth-write disabled so it sits
    // behind all world geometry. Top/bottom use the active skybox (cap),
    // 4 sides use the side texture from LevelInfo (g_skySideTex).
    // -----------------------------------------------------------------------
    {
        Texture2D capTex = {0};
        // Precedence: the level skybox (levelinfo / set_skybox / world default)
        // is authoritative; a zone's authored skybox is only used when the level
        // defines none. Previously the zone texture always won, so a saved
        // levelinfo skybox appeared to be ignored inside sky zones.
        if (WorldModels.Skybox.id > 0 && OmegaTechData.SkyboxEnabled) {
            capTex = WorldModels.Skybox;
        } else if (inSkyZone) {
            SkyZoneNode* sky = PawnSystem::Instance().GetActiveSkyZone();
            if (sky && sky->skyboxTex.id > 0)
                capTex = sky->skyboxTex;
            else if (WorldModels.Skybox.id > 0)
                capTex = WorldModels.Skybox;
        }
        // Sides use an authored side texture when available; otherwise reuse the
        // cap texture so the sky renders all around (previously they drew a flat
        // color and appeared black).
        Texture2D sideTex = g_skySideTex;
        if (sideTex.id == 0) sideTex = capTex;

        if (capTex.id > 0 || sideTex.id > 0) {
            Vector3 camPos = OmegaTechData.MainCamera.position;
            rlDisableDepthMask();
            rlDisableBackfaceCulling();

            // Top face (index 0) — at y=+1000, normal -Y (faces down)
            {
                Color fallback = {80, 120, 200, 255};
                rlPushMatrix();
                rlTranslatef(camPos.x, camPos.y + kSkyboxDist, camPos.z);
                rlRotatef(180.0f, 1.0f, 0.0f, 0.0f);
                if (capTex.id > 0) {
                    OmegaTechData.SkyboxFace[0].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = capTex;
                    DrawModel(OmegaTechData.SkyboxFace[0], {0,0,0}, 1.0f, WHITE);
                } else {
                    DrawModel(OmegaTechData.SkyboxFace[0], {0,0,0}, 1.0f, fallback);
                }
                rlPopMatrix();
            }
            // Bottom face (index 1) — at y=-1000, normal +Y (faces up)
            {
                Color fallback = {80, 120, 200, 255};
                rlPushMatrix();
                rlTranslatef(camPos.x, camPos.y - kSkyboxDist, camPos.z);
                if (capTex.id > 0) {
                    OmegaTechData.SkyboxFace[1].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = capTex;
                    DrawModel(OmegaTechData.SkyboxFace[1], {0,0,0}, 1.0f, WHITE);
                } else {
                    DrawModel(OmegaTechData.SkyboxFace[1], {0,0,0}, 1.0f, fallback);
                }
                rlPopMatrix();
            }
            // +X face (index 2) - vertical wall at x=+kSkyboxDist
            // NOTE: the plane is generated in the XZ plane, so standing it up
            // requires a rotation about X or Z. A rotation about Y (as this code
            // previously used) only spins the quad within its own plane and
            // leaves it horizontal, i.e. edge-on and invisible at the camera's
            // own height - which is why outdoor levels showed a black void
            // wherever a side face should have been.
            {
                Color fallback = {120, 180, 240, 255};
                rlPushMatrix();
                rlTranslatef(camPos.x + kSkyboxDist, camPos.y, camPos.z);
                rlRotatef(90.0f, 0.0f, 0.0f, 1.0f);
                if (sideTex.id > 0) {
                    OmegaTechData.SkyboxFace[2].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = sideTex;
                    DrawModel(OmegaTechData.SkyboxFace[2], {0,0,0}, 1.0f, WHITE);
                } else {
                    DrawModel(OmegaTechData.SkyboxFace[2], {0,0,0}, 1.0f, fallback);
                }
                rlPopMatrix();
            }
            // -X face (index 3) - vertical wall at x=-kSkyboxDist
            {
                Color fallback = {120, 180, 240, 255};
                rlPushMatrix();
                rlTranslatef(camPos.x - kSkyboxDist, camPos.y, camPos.z);
                rlRotatef(-90.0f, 0.0f, 0.0f, 1.0f);
                if (sideTex.id > 0) {
                    OmegaTechData.SkyboxFace[3].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = sideTex;
                    DrawModel(OmegaTechData.SkyboxFace[3], {0,0,0}, 1.0f, WHITE);
                } else {
                    DrawModel(OmegaTechData.SkyboxFace[3], {0,0,0}, 1.0f, fallback);
                }
                rlPopMatrix();
            }
            // +Z face (index 4) - vertical wall at z=+kSkyboxDist
            {
                Color fallback = {120, 180, 240, 255};
                rlPushMatrix();
                rlTranslatef(camPos.x, camPos.y, camPos.z + kSkyboxDist);
                rlRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
                if (sideTex.id > 0) {
                    OmegaTechData.SkyboxFace[4].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = sideTex;
                    DrawModel(OmegaTechData.SkyboxFace[4], {0,0,0}, 1.0f, WHITE);
                } else {
                    DrawModel(OmegaTechData.SkyboxFace[4], {0,0,0}, 1.0f, fallback);
                }
                rlPopMatrix();
            }
            // -Z face (index 5) - vertical wall at z=-kSkyboxDist
            {
                Color fallback = {120, 180, 240, 255};
                rlPushMatrix();
                rlTranslatef(camPos.x, camPos.y, camPos.z - kSkyboxDist);
                rlRotatef(90.0f, 1.0f, 0.0f, 0.0f);
                if (sideTex.id > 0) {
                    OmegaTechData.SkyboxFace[5].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = sideTex;
                    DrawModel(OmegaTechData.SkyboxFace[5], {0,0,0}, 1.0f, WHITE);
                } else {
                    DrawModel(OmegaTechData.SkyboxFace[5], {0,0,0}, 1.0f, fallback);
                }
                rlPopMatrix();
            }
            rlEnableBackfaceCulling();
            rlEnableDepthMask();
        }
    }

    // -----------------------------------------------------------------------
    // SKY PASS — render SURF_FAKEBACKDROP brushes from the main camera with
    // depth disabled so the skybox shows through behind them.
    // -----------------------------------------------------------------------
if (inSkyZone)
    {
        rlDisableDepthMask();
        OzoneLoader::Instance().DrawZoneGeometry(OmegaTechData.MainCamera);
        rlEnableDepthMask();
    }

    // GameplaySoundZone - zone music/ambience/enter-sfx profiles plus the
    // per-frame music stream pump are owned by SoundManager.
    SoundManager::Instance().UpdateSoundZones(ZoneManager::Instance().GetPlayerRegion());
    SoundManager::Instance().Update();

    OzoneLoader::Instance().DrawWorldGeometry(OmegaTechData.MainCamera);

    // OZONE brush collision — chunk-accelerated query, delegated to the
    // physics module via the adapter below.
    {
        oz::physics::OzoneCollisionQuery q;
        Vector3 cp = OmegaTechData.MainCamera.position;
        // Effective eye height shrinks while crouching (dynamic player stance)
        float playerFeet = cp.y - g_playerMovement.EyeHeight;
        if (oz::physics::PlayerPhysics().TestObstacleOverlap(g_playerMovement.PlayerBounds,
                                                             playerFeet, q))
            ObjectCollision = true;
    }

    // OZONE ground clamp -- delegated to the physics module (heightmap first,
    // then brush primitives, as supplied by the adapter).
    if (!g_playerMovement.isFlying && !g_playerMovement.isNoClip && !g_playerMovement.isClimbing)
    {
        oz::physics::OzoneCollisionQuery q;
        oz::physics::PlayerPhysics pphys;
        oz::physics::PlayerPhysics::Motion motion;
        motion.onGround = g_playerMovement.onGround;
        motion.velocityY = g_playerMovement.velocityY;
        pphys.ClampToGround(OmegaTechData.MainCamera, motion, g_playerMovement.EyeHeight, q);
        g_playerMovement.onGround = motion.onGround;
        g_playerMovement.velocityY = motion.velocityY;
    }

    UpdatePlayer();

    // Collision debug overlay (/showcollisions)
    if (g_showCollisionDebug)
    {
        // OZONE brush collision volumes
        auto &vols = OzoneLoader::Instance().GetCollisionVolumes();
        for (auto &cv : vols)
            DrawBoundingBox(cv.aabb, PURPLE);

        // Heightmap bounds
        if (OzoneLoader::Instance().HasHeightmap())
        {
            auto &hmModel = OzoneLoader::Instance().GetHeightmapModel();
            Vector3 hmPos = OzoneLoader::Instance().GetHeightmapPosition();
            float hmScale = OzoneLoader::Instance().GetHeightmapScale();
            BoundingBox hmBox = GetMeshBoundingBox(hmModel.meshes[0]);
            hmBox.min.x = hmPos.x + hmBox.min.x * hmScale;
            hmBox.min.y = hmPos.y + hmBox.min.y * hmScale;
            hmBox.min.z = hmPos.z + hmBox.min.z * hmScale;
            hmBox.max.x = hmPos.x + hmBox.max.x * hmScale;
            hmBox.max.y = hmPos.y + hmBox.max.y * hmScale;
            hmBox.max.z = hmPos.z + hmBox.max.z * hmScale;
            DrawBoundingBox(hmBox, ORANGE);
        }

        // Zone volumes
        for (auto &zone : ZoneManager::Instance().GetZones())
            DrawBoundingBox(zone.bounds, BLUE);

        // Player start markers
        for (auto &ps : PawnSystem::Instance().GetPlayerStarts())
            DrawCubeWires(ps.position, 0.5f, 1.0f, 0.5f, GREEN);
    }

    // Combat FX — projectile rendering, impacts, decals, muzzle flash and
    // remote players all live here so they render inside the 3D camera pass.
    {
        CombatFX::Instance().Update(GetFrameTime());
        PawnSystem::Instance().DrawProjectiles(OmegaTechData.MainCamera, OmegaTechData.Lights);
        CombatFX::Instance().Draw3D(OmegaTechData.MainCamera);
        DrawRemotePlayers3D();
    }

    if (Debug)
    {
        DrawLights();
    }
    else
    {
        // Fixed 60 Hz timestep for the client simulation. Physics, pawn FSM,
        // projectiles and portal scans advance in discrete 1/60s steps (up to 4
        // per render frame to avoid an unbounded catch-up spiral) so gameplay
        // is frame-rate independent. Rendering happens once per frame below.
        static double s_sim_accumulator = 0.0;
        static constexpr double kSimTick = 1.0 / 60.0;
        s_sim_accumulator += GetFrameTime();
        int steps = 0;
        while (s_sim_accumulator >= kSimTick && steps < 4) {
            s_sim_accumulator -= kSimTick;
            UpdateEntitiesSim(static_cast<float>(kSimTick));
            // Wall/terrain impact sweep, then update projectiles (age, movement, gravity)
            SweepProjectilesVsWorld(static_cast<float>(kSimTick));
            PawnSystem::Instance().UpdateProjectiles(static_cast<float>(kSimTick));
            ++steps;
        }
        if (steps >= 4 && s_sim_accumulator >= kSimTick)
            s_sim_accumulator = 0.0; // drift guard: discard excess catch-up

        // GameEngine.ParticleEmitter — simulated in isolation *after* the
        // weapon/NPC/projectile ticks so particles cannot re-enter gameplay.
        OzParticleSimulationManager::Instance().Update(GetFrameTime());

        // Draw pawns / entity billboards exactly once per frame (kept out of
        // the sim steps so a catch-up frame doesn't re-draw the scene).
        PawnSystem::Instance().DrawAll(OmegaTechData.MainCamera, OmegaTechData.Lights);
        PawnSystem::Instance().DrawEntities(OmegaTechData.MainCamera, OmegaTechData.Lights, OmegaTechData.WindShader);
        DrawLightFlares(OmegaTechData.MainCamera);

        // First-person weapon view-model (drawn on top of the world).
        // Suppressed for --shot captures: the view-model is camera-locked UI,
        // not the level, and it covers a third of the frame.
        if (!(g_shot.active && g_shot.hideHud)) {
            oz::ViewModel::Instance().Update(GetFrameTime());
            oz::ViewModel::Instance().Draw(OmegaTechData.MainCamera, OmegaTechData.Lights);
        }
    }
    // Collision rollback - delegated to the physics module.
    if (ObjectCollision)
    {
        oz::physics::PlayerPhysics().RestorePosition(
            OmegaTechData.MainCamera,
            g_playerMovement.OldX, g_playerMovement.OldY, g_playerMovement.OldZ,
            g_playerMovement.isNoClip);
        ObjectCollision = false;
    }

    // Zone reverb - the simulated DSP (mix/decay + music ducking) is applied
    // by SoundManager on zone transitions.
    SoundManager::Instance().UpdateReverb(ZoneManager::Instance().GetPlayerRegion());

    // LightningScript entity tick
    {
        float dt = GetFrameTime();
        LightningEntityManager::Instance().Update(dt);

        // Sync pending script effects into PawnSystem's active SkyZoneNode
        PawnSystem::Instance().SyncSkyboxState();

        // Apply pending Fog changes from script contexts
        auto &lem = LightningEntityManager::Instance();

        // restore_fog: hand fog back to the level defaults. The three restore_*
        // opcodes used to clear the pending value instead of signalling the host,
        // so they never restored anything.
        if (lem.TakePendingFogRestore()) {
            static int fogD = GetShaderLocation(OmegaTechData.Lights, "fogDensity");
            static int fogC = GetShaderLocation(OmegaTechData.Lights, "fogColor");
            float density = 0.0f;
            float color[3] = {FogTint.r / 255.0f, FogTint.g / 255.0f, FogTint.b / 255.0f};
            SetShaderValue(OmegaTechData.Lights, fogD, &density, SHADER_UNIFORM_FLOAT);
            SetShaderValue(OmegaTechData.Lights, fogC, color, SHADER_UNIFORM_VEC3);
            FogEnabled = false;
            FogIntensity = 0.0f;
            OZ_INFO("LightningScript: restore_fog - reverted to level default");
        }

        if (lem.HasPendingFog())
        {
            static int fogDensityLoc = GetShaderLocation(OmegaTechData.Lights, "fogDensity");
            static int fogColorLoc = GetShaderLocation(OmegaTechData.Lights, "fogColor");
            float density = lem.PendingFogDensity();
            float color[3] = {lem.PendingFogR(), lem.PendingFogG(), lem.PendingFogB()};
            SetShaderValue(OmegaTechData.Lights, fogDensityLoc, &density, SHADER_UNIFORM_FLOAT);
            SetShaderValue(OmegaTechData.Lights, fogColorLoc, color, SHADER_UNIFORM_VEC3);
            FogEnabled = true;
            FogIntensity = (density > 0) ? density : 0.3f;
            FogTint = {(unsigned char)(color[0] * 255), (unsigned char)(color[1] * 255),
                       (unsigned char)(color[2] * 255), 255};
            lem.ClearPendingFog();
        }

        // restore_skybox: drop the script override and go back to the level skybox.
        // This used to set __skybox to "", which PopPendingSkybox rejects as
        // "nothing pending", so the skybox was never restored.
        if (lem.TakePendingSkyboxRestore()) {
            SkyZoneNode* sky = PawnSystem::Instance().GetActiveSkyZone();
            if (sky && sky->skyboxTex.id > 0) {
                UnloadTexture(sky->skyboxTex);
                sky->skyboxTex = Texture2D{0};
            }
            // WorldModels.Skybox is left alone: it is the level's own texture and
            // is still what the sky pass falls back to.
            OmegaTechData.SkyboxEnabled = (WorldModels.Skybox.id > 0);
            OZ_INFO("LightningScript: restore_skybox - reverted to level skybox");
        }

        // Apply pending Skybox changes from script contexts.
        // Only an explicit script request (set_skybox) overrides the levelinfo
        // skybox; the zone's authored skyboxPath is kept for inside-zone
        // rendering by the sky pass and must NOT clobber the global level skybox
        // every frame (that made a saved levelinfo skybox appear ignored).
        if (lem.HasPendingSkybox()) {
            std::string path = lem.PendingSkybox();
            lem.ClearPendingSkybox();
            if (!path.empty()) {
                OZ_INFO("LightningScript: loading skybox '%s'", path.c_str());
                Texture2D newSky = LoadTextureWithFallback(path.c_str());
                if (newSky.id > 0) {
                    SkyZoneNode* sky = PawnSystem::Instance().GetActiveSkyZone();
                    if (sky) {
                        if (sky->skyboxTex.id > 0)
                            UnloadTexture(sky->skyboxTex);
                        sky->skyboxTex = newSky;
                    }
                    if (WorldModels.Skybox.id > 0)
                        UnloadTexture(WorldModels.Skybox);
                    WorldModels.Skybox = newSky;
                    OmegaTechData.SkyboxEnabled = true;
                } else {
                    OZ_WARN("LightningScript: skybox '%s' not found", path.c_str());
                }
            }
        }

        // Apply pending Ambient changes from script contexts
        if (lem.TakePendingAmbientRestore()) {
            static int ambLoc = GetShaderLocation(OmegaTechData.Lights, "ambient");
            float amb[4] = {1.0f, 1.0f, 1.0f, 1.0f};   // matches LitLightning default
            SetShaderValue(OmegaTechData.Lights, ambLoc, amb, SHADER_UNIFORM_VEC4);
            OZ_INFO("LightningScript: restore_ambient - reverted to level default");
        }
        if (lem.HasPendingAmbient())
        {
            float amb[4] = {lem.PendingAmbientR(), lem.PendingAmbientG(), lem.PendingAmbientB(), 1.0f};
            static int ambientLoc = GetShaderLocation(OmegaTechData.Lights, "ambient");
            SetShaderValue(OmegaTechData.Lights, ambientLoc, amb, SHADER_UNIFORM_VEC4);
            lem.ClearPendingAmbient();
        }

        // Zone music authored on a skyzone def (`music = "..."`).
        if (lem.HasPendingMusic()) {
            const std::string track = lem.PendingMusic();
            lem.ClearPendingMusic();
            SoundManager::Instance().PlayWorldMusic(track);
        }

        // Zone environment override application (from combined player region)
        {
            auto& region = ZoneManager::Instance().GetPlayerRegion();
            bool inEnvZone = region.combinedEnv.applyFog || region.combinedEnv.applyAmbient;
            std::string envZoneName = (region.primaryZoneId >= 0) ? std::to_string(region.primaryZoneId) : "";

            if (inEnvZone && g_activeEnvZone != envZoneName)
            {
                auto &eo = region.combinedEnv;
                OZ_DEBUG("Zone env: applyFog=%d applyAmbient=%d", eo.applyFog, eo.applyAmbient);
                if (eo.applyFog && OmegaTechData.Lights.id > 0)
                {
                    float fc[3] = {eo.fogR / 255.0f, eo.fogG / 255.0f, eo.fogB / 255.0f};
                    float fd = eo.fogDensity;
                    static int fogDensityLoc = GetShaderLocation(OmegaTechData.Lights, "fogDensity");
                    static int fogColorLoc = GetShaderLocation(OmegaTechData.Lights, "fogColor");
                    static int fogStartLoc = GetShaderLocation(OmegaTechData.Lights, "fogStart");
                    static int fogEndLoc = GetShaderLocation(OmegaTechData.Lights, "fogEnd");
                    SetShaderValue(OmegaTechData.Lights, fogColorLoc, fc, SHADER_UNIFORM_VEC3);
                    SetShaderValue(OmegaTechData.Lights, fogDensityLoc, &fd, SHADER_UNIFORM_FLOAT);
                    float fs = eo.fogStart, fe = eo.fogEnd;
                    SetShaderValue(OmegaTechData.Lights, fogStartLoc, &fs, SHADER_UNIFORM_FLOAT);
                    SetShaderValue(OmegaTechData.Lights, fogEndLoc, &fe, SHADER_UNIFORM_FLOAT);
                    FogEnabled = true;
                    FogIntensity = (fd > 0) ? fd : 0.3f;
                    FogTint = {(unsigned char)eo.fogR, (unsigned char)eo.fogG, (unsigned char)eo.fogB, 255};
                }
                if (eo.applyAmbient && OmegaTechData.Lights.id > 0)
                {
                    float amb[4] = {eo.ambR / 255.0f * eo.ambIntensity,
                                    eo.ambG / 255.0f * eo.ambIntensity,
                                    eo.ambB / 255.0f * eo.ambIntensity, 1.0f};
                    static int ambientLoc = GetShaderLocation(OmegaTechData.Lights, "ambient");
                    SetShaderValue(OmegaTechData.Lights, ambientLoc, amb, SHADER_UNIFORM_VEC4);
                }
                g_activeEnvZone = envZoneName;
            }
            else if (!inEnvZone && !g_activeEnvZone.empty())
            {
                // Exited env zone â€” restore defaults from WorldInfo
                OZ_DEBUG("Zone env: restoring WorldInfo defaults");
                if (OmegaTechData.Lights.id > 0)
                {
                    auto& wi = PawnSystem::Instance().GetWorldInfo();
                    float defFogColor[3] = {wi.defaultEnv.fogR / 255.0f,
                                            wi.defaultEnv.fogG / 255.0f,
                                            wi.defaultEnv.fogB / 255.0f};
                    float defFogDensity = wi.defaultEnv.fogDensity;
                    float defFogStart = wi.defaultEnv.fogStart, defFogEnd = wi.defaultEnv.fogEnd;
                    static int fogDensityLoc = GetShaderLocation(OmegaTechData.Lights, "fogDensity");
                    static int fogColorLoc = GetShaderLocation(OmegaTechData.Lights, "fogColor");
                    static int fogStartLoc = GetShaderLocation(OmegaTechData.Lights, "fogStart");
                    static int fogEndLoc = GetShaderLocation(OmegaTechData.Lights, "fogEnd");
                    SetShaderValue(OmegaTechData.Lights, fogColorLoc, defFogColor, SHADER_UNIFORM_VEC3);
                    SetShaderValue(OmegaTechData.Lights, fogDensityLoc, &defFogDensity, SHADER_UNIFORM_FLOAT);
                    SetShaderValue(OmegaTechData.Lights, fogStartLoc, &defFogStart, SHADER_UNIFORM_FLOAT);
                    SetShaderValue(OmegaTechData.Lights, fogEndLoc, &defFogEnd, SHADER_UNIFORM_FLOAT);
                }
                FogEnabled = false;
                FogIntensity = 0.3f;
                FogTint = {200, 200, 210, 255};
                g_activeEnvZone.clear();
            }
        }
    // Route script HUD messages (msg opcode) to the on-screen text system
        if (!lem.PendingMessage().empty())
        {
            OmegaTechTextSystem.Write(lem.PendingMessage());
            lem.ClearPendingMessage();
        }

        // Scripted damage flash (damage opcode)
        if (lem.PlayerHurt())
        {
            lem.ClearPlayerHurt();
            if (OmegaTechData.DamageFlash <= 0.0f)
                OmegaTechData.DamageFlash = kDamageFlashDuration;
        }
    }

    EndMode3D();
    EndTextureMode();

    if (SetSceneFlag)
    {
        OmegaTechData.LevelIndex = SetSceneId;
        LoadWorld();
        if (g_portalTransitionPending)
        {
            // Arriving via portal: place player at the portal's target spawn,
            // preserving view direction.
            Vector3 oldPos = OmegaTechData.MainCamera.position;
            OmegaTechData.MainCamera.position = g_portalSpawnPos;
            // Never arrive under the terrain: lift the camera to the OZONE
            // heightmap surface at this XZ when the raw spawn sits below grade.
            float groundY = OzoneLoader::Instance().HasHeightmap()
                                ? OzoneLoader::Instance().SampleHeightmapY(g_portalSpawnPos.x, g_portalSpawnPos.z)
                                : -99999.0f;
            if (groundY > -50000.0f &&
                OmegaTechData.MainCamera.position.y < groundY + g_playerMovement.EyeHeight)
                OmegaTechData.MainCamera.position.y = groundY + g_playerMovement.EyeHeight;
            OmegaTechData.MainCamera.target.x += OmegaTechData.MainCamera.position.x - oldPos.x;
            OmegaTechData.MainCamera.target.y += OmegaTechData.MainCamera.position.y - oldPos.y;
            OmegaTechData.MainCamera.target.z += OmegaTechData.MainCamera.position.z - oldPos.z;
            // Reset movement state to the arrival point so the collision
            // "restore" path can't yank the player back to a stale position
            // from the previous world.
            g_playerMovement.OldX = OmegaTechData.MainCamera.position.x;
            g_playerMovement.OldY = OmegaTechData.MainCamera.position.y;
            g_playerMovement.OldZ = OmegaTechData.MainCamera.position.z;
            g_playerMovement.velocityY = 0.0f;
            g_playerMovement.onGround = true;
            g_portalTransitionPending = false;

            // Bidirectional portal: drop a return portal at the arrival point.
            // It starts disabled and arms once the player steps away (the
            // arming check lives in the portal trigger block above).
            if (g_portalHasReturn && !g_portalReturnWorld.empty())
            {
                ZonePortal rp;
                rp.targetWorld = g_portalReturnWorld;
                rp.targetSpawn = g_portalReturnPos;
                rp.bidirectional = true;
                rp.enabled = false; // armed after the player moves away
                Vector3 c = OmegaTechData.MainCamera.position;
                rp.bounds = {
                    {c.x - 1.5f, c.y - 1.5f, c.z - 1.5f},
                    {c.x + 1.5f, c.y + 1.5f, c.z + 1.5f}
                };
                g_returnPortalId = ZoneManager::Instance().AddPortal(rp);
                OZ_INFO("Portal: return portal to '%s' placed at arrival point",
                        g_portalReturnWorld.c_str());
            }
            else g_returnPortalId = -1;
        }
        SetSceneFlag = false;
    }

    if (SetCameraFlag)
    {
        OmegaTechData.MainCamera.position = SetCameraPos;
        SetCameraFlag = false;
    }

    if (ScriptTimer != 0)
    {
        ScriptTimer--;
    }

    if (OmegaTechData.Ticker != 60)
    {
        OmegaTechData.Ticker++;
    }
    else
    {
        OmegaTechData.Ticker = 0;
    }
}
