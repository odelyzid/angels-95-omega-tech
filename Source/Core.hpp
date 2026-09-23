#include "Data.hpp"
#include "Log.hpp"
#include "Package/OzAssetMapper.hpp"
#include "Audio/OzSoundLoader.hpp"
#include "Audio/DspReverb.hpp"
#include "OzOzoneLoader.hpp"
#include "Pawn/OzPawnSystem.hpp"
#include "Package/PackageAssetLoader.hpp"
#include "Renderer/EngineBillboard.hpp"
#include "Script/LightningEntityRegistry.hpp"
#include "Script/LightningEntityManager.hpp"

#include "raymath.h"
#include "rlights/rlights.h"
#include "Custom/OTCustom.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

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

int ScriptTimer = 0;

// Cross-world state that must be reset on each LoadWorld()
float g_damageCooldown = 0.0f;
std::string g_prevSoundZone;
Music g_defaultWorldMusic = {0};
Sound g_ambienceHandle = {0};
std::string g_ambienceZoneName;
bool g_wasInReverb = false;
std::string g_activeEnvZone;
Texture2D g_skySideTex = {0};   // optional horizon/side skybox variant

// Set from PlayHomeScreen to request a server join
bool SetServerJoinFlag = false;
const char *SetServerJoinIP = nullptr;

void LoadSave();
void SaveGame();
void UpdateCustom();
void CacheWDL();
float SampleHeightmapGroundY(float px, float pz);
void DrawRemotePlayers3D();

#include "ParticleDemon/ParticleDemon.hpp"
#include "Renderer/CombatFX.hpp"

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
    Light GameLights[MAX_LIGHTS];

    ParticleSystem RainParticles;
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
    bool UseCachedRenderer = false;
    int BadPreformaceCounter = 0;
    bool SkyboxEnabled = false;
    int Ending = 0;
    int PanicCounter = 0;

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

void LoadEntitiesFromWDL()
{
    wstring WData = WorldData;
    int Size = GetWDLSize(WorldData, L"");

    for (int i = 0; i <= Size; i++)
    {
        wstring Instruction = WSplitValue(WData, i);

        if (Instruction.empty() || Instruction[0] == L'#')
            continue;

        // Pickup: "Pickup:typeName:X:Y:Z:S:R:" (old format: integer index at field 1)
        if (Instruction.substr(0, 6) == L"Pickup")
        {
            float x = ToFloat(WSplitValue(WData, i + 2));
            float y = ToFloat(WSplitValue(WData, i + 3));
            float z = ToFloat(WSplitValue(WData, i + 4));
            wstring typeField = WSplitValue(WData, i + 1);
            std::string typeName;
            try
            {
                int legacyIdx = std::stoi(typeField);
                static const char *legacyMap[] = {"HealthVial", "ManaVial", "EnergyCrystal", "Key", "Coin", "Powerup"};
                if (legacyIdx >= 0 && legacyIdx < 6)
                    typeName = legacyMap[legacyIdx];
            }
            catch (...)
            {
                typeName = std::string(typeField.begin(), typeField.end());
            }
            PickupNode node;
            node.position = {x, y, z};
            // Snap pickup to ground height if floating
            if (WDLModels.HeightMapReady) {
                float groundY = SampleHeightmapGroundY(x, z);
                if (groundY > -99990.0f && y > groundY + 1.0f)
                    node.position.y = groundY + 0.5f;
            }
            node.typeName = typeName;
            PawnSystem::Instance().AddPickup(node);
            i += 6;
            continue;
        }
        // Spawn: "Spawn:X:Y:Z:S:Rotation"
        else if (Instruction.substr(0, 5) == L"Spawn")
        {
            float x = ToFloat(WSplitValue(WData, i + 1));
            float y = ToFloat(WSplitValue(WData, i + 2));
            float z = ToFloat(WSplitValue(WData, i + 3));
            float yaw = ToFloat(WSplitValue(WData, i + 5));
            PlayerStartNode node;
            node.position = {x, y, z};
            node.yaw = yaw;
            PawnSystem::Instance().AddPlayerStart(node);
            i += 5;
            continue;
        }
        // NPC: "NPC<ClassName>:X:Y:Z:S:Rotation" or "NPC:X:Y:Z:S:Rotation:ClassName"
        else if (Instruction.substr(0, 3) == L"NPC")
        {
            float x = ToFloat(WSplitValue(WData, i + 1));
            float y = ToFloat(WSplitValue(WData, i + 2));
            float z = ToFloat(WSplitValue(WData, i + 3));
            string className;
            if (Instruction.size() > 3)
                className = string(Instruction.begin() + 3, Instruction.end());
            else
                className = string(WSplitValue(WData, i + 6).begin(), WSplitValue(WData, i + 6).end());
            PawnSystem::Instance().Spawn({x, y, z}, className.c_str());
            i += 5;
            continue;
        }
        // Light: "Light:X:Y:Z:R:G:B:I:Rad:T:E:"
        else if (Instruction == L"Light")
        {
            LightNode node;
            node.active = true;
            node.position.x = ToFloat(WSplitValue(WData, i + 1));
            node.position.y = ToFloat(WSplitValue(WData, i + 2));
            node.position.z = ToFloat(WSplitValue(WData, i + 3));
            node.color.r = (unsigned char)ToFloat(WSplitValue(WData, i + 4));
            node.color.g = (unsigned char)ToFloat(WSplitValue(WData, i + 5));
            node.color.b = (unsigned char)ToFloat(WSplitValue(WData, i + 6));
            node.color.a = 255;
            node.intensity = ToFloat(WSplitValue(WData, i + 7));
            node.radius = ToFloat(WSplitValue(WData, i + 8));
            int typeVal = (int)ToFloat(WSplitValue(WData, i + 9));
            node.type = (typeVal == 1) ? LitLightType::DIRECTIONAL : (typeVal == 2) ? LitLightType::SPOT : LitLightType::POINT;
            node.effect = (LitLightEffect)(int)ToFloat(WSplitValue(WData, i + 10));
            PawnSystem::Instance().AddLight(node);
            i += 10;
            continue;
        }
        // Sound: "Sound:X:Y:Z:S:Rotation"
        else if (Instruction.substr(0, 5) == L"Sound")
        {
            float x = ToFloat(WSplitValue(WData, i + 1));
            float y = ToFloat(WSplitValue(WData, i + 2));
            float z = ToFloat(WSplitValue(WData, i + 3));
            EmitterNode node;
            node.position = {x, y, z};
            node.type = EmitterType::SOUND;
            PawnSystem::Instance().AddEmitter(node);
            i += 5;
            continue;
        }
        // Music: "Music:X:Y:Z:S:Rotation"
        else if (Instruction.substr(0, 5) == L"Music")
        {
            float x = ToFloat(WSplitValue(WData, i + 1));
            float y = ToFloat(WSplitValue(WData, i + 2));
            float z = ToFloat(WSplitValue(WData, i + 3));
            EmitterNode node;
            node.position = {x, y, z};
            node.type = EmitterType::MUSIC;
            PawnSystem::Instance().AddEmitter(node);
            i += 5;
            continue;
        }
        // ZoneInfo — unified: "ZoneInfo:type:minX:minY:minZ:maxX:maxY:maxZ:intensity:"
        // legacy fallback: "ZoneInfo:X:Y:Z:S:Rotation:W:H:L:TypeName"
        else if (Instruction.substr(0, 8) == L"ZoneInfo")
        {
            ZoneVolumeNode node;
            wstring firstField = WSplitValue(WData, i + 1);
            // Type embedded in the instruction token ("ZoneInfoLadder:...") = legacy format
            bool legacy = Instruction.size() > 8;
            if (!legacy)
            {
                try
                {
                    size_t parsed = 0;
                    std::stof(firstField, &parsed);
                    legacy = parsed == firstField.size(); // fully numeric -> legacy client format
                }
                catch (...) { legacy = false; }
            }

            if (!legacy)
            {
                // Unified parser format
                string zoneTypeName(firstField.begin(), firstField.end());
                float minX = ToFloat(WSplitValue(WData, i + 2));
                float minY = ToFloat(WSplitValue(WData, i + 3));
                float minZ = ToFloat(WSplitValue(WData, i + 4));
                float maxX = ToFloat(WSplitValue(WData, i + 5));
                float maxY = ToFloat(WSplitValue(WData, i + 6));
                float maxZ = ToFloat(WSplitValue(WData, i + 7));
                float intensity = ToFloat(WSplitValue(WData, i + 8));
                if (intensity <= 0.0f) intensity = 1.0f;
                ZoneType zt = ZoneType::ZONE_WATER;
                if (zoneTypeName == "ladder" || zoneTypeName == "Ladder" || zoneTypeName == "1")
                    zt = ZoneType::ZONE_LADDER;
                else if (zoneTypeName == "sky" || zoneTypeName == "Sky" || zoneTypeName == "2")
                    zt = ZoneType::ZONE_SKY;
                else if (zoneTypeName == "reverb" || zoneTypeName == "Reverb" || zoneTypeName == "3")
                    zt = ZoneType::ZONE_REVERB;
                else if (zoneTypeName == "sound" || zoneTypeName == "Sound" || zoneTypeName == "4")
                    zt = ZoneType::ZONE_GAMEPLAY_SOUND;
                node.bounds = {{minX, minY, minZ}, {maxX, maxY, maxZ}};
                node.zoneType = zt;
                node.intensity = intensity;
                PawnSystem::Instance().AddZone(node);
                i += 8;
            }
            else
            {
                // Legacy client format
                float x = ToFloat(WSplitValue(WData, i + 1));
                float y = ToFloat(WSplitValue(WData, i + 2));
                float z = ToFloat(WSplitValue(WData, i + 3));
                float w = ToFloat(WSplitValue(WData, i + 6));
                float h = ToFloat(WSplitValue(WData, i + 7));
                float l = ToFloat(WSplitValue(WData, i + 8));
                string zoneTypeName;
                if (Instruction.size() > 8)
                    zoneTypeName = string(Instruction.begin() + 8, Instruction.end());
                else
                    zoneTypeName = string(WSplitValue(WData, i + 9).begin(), WSplitValue(WData, i + 9).end());
                ZoneType zt = ZoneType::ZONE_WATER;
                if (zoneTypeName == "Ladder")
                    zt = ZoneType::ZONE_LADDER;
                else if (zoneTypeName == "Sky")
                    zt = ZoneType::ZONE_SKY;
                else if (zoneTypeName == "Reverb")
                    zt = ZoneType::ZONE_REVERB;
                node.bounds = {{x, y, z}, {w, h, l}};
                node.zoneType = zt;
                PawnSystem::Instance().AddZone(node);
                i += 9;
            }
            continue;
        }
        // Portal: "Portal:targetWorld:minX:minY:minZ:maxX:maxY:maxZ:[spawnX:spawnY:spawnZ:[bidir]]"
        else if (Instruction.substr(0, 6) == L"Portal")
        {
            wstring wtgt = WSplitValue(WData, i + 1);
            string targetWorld(wtgt.begin(), wtgt.end());
            float minX = ToFloat(WSplitValue(WData, i + 2));
            float minY = ToFloat(WSplitValue(WData, i + 3));
            float minZ = ToFloat(WSplitValue(WData, i + 4));
            float maxX = ToFloat(WSplitValue(WData, i + 5));
            float maxY = ToFloat(WSplitValue(WData, i + 6));
            float maxZ = ToFloat(WSplitValue(WData, i + 7));
            ZonePortal portal;
            portal.bounds = {{minX, minY, minZ}, {maxX, maxY, maxZ}};
            portal.targetWorld = targetWorld;
            // Optional spawn point (defaults to volume center floor)
            wstring sx = WSplitValue(WData, i + 8);
            if (!sx.empty())
            {
                portal.targetSpawn = {ToFloat(sx),
                                      ToFloat(WSplitValue(WData, i + 9)),
                                      ToFloat(WSplitValue(WData, i + 10))};
                wstring bidir = WSplitValue(WData, i + 11);
                if (!bidir.empty())
                    portal.bidirectional = ToFloat(bidir) != 0.0f;
            }
            else
            {
                portal.targetSpawn = {minX + (maxX - minX) * 0.5f, minY, minZ + (maxZ - minZ) * 0.5f};
            }
            PawnSystem::Instance().AddPortal(portal);
            i += 11;
            continue;
        }
        // LevelInfo: "LevelInfo:gameType:maxPlayers:respawnTime:timeLimitEnabled:timeLimitMinutes:scoreLimit:friendlyFire:skyboxPath:"
        else if (Instruction.substr(0, 9) == L"LevelInfo")
        {
            LevelSettings& s = PawnSystem::Instance().GetWorldInfo().settings;
            s.gameType = (int)ToFloat(WSplitValue(WData, i + 1));
            s.maxPlayers = (int)ToFloat(WSplitValue(WData, i + 2));
            s.respawnTime = ToFloat(WSplitValue(WData, i + 3));
            s.timeLimitEnabled = ToFloat(WSplitValue(WData, i + 4)) != 0.0f;
            s.timeLimitMinutes = ToFloat(WSplitValue(WData, i + 5));
            s.scoreLimit = (int)ToFloat(WSplitValue(WData, i + 6));
            s.friendlyFire = ToFloat(WSplitValue(WData, i + 7)) != 0.0f;
            wstring wsky = WSplitValue(WData, i + 8);
            s.skyboxPath = string(wsky.begin(), wsky.end());
            i += 8;
            continue;
        }
        // Particles: "Particles:type:density:speed:r:g:b:windX:windZ:"
        else if (Instruction.substr(0, 9) == L"Particles")
        {
            LevelSettings& s = PawnSystem::Instance().GetWorldInfo().settings;
            s.particleType = (int)ToFloat(WSplitValue(WData, i + 1));
            s.particleDensity = ToFloat(WSplitValue(WData, i + 2));
            s.particleSpeed = ToFloat(WSplitValue(WData, i + 3));
            s.particleR = (int)ToFloat(WSplitValue(WData, i + 4));
            s.particleG = (int)ToFloat(WSplitValue(WData, i + 5));
            s.particleB = (int)ToFloat(WSplitValue(WData, i + 6));
            s.particleWindX = ToFloat(WSplitValue(WData, i + 7));
            s.particleWindZ = ToFloat(WSplitValue(WData, i + 8));
            i += 8;
            continue;
        }
    }
}

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
    g_prevSoundZone.clear();
    g_defaultWorldMusic = Music{0};
    if (g_ambienceHandle.frameCount > 0)
    {
        StopSound(g_ambienceHandle);
        UnloadSound(g_ambienceHandle);
    }
    g_ambienceHandle = {0};
    g_ambienceZoneName.clear();
    g_wasInReverb = false;
    g_activeEnvZone.clear();
    ScriptTimer = 0;

    {
        OmegaTechData.PanicCounter = 0;

        LightningEntityManager::Instance().SetPlayerHealth(100.0f);

        OmegaTechData.SkyboxEnabled = false;

        const char* worldAssetDir = g_world_dir_override[0] ? g_world_dir_override : g_world_to_load;

        for (int ne = 1; ne <= 3; ne++)
        {
            const char* path = TextFormat("GameData/Worlds/%s/NoiseEmitter/NE%d.mp3", worldAssetDir, ne);
            auto getStream = [&](Music& ms) {
                if (IsPathFile(path)) {
                    StopMusicStream(ms);
                    UnloadMusicStream(ms);
                    ms = LoadMusicStream(path);
                } else {
                    UnloadMusicStream(ms);
                }
            };
            if (ne == 1) getStream(OmegaTechSoundData.NESound1);
            else if (ne == 2) getStream(OmegaTechSoundData.NESound2);
            else getStream(OmegaTechSoundData.NESound3);
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Skybox.png", worldAssetDir)))
        {
            if (WDLModels.Skybox.id > 0)
                UnloadTexture(WDLModels.Skybox);
            WDLModels.Skybox = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Skybox.png", worldAssetDir));
            OmegaTechData.SkyboxEnabled = true;
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Scripts/Launch.ps", worldAssetDir)))
        {
            ParasiteScriptInit();
            LoadScript(TextFormat("GameData/Worlds/%s/Scripts/Launch.ps", worldAssetDir));
            for (int x = 0; x <= ParasiteScriptCoreData.ProgramSize; x++)
            {
                CycleInstruction();
                ParasiteScriptCoreData.LineCounter++;
            }
        }

        if (WDLModels.HeightMapImage.data)
        {
            UnloadImage(WDLModels.HeightMapImage);
            WDLModels.HeightMapImage = (Image){0};
        }
        WDLModels.HeightMapReady = false;

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/HeightMap.png", worldAssetDir)))
        {
            WDLModels.HeightMapTexture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/HeightMapTexture.png", worldAssetDir));
            WDLModels.HeightMapImage = LoadImage(TextFormat("GameData/Worlds/%s/Models/HeightMap.png", worldAssetDir));
            WDLModels.HeightMapReady = (WDLModels.HeightMapImage.data != nullptr);
            if (WDLModels.HeightMapReady)
            {
                ImageFormat(&WDLModels.HeightMapImage, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
                int X = PullConfigValue(TextFormat("GameData/Worlds/%s/Models/HeightMapConfig.conf", worldAssetDir), 0);
                int Y = PullConfigValue(TextFormat("GameData/Worlds/%s/Models/HeightMapConfig.conf", worldAssetDir), 1);
                int Z = PullConfigValue(TextFormat("GameData/Worlds/%s/Models/HeightMapConfig.conf", worldAssetDir), 2);
                WDLModels.HeightMapSize = (Vector3){(float)X, (float)Y, (float)Z};
                Mesh Mesh1 = GenMeshHeightmap(WDLModels.HeightMapImage, WDLModels.HeightMapSize);
                OZ_INFO("HeightMap: world=%d size=(%d,%d,%d) mesh=(v=%d t=%d) tex=%d img=%dx%d",
                        OmegaTechData.LevelIndex, X, Y, Z, Mesh1.vertexCount, Mesh1.triangleCount,
                        WDLModels.HeightMapTexture.id,
                        WDLModels.HeightMapImage.width, WDLModels.HeightMapImage.height);
                WDLModels.HeightMap = LoadModelFromMesh(Mesh1);
                if (WDLModels.HeightMap.materialCount > 0)
                {
                    WDLModels.HeightMap.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.HeightMapTexture;
                    WDLModels.HeightMap.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
                }
            }
        }
        else
        {
            OZ_INFO("HeightMap: world=%d not found (no heightmap)", OmegaTechData.LevelIndex);
        }

        for (int mid = 1; mid <= GameModels::MAX_WDL_MODELS; mid++)
        {
            char modelPath[256], texPath[256];
            snprintf(modelPath, sizeof(modelPath), "GameData/Worlds/%s/Models/Model%d.obj", worldAssetDir, mid);
            snprintf(texPath, sizeof(texPath), "GameData/Worlds/%s/Models/Model%dTexture.png", worldAssetDir, mid);
            if (IsPathFile(modelPath))
            {
                WDLModels.wdlModels[mid] = LoadModel(modelPath);
                WDLModels.wdlModelTextures[mid] = LoadTexture(texPath);
                if (WDLModels.wdlModels[mid].materialCount > 0)
                {
                    WDLModels.wdlModels[mid].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.wdlModelTextures[mid];
                    WDLModels.wdlModels[mid].materials[0].shader = OmegaTechData.Lights;
                }
            }
            else
            {
                if (WDLModels.wdlModels[mid].meshCount != 0)
                    UnloadModel(WDLModels.wdlModels[mid]);
                if (WDLModels.wdlModelTextures[mid].id != 0)
                    UnloadTexture(WDLModels.wdlModelTextures[mid]);
            }
        }

        bool isDirectWdl = (strstr(g_world_to_load, ".wdl") != nullptr);
        if (isDirectWdl)
        {
            WorldData = LoadFile(g_world_to_load);
            OtherWDLData = L"";
            CacheWDL();
        }
        else
        {
            WorldData = L"";
            WorldData = LoadFile(TextFormat("GameData/Worlds/%s/World.wdl", worldAssetDir));
            OtherWDLData = L"";
            CacheWDL();
        }

        // Clear all existing entities before loading new world
        PawnSystem::Instance().ClearLights();
        PawnSystem::Instance().ClearPlayerStarts();
        PawnSystem::Instance().ClearPickups();
        PawnSystem::Instance().ClearZones();
        PawnSystem::Instance().ClearPortals();
        PawnSystem::Instance().ClearEmitters();
        PawnSystem::Instance().DespawnAll();
        // Reset level metadata so stale settings never leak across worlds
        PawnSystem::Instance().GetWorldInfo() = WorldInfo{};
        ParticlesEnabled = false;
        CombatFX::Instance().ClearAll();
        if (g_skySideTex.id > 0) { UnloadTexture(g_skySideTex); g_skySideTex = {0}; }

        if (!isDirectWdl)
        {
            char ozonePath[512];
            snprintf(ozonePath, sizeof(ozonePath), "GameData/Worlds/%s/World.ozone", worldAssetDir);
            if (IsPathFile(ozonePath))
                OzoneLoader::Instance().LoadFile(ozonePath);
            else
                LoadEntitiesFromWDL();
        }
        else
        {
            LoadEntitiesFromWDL();
        }

        // Apply level metadata (LevelInfo/Particles) after entities are loaded
        {
            LevelSettings& s = PawnSystem::Instance().GetWorldInfo().settings;
            if (!s.skyboxPath.empty())
            {
                Texture2D newSky = LoadTextureWithFallback(s.skyboxPath.c_str());
                if (newSky.id > 0)
                {
                    if (WDLModels.Skybox.id > 0) UnloadTexture(WDLModels.Skybox);
                    WDLModels.Skybox = newSky;
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

        if (OmegaTechSoundData.MusicFound)
        {
            StopMusicStream(OmegaTechSoundData.BackgroundMusic);
            UnloadMusicStream(OmegaTechSoundData.BackgroundMusic);
        }

        OmegaTechSoundData.MusicFound = false;

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Music/Main.mp3", worldAssetDir)))
        {
            OmegaTechSoundData.BackgroundMusic = LoadMusicStream(TextFormat("GameData/Worlds/%s/Music/Main.mp3", worldAssetDir));
            OmegaTechSoundData.MusicFound = true;
            PlayMusicStream(OmegaTechSoundData.BackgroundMusic);
        }
        else if (IsPathFile("GameData/Global/Sounds/Ambience/Music_Atmo_1.wav"))
        {
            OmegaTechSoundData.BackgroundMusic = LoadMusicStream("GameData/Global/Sounds/Ambience/Music_Atmo_1.wav");
            OmegaTechSoundData.MusicFound = true;
            PlayMusicStream(OmegaTechSoundData.BackgroundMusic);
        }

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
    float dt = GetFrameTime();
    float cameraPos[3] = {OmegaTechData.MainCamera.position.x, OmegaTechData.MainCamera.position.y, OmegaTechData.MainCamera.position.z};

    // Light[0] = directional headlight (attached to camera)
    OmegaTechData.GameLights[0].position = OmegaTechData.MainCamera.position;
    OmegaTechData.GameLights[0].target = {OmegaTechData.MainCamera.target.x, OmegaTechData.MainCamera.target.y - 5, OmegaTechData.MainCamera.target.z};
    OmegaTechData.GameLights[0].enabled = true;
    OmegaTechData.GameLights[0].type = LIGHT_DIRECTIONAL;

    SetShaderValue(OmegaTechData.Lights, OmegaTechData.Lights.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);

    // Submit PawnSystem lights via LitLightning_Update
    auto &pawnLights = PawnSystem::Instance().GetLights();
    LitLightning_Update(pawnLights, OmegaTechData.Lights, OmegaTechData.MainCamera, dt);

    // Re-submit the headlight on top (index 0)
    UpdateLightValues(OmegaTechData.Lights, OmegaTechData.GameLights[0]);

    // Update uTime for GPU light animation
    static int uTimeLoc = GetShaderLocation(OmegaTechData.Lights, "uTime");
    float timeVal = (float)GetTime();
    SetShaderValue(OmegaTechData.Lights, uTimeLoc, &timeVal, SHADER_UNIFORM_FLOAT);
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

void OmegaTechInit()
{
    OZ_INFO("=== OmegaTech Engine starting ===");
    OZ_INFO("CWD: %s", fs::current_path().string().c_str());
    OZ_INFO("GameData/Worlds exists: %d", (int)fs::exists("GameData/Worlds"));
    OZ_INFO("System/Data/Zones exists: %d", (int)fs::exists("System/Data/Zones"));
    LoadLaunchConfig();
    ParasiteScriptTFlagWipe();

    GuiLoadStyleDark();

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
                std::string name, sp, sc;
                float speed = 1.5f, aggroRange = 6.0f, attackRange = 1.5f, damage = 10.0f;
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
    OZ_INFO("Shaders loaded (Pixel=%d, Line=%d, Sobel=%d, Toon=%d, Jitter=%d, Lights=%d)",
            OmegaTechData.PixelShader.id, OmegaTechData.LineShader.id,
            OmegaTechData.SobelShader.id, OmegaTechData.ToonShader.id,
            OmegaTechData.JitterShader.id, OmegaTechData.Lights.id);
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
    OmegaTechSoundData.CollisionSound = LoadSoundWithFallback("GameData/Global/Sounds/CollisionSound.mp3");
    OmegaTechSoundData.WalkingSound = LoadSoundWithFallback("GameData/Global/Sounds/WalkingSound.mp3");
    OmegaTechSoundData.ChasingSound = LoadSoundWithFallback("GameData/Global/Sounds/ChasingSound.mp3");
    OmegaTechSoundData.UIClick = LoadSoundWithFallback("GameData/Global/Title/Click.mp3");
    OmegaTechSoundData.Death = LoadSoundWithFallback("GameData/Global/Sounds/Hurt.mp3");

    OmegaTechTextSystem.TextNoise = LoadSoundWithFallback("GameData/Global/Sounds/TalkingNoise.mp3");

    OmegaTechData.GameLights[0] = CreateLight(LIGHT_DIRECTIONAL, {OmegaTechData.MainCamera.position.x, OmegaTechData.MainCamera.position.y, OmegaTechData.MainCamera.position.z}, Vector3Zero(), WHITE, OmegaTechData.Lights);

    Target = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());

    // Initialize sound system
    SoundLoader::Instance().RegisterDefaults();

    // Pre-cache weapon object models from entity definitions (Object1-5)
    {
        static const char* objNames[] = {"Object1", "Object2", "Object3", "Object4", "Object5"};
        for (int o = 0; o < 5; o++) {
            int rIdx = LightningEntityManager::Instance().PrecacheModelForDef(objNames[o]);
            if (rIdx >= 0) {
                Model* m = LightningEntityManager::Instance().GetModelByResourceIdx(rIdx);
                if (m && m->meshes) {
                    WDLModels.objectModels[o] = *m;
                    WDLModels.objectModelsLoaded[o] = true;
                }
            }
        }
    }

    PlayMusicStream(OmegaTechData.HomeScreenMusic);
}

void PlaySplashScreen()
{
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

    StopMusicStream(OmegaTechData.HomeScreenMusic);

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
        SetServerJoinIP = menu.GetJoinIP();
        SetServerJoinFlag = true;
    }

    if (menu.ShouldStartServer())
    {
        SetServerJoinIP = "127.0.0.1";
        SetServerJoinFlag = true;
        // Launch dedicated server as a subprocess
        int serverPort = 27015;
        std::string cmd = "start /B \"\" System\\AngelServ.exe --port " +
                          std::to_string(serverPort) + " --dir GameData";
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

void CacheWDL()
{
    wstring WData = WorldData;

    OtherWDLData = L"";

    CachedModelCounter = 0;
    CachedCollisionCounter = 0;

    bool NextCollision = false;

    for (int i = 0; i <= MaxCachedModels - 1; i++)
    {
        CachedModels[i].Init();
        CachedCollision[i].Init();
    }

    for (int i = 0; i <= GetWDLSize(WorldData, L""); i++)
    {

        if (CachedModelCounter == MaxCachedModels)
            break;

        wstring Instruction = WSplitValue(WData, i);

        if (WReadValue(Instruction, 0, 4) == L"Model" || WReadValue(Instruction, 0, 8) == L"HeightMap")
        {
            if (WReadValue(Instruction, 0, 8) != L"HeightMap")
            {
                CachedModels[CachedModelCounter].ModelId = int(ToFloat(WReadValue(Instruction, 5, 6)));
            }
            else
            {
                CachedModels[CachedModelCounter].ModelId = -1;
            }

            CachedModels[CachedModelCounter].X = ToFloat(WSplitValue(WData, i + 1));
            CachedModels[CachedModelCounter].Y = ToFloat(WSplitValue(WData, i + 2));
            CachedModels[CachedModelCounter].Z = ToFloat(WSplitValue(WData, i + 3));
            CachedModels[CachedModelCounter].S = ToFloat(WSplitValue(WData, i + 4));
            CachedModels[CachedModelCounter].R = ToFloat(WSplitValue(WData, i + 5));

            if (NextCollision)
            {
                CachedModels[CachedModelCounter].Collision = true;
                NextCollision = false;
            }

            CachedModelCounter++;
        }

        if (WReadValue(Instruction, 0, 8) == L"Collision")
        {
            CachedModels[CachedModelCounter].ModelId = -2;
            CachedModels[CachedModelCounter].X = ToFloat(WSplitValue(WData, i + 1));
            CachedModels[CachedModelCounter].Y = ToFloat(WSplitValue(WData, i + 2));
            CachedModels[CachedModelCounter].Z = ToFloat(WSplitValue(WData, i + 3));
            CachedModels[CachedModelCounter].S = ToFloat(WSplitValue(WData, i + 4));
            CachedModels[CachedModelCounter].R = ToFloat(WSplitValue(WData, i + 5));
        }

        if (WReadValue(Instruction, 0, 11) == L"AdvCollision")
        {
            CachedCollision[CachedCollisionCounter].X = ToFloat(WSplitValue(WData, i + 1));
            CachedCollision[CachedCollisionCounter].Y = ToFloat(WSplitValue(WData, i + 2));
            CachedCollision[CachedCollisionCounter].Z = ToFloat(WSplitValue(WData, i + 3));
            CachedCollision[CachedCollisionCounter].W = ToFloat(WSplitValue(WData, i + 6));
            CachedCollision[CachedCollisionCounter].H = ToFloat(WSplitValue(WData, i + 7));
            CachedCollision[CachedCollisionCounter].L = ToFloat(WSplitValue(WData, i + 8));
            CachedCollisionCounter++;
        }

        if (WReadValue(Instruction, 0, 5) == L"Object" || WReadValue(Instruction, 0, 5) == L"Script") // Dont Cache Dynamic Objs
        {
            OtherWDLData += WSplitValue(WData, i) + L":" + WSplitValue(WData, i + 1) + L":" + WSplitValue(WData, i + 2) + L":" + WSplitValue(WData, i + 3) + L":" + WSplitValue(WData, i + 4) + L":" + WSplitValue(WData, i + 5) + L":";
        }

        if (Instruction == L"C")
        {
            NextCollision = true;
        }
    }
}

void CWDLProcess()
{
    for (int i = 0; i <= CachedCollisionCounter; i++)
    {
        X = CachedCollision[i].X;
        Y = CachedCollision[i].Y;
        Z = CachedCollision[i].Z;
        W = CachedCollision[i].W;
        H = CachedCollision[i].H;
        L = CachedCollision[i].L;

        if (CachedModels[i].ModelId == -1 || (OmegaTechData.MainCamera.position.z - OmegaTechData.RenderRadius < Z && OmegaTechData.MainCamera.position.z + OmegaTechData.RenderRadius > Z))
        {
            if (CachedModels[i].ModelId == -1 || (OmegaTechData.MainCamera.position.x - OmegaTechData.RenderRadius < X && OmegaTechData.MainCamera.position.x + OmegaTechData.RenderRadius > X))
            {
                if (CheckCollisionBoxSphere((BoundingBox){(Vector3){X, Y, Z}, (Vector3){W, H, L}}, {OmegaTechData.MainCamera.position.x + g_playerMovement.Width / 2, OmegaTechData.MainCamera.position.y - g_playerMovement.Height / 2, OmegaTechData.MainCamera.position.z - g_playerMovement.Width / 2}, 1.0))
                {
                    ObjectCollision = true;
                    if (!IsSoundPlaying(OmegaTechSoundData.CollisionSound))
                    {
                        PlaySound(OmegaTechSoundData.CollisionSound);
                    }
                }
            }
        }
    }

    for (int i = 0; i <= CachedModelCounter; i++)
    {
        X = CachedModels[i].X;
        Y = CachedModels[i].Y;
        Z = CachedModels[i].Z;
        S = CachedModels[i].S;
        Rotation = CachedModels[i].R;

        if (CachedModels[i].ModelId == -1 || (OmegaTechData.MainCamera.position.z - OmegaTechData.RenderRadius < Z && OmegaTechData.MainCamera.position.z + OmegaTechData.RenderRadius > Z))
        {
            if (CachedModels[i].ModelId == -1 || (OmegaTechData.MainCamera.position.x - OmegaTechData.RenderRadius < X && OmegaTechData.MainCamera.position.x + OmegaTechData.RenderRadius > X))
            {

                int mid = CachedModels[i].ModelId;
                if (mid == -2)
                {
                    if (CheckCollisionBoxes(g_playerMovement.PlayerBounds, (BoundingBox){(Vector3){X, Y, Z}, (Vector3){X + S, Y + S, Z + S}}))
                    {
                        ObjectCollision = true;
                        if (!IsSoundPlaying(OmegaTechSoundData.CollisionSound))
                            PlaySound(OmegaTechSoundData.CollisionSound);
                    }
                }
                else if (mid == -1)
                {
                    DrawModelEx(WDLModels.HeightMap, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                }
                else if (mid >= 1 && mid <= GameModels::MAX_WDL_MODELS && WDLModels.wdlModels[mid].meshCount > 0)
                {
                    DrawModelEx(WDLModels.wdlModels[mid], {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                }
                if (CachedModels[i].Collision)
                {
                    BoundingBox ModelBox = {{(X - S), (Y - S), (Z - S)}, {(X + S), (Y + S), (Z + S)}};
                    if (CheckCollisionBoxes(g_playerMovement.PlayerBounds, ModelBox))
                    {
                        ObjectCollision = true;
                    }
                }
            }
        }
    }
}

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

// Sample heightmap at world XZ location, returns terrain-surface Y or -99999
float SampleHeightmapGroundY(float px, float pz)
{
    if (!WDLModels.HeightMapReady || WDLModels.HeightMapImage.data == 0)
        return -99999.0f;

    Vector3 o = WDLModels.HeightMapPosition;
    float scale = WDLModels.HeightMapScale;
    float sx = WDLModels.HeightMapSize.x * scale;
    float sz = WDLModels.HeightMapSize.z * scale;
    int iw = WDLModels.HeightMapImage.width;
    int ih = WDLModels.HeightMapImage.height;
    if (iw < 1 || ih < 1)
        return -99999.0f;

    float hx = (px - o.x) / sx;
    float hz = (pz - o.z) / sz;
    float fx = hx * (float)(iw - 1);
    float fz = hz * (float)(ih - 1);
    int ix = (int)fx;
    int iz = (int)fz;
    if (ix < 0 || ix >= iw - 1 || iz < 0 || iz >= ih - 1)
        return o.y;
    float tx = fx - ix;
    float tz = fz - iz;
    uint8_t *p = (uint8_t *)WDLModels.HeightMapImage.data;
    float h00 = p[iz * iw + ix] / 255.0f;
    float h10 = p[iz * iw + ix + 1] / 255.0f;
    float h01 = p[(iz + 1) * iw + ix] / 255.0f;
    float h11 = p[(iz + 1) * iw + ix + 1] / 255.0f;
    float ht = h00 * (1 - tx) * (1 - tz) + h10 * tx * (1 - tz) + h01 * (1 - tx) * tz + h11 * tx * tz;
    return o.y + ht * WDLModels.HeightMapSize.y * scale;
}

void WDLProcess()
{

    wstring WData = L"";
    int Size = 0;
    if (OmegaTechData.UseCachedRenderer)
    {
        WData = OtherWDLData + ExtraWDLInstructions;
        Size = GetWDLSize(OtherWDLData, ExtraWDLInstructions);
    }
    else
    {
        WData = WorldData + ExtraWDLInstructions;
        Size = GetWDLSize(WorldData, ExtraWDLInstructions);
    }

    bool Render = false;
    bool FoundPlatform = false;
    float PlatformHeight = 0.0f;

    for (int i = 0; i <= Size; i++)
    {
        wstring Instruction = WSplitValue(WData, i);

        if (Instruction == L"C")
        {
            NextCollision = true;
        }

        if (WReadValue(Instruction, 0, 4) == L"Model" || WReadValue(Instruction, 0, 1) == L"NE" || WReadValue(Instruction, 0, 6) == L"ClipBox" || WReadValue(Instruction, 0, 5) == L"Object" || WReadValue(Instruction, 0, 5) == L"Script" || WReadValue(Instruction, 0, 8) == L"HeightMap" || WReadValue(Instruction, 0, 8) == L"Collision" || WReadValue(Instruction, 0, 11) == L"AdvCollision" ||
            WReadValue(Instruction, 0, 5) == L"Spawn" ||
            WReadValue(Instruction, 0, 3) == L"NPC" || WReadValue(Instruction, 0, 5) == L"Light" ||
            WReadValue(Instruction, 0, 5) == L"Sound" || WReadValue(Instruction, 0, 5) == L"Music" ||
            WReadValue(Instruction, 0, 8) == L"ZoneInfo")
        {

            X = ToFloat(WSplitValue(WData, i + 1));
            Y = ToFloat(WSplitValue(WData, i + 2));
            Z = ToFloat(WSplitValue(WData, i + 3));
            S = ToFloat(WSplitValue(WData, i + 4));

            Rotation = ToFloat(WSplitValue(WData, i + 5));

            if (OmegaTechData.MainCamera.position.z - OmegaTechData.RenderRadius < Z && OmegaTechData.MainCamera.position.z + OmegaTechData.RenderRadius > Z)
            {
                if (OmegaTechData.MainCamera.position.x - OmegaTechData.RenderRadius < X && OmegaTechData.MainCamera.position.x + OmegaTechData.RenderRadius > X)
                {
                    Render = true;

                    if (Instruction == L"NE1")
                    {
                        if (!IsMusicStreamPlaying(OmegaTechSoundData.NESound1))
                            PlayMusicStream(OmegaTechSoundData.NESound1);
                    }
                    if (Instruction == L"NE2")
                    {
                        if (!IsMusicStreamPlaying(OmegaTechSoundData.NESound2))
                            PlayMusicStream(OmegaTechSoundData.NESound2);
                    }
                    if (Instruction == L"NE3")
                    {
                        if (!IsMusicStreamPlaying(OmegaTechSoundData.NESound3))
                            PlayMusicStream(OmegaTechSoundData.NESound3);
                    }
                }
            }
        }
        else
        {
            if (Instruction == L"NE1")
            {
                StopMusicStream(OmegaTechSoundData.NESound1);
            }
            if (Instruction == L"NE2")
            {
                StopMusicStream(OmegaTechSoundData.NESound2);
            }
            if (Instruction == L"NE3")
            {
                StopMusicStream(OmegaTechSoundData.NESound3);
            }
        }

        if (Render)
        {
            if (WReadValue(Instruction, 0, 4) == L"Model")
            {
                int Identifier = ToFloat(WReadValue(Instruction, 5, 6));
                if (Identifier >= 1 && Identifier <= GameModels::MAX_WDL_MODELS && WDLModels.wdlModels[Identifier].meshCount > 0)
                    DrawModelEx(WDLModels.wdlModels[Identifier], {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
            }

            if (NextCollision)
            {
                BoundingBox ModelBox = {{(X - S), (Y - S), (Z - S)}, {(X + S), (Y + S), (Z + S)}};
                if (CheckCollisionBoxes(g_playerMovement.PlayerBounds, ModelBox))
                {
                    ObjectCollision = true;
                }
                NextCollision = false;
            }

            int AudioValue = 0;

            if (Instruction == L"NE1")
            {
                AudioValue = FlipNumber(GetDistance(X, Z, OmegaTechData.MainCamera.position.x, OmegaTechData.MainCamera.position.z));
                if (AudioValue > 0 && AudioValue < 100)
                    SetMusicVolume(OmegaTechSoundData.NESound1, float(AudioValue) / 100.0f);
                else
                {
                    SetMusicVolume(OmegaTechSoundData.NESound1, 0);
                }
            }
            if (Instruction == L"NE2")
            {
                AudioValue = FlipNumber(GetDistance(X, Z, OmegaTechData.MainCamera.position.x, OmegaTechData.MainCamera.position.z));
                if (AudioValue > 0 && AudioValue < 100)
                    SetMusicVolume(OmegaTechSoundData.NESound2, float(AudioValue) / 100.0f);
                else
                {
                    SetMusicVolume(OmegaTechSoundData.NESound2, 0);
                }
            }
            if (Instruction == L"NE3")
            {
                AudioValue = FlipNumber(GetDistance(X, Z, OmegaTechData.MainCamera.position.x, OmegaTechData.MainCamera.position.z));
                if (AudioValue > 0 && AudioValue < 100)
                    SetMusicVolume(OmegaTechSoundData.NESound3, float(AudioValue) / 100.0f);
                else
                {
                    SetMusicVolume(OmegaTechSoundData.NESound3, 0);
                }
            }

            if (Instruction == L"Object1" && WDLModels.objectModelsLoaded[0])
                DrawModelEx(WDLModels.objectModels[0], {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
            if (Instruction == L"Object2" && WDLModels.objectModelsLoaded[1])
                DrawModelEx(WDLModels.objectModels[1], {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
            if (Instruction == L"Object3" && WDLModels.objectModelsLoaded[2])
                DrawModelEx(WDLModels.objectModels[2], {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
            if (Instruction == L"Object4" && WDLModels.objectModelsLoaded[3])
                DrawModelEx(WDLModels.objectModels[3], {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
            if (Instruction == L"Object5" && WDLModels.objectModelsLoaded[4])
                DrawModelEx(WDLModels.objectModels[4], {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);

            if (Instruction == L"Collision")
            { // Collision
                if (CheckCollisionBoxes(g_playerMovement.PlayerBounds, (BoundingBox){(Vector3){X, Y, Z}, (Vector3){X + S, Y + S, Z + S}}))
                {
                    ObjectCollision = true;
                }

                if (Debug)
                {
                    if (ObjectCollision)
                    {
                        DrawCubeWires({X, Y, Z}, S, S, S, GREEN);
                    }
                    else
                    {
                        DrawCubeWires({X, Y, Z}, S, S, S, RED);
                    }
                }
                if (ObjectCollision)
                {
                    if (!IsSoundPlaying(OmegaTechSoundData.CollisionSound))
                    {
                        PlaySound(OmegaTechSoundData.CollisionSound);
                    }
                }
            }

            if (WReadValue(Instruction, 0, 5) == L"Script")
            {
                if (CheckCollisionBoxes(g_playerMovement.PlayerBounds, (BoundingBox){(Vector3){X, Y, Z}, (Vector3){X + S, Y + S, Z + S}}))
                {
                    ObjectCollision = true;
                    if (ScriptTimer == 0)
                    {
                        ParasiteScriptInit();
                        LoadScript(TextFormat("GameData/Worlds/%s/Scripts/Script%i.ps", g_world_dir_override[0] ? g_world_dir_override : g_world_to_load, int(ToFloat(WReadValue(Instruction, 6, Instruction.size() - 1)))));

                        for (int x = 0; x <= ParasiteScriptCoreData.ProgramSize; x++)
                        {
                            CycleInstruction();
                            ParasiteScriptCoreData.LineCounter++;
                        }

                        ScriptTimer = 180;
                    }
                }

                if (Debug)
                {
                    if (ObjectCollision)
                    {
                        DrawCubeWires({X, Y, Z}, S, S, S, GREEN);
                    }
                    else
                    {
                        DrawCubeWires({X, Y, Z}, S, S, S, YELLOW);
                    }
                }
            }
        }
        if (Instruction == L"ClipBox")
        {

            W = ToFloat(WSplitValue(WData, i + 6));
            H = ToFloat(WSplitValue(WData, i + 7));
            L = ToFloat(WSplitValue(WData, i + 8));

            if (CheckCollisionBoxSphere(
                    (BoundingBox){(Vector3){X, Y, Z}, (Vector3){W, H, L}},
                    {OmegaTechData.MainCamera.position.x + g_playerMovement.Width / 2,
                     OmegaTechData.MainCamera.position.y - g_playerMovement.Height / 2,
                     OmegaTechData.MainCamera.position.z - g_playerMovement.Width / 2},
                    1.0))
            {
                PlatformHeight = H;
                FoundPlatform = true;
            }

            if (Debug)
                DrawBoundingBox((BoundingBox){(Vector3){X, Y, Z}, (Vector3){W, H - 5, L}}, PURPLE);

            i += 3;
        }
        if (Instruction == L"AdvCollision")
        { // Collision

            if (Render)
            {
                W = ToFloat(WSplitValue(WData, i + 6));
                H = ToFloat(WSplitValue(WData, i + 7));
                L = ToFloat(WSplitValue(WData, i + 8));

                if (CheckCollisionBoxSphere(
                        (BoundingBox){(Vector3){X, Y, Z}, (Vector3){W, H, L}},
                        {OmegaTechData.MainCamera.position.x + g_playerMovement.Width / 2,
                         OmegaTechData.MainCamera.position.y - g_playerMovement.Height / 2,
                         OmegaTechData.MainCamera.position.z - g_playerMovement.Width / 2},
                        1.0))
                    ObjectCollision = true;

                if (Debug)
                {
                    if (ObjectCollision)
                    {
                        DrawBoundingBox((BoundingBox){(Vector3){X, Y, Z}, (Vector3){W, H, L}}, GREEN);
                    }
                    else
                    {
                        DrawBoundingBox((BoundingBox){(Vector3){X, Y, Z}, (Vector3){W, H, L}}, PURPLE);
                    }
                }

                if (ObjectCollision)
                {
                    if (!IsSoundPlaying(OmegaTechSoundData.CollisionSound))
                    {
                        PlaySound(OmegaTechSoundData.CollisionSound);
                    }
                }
            }

            i += 3;
        }

        if (Instruction == L"HeightMap")
        {
            WDLModels.HeightMapPosition.x = X;
            WDLModels.HeightMapPosition.y = Y;
            WDLModels.HeightMapPosition.z = Z;
            WDLModels.HeightMapScale = S;
            DrawModelEx(WDLModels.HeightMap, {X, Y, Z}, {0, 1, 0}, 0, {S, S, S}, WHITE);
        }

        if (!NextCollision)
        {
            i += 5;
        }

        Render = false;
    }

    // Stand on heightmap terrain (preferred) or ClipBox platforms
    // Skipped when flying or noclipping Ã¢â‚¬â€ player controls Y manually
    if (!g_playerMovement.isFlying && !g_playerMovement.isNoClip)
    {
        float groundY = SampleHeightmapGroundY(
            OmegaTechData.MainCamera.position.x,
            OmegaTechData.MainCamera.position.z);
        if (groundY > -50000.0f)
        {

            // Only snap if at or below ground (allows jumping above terrain)
            if (OmegaTechData.MainCamera.position.y <= groundY + PLAYER_EYE_HEIGHT + 0.1f)
            {
                OmegaTechData.MainCamera.position.y = groundY + PLAYER_EYE_HEIGHT;
                g_playerMovement.velocityY = 0.0f;
                g_playerMovement.onGround = true;
            }
            else
            {
                g_playerMovement.onGround = false;
            }
        }
        else if (FoundPlatform)
        {

            if (OmegaTechData.MainCamera.position.y <= PlatformHeight + PLAYER_EYE_HEIGHT + 0.1f)
            {
                OmegaTechData.MainCamera.position.y = PlatformHeight + PLAYER_EYE_HEIGHT;
                g_playerMovement.velocityY = 0.0f;
                g_playerMovement.onGround = true;
            }
            else
            {
                g_playerMovement.onGround = false;
            }
        }
        else
        {
            g_playerMovement.onGround = false;
        }
    }
}

void UpdateEntities()
{
    Vector3 playerPos = OmegaTechData.MainCamera.position;
    float dt = GetFrameTime();

    // Single-pass zone scan for player â€” replaces 4 separate CheckZoneCollision calls
    PawnSystem::Instance().UpdatePlayerRegion(playerPos, g_playerMovement.PlayerBounds);

    // Portal trigger — level-to-level transitions (campaign system)
    if (g_portalCooldown > 0.0f)
        g_portalCooldown -= dt;
    else
    {
        ZonePortal* portal = PawnSystem::Instance().CheckPortalCollision(playerPos, g_playerMovement.PlayerBounds);
        if (portal && !SetSceneFlag)
        {
            OZ_INFO("Portal: entering level '%s'", portal->targetWorld.c_str());
            strncpy(g_world_to_load, portal->targetWorld.c_str(), sizeof(g_world_to_load) - 1);
            g_world_to_load[sizeof(g_world_to_load) - 1] = '\0';
            g_portalSpawnPos = portal->targetSpawn;
            g_portalTransitionPending = true;
            SetSceneFlag = true;      // LoadWorld runs at end of frame
            g_portalCooldown = 3.0f;  // latch so the trigger can't re-fire mid-transition
        }
    }

    // Update all pawns via PawnSystem (FSM: IDLE/PATROL/CHASE/RETURN)
    PawnSystem::Instance().Update(playerPos, dt);

    // Update pickups (respawn timers, player collision)
    PawnSystem::Instance().UpdatePickups(dt, playerPos, g_playerMovement.PlayerBounds);

    // Pickup collection feedback (console message + flash)
    {
        auto& fb = PawnSystem::Instance().m_pickupFeedback;
        if (fb.collected) {
            OmegaTechTextSystem.Write(TextFormat("Collected: %s", fb.typeName.c_str()));
            fb.collected = false;
        }
    }

    // Draw all pawns (with lit shader for fog/lighting)
    PawnSystem::Instance().DrawAll(OmegaTechData.MainCamera, OmegaTechData.Lights);

    // Draw entity billboards (player starts, pickups, zones) with lit shader
    PawnSystem::Instance().DrawEntities(OmegaTechData.MainCamera, OmegaTechData.Lights);

    // Check if any pawn is attacking the player (contact damage)
    {
        g_damageCooldown -= dt;
        float damage = 0;
        if (g_damageCooldown <= 0.0f && PawnSystem::Instance().IsPlayerAttacked(playerPos, damage))
        {
            LightningEntityManager::Instance().SetPlayerHealth(std::max(0.0f, LightningEntityManager::Instance().GetPlayerHealth() - damage));
            g_damageCooldown = 1.0f;
            if (OmegaTechData.PanicCounter != 240)
                OmegaTechData.PanicCounter += 2;
            if (OmegaTechData.Ticker % 2 == 0)
            {
                if (!IsSoundPlaying(OmegaTechSoundData.ChasingSound))
                    PlaySound(OmegaTechSoundData.ChasingSound);
            }
        }
    }

    // Death / respawn check
    if (LightningEntityManager::Instance().GetPlayerHealth() <= 0.0f)
    {
        LightningEntityManager::Instance().SetPlayerHealth(100.0f);
        LightningEntityManager::Instance().SetPlayerMana(100.0f);
        OmegaTechData.Deaths++;
        OZ_INFO("Player died! Death #%d", OmegaTechData.Deaths);
        OmegaTechTextSystem.Write(TextFormat("You died! Death #%d", OmegaTechData.Deaths));
        PawnSystem::Instance().RespawnPlayerAtStart(OmegaTechData.MainCamera);
        // Refill items from save
        LoadSave();
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
        if (!IsSoundPlaying(OmegaTechSoundData.WalkingSound))
        {
            PlaySound(OmegaTechSoundData.WalkingSound);
        }
    }
    else
    {
        if (IsSoundPlaying(OmegaTechSoundData.WalkingSound))
        {
            StopSound(OmegaTechSoundData.WalkingSound);
        }
    }

    g_playerMovement.UpdateBounds(OmegaTechData.MainCamera);
}

void UpdateNoiseEmitters()
{
    UpdateMusicStream(OmegaTechSoundData.NESound1);
    UpdateMusicStream(OmegaTechSoundData.NESound2);
    UpdateMusicStream(OmegaTechSoundData.NESound3);
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

    wofstream Outfile2;
    Outfile2.open("GameData/Saves/Script.sav");
    Outfile2 << ExtraWDLInstructions;
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

    ExtraWDLInstructions = LoadFile("GameData/Saves/Script.sav");
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
        if (inSkyZone) {
            SkyZoneNode* sky = PawnSystem::Instance().GetActiveSkyZone();
            if (sky && sky->skyboxTex.id > 0)
                capTex = sky->skyboxTex;
            else if (WDLModels.Skybox.id > 0)
                capTex = WDLModels.Skybox;
        } else if (OmegaTechData.SkyboxEnabled && WDLModels.Skybox.id > 0) {
            capTex = WDLModels.Skybox;
        }
        Texture2D sideTex = g_skySideTex;

        if (capTex.id > 0 || sideTex.id > 0) {
            Vector3 camPos = OmegaTechData.MainCamera.position;
            rlDisableDepthMask();
            rlDisableBackfaceCulling();

            // Top face (index 0) — at y=+1000, normal -Y (faces down)
            {
                Color fallback = {80, 120, 200, 255};
                rlPushMatrix();
                rlTranslatef(camPos.x, camPos.y + 1000.0f, camPos.z);
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
                rlTranslatef(camPos.x, camPos.y - 1000.0f, camPos.z);
                if (capTex.id > 0) {
                    OmegaTechData.SkyboxFace[1].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = capTex;
                    DrawModel(OmegaTechData.SkyboxFace[1], {0,0,0}, 1.0f, WHITE);
                } else {
                    DrawModel(OmegaTechData.SkyboxFace[1], {0,0,0}, 1.0f, fallback);
                }
                rlPopMatrix();
            }
            // +X face (index 2) — at x=+1000, normal -X (faces west)
            {
                Color fallback = {120, 180, 240, 255};
                rlPushMatrix();
                rlTranslatef(camPos.x + 1000.0f, camPos.y, camPos.z);
                rlRotatef(90.0f, 0.0f, 1.0f, 0.0f);
                if (sideTex.id > 0) {
                    OmegaTechData.SkyboxFace[2].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = sideTex;
                    DrawModel(OmegaTechData.SkyboxFace[2], {0,0,0}, 1.0f, WHITE);
                } else {
                    DrawModel(OmegaTechData.SkyboxFace[2], {0,0,0}, 1.0f, fallback);
                }
                rlPopMatrix();
            }
            // -X face (index 3) — at x=-1000, normal +X (faces east)
            {
                Color fallback = {120, 180, 240, 255};
                rlPushMatrix();
                rlTranslatef(camPos.x - 1000.0f, camPos.y, camPos.z);
                rlRotatef(-90.0f, 0.0f, 1.0f, 0.0f);
                if (sideTex.id > 0) {
                    OmegaTechData.SkyboxFace[3].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = sideTex;
                    DrawModel(OmegaTechData.SkyboxFace[3], {0,0,0}, 1.0f, WHITE);
                } else {
                    DrawModel(OmegaTechData.SkyboxFace[3], {0,0,0}, 1.0f, fallback);
                }
                rlPopMatrix();
            }
            // +Z face (index 4) — at z=+1000, normal -Z (faces south)
            {
                Color fallback = {120, 180, 240, 255};
                rlPushMatrix();
                rlTranslatef(camPos.x, camPos.y, camPos.z + 1000.0f);
                rlRotatef(180.0f, 0.0f, 1.0f, 0.0f);
                if (sideTex.id > 0) {
                    OmegaTechData.SkyboxFace[4].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = sideTex;
                    DrawModel(OmegaTechData.SkyboxFace[4], {0,0,0}, 1.0f, WHITE);
                } else {
                    DrawModel(OmegaTechData.SkyboxFace[4], {0,0,0}, 1.0f, fallback);
                }
                rlPopMatrix();
            }
            // -Z face (index 5) — at z=-1000, normal +Z (faces north)
            {
                Color fallback = {120, 180, 240, 255};
                rlPushMatrix();
                rlTranslatef(camPos.x, camPos.y, camPos.z - 1000.0f);
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

    // GameplaySoundZone â€” trigger zone-specific music/sound profiles
    {
        auto& region = PawnSystem::Instance().GetPlayerRegion();
        // Find the primary gameplay sound zone from the player region
        ZoneVolumeNode* soundZone = nullptr;
        if (region.primaryZoneId >= 0) {
            soundZone = PawnSystem::Instance().GetZone(region.primaryZoneId);
            if (soundZone && soundZone->zoneType != ZoneType::ZONE_GAMEPLAY_SOUND)
                soundZone = nullptr;
        }
        if (soundZone && soundZone->zoneType == ZoneType::ZONE_GAMEPLAY_SOUND)
        {
            auto &sp = soundZone->soundProfile;
            if (!sp.music_on_enter.empty() && g_prevSoundZone != soundZone->name)
            {
                // Save default world music before crossfading
                if (g_prevSoundZone.empty() && OmegaTechSoundData.MusicFound)
                    g_defaultWorldMusic = OmegaTechSoundData.BackgroundMusic;
                StopMusicStream(OmegaTechSoundData.BackgroundMusic);
                Music newMusic = LoadMusicWithFallback(sp.music_on_enter.c_str());
                if (newMusic.ctxData != nullptr)
                {
                    OmegaTechSoundData.BackgroundMusic = newMusic;
                    OmegaTechSoundData.MusicFound = true;
                    PlayMusicStream(OmegaTechSoundData.BackgroundMusic);
                }
            }
            if (!sp.ambience_loop.empty())
            {
                if (g_ambienceZoneName != soundZone->name)
                {
                    if (g_ambienceHandle.frameCount > 0)
                    {
                        StopSound(g_ambienceHandle);
                        UnloadSound(g_ambienceHandle);
                        g_ambienceHandle = {0};
                    }
                    g_ambienceHandle = LoadSoundWithFallback(sp.ambience_loop.c_str());
                    if (g_ambienceHandle.frameCount > 0)
                        PlaySound(g_ambienceHandle);
                    g_ambienceZoneName = soundZone->name;
                }
                if (g_ambienceHandle.frameCount > 0 && !IsSoundPlaying(g_ambienceHandle))
                    PlaySound(g_ambienceHandle);
            }
            if (!sp.sfx_on_enter.empty() && g_prevSoundZone != soundZone->name)
            {
                Sound sfx = LoadSoundWithFallback(sp.sfx_on_enter.c_str());
                if (sfx.frameCount > 0)
                    PlaySound(sfx);
            }
            g_prevSoundZone = soundZone->name;
        }
        else if (region.primaryZoneId < 0 && !g_prevSoundZone.empty())
        {
            // Exited sound zone â€” stop ambience loop, restore default music
            if (g_ambienceHandle.frameCount > 0)
            {
                StopSound(g_ambienceHandle);
                UnloadSound(g_ambienceHandle);
                g_ambienceHandle = {0};
            }
            g_ambienceZoneName.clear();
            if (g_defaultWorldMusic.ctxData != nullptr)
            {
                StopMusicStream(OmegaTechSoundData.BackgroundMusic);
                OmegaTechSoundData.BackgroundMusic = g_defaultWorldMusic;
                OmegaTechSoundData.MusicFound = true;
                PlayMusicStream(OmegaTechSoundData.BackgroundMusic);
                g_defaultWorldMusic = Music{0};
            }
            g_prevSoundZone.clear();
        }
    }

    if (OmegaTechSoundData.MusicFound)
    {
        UpdateMusicStream(OmegaTechSoundData.BackgroundMusic);
    }

    UpdateNoiseEmitters();

    if (!OmegaTechData.UseCachedRenderer)
    {
        WDLProcess();
    }
    else
    {
        CWDLProcess();
        WDLProcess();
    }

    OzoneLoader::Instance().DrawWorldGeometry(OmegaTechData.MainCamera);

    // OZONE brush collision - chunk-accelerated query
    {
        Vector3 cp = OmegaTechData.MainCamera.position;
        float playerFeet = cp.y - PLAYER_EYE_HEIGHT;
        auto &chunkMgr = OzoneLoader::Instance().GetChunkManager();
        std::vector<int> nearIndices;
        chunkMgr.GetVolumesNear(cp.x, cp.z, nearIndices);
        auto &vols = OzoneLoader::Instance().GetCollisionVolumes();
        for (int idx : nearIndices)
        {
            if (idx >= 0 && idx < (int)vols.size() &&
                CheckCollisionBoxes(g_playerMovement.PlayerBounds, vols[idx].aabb))
            {
                // Skip volumes whose top is at or below the player's feet —
                // these are floors/surfaces the player stands on, not obstacles.
                if (vols[idx].aabb.max.y <= playerFeet + 0.1f)
                    continue;
                // The heightmap is the ground: support comes from the ground
                // clamp below. Its tall AABB would otherwise freeze the player
                // mid-air above the terrain (restore cancels gravity).
                if (vols[idx].isHeightmap)
                    continue;
                ObjectCollision = true;
                break;
            }
        }
    }

    // OZONE ground clamp Ã¢â‚¬â€ OZONE heightmap first, then brush primitives
    // (only when WDL heightmap and ClipBox didn't already provide ground)
    if (!g_playerMovement.isFlying && !g_playerMovement.isNoClip)
    {
        Vector3 cp = OmegaTechData.MainCamera.position;

        // Check OZONE-loaded heightmap
        auto &ozLoader = OzoneLoader::Instance();
        float hmY = ozLoader.HasHeightmap()
                        ? ozLoader.SampleHeightmapY(cp.x, cp.z)
                        : -99999.0f;
        // Only treat the heightmap as ground when its surface is at or below
        // the player's feet — underground areas (tunnels) must fall through to
        // brush-top support instead of being teleported to the surface.
        if (hmY > -50000.0f && hmY <= cp.y + 0.1f)
        {

            if (cp.y <= hmY + PLAYER_EYE_HEIGHT + 0.1f)
            {
                OmegaTechData.MainCamera.position.y = hmY + PLAYER_EYE_HEIGHT;
                g_playerMovement.velocityY = 0.0f;
                g_playerMovement.onGround = true;
            }
            else
            {
                g_playerMovement.onGround = false;
            }
        }
        else
        {
            // Fallback: stand on top of OZONE brush primitives (chunk-accelerated)
            float brushTop = -99999.0f;
            auto &chunkMgr = ozLoader.GetChunkManager();
            std::vector<int> nearIndices;
            chunkMgr.GetVolumesNear(cp.x, cp.z, nearIndices);
            auto &vols = ozLoader.GetCollisionVolumes();
            for (int idx : nearIndices)
            {
                if (idx < 0 || idx >= (int)vols.size())
                    continue;
                auto &vol = vols[idx];
                if (cp.x >= vol.aabb.min.x && cp.x <= vol.aabb.max.x &&
                    cp.z >= vol.aabb.min.z && cp.z <= vol.aabb.max.z)
                {
                    float top = vol.aabb.max.y;
                    if (top > brushTop && top <= cp.y + 0.1f)
                        brushTop = top;
                }
            }
            if (brushTop > -50000.0f)
            {

                if (cp.y <= brushTop + PLAYER_EYE_HEIGHT + 0.1f)
                {
                    OmegaTechData.MainCamera.position.y = brushTop + PLAYER_EYE_HEIGHT;
                    g_playerMovement.velocityY = 0.0f;
                    g_playerMovement.onGround = true;
                }
                else
                {
                    g_playerMovement.onGround = false;
                }
            }
            else
            {
                g_playerMovement.onGround = false;
            }
        }
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
        for (auto &zone : PawnSystem::Instance().GetZones())
            DrawBoundingBox(zone.bounds, BLUE);

        // Player start markers
        for (auto &ps : PawnSystem::Instance().GetPlayerStarts())
            DrawCubeWires(ps.position, 0.5f, 1.0f, 0.5f, GREEN);
    }

    // Combat FX — projectile rendering, impacts, decals, muzzle flash and
    // remote players all live here so they render inside the 3D camera pass.
    {
        CombatFX::Instance().Update(GetFrameTime());
        PawnSystem::Instance().DrawProjectiles(OmegaTechData.MainCamera);
        CombatFX::Instance().Draw3D(OmegaTechData.MainCamera);
        DrawRemotePlayers3D();
    }

    if (Debug)
    {
        DrawLights();
    }
    else
    {
        UpdateEntities();
        // Wall/terrain impact sweep, then update projectiles (age, movement, gravity)
        float dt = GetFrameTime();
        SweepProjectilesVsWorld(dt);
        PawnSystem::Instance().UpdateProjectiles(dt);
    }
    if (ObjectCollision)
    {
        if (!g_playerMovement.isNoClip)
        {
            OmegaTechData.MainCamera.position.x = g_playerMovement.OldX;
            OmegaTechData.MainCamera.position.y = g_playerMovement.OldY;
            OmegaTechData.MainCamera.position.z = g_playerMovement.OldZ;
        }
        ObjectCollision = false;
    }

    // Zone reverb â€” apply simulated DSP (volume/muffle) while inside reverb zone
    {
        auto& region = PawnSystem::Instance().GetPlayerRegion();
        bool inReverb = region.HasZoneType(ZoneType::ZONE_REVERB);
        // Get reverb params from combined env (or highest-priority reverb zone)
        float mix = region.combinedEnv.reverbMix;
        float decay = region.combinedEnv.reverbDecay;
        if (inReverb && !g_wasInReverb)
        {
            if (mix <= 0.0f) mix = 0.35f;
            if (decay <= 0.0f) decay = 0.5f;
            float vol = 1.0f - mix * 0.5f;
            OZ_INFO("ZONE_REVERB entered â€” mix=%.2f decay=%.2f vol=%.2f", mix, decay, vol);
            if (OmegaTechSoundData.MusicFound)
            {
                SetMusicVolume(OmegaTechSoundData.BackgroundMusic, vol);
            }
            DspReverb::SetMix(mix);
            DspReverb::SetDecay(decay);
        }
        else if (!inReverb && g_wasInReverb)
        {
            OZ_INFO("ZONE_REVERB exited â€” restoring audio");
            if (OmegaTechSoundData.MusicFound)
            {
                SetMusicVolume(OmegaTechSoundData.BackgroundMusic, 1.0f);
            }
            DspReverb::SetMix(0.0f);
            DspReverb::SetDecay(0.5f);
        }
        g_wasInReverb = inReverb;
    }

    // LightningScript entity tick
    {
        float dt = GetFrameTime();
        LightningEntityManager::Instance().Update(dt);

        // Sync pending script effects into PawnSystem's active SkyZoneNode
        PawnSystem::Instance().SyncSkyboxState();

        // Apply pending Fog changes from script contexts
        auto &lem = LightningEntityManager::Instance();
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

        // Apply pending Skybox changes from script contexts
        // Loads into active SkyZoneNode's skyboxTex (or WDLModels.Skybox fallback)
        {
            SkyZoneNode* sky = PawnSystem::Instance().GetActiveSkyZone();
            std::string path;
            if (sky && !sky->skyboxPath.empty()) {
                path = sky->skyboxPath;
                sky->skyboxPath.clear();
            } else if (lem.HasPendingSkybox()) {
                path = lem.PendingSkybox();
                lem.ClearPendingSkybox();
            }
            if (!path.empty()) {
                OZ_INFO("LightningScript: loading skybox '%s'", path.c_str());
                Texture2D newSky = LoadTextureWithFallback(path.c_str());
                if (newSky.id > 0) {
                    // Load into zone's dedicated texture slot
                    if (sky) {
                        if (sky->skyboxTex.id > 0)
                            UnloadTexture(sky->skyboxTex);
                        sky->skyboxTex = newSky;
                    }
                    // Also set global fallback so 2D path works if leaving the zone
                    if (WDLModels.Skybox.id > 0)
                        UnloadTexture(WDLModels.Skybox);
                    WDLModels.Skybox = newSky;
                    OmegaTechData.SkyboxEnabled = true;
                } else {
                    OZ_WARN("LightningScript: skybox '%s' not found", path.c_str());
                }
            }
        }

        // Apply pending Ambient changes from script contexts
        if (lem.HasPendingAmbient())
        {
            float amb[4] = {lem.PendingAmbientR(), lem.PendingAmbientG(), lem.PendingAmbientB(), 1.0f};
            static int ambientLoc = GetShaderLocation(OmegaTechData.Lights, "ambient");
            SetShaderValue(OmegaTechData.Lights, ambientLoc, amb, SHADER_UNIFORM_VEC4);
            lem.ClearPendingAmbient();
        }

        // Zone environment override application (from combined player region)
        {
            auto& region = PawnSystem::Instance().GetPlayerRegion();
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
            if (OmegaTechData.PanicCounter != 240)
                OmegaTechData.PanicCounter += 2;
        }
    }

    UpdateCustom();

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
            // Never arrive under the terrain: lift the camera to the highest
            // ground surface (WDL or OZONE heightmap) at this XZ when the raw
            // spawn sits below grade.
            float groundY = fmaxf(SampleHeightmapGroundY(g_portalSpawnPos.x, g_portalSpawnPos.z),
                                  OzoneLoader::Instance().HasHeightmap()
                                      ? OzoneLoader::Instance().SampleHeightmapY(g_portalSpawnPos.x, g_portalSpawnPos.z)
                                      : -99999.0f);
            if (groundY > -50000.0f &&
                OmegaTechData.MainCamera.position.y < groundY + PLAYER_EYE_HEIGHT)
                OmegaTechData.MainCamera.position.y = groundY + PLAYER_EYE_HEIGHT;
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
