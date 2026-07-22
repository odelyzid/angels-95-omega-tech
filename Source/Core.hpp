#include "Data.hpp"
#include "Log.hpp"
#include "Package/OzAssetMapper.hpp"
#include "Audio/OzSoundLoader.hpp"
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

bool FloorCollision = true;
bool ObjectCollision = false;
extern bool g_showCollisionDebug;

const char* g_world_to_load = "EngineTest";

// Set from PlayHomeScreen to request a server join
bool SetServerJoinFlag = false;
const char* SetServerJoinIP = nullptr;

void LoadSave();
void SaveGame();
void UpdateCustom();
void CacheWDL();
float SampleHeightmapGroundY(float px, float pz);
void PutLight(Vector3 Position);
void ClearLights();

#include "ParticleDemon/ParticleDemon.hpp"

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
        ray_video_t HomeScreenVideo;
        Music HomeScreenMusic;

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

static EngineData OmegaTechData;

void SpawnWDLProcess(const char *Path)
{
    wstring WData;

    if (GameDataEncoded)
    {
        WData = Encode(LoadFile(Path), MainKey);
    }
    else
    {
        WData = LoadFile(Path);
    }

    int WDLSize = 0;

    for (int i = 0; i <= WData.size(); i++)
    {
        if (WData[i] == L':')
        {
            WDLSize++;
        }
    }

    for (int i = 0; i <= WDLSize; i++)
    {
        wstring Instruction = WSplitValue(WData, i);

        if (WReadValue(Instruction, 0, 5) == L"Walker")
        {
            float x = ToFloat(WSplitValue(WData, i + 1));
            float y = ToFloat(WSplitValue(WData, i + 2));
            float z = ToFloat(WSplitValue(WData, i + 3));
            
            // Spawn via PawnSystem (dynamic, unlimited NPCs)
            PawnSystem::Instance().Spawn({x, y, z}, "Walker");
        }

        if (Instruction == L"Light")
        {
            PutLight({ToFloat(WSplitValue(WData, i + 1)) , ToFloat(WSplitValue(WData, i + 2)) , ToFloat(WSplitValue(WData, i + 3))});
        }

        i += 3;
    }
}

void LoadEntitiesFromWDL()
{
    wstring WData = WorldData;
    int Size = GetWDLSize(WorldData, L"");

    for (int i = 0; i <= Size; i++)
    {
        wstring Instruction = WSplitValue(WData, i);

        if (Instruction.empty() || Instruction[0] == L'#')
            continue;

        // Pickup: "Pickup:X:Y:Z:S:Rotation:TypeName"
        if (Instruction.substr(0, 6) == L"Pickup")
        {
            float x = ToFloat(WSplitValue(WData, i + 1));
            float y = ToFloat(WSplitValue(WData, i + 2));
            float z = ToFloat(WSplitValue(WData, i + 3));
            wstring typeName = WSplitValue(WData, i + 6);
            PickupNode node;
            node.position = {x, y, z};
            node.typeName = string(typeName.begin(), typeName.end());
            PawnSystem::Instance().AddPickup(node);
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
        }
        // Light: "Light:X:Y:Z"
        else if (Instruction == L"Light")
        {
            float x = ToFloat(WSplitValue(WData, i + 1));
            float y = ToFloat(WSplitValue(WData, i + 2));
            float z = ToFloat(WSplitValue(WData, i + 3));
            PutLight({x, y, z});
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
        }
        // ZoneInfo: "ZoneInfo:X:Y:Z:S:Rotation:W:H:L:TypeName"
        else if (Instruction.substr(0, 8) == L"ZoneInfo")
        {
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
            if (zoneTypeName == "Ladder") zt = ZoneType::ZONE_LADDER;
            else if (zoneTypeName == "Sky") zt = ZoneType::ZONE_SKY;
            else if (zoneTypeName == "Reverb") zt = ZoneType::ZONE_REVERB;
            ZoneVolumeNode node;
            node.bounds = {{x, y, z}, {w, h, l}};
            node.zoneType = zt;
            PawnSystem::Instance().AddZone(node);
        }
    }
}

bool LoadFlag = false;

auto LoadWorld()
{
    OZ_INFO("LoadWorld: loading world %d", OmegaTechData.LevelIndex);
    PlayFade();
    ClearLights();

    // Reset movement state so player falls to new world's collision
    OmegaPlayer.onGround = false;
    OmegaPlayer.velocityY = 0.0f;

    if (OmegaTechData.Deaths != 3)
    {

        OmegaTechData.PanicCounter = 0;

        OmegaPlayer.Health = 100;

        OmegaTechData.SkyboxEnabled = false;

        if (IsPathFile(TextFormat("GameData/Worlds/%s/NoiseEmitter/NE1.mp3", g_world_to_load)))
        {
            StopMusicStream(OmegaTechSoundData.NESound1);
            OmegaTechSoundData.NESound1 = LoadMusicStream(TextFormat("GameData/Worlds/%s/NoiseEmitter/NE1.mp3", g_world_to_load));
        }
        else {
            UnloadMusicStream(OmegaTechSoundData.NESound1);
        }
        if (IsPathFile(TextFormat("GameData/Worlds/%s/NoiseEmitter/NE2.mp3", g_world_to_load)))
        {
            StopMusicStream(OmegaTechSoundData.NESound2);
            OmegaTechSoundData.NESound2 = LoadMusicStream(TextFormat("GameData/Worlds/%s/NoiseEmitter/NE2.mp3", g_world_to_load));
        }
        else {
            UnloadMusicStream(OmegaTechSoundData.NESound2);
        }
        if (IsPathFile(TextFormat("GameData/Worlds/%s/NoiseEmitter/NE3.mp3", g_world_to_load)))
        {
            StopMusicStream(OmegaTechSoundData.NESound3);
            OmegaTechSoundData.NESound3 = LoadMusicStream(TextFormat("GameData/Worlds/%s/NoiseEmitter/NE3.mp3", g_world_to_load));
        }
        else {
            UnloadMusicStream(OmegaTechSoundData.NESound3);
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Skybox.png", g_world_to_load)))
        {

            WDLModels.Skybox = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Skybox.png", g_world_to_load));
            OmegaTechData.SkyboxEnabled = true;
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Scripts/Launch.ps", g_world_to_load)))
        {
            ParasiteScriptInit();
            LoadScript(TextFormat("GameData/Worlds/%s/Scripts/Launch.ps", g_world_to_load));
            for (int x = 0; x <= ParasiteScriptCoreData.ProgramSize; x++)
            {
                CycleInstruction();
                ParasiteScriptCoreData.LineCounter++;
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Entities/Walker/Frame1.png", g_world_to_load)))
        {
            EnemyTextures.Frame1 = LoadTexture(TextFormat("GameData/Worlds/%s/Entities/Walker/Frame1.png", g_world_to_load));
            EnemyTextures.Scream = LoadSound(TextFormat("GameData/Worlds/%s/Entities/Walker/Scream.mp3", g_world_to_load));
            SpawnWDLProcess(TextFormat("GameData/Worlds/%s/Entities/Entities.wdl", g_world_to_load));
        }

        if (WDLModels.HeightMapImage.data) {
            UnloadImage(WDLModels.HeightMapImage);
            WDLModels.HeightMapImage = (Image){0};
        }
        WDLModels.HeightMapReady = false;

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/HeightMap.png", g_world_to_load)))
        {
            WDLModels.HeightMapTexture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/HeightMapTexture.png", g_world_to_load));
            WDLModels.HeightMapImage = LoadImage(TextFormat("GameData/Worlds/%s/Models/HeightMap.png", g_world_to_load));
            ImageFormat(&WDLModels.HeightMapImage, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
            int X = PullConfigValue(TextFormat("GameData/Worlds/%s/Models/HeightMapConfig.conf", g_world_to_load), 0);
            int Y = PullConfigValue(TextFormat("GameData/Worlds/%s/Models/HeightMapConfig.conf", g_world_to_load), 1);
            int Z = PullConfigValue(TextFormat("GameData/Worlds/%s/Models/HeightMapConfig.conf", g_world_to_load), 2);
            WDLModels.HeightMapSize = (Vector3){(float)X, (float)Y, (float)Z};
            Mesh Mesh1 = GenMeshHeightmap(WDLModels.HeightMapImage, WDLModels.HeightMapSize);
            OZ_INFO("HeightMap: world=%d size=(%d,%d,%d) mesh=(v=%d t=%d) tex=%d img=%dx%d",
                    OmegaTechData.LevelIndex, X, Y, Z, Mesh1.vertexCount, Mesh1.triangleCount,
                    WDLModels.HeightMapTexture.id,
                    WDLModels.HeightMapImage.width, WDLModels.HeightMapImage.height);
            WDLModels.HeightMap = LoadModelFromMesh(Mesh1);
            WDLModels.HeightMap.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.HeightMapTexture;
            WDLModels.HeightMap.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
            WDLModels.HeightMapReady = (WDLModels.HeightMapImage.data != nullptr);
        } else {
            OZ_INFO("HeightMap: world=%d not found (no heightmap)", OmegaTechData.LevelIndex);
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model1.obj", g_world_to_load)))
        {
            WDLModels.Model1 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model1.obj", g_world_to_load));
            WDLModels.Model1Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model1Texture.png", g_world_to_load));
            WDLModels.Model1.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model1Texture;
            WDLModels.Model1.materials[0].shader = OmegaTechData.Lights;
        }
        else {
            if (WDLModels.Model1.meshCount != 0){
                UnloadModel(WDLModels.Model1);
            }
            if (WDLModels.Model1Texture.id != 0){
                UnloadTexture(WDLModels.Model1Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model2.obj", g_world_to_load)))
        {
            WDLModels.Model2 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model2.obj", g_world_to_load));
            WDLModels.Model2Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model2Texture.png", g_world_to_load));
            WDLModels.Model2.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model2Texture;
            WDLModels.Model2.materials[0].shader = OmegaTechData.Lights;
        }
        else {
            if (WDLModels.Model2.meshCount != 0){
                UnloadModel(WDLModels.Model2);
            }
            if (WDLModels.Model2Texture.id != 0){
                UnloadTexture(WDLModels.Model2Texture);
            }
        }
        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model3.obj", g_world_to_load)))
        {
            WDLModels.Model3 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model3.obj", g_world_to_load));
            WDLModels.Model3Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model3Texture.png", g_world_to_load));
            WDLModels.Model3.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model3Texture;
            WDLModels.Model3.materials[0].shader = OmegaTechData.Lights;
        }
        else {
            if (WDLModels.Model3.meshCount != 0){
                UnloadModel(WDLModels.Model3);
            }
            if (WDLModels.Model3Texture.id != 0){
                UnloadTexture(WDLModels.Model3Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model4.obj", g_world_to_load)))
        {
            WDLModels.Model4 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model4.obj", g_world_to_load));
            WDLModels.Model4Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model4Texture.png", g_world_to_load));
            WDLModels.Model4.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model4Texture;
            WDLModels.Model4.materials[0].shader = OmegaTechData.Lights;
        }
        else {
            if (WDLModels.Model4.meshCount != 0){
                UnloadModel(WDLModels.Model4);
            }
            if (WDLModels.Model4Texture.id != 0){
                UnloadTexture(WDLModels.Model4Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model5.obj", g_world_to_load)))
        {
            WDLModels.Model5 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model5.obj", g_world_to_load));
            WDLModels.Model5Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model5Texture.png", g_world_to_load));
            WDLModels.Model5.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model5Texture;
            WDLModels.Model5.materials[0].shader = OmegaTechData.Lights;
        }
        else {
            if (WDLModels.Model5.meshCount != 0){
                UnloadModel(WDLModels.Model5);
            }
            if (WDLModels.Model5Texture.id != 0){
                UnloadTexture(WDLModels.Model5Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model6.obj", g_world_to_load)))
        {
            WDLModels.Model6 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model6.obj", g_world_to_load));
            WDLModels.Model6Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model6Texture.png", g_world_to_load));
            WDLModels.Model6.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model6Texture;
            WDLModels.Model6.materials[0].shader = OmegaTechData.Lights;
        }
        else {
            if (WDLModels.Model6.meshCount != 0){
                UnloadModel(WDLModels.Model6);
            }
            if (WDLModels.Model6Texture.id != 0){
                UnloadTexture(WDLModels.Model6Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model7.obj", g_world_to_load)))
        {
            WDLModels.Model7 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model7.obj", g_world_to_load));
            WDLModels.Model7Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model7Texture.png", g_world_to_load));
            WDLModels.Model7.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model7Texture;
            WDLModels.Model7.materials[0].shader = OmegaTechData.Lights;

        }
        else {
            if (WDLModels.Model7.meshCount != 0){
                UnloadModel(WDLModels.Model7);
            }
            if (WDLModels.Model7Texture.id != 0){
                UnloadTexture(WDLModels.Model7Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model8.obj", g_world_to_load)))
        {
            WDLModels.Model8 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model8.obj", g_world_to_load));
            WDLModels.Model8Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model8Texture.png", g_world_to_load));
            WDLModels.Model8.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model8Texture;
            WDLModels.Model8.materials[0].shader = OmegaTechData.Lights;

        }
        else {
            if (WDLModels.Model8.meshCount != 0){
                UnloadModel(WDLModels.Model8);
            }
            if (WDLModels.Model8Texture.id != 0){
                UnloadTexture(WDLModels.Model8Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model9.obj", g_world_to_load)))
        {
            WDLModels.Model9 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model9.obj", g_world_to_load));
            WDLModels.Model9Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model9Texture.png", g_world_to_load));
            WDLModels.Model9.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model9Texture;
            WDLModels.Model9.materials[0].shader = OmegaTechData.Lights;

        }
        else {
            if (WDLModels.Model9.meshCount != 0){
                UnloadModel(WDLModels.Model9);
            }
            if (WDLModels.Model9Texture.id != 0){
                UnloadTexture(WDLModels.Model9Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model10.obj", g_world_to_load)))
        {
            WDLModels.Model10 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model10.obj", g_world_to_load));
            WDLModels.Model10Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model10Texture.png", g_world_to_load));
            WDLModels.Model10.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model10Texture;
            WDLModels.Model10.materials[0].shader = OmegaTechData.Lights;

        }
        else {
            if (WDLModels.Model10.meshCount != 0){
                UnloadModel(WDLModels.Model10);
            }
            if (WDLModels.Model10Texture.id != 0){
                UnloadTexture(WDLModels.Model10Texture);
            }
        }
        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model11.obj", g_world_to_load)))
        {
            WDLModels.Model11 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model11.obj", g_world_to_load));
            WDLModels.Model11Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model11Texture.png", g_world_to_load));
            WDLModels.Model11.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model11Texture;
            WDLModels.Model11.materials[0].shader = OmegaTechData.Lights;

        }
        else {
            if (WDLModels.Model11.meshCount != 0){
                UnloadModel(WDLModels.Model11);
            }
            if (WDLModels.Model11Texture.id != 0){
                UnloadTexture(WDLModels.Model11Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model12.obj", g_world_to_load)))
        {
            WDLModels.Model12 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model12.obj", g_world_to_load));
            WDLModels.Model12Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model12Texture.png", g_world_to_load));
            WDLModels.Model12.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model12Texture;
            WDLModels.Model12.materials[0].shader = OmegaTechData.Lights;

        }
        else {
            if (WDLModels.Model12.meshCount != 0){
                UnloadModel(WDLModels.Model12);
            }
            if (WDLModels.Model12Texture.id != 0){
                UnloadTexture(WDLModels.Model12Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model13.obj", g_world_to_load)))
        {
            WDLModels.Model13 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model13.obj", g_world_to_load));
            WDLModels.Model13Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model13Texture.png", g_world_to_load));
            WDLModels.Model13.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model13Texture;
            WDLModels.Model13.materials[0].shader = OmegaTechData.Lights;

        }
        else {
            if (WDLModels.Model13.meshCount != 0){
                UnloadModel(WDLModels.Model13);
            }
            if (WDLModels.Model13Texture.id != 0){
                UnloadTexture(WDLModels.Model13Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model14.obj", g_world_to_load)))
        {
            WDLModels.Model14 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model14.obj", g_world_to_load));
            WDLModels.Model14Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model14Texture.png", g_world_to_load));
            WDLModels.Model14.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model14Texture;
            WDLModels.Model14.materials[0].shader = OmegaTechData.Lights;

        }
        else {
            if (WDLModels.Model14.meshCount != 0){
                UnloadModel(WDLModels.Model14);
            }
            if (WDLModels.Model14Texture.id != 0){
                UnloadTexture(WDLModels.Model14Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model15.obj", g_world_to_load)))
        {
            WDLModels.Model15 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model15.obj", g_world_to_load));
            WDLModels.Model15Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model15Texture.png", g_world_to_load));
            WDLModels.Model15.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model15Texture;
            WDLModels.Model15.materials[0].shader = OmegaTechData.Lights;

        }
        else {
            if (WDLModels.Model15.meshCount != 0){
                UnloadModel(WDLModels.Model15);
            }
            if (WDLModels.Model15Texture.id != 0){
                UnloadTexture(WDLModels.Model15Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model16.obj", g_world_to_load)))
        {
            WDLModels.Model16 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model16.obj", g_world_to_load));
            WDLModels.Model16Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model16Texture.png", g_world_to_load));
            WDLModels.Model16.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model16Texture;
            WDLModels.Model16.materials[0].shader = OmegaTechData.Lights;

        }
        else {
            if (WDLModels.Model16.meshCount != 0){
                UnloadModel(WDLModels.Model16);
            }
            if (WDLModels.Model16Texture.id != 0){
                UnloadTexture(WDLModels.Model16Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model17.obj", g_world_to_load)))
        {
            WDLModels.Model17 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model17.obj", g_world_to_load));
            WDLModels.Model17Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model17Texture.png", g_world_to_load));
            WDLModels.Model17.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model17Texture;
            WDLModels.Model17.materials[0].shader = OmegaTechData.Lights;

        }
        else {
            if (WDLModels.Model17.meshCount != 0){
                UnloadModel(WDLModels.Model17);
            }
            if (WDLModels.Model17Texture.id != 0){
                UnloadTexture(WDLModels.Model17Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model18.obj", g_world_to_load)))
        {
            WDLModels.Model18 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model18.obj", g_world_to_load));
            WDLModels.Model18Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model18Texture.png", g_world_to_load));
            WDLModels.Model18.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model18Texture;
            WDLModels.Model18.materials[0].shader = OmegaTechData.Lights;

        }
        else {
            if (WDLModels.Model18.meshCount != 0){
                UnloadModel(WDLModels.Model18);
            }
            if (WDLModels.Model18Texture.id != 0){
                UnloadTexture(WDLModels.Model18Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model19.obj", g_world_to_load)))
        {
            WDLModels.Model19 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model19.obj", g_world_to_load));
            WDLModels.Model19Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model19Texture.png", g_world_to_load));
            WDLModels.Model19.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model19Texture;
            WDLModels.Model19.materials[0].shader = OmegaTechData.Lights;

        }
        else {
            if (WDLModels.Model19.meshCount != 0){
                UnloadModel(WDLModels.Model19);
            }
            if (WDLModels.Model19Texture.id != 0){
                UnloadTexture(WDLModels.Model19Texture);
            }
        }

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Models/Model20.obj", g_world_to_load)))
        {
            WDLModels.Model20 = LoadModel(TextFormat("GameData/Worlds/%s/Models/Model20.obj", g_world_to_load));
            WDLModels.Model20Texture = LoadTexture(TextFormat("GameData/Worlds/%s/Models/Model20Texture.png", g_world_to_load));
            WDLModels.Model20.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model20Texture;
            WDLModels.Model20.materials[0].shader = OmegaTechData.Lights;

        }
        else {
            if (WDLModels.Model20.meshCount != 0){
                UnloadModel(WDLModels.Model20);
            }
            if (WDLModels.Model20Texture.id != 0){
                UnloadTexture(WDLModels.Model20Texture);
            }
        }

        if (GameDataEncoded)
        {
            WorldData = Encode(LoadFile(TextFormat("GameData/Worlds/%s/World.wdl", g_world_to_load)), MainKey);
        }
        else
        {
            WorldData = L"";
            WorldData = LoadFile(TextFormat("GameData/Worlds/%s/World.wdl", g_world_to_load));
            OtherWDLData = L"";
            CacheWDL();
        }

        char ozonePath[512];
        snprintf(ozonePath, sizeof(ozonePath), "GameData/Worlds/%s/World.ozone", g_world_to_load);
        if (IsPathFile(ozonePath))
            OzoneLoader::Instance().LoadFile(ozonePath);

        // Clear existing entities before loading new world
        PawnSystem::Instance().ClearPlayerStarts();
        PawnSystem::Instance().ClearPickups();
        PawnSystem::Instance().ClearZones();
        PawnSystem::Instance().ClearEmitters();
        PawnSystem::Instance().DespawnAll();
        LoadEntitiesFromWDL();

        if (OmegaTechSoundData.MusicFound)StopMusicStream(OmegaTechSoundData.BackgroundMusic);

        OmegaTechSoundData.MusicFound = false;

        if (IsPathFile(TextFormat("GameData/Worlds/%s/Music/Main.mp3", g_world_to_load)))
        {
            OmegaTechSoundData.BackgroundMusic = LoadMusicStream(TextFormat("GameData/Worlds/%s/Music/Main.mp3", g_world_to_load));
            OmegaTechSoundData.MusicFound = true;
            PlayMusicStream(OmegaTechSoundData.BackgroundMusic);
        }
        else if (IsPathFile("GameData/Global/Sounds/Ambience/Music_Atmo_1.wav"))
        {
            OmegaTechSoundData.BackgroundMusic = LoadMusicStream("GameData/Global/Sounds/Ambience/Music_Atmo_1.wav");
            OmegaTechSoundData.MusicFound = true;
            PlayMusicStream(OmegaTechSoundData.BackgroundMusic);
        }

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

int LightCounter = 1;

void UpdateLightSources(){
    float cameraPos[3] = { OmegaTechData.MainCamera.position.x, OmegaTechData.MainCamera.position.y, OmegaTechData.MainCamera.position.z };
    OmegaTechData.GameLights[0].position = { OmegaTechData.MainCamera.position.x, OmegaTechData.MainCamera.position.y, OmegaTechData.MainCamera.position.z };
    OmegaTechData.GameLights[0].target = { OmegaTechData.MainCamera.target.x, OmegaTechData.MainCamera.target.y - 5, OmegaTechData.MainCamera.target.z };
    SetShaderValue(OmegaTechData.Lights, OmegaTechData.Lights.locs[SHADER_LOC_VECTOR_VIEW], cameraPos, SHADER_UNIFORM_VEC3);
    for (int i = 0; i < MAX_LIGHTS; i++) UpdateLightValues(OmegaTechData.Lights, OmegaTechData.GameLights[i]);
}

void ClearLights(){
    LightCounter = 1;
    for (int i = 0; i < MAX_LIGHTS; i++) OmegaTechData.GameLights[i] = { 0 };
}

void PutLight(Vector3 Position){
    OmegaTechData.GameLights[LightCounter] = CreateLight(LIGHT_POINT, Position, Vector3Zero(), RED, OmegaTechData.Lights);
    LightCounter ++;
}

void DrawLights(){
    for (int i = 1; i < MAX_LIGHTS; i++)
    {
        if (OmegaTechData.GameLights[i].enabled) DrawSphereEx(OmegaTechData.GameLights[i].position, 0.2f, 8, 8, OmegaTechData.GameLights[i].color);
        else DrawSphereWires(OmegaTechData.GameLights[i].position, 0.2f, 8, 8, ColorAlpha(OmegaTechData.GameLights[i].color, 0.3f));
    }
}

void OmegaTechInit()
{
    OZ_INFO("=== OmegaTech Engine starting ===");
    LoadLaunchConfig();
    ParasiteScriptTFlagWipe();

    GuiLoadStyleDark();

    // Initialize package-based asset loading
    PackageAssetLoader::Instance().Init();

    // Initialize LightningScript entity system
    LightningEntityRegistry::Instance().Init();
    LightningEntityManager::Instance().Init();

    // Initialize engine billboard system
    EngineBillboard::Init();

    // Register pawn definitions for NPC system
    PawnSystem::Instance().RegisterDef({"Walker", 1.5f, 6.0f, 1.5f, 10.0f, 100});
    PawnSystem::Instance().RegisterDef({"Skaarj", 2.0f, 8.0f, 2.0f, 15.0f, 150});
    PawnSystem::Instance().RegisterDef({"Brute", 1.0f, 4.0f, 1.5f, 30.0f, 250});
    PawnSystem::Instance().RegisterDef({"Floater", 1.2f, 8.0f, 3.0f, 15.0f, 80});

    OmegaTechData.InitCamera();

    OmegaTechData.PixelShader = LoadShaderWithFallback(0, "GameData/Shaders/Pixel.fs");
    // Legacy FogShader disabled - using LitFog material shader instead
    OmegaTechData.FogShader = {0};
    OmegaTechData.LineShader = LoadShaderWithFallback(0, "GameData/Shaders/Scanlines.fs");
    OmegaTechData.SobelShader = LoadShaderWithFallback(0, "GameData/Shaders/Sobel.fs");
    OmegaTechData.ToonShader = LoadShaderWithFallback(0, "GameData/Shaders/Toon.fs");
    OmegaTechData.JitterShader = LoadShaderWithFallback(0, "GameData/Shaders/Jitter.fs");
    OmegaTechData.Lights = LoadShaderWithFallback("GameData/Shaders/Lights/Lighting.vs","GameData/Shaders/Lights/LitFog.fs");
    OZ_INFO("Shaders loaded (Pixel=%d, Line=%d, Sobel=%d, Toon=%d, Jitter=%d, Lights=%d)",
            OmegaTechData.PixelShader.id, OmegaTechData.LineShader.id,
            OmegaTechData.SobelShader.id, OmegaTechData.ToonShader.id,
            OmegaTechData.JitterShader.id, OmegaTechData.Lights.id);
    OzoneLoader::Instance().SetLitFogShader(OmegaTechData.Lights);
    
    // Initialize all lights to disabled
    for (int i = 0; i < MAX_LIGHTS; i++) {
        OmegaTechData.GameLights[i] = { 0 };
    }


    OmegaTechData.Lights.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(OmegaTechData.Lights, "viewPos");
    int AmbientLoc = GetShaderLocation(OmegaTechData.Lights, "ambient");
    float ambient[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
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


    OmegaTechData.HomeScreen = LoadTextureWithFallback("GameData/Global/Title/Title.png");
    if (IsPathFile("GameData/Global/Title/Title.mpg"))OmegaTechData.HomeScreenVideo = ray_video_open("GameData/Global/Title/Title.mpg");
    OmegaTechData.HomeScreenMusic = LoadMusicWithFallback("GameData/Global/Title/Title.mp3");

    OmegaTechTextSystem.Bar = LoadTextureWithFallback("GameData/Global/TextBar.png");
    OmegaTechTextSystem.BarFont = LoadFontWithFallback("GameData/Global/Font.ttf");
    OmegaTechSoundData.CollisionSound = LoadSoundWithFallback("GameData/Global/Sounds/CollisionSound.mp3");
    OmegaTechSoundData.WalkingSound = LoadSoundWithFallback("GameData/Global/Sounds/WalkingSound.mp3");
    OmegaTechSoundData.ChasingSound = LoadSoundWithFallback("GameData/Global/Sounds/ChasingSound.mp3");
    OmegaTechSoundData.UIClick = LoadSoundWithFallback("GameData/Global/Title/Click.mp3");
    OmegaTechSoundData.Death = LoadSoundWithFallback("GameData/Global/Sounds/Hurt.mp3");

    OmegaTechTextSystem.TextNoise = LoadSoundWithFallback("GameData/Global/Sounds/TalkingNoise.mp3");

    {
        Model m = LoadModelWithFallback("GameData/Global/FModels/FModel1.gltf");
        if (m.meshes != nullptr) {
            WDLModels.FastModel1 = m;
            WDLModels.FastModel1Texture = LoadTextureWithFallback("GameData/Global/FModels/FModel1Texture.png");
            WDLModels.FastModel1.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.FastModel1Texture;
            WDLModels.FastModel1.materials[0].shader = OmegaTechData.Lights;
        }
    }
    {
        Model m = LoadModelWithFallback("GameData/Global/FModels/FModel2.gltf");
        if (m.meshes != nullptr) {
            WDLModels.FastModel2 = m;
            WDLModels.FastModel2Texture = LoadTextureWithFallback("GameData/Global/FModels/FModel2Texture.png");
            WDLModels.FastModel2.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.FastModel2Texture;
            WDLModels.FastModel2.materials[0].shader = OmegaTechData.Lights;
        }
    }
    {
        Model m = LoadModelWithFallback("GameData/Global/FModels/FModel3.gltf");
        if (m.meshes != nullptr) {
            WDLModels.FastModel3 = m;
            WDLModels.FastModel3Texture = LoadTextureWithFallback("GameData/Global/FModels/FModel3Texture.png");
            WDLModels.FastModel3.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.FastModel3Texture;
            WDLModels.FastModel3.materials[0].shader = OmegaTechData.Lights;
        }
    }
    {
        Model m = LoadModelWithFallback("GameData/Global/FModels/FModel4.gltf");
        if (m.meshes != nullptr) {
            WDLModels.FastModel4 = m;
            WDLModels.FastModel4Texture = LoadTextureWithFallback("GameData/Global/FModels/FModel4Texture.png");
            WDLModels.FastModel4.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.FastModel4Texture;
            WDLModels.FastModel4.materials[0].shader = OmegaTechData.Lights;
        }
    }
    {
        Model m = LoadModelWithFallback("GameData/Global/FModels/FModel5.gltf");
        if (m.meshes != nullptr) {
            WDLModels.FastModel5 = m;
            WDLModels.FastModel5Texture = LoadTextureWithFallback("GameData/Global/FModels/FModel5Texture.png");
            WDLModels.FastModel5.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.FastModel5Texture;
            WDLModels.FastModel5.materials[0].shader = OmegaTechData.Lights;
        }
    }

    OmegaTechData.GameLights[0] = CreateLight(LIGHT_DIRECTIONAL, { OmegaTechData.MainCamera.position.x, OmegaTechData.MainCamera.position.y, OmegaTechData.MainCamera.position.z }, Vector3Zero(), WHITE, OmegaTechData.Lights);

    Target = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());

    // Initialize oz_* subsystems â€” lazy init on first access
    AssetMapper::Instance().Init();
    SoundLoader::Instance().RegisterDefaults();

    InitObjects();

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

    if (splash.id > 0) UnloadTexture(splash);
}

void PlayHomeScreen()
{
    static char JoinIP[32] = "127.0.0.1";
    static bool ShowJoinInput = false;
    static bool StartServerMode = false;

    Rectangle LayoutRecs[6] = {
        (Rectangle){583, 320, 120, 24},
        (Rectangle){583, 355, 120, 24},
        (Rectangle){583, 390, 120, 24},
        (Rectangle){583, 425, 120, 24},
        (Rectangle){583, 460, 120, 24},
        (Rectangle){583, 495, 120, 24},
    };

    while (true && !WindowShouldClose())
    {
        BeginTextureMode(Target);
        UpdateMusicStream(OmegaTechData.HomeScreenMusic);

        ClearBackground(BLACK);

        if (IsPathFile("GameData/Global/Title/Title.mpg"))ray_video_update(&OmegaTechData.HomeScreenVideo, GetFrameTime());

        DrawTextureEx(OmegaTechData.HomeScreenVideo.texture, {0, 0}, 0, 5, WHITE);
        DrawTexture(OmegaTechData.HomeScreen, 0, 0, WHITE);

        if (OmegaInputController.InteractPressed)
        {
            PlaySound(OmegaTechSoundData.UIClick);
        }

        if (!ShowJoinInput && !StartServerMode) {
            if (GuiButton(LayoutRecs[0], "Start New Game"))
            {
                UnloadRenderTexture(Target);
                Target = LoadRenderTexture(GetScreenWidth() / 4, GetScreenHeight() / 4);
                break;
            }

            if (GuiButton(LayoutRecs[1], "Join Game"))
            {
                ShowJoinInput = true;
            }

            if (GuiButton(LayoutRecs[2], "Start Game"))
            {
                StartServerMode = true;
            }

            GuiLine(LayoutRecs[3], NULL);

            if (GuiButton(LayoutRecs[4], "Load Game"))
            {
                UnloadRenderTexture(Target);
                Target = LoadRenderTexture(320 , 240);

                if (IsPathFile("GameData/Saves/TF.sav"))
                {
                    LoadSave();
                    LoadFlag = true;
                }
                break;
            }

            if (GuiButton(LayoutRecs[5], "Settings"))
            {
                MenuSettings = !MenuSettings;
            }

            if (MenuSettings)
            {
                ShowMenuSetiings();
            }
        } else if (ShowJoinInput) {
            // Join Game: show IP input
            DrawText("Enter Server IP:", 560, 280, 20, WHITE);
            GuiTextBox((Rectangle){540, 310, 200, 30}, JoinIP, 32, true);

            if (GuiButton((Rectangle){540, 350, 90, 24}, "Connect")) {
                UnloadRenderTexture(Target);
                Target = LoadRenderTexture(GetScreenWidth() / 4, GetScreenHeight() / 4);
                SetServerJoinIP = JoinIP;
                SetServerJoinFlag = true;
                break;
            }
            if (GuiButton((Rectangle){640, 350, 90, 24}, "Cancel")) {
                ShowJoinInput = false;
            }
        } else if (StartServerMode) {
            DrawText("Starting Server...", 560, 280, 20, GREEN);
            DrawText("Connect to 127.0.0.1:27015", 560, 310, 16, LIGHTGRAY);

            if (GuiButton((Rectangle){540, 380, 120, 24}, "Launch Game")) {
                UnloadRenderTexture(Target);
                Target = LoadRenderTexture(GetScreenWidth() / 4, GetScreenHeight() / 4);
                SetServerJoinIP = "127.0.0.1";
                SetServerJoinFlag = true;
                break;
            }
            if (GuiButton((Rectangle){540, 420, 120, 24}, "Cancel")) {
                StartServerMode = false;
            }
        }

        EndTextureMode();
        BeginDrawing();

        DrawTexturePro(Target.texture, (Rectangle){0, 0, Target.texture.width, -Target.texture.height}, (Rectangle){0, 0, GetScreenWidth(), GetScreenHeight()}, (Vector2){0, 0}, 0.f, WHITE);

        EndDrawing();

        if (IsKeyPressed(KEY_ESCAPE)) break;
    }
    StopMusicStream(OmegaTechData.HomeScreenMusic);
    OmegaTechData.Deaths = 1;
}

static int ScriptTimer = 0;
static float X, Y, Z, S, Rotation, W, H, L;
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
            CachedCollision[CachedCollisionCounter].X =  ToFloat(WSplitValue(WData, i + 1));
            CachedCollision[CachedCollisionCounter].Y =  ToFloat(WSplitValue(WData, i + 2));
            CachedCollision[CachedCollisionCounter].Z =  ToFloat(WSplitValue(WData, i + 3));
            CachedCollision[CachedCollisionCounter].W =  ToFloat(WSplitValue(WData, i + 6));
            CachedCollision[CachedCollisionCounter].H =  ToFloat(WSplitValue(WData, i + 7));
            CachedCollision[CachedCollisionCounter].L =  ToFloat(WSplitValue(WData, i + 8));
            CachedCollisionCounter ++;
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

        if (OmegaTechData.MainCamera.position.z - OmegaTechData.RenderRadius < Z && OmegaTechData.MainCamera.position.z + OmegaTechData.RenderRadius > Z || CachedModels[i].ModelId == -1)
        {
            if (OmegaTechData.MainCamera.position.x - OmegaTechData.RenderRadius < X && OmegaTechData.MainCamera.position.x + OmegaTechData.RenderRadius > X || CachedModels[i].ModelId == -1)
            {
                if (CheckCollisionBoxSphere((BoundingBox){(Vector3){X, Y, Z}, (Vector3){W, H, L}},{OmegaTechData.MainCamera.position.x + OmegaPlayer.Width / 2,OmegaTechData.MainCamera.position.y - OmegaPlayer.Height / 2,OmegaTechData.MainCamera.position.z - OmegaPlayer.Width / 2},1.0)){
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

        if (OmegaTechData.MainCamera.position.z - OmegaTechData.RenderRadius < Z && OmegaTechData.MainCamera.position.z + OmegaTechData.RenderRadius > Z || CachedModels[i].ModelId == -1)
        {
            if (OmegaTechData.MainCamera.position.x - OmegaTechData.RenderRadius < X && OmegaTechData.MainCamera.position.x + OmegaTechData.RenderRadius > X || CachedModels[i].ModelId == -1)
            {

                switch (CachedModels[i].ModelId)
                {
                case -2:
                    if (CheckCollisionBoxes(OmegaPlayer.PlayerBounds, (BoundingBox){(Vector3){X, Y, Z}, (Vector3){X + S, Y + S, Z + S}}))
                    {
                        ObjectCollision = true;
                        if (!IsSoundPlaying(OmegaTechSoundData.CollisionSound))
                        {
                            PlaySound(OmegaTechSoundData.CollisionSound);
                        }
                    }
                    break;
                case -1:
                    DrawModelEx(WDLModels.HeightMap, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 1:
                    DrawModelEx(WDLModels.Model1, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 2:
                    DrawModelEx(WDLModels.Model2, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 3:
                    DrawModelEx(WDLModels.Model3, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 4:
                    DrawModelEx(WDLModels.Model4, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 5:
                    DrawModelEx(WDLModels.Model5, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 6:
                    DrawModelEx(WDLModels.Model6, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 7:
                    DrawModelEx(WDLModels.Model7, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 8:
                    DrawModelEx(WDLModels.Model8, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 9:
                    DrawModelEx(WDLModels.Model9, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 10:
                    DrawModelEx(WDLModels.Model10, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 11:
                    DrawModelEx(WDLModels.Model11, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 12:
                    DrawModelEx(WDLModels.Model12, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 13:
                    DrawModelEx(WDLModels.Model13, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 14:
                    DrawModelEx(WDLModels.Model14, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 15:
                    DrawModelEx(WDLModels.Model15, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 16:
                    DrawModelEx(WDLModels.Model16, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 17:
                    DrawModelEx(WDLModels.Model17, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 18:
                    DrawModelEx(WDLModels.Model18, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 19:
                    DrawModelEx(WDLModels.Model19, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 20:
                    DrawModelEx(WDLModels.Model20, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                default:
                    break;
                }
                if (CachedModels[i].Collision)
                {
                    BoundingBox ModelBox = {{(X - S), (Y - S), (Z - S)}, {(X + S), (Y + S), (Z + S)}};
                    if (CheckCollisionBoxes(OmegaPlayer.PlayerBounds, ModelBox))
                    {
                        ObjectCollision = true;
                    }
                }
            }
        }
    }
}

float GetDistance(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float distance = std::sqrt(dx * dx + dy * dy);
    return distance;
}

int FlipNumber(int num) {
    int i = 100;
    return i - num;
}

// Sample heightmap at world XZ location, returns terrain-surface Y or -99999
float SampleHeightmapGroundY(float px, float pz) {
    if (!WDLModels.HeightMapReady || WDLModels.HeightMapImage.data == 0)
        return -99999.0f;

    Vector3 o = WDLModels.HeightMapPosition;
    float scale = WDLModels.HeightMapScale;
    float sx = WDLModels.HeightMapSize.x * scale;
    float sz = WDLModels.HeightMapSize.z * scale;
    int iw = WDLModels.HeightMapImage.width;
    int ih = WDLModels.HeightMapImage.height;
    if (iw < 1 || ih < 1) return -99999.0f;

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
    uint8_t* p = (uint8_t*)WDLModels.HeightMapImage.data;
    float h00 = p[iz * iw + ix] / 255.0f;
    float h10 = p[iz * iw + ix + 1] / 255.0f;
    float h01 = p[(iz + 1) * iw + ix] / 255.0f;
    float h11 = p[(iz + 1) * iw + ix + 1] / 255.0f;
    float ht = h00 * (1 - tx) * (1 - tz)
             + h10 * tx * (1 - tz)
             + h01 * (1 - tx) * tz
             + h11 * tx * tz;
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

        if (WReadValue(Instruction, 0, 4) == L"Model" || WReadValue(Instruction, 0, 1) == L"NE" || WReadValue(Instruction, 0, 6) == L"ClipBox" ||WReadValue(Instruction, 0, 5) == L"Object" || WReadValue(Instruction, 0, 5) == L"Script" || WReadValue(Instruction, 0, 8) == L"HeightMap" || WReadValue(Instruction, 0, 8) == L"Collision" || WReadValue(Instruction, 0, 11) == L"AdvCollision" ||
            WReadValue(Instruction, 0, 6) == L"Pickup" || WReadValue(Instruction, 0, 5) == L"Spawn" ||
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

                    if (Instruction == L"NE1"){
                        if (!IsMusicStreamPlaying(OmegaTechSoundData.NESound1))PlayMusicStream(OmegaTechSoundData.NESound1);
                    }
                    if (Instruction == L"NE2"){
                        if (!IsMusicStreamPlaying(OmegaTechSoundData.NESound2))PlayMusicStream(OmegaTechSoundData.NESound2);
                    }
                    if (Instruction == L"NE3"){
                        if (!IsMusicStreamPlaying(OmegaTechSoundData.NESound3))PlayMusicStream(OmegaTechSoundData.NESound3);
                    }

                }
            }
        }
        else {
            if (Instruction == L"NE1"){
                StopMusicStream(OmegaTechSoundData.NESound1);
            }
            if (Instruction == L"NE2"){
                StopMusicStream(OmegaTechSoundData.NESound2);
            }
            if (Instruction == L"NE3"){
                StopMusicStream(OmegaTechSoundData.NESound3);
            }

        }

        if (Render)
        {
            if (WReadValue(Instruction, 0, 4) == L"Model")
            {
                int Identifier = ToFloat(WReadValue(Instruction, 5, 6));
                switch (Identifier)
                {
                case 1:
                    DrawModelEx(WDLModels.Model1, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 2:
                    DrawModelEx(WDLModels.Model2, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 3:
                    DrawModelEx(WDLModels.Model3, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 4:
                    DrawModelEx(WDLModels.Model4, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 5:
                    DrawModelEx(WDLModels.Model5, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 6:
                    DrawModelEx(WDLModels.Model6, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 7:
                    DrawModelEx(WDLModels.Model7, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 8:
                    DrawModelEx(WDLModels.Model8, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 9:
                    DrawModelEx(WDLModels.Model9, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 10:
                    DrawModelEx(WDLModels.Model10, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 11:
                    DrawModelEx(WDLModels.Model11, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 12:
                    DrawModelEx(WDLModels.Model12, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 13:
                    DrawModelEx(WDLModels.Model13, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 14:
                    DrawModelEx(WDLModels.Model14, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 15:
                    DrawModelEx(WDLModels.Model15, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 16:
                    DrawModelEx(WDLModels.Model16, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 17:
                    DrawModelEx(WDLModels.Model17, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 18:
                    DrawModelEx(WDLModels.Model18, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 19:
                    DrawModelEx(WDLModels.Model19, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                case 20:
                    DrawModelEx(WDLModels.Model20, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
                    break;
                default:
                    break;
                }
            }

            if (NextCollision)
            {
                BoundingBox ModelBox = {{(X - S), (Y - S), (Z - S)}, {(X + S), (Y + S), (Z + S)}};
                if (CheckCollisionBoxes(OmegaPlayer.PlayerBounds, ModelBox))
                {
                    ObjectCollision = true;
                }
                NextCollision = false;
            }

            int AudioValue = 0;
                                        
            if (Instruction == L"NE1"){
                AudioValue = FlipNumber(GetDistance( X , Z, OmegaTechData.MainCamera.position.x , OmegaTechData.MainCamera.position.z));
                if (AudioValue > 0 && AudioValue < 100)SetMusicVolume(OmegaTechSoundData.NESound1 , float(AudioValue) / 100.0f);
                else {SetMusicVolume(OmegaTechSoundData.NESound1 , 0);}
            }
            if (Instruction == L"NE2"){
                AudioValue = FlipNumber(GetDistance( X , Z, OmegaTechData.MainCamera.position.x , OmegaTechData.MainCamera.position.z));
                if (AudioValue > 0 && AudioValue < 100)SetMusicVolume(OmegaTechSoundData.NESound2 ,  float(AudioValue) / 100.0f);
                else {SetMusicVolume(OmegaTechSoundData.NESound2 , 0);}
            }
            if (Instruction == L"NE3"){
                AudioValue = FlipNumber(GetDistance( X , Z, OmegaTechData.MainCamera.position.x , OmegaTechData.MainCamera.position.z));
                if (AudioValue > 0 && AudioValue < 100)SetMusicVolume(OmegaTechSoundData.NESound3 ,  float(AudioValue) / 100.0f);
                else {SetMusicVolume(OmegaTechSoundData.NESound3 , 0);}
            }

            

            if (Instruction == L"Object1")
                DrawModelEx(OmegaTechGameObjects.Object1, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
            if (Instruction == L"Object2")
                DrawModelEx(OmegaTechGameObjects.Object2, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
            if (Instruction == L"Object3")
                DrawModelEx(OmegaTechGameObjects.Object3, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
            if (Instruction == L"Object4")
                DrawModelEx(OmegaTechGameObjects.Object4, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);
            if (Instruction == L"Object5")
                DrawModelEx(OmegaTechGameObjects.Object5, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, FadeColor);

            if (Instruction == L"Collision")
            { // Collision
                if (CheckCollisionBoxes(OmegaPlayer.PlayerBounds, (BoundingBox){(Vector3){X, Y, Z}, (Vector3){X + S, Y + S, Z + S}}))
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
                if (CheckCollisionBoxes(OmegaPlayer.PlayerBounds, (BoundingBox){(Vector3){X, Y, Z}, (Vector3){X + S, Y + S, Z + S}}))
                {
                    ObjectCollision = true;
                    if (ScriptTimer == 0)
                    {
                        ParasiteScriptInit();
                        LoadScript(TextFormat("GameData/Worlds/%s/Scripts/Script%i.ps", g_world_to_load, int(ToFloat(WReadValue(Instruction, 6, Instruction.size() - 1)))));

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
                        {OmegaTechData.MainCamera.position.x + OmegaPlayer.Width / 2,
                         OmegaTechData.MainCamera.position.y - OmegaPlayer.Height / 2,
                         OmegaTechData.MainCamera.position.z - OmegaPlayer.Width / 2},
                        1.0)){
                            PlatformHeight = H;
                            FoundPlatform = true;
                        }

                if (Debug)DrawBoundingBox((BoundingBox){(Vector3){X, Y, Z}, (Vector3){W, H  - 5, L}}, PURPLE);
            

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
                        {OmegaTechData.MainCamera.position.x + OmegaPlayer.Width / 2,
                         OmegaTechData.MainCamera.position.y - OmegaPlayer.Height / 2,
                         OmegaTechData.MainCamera.position.z - OmegaPlayer.Width / 2},
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
    // Skipped when flying or noclipping â€” player controls Y manually
    if (!OmegaPlayer.isFlying && !OmegaPlayer.isNoClip)
    {
        float groundY = SampleHeightmapGroundY(
            OmegaTechData.MainCamera.position.x,
            OmegaTechData.MainCamera.position.z);
        if (groundY > -50000.0f) {
            const float eyeHeight = 2.0f;
            // Only snap if at or below ground (allows jumping above terrain)
            if (OmegaTechData.MainCamera.position.y <= groundY + eyeHeight + 0.1f) {
                OmegaTechData.MainCamera.position.y = groundY + eyeHeight;
                OmegaPlayer.velocityY = 0.0f;
                OmegaPlayer.onGround = true;
            } else {
                OmegaPlayer.onGround = false;
            }
        } else if (FoundPlatform) {
            const float eyeHeight = 2.0f;
            if (OmegaTechData.MainCamera.position.y <= PlatformHeight + eyeHeight + 0.1f) {
                OmegaTechData.MainCamera.position.y = PlatformHeight + eyeHeight;
                OmegaPlayer.velocityY = 0.0f;
                OmegaPlayer.onGround = true;
            } else {
                OmegaPlayer.onGround = false;
            }
        } else {
            OmegaPlayer.onGround = false;
        }
    }
}

void UpdateEntities()
{
    Vector3 playerPos = OmegaTechData.MainCamera.position;
    float dt = GetFrameTime();
    
    // Update all pawns via PawnSystem (FSM: IDLE/PATROL/CHASE/RETURN)
    PawnSystem::Instance().Update(playerPos, dt);
    
    // Update pickups (respawn timers, player collision)
    PawnSystem::Instance().UpdatePickups(dt, playerPos, OmegaPlayer.PlayerBounds);
    
    // Draw all pawns
    PawnSystem::Instance().DrawAll(OmegaTechData.MainCamera);
    
    // Draw entity billboards (player starts, pickups, zones)
    PawnSystem::Instance().DrawEntities(OmegaTechData.MainCamera);
    
    // Check if any pawn is attacking the player
    float damage = 0;
    if (PawnSystem::Instance().IsPlayerAttacked(playerPos, damage)) {
        OmegaPlayer.Health = 0;
        
        if (OmegaTechData.PanicCounter != 240) {
            OmegaTechData.PanicCounter += 2;
        }
        
        if (OmegaTechData.Ticker % 2 == 0) {
            if (!IsSoundPlaying(OmegaTechSoundData.ChasingSound)) {
                PlaySound(OmegaTechSoundData.ChasingSound);
            }
        }
    }
}

void UpdatePlayer()
{
    if (IsKeyDown(KEY_W) || GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y) != 0 && !Debug)
    {
        if (HeadBob)
        {
            if (OmegaTechData.Ticker % 4 == 0)
            {
                float bob = (OmegaPlayer.HeadBobDirection == 1) ? 0.05f : -0.05f;
                OmegaTechData.MainCamera.target.y += bob;
                if (OmegaPlayer.HeadBob >= 1)
                    OmegaPlayer.HeadBobDirection = 0;
                else if (OmegaPlayer.HeadBob <= -1)
                    OmegaPlayer.HeadBobDirection = 1;
                OmegaPlayer.HeadBob += (OmegaPlayer.HeadBobDirection == 1) ? 1 : -1;
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

    OmegaPlayer.PlayerBounds = (BoundingBox){(Vector3){OmegaTechData.MainCamera.position.x - OmegaPlayer.Width / 2,
                                                       OmegaTechData.MainCamera.position.y - OmegaPlayer.Height,
                                                       OmegaTechData.MainCamera.position.z - OmegaPlayer.Width / 2},
                                             (Vector3){OmegaTechData.MainCamera.position.x + OmegaPlayer.Width / 2,
                                                       OmegaTechData.MainCamera.position.y,
                                                       OmegaTechData.MainCamera.position.z + OmegaPlayer.Width / 2}};
}

void UpdateNoiseEmitters(){
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

    if (OmegaTechGameObjects.Object1Owned)TFlags += L'1';
    else {TFlags += L'0';}
    if (OmegaTechGameObjects.Object2Owned)TFlags += L'1';
    else {TFlags += L'0';}
    if (OmegaTechGameObjects.Object3Owned)TFlags += L'1';
    else {TFlags += L'0';}
    if (OmegaTechGameObjects.Object4Owned)TFlags += L'1';
    else {TFlags += L'0';}
    if (OmegaTechGameObjects.Object5Owned)TFlags += L'1';
    else {TFlags += L'0';}
    // Armory slots (positions 106-107)
    if (OmegaTechGameObjects.Armory1Owned)TFlags += L'1';
    else {TFlags += L'0';}
    if (OmegaTechGameObjects.Armory2Owned)TFlags += L'1';
    else {TFlags += L'0';}
    // Jewelry slots (positions 108-109)
    if (OmegaTechGameObjects.Jewelry1Owned)TFlags += L'1';
    else {TFlags += L'0';}
    if (OmegaTechGameObjects.Jewelry2Owned)TFlags += L'1';
    else {TFlags += L'0';}
    // RPG expansion equipment (positions 110-114)
    if (OmegaTechGameObjects.HelmetOwned)TFlags += L'1';
    else {TFlags += L'0';}
    if (OmegaTechGameObjects.BootsOwned)TFlags += L'1';
    else {TFlags += L'0';}
    if (OmegaTechGameObjects.LegsOwned)TFlags += L'1';
    else {TFlags += L'0';}
    if (OmegaTechGameObjects.Accessory1Owned)TFlags += L'1';
    else {TFlags += L'0';}
    if (OmegaTechGameObjects.Accessory2Owned)TFlags += L'1';
    else {TFlags += L'0';}
    // Backpack data
    TFlags += L':';
    for (int i = 0; i < BACKPACK_SLOTS; i++) {
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

    for (int i = 0; i <= 99; i++)
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

    if (TFlags[101] == L'1')OmegaTechGameObjects.Object1Owned = true;
    if (TFlags[102] == L'1')OmegaTechGameObjects.Object2Owned = true;
    if (TFlags[103] == L'1')OmegaTechGameObjects.Object3Owned = true;
    if (TFlags[104] == L'1')OmegaTechGameObjects.Object4Owned = true;
    if (TFlags[105] == L'1')OmegaTechGameObjects.Object5Owned = true;
    if (TFlags[106] == L'1')OmegaTechGameObjects.Armory1Owned = true;
    if (TFlags[107] == L'1')OmegaTechGameObjects.Armory2Owned = true;
    if (TFlags[108] == L'1')OmegaTechGameObjects.Jewelry1Owned = true;
    if (TFlags[109] == L'1')OmegaTechGameObjects.Jewelry2Owned = true;
    if (TFlags[110] == L'1')OmegaTechGameObjects.HelmetOwned = true;
    if (TFlags[111] == L'1')OmegaTechGameObjects.BootsOwned = true;
    if (TFlags[112] == L'1')OmegaTechGameObjects.LegsOwned = true;
    if (TFlags[113] == L'1')OmegaTechGameObjects.Accessory1Owned = true;
    if (TFlags[114] == L'1')OmegaTechGameObjects.Accessory2Owned = true;

    // Load backpack data (after second ':')
    size_t bpStart = TFlags.find(L':', 110);
    if (bpStart != string::npos) {
        wstring bpData = TFlags.substr(bpStart + 1);
        size_t secondColon = bpData.find(L':');
        wstring slotData = (secondColon != string::npos) ? bpData.substr(0, secondColon) : bpData;
        // Parse slot data: "id,qty;id,qty;..."
        size_t pos = 0;
        int slotIdx = 0;
        while (pos < slotData.size() && slotIdx < BACKPACK_SLOTS) {
            size_t semi = slotData.find(L';', pos);
            if (semi == string::npos) break;
            wstring pair = slotData.substr(pos, semi - pos);
            size_t comma = pair.find(L',');
            if (comma != string::npos) {
                int id = stoi(pair.substr(0, comma));
                int qty = stoi(pair.substr(comma + 1));
                gInventory.backpack[slotIdx].itemId = id;
                gInventory.backpack[slotIdx].quantity = qty;
            }
            pos = semi + 1;
            slotIdx++;
        }
        // Coins
        if (secondColon != string::npos) {
            wstring coinStr = bpData.substr(secondColon + 1);
            if (!coinStr.empty()) gInventory.coins = stoi(coinStr);
        }
    }

    wstring Position = LoadFile("GameData/Saves/POS.sav");

    OmegaTechData.LevelIndex = int(ToFloat(WSplitValue(Position, 3)));

    SetCameraFlag = true;

    int X = ToFloat(WSplitValue(Position, 0));
    int Y = ToFloat(WSplitValue(Position, 1));
    int Z = ToFloat(WSplitValue(Position, 2));
    SetCameraPos = {float(X), float(Y), float(Z)};

    ExtraWDLInstructions = LoadFile("GameData/Saves/Script.sav");
}

void DrawWorld()
{
    BeginTextureMode(Target);
    ClearBackground(BLACK);

    // Detect sky zone for skybox rendering
    PawnSystem::Instance().UpdateSkyZone(
        OmegaTechData.MainCamera.position,
        OmegaPlayer.PlayerBounds);
    bool inSkyZone = PawnSystem::Instance().IsInSkyZone();

    BeginMode3D(OmegaTechData.MainCamera);

    // Sky zone geometry â€” drawn first as backdrop (unlit background)
    if (inSkyZone) {
        rlDisableDepthMask();
        OzoneLoader::Instance().DrawZoneGeometry(
            OmegaTechData.MainCamera,
            PawnSystem::Instance().GetSkyZoneBounds());
        rlEnableDepthMask();
    }

    // GameplaySoundZone — trigger zone-specific music/sound profiles
    {
        Vector3 pp = OmegaTechData.MainCamera.position;
        ZoneVolumeNode* soundZone = PawnSystem::Instance().CheckZoneCollision(pp, OmegaPlayer.PlayerBounds);
        static std::string prevSoundZone;
        static Music defaultWorldMusic;
        if (soundZone && soundZone->zoneType == ZoneType::ZONE_GAMEPLAY_SOUND) {
            auto& sp = soundZone->soundProfile;
            if (!sp.music_on_enter.empty() && prevSoundZone != soundZone->name) {
                // Save default world music before crossfading
                if (prevSoundZone.empty() && OmegaTechSoundData.MusicFound)
                    defaultWorldMusic = OmegaTechSoundData.BackgroundMusic;
                StopMusicStream(OmegaTechSoundData.BackgroundMusic);
                Music newMusic = LoadMusicWithFallback(sp.music_on_enter.c_str());
                if (newMusic.ctxData != nullptr) {
                    OmegaTechSoundData.BackgroundMusic = newMusic;
                    OmegaTechSoundData.MusicFound = true;
                    PlayMusicStream(OmegaTechSoundData.BackgroundMusic);
                }
            }
            if (!sp.ambience_loop.empty()) {
                OZ_DEBUG("SoundZone '%s': ambience_loop='%s' (not yet implemented)",
                         soundZone->name.c_str(), sp.ambience_loop.c_str());
            }
            if (!sp.sfx_on_enter.empty() && prevSoundZone != soundZone->name) {
                OZ_DEBUG("SoundZone '%s': sfx_on_enter='%s' (not yet implemented)",
                         soundZone->name.c_str(), sp.sfx_on_enter.c_str());
            }
            prevSoundZone = soundZone->name;
        } else if (!soundZone && !prevSoundZone.empty()) {
            // Exited sound zone — restore default world music
            if (defaultWorldMusic.ctxData != nullptr) {
                StopMusicStream(OmegaTechSoundData.BackgroundMusic);
                OmegaTechSoundData.BackgroundMusic = defaultWorldMusic;
                OmegaTechSoundData.MusicFound = true;
                PlayMusicStream(OmegaTechSoundData.BackgroundMusic);
                defaultWorldMusic = Music{0};
            }
            prevSoundZone.clear();
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

    OzoneLoader::Instance().Draw(OmegaTechData.MainCamera);

    // OZONE brush collision - chunk-accelerated query
    {
        Vector3 cp = OmegaTechData.MainCamera.position;
        float playerFeet = cp.y - 2.0f; // eyeHeight
        auto& chunkMgr = OzoneLoader::Instance().GetChunkManager();
        std::vector<int> nearIndices;
        chunkMgr.GetVolumesNear(cp.x, cp.z, nearIndices);
        auto& vols = OzoneLoader::Instance().GetCollisionVolumes();
        for (int idx : nearIndices) {
            if (idx >= 0 && idx < (int)vols.size() &&
                CheckCollisionBoxes(OmegaPlayer.PlayerBounds, vols[idx].aabb)) {
                // Skip volumes whose top is at or below the player's feet —
                // these are floors/surfaces the player stands on, not obstacles.
                if (vols[idx].aabb.max.y <= playerFeet + 0.1f)
                    continue;
                ObjectCollision = true;
                break;
            }
        }
    }

    // OZONE ground clamp â€” OZONE heightmap first, then brush primitives
    // (only when WDL heightmap and ClipBox didn't already provide ground)
    if (!OmegaPlayer.isFlying && !OmegaPlayer.isNoClip)
    {
        Vector3 cp = OmegaTechData.MainCamera.position;

        // Check OZONE-loaded heightmap
        auto& ozLoader = OzoneLoader::Instance();
        float hmY = ozLoader.HasHeightmap()
            ? ozLoader.SampleHeightmapY(cp.x, cp.z) : -99999.0f;
        if (hmY > -50000.0f) {
            const float eyeHeight = 2.0f;
            if (cp.y <= hmY + eyeHeight + 0.1f) {
                OmegaTechData.MainCamera.position.y = hmY + eyeHeight;
                OmegaPlayer.velocityY = 0.0f;
                OmegaPlayer.onGround = true;
            } else {
                OmegaPlayer.onGround = false;
            }
        } else {
            // Fallback: stand on top of OZONE brush primitives (chunk-accelerated)
            float brushTop = -99999.0f;
            auto& chunkMgr = ozLoader.GetChunkManager();
            std::vector<int> nearIndices;
            chunkMgr.GetVolumesNear(cp.x, cp.z, nearIndices);
            auto& vols = ozLoader.GetCollisionVolumes();
            for (int idx : nearIndices) {
                if (idx < 0 || idx >= (int)vols.size()) continue;
                auto& vol = vols[idx];
                if (cp.x >= vol.aabb.min.x && cp.x <= vol.aabb.max.x &&
                    cp.z >= vol.aabb.min.z && cp.z <= vol.aabb.max.z) {
                    float top = vol.aabb.max.y;
                    if (top > brushTop && top <= cp.y + 0.1f)
                        brushTop = top;
                }
            }
            if (brushTop > -50000.0f) {
                const float eyeHeight = 2.0f;
                if (cp.y <= brushTop + eyeHeight + 0.1f) {
                    OmegaTechData.MainCamera.position.y = brushTop + eyeHeight;
                    OmegaPlayer.velocityY = 0.0f;
                    OmegaPlayer.onGround = true;
                } else {
                    OmegaPlayer.onGround = false;
                }
            } else {
                OmegaPlayer.onGround = false;
            }
        }
    }

    UpdatePlayer();

    // Collision debug overlay (/showcollisions)
    if (g_showCollisionDebug)
    {
        // OZONE brush collision volumes
        auto& vols = OzoneLoader::Instance().GetCollisionVolumes();
        for (auto& cv : vols)
            DrawBoundingBox(cv.aabb, PURPLE);

        // Heightmap bounds
        if (OzoneLoader::Instance().HasHeightmap()) {
            auto& hmModel = OzoneLoader::Instance().GetHeightmapModel();
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
        for (auto& zone : PawnSystem::Instance().GetZones())
            DrawBoundingBox(zone.bounds, BLUE);

        // Player start markers
        for (auto& ps : PawnSystem::Instance().GetPlayerStarts())
            DrawCubeWires(ps.position, 0.5f, 1.0f, 0.5f, GREEN);
    }

    if (Debug)
    {
        DrawLights();
    }
    else
    {
        UpdateEntities();
    }
    if (ObjectCollision)
    {
        if (!OmegaPlayer.isNoClip)
        {
            OmegaTechData.MainCamera.position.x = OmegaPlayer.OldX;
            OmegaTechData.MainCamera.position.y = OmegaPlayer.OldY;
            OmegaTechData.MainCamera.position.z = OmegaPlayer.OldZ;
        }
        ObjectCollision = false;
    }

    // LightningScript entity tick
    {
        float dt = GetFrameTime();
        LightningEntityManager::Instance().Update(dt);

        // Apply pending Fog changes from script contexts
        auto& lem = LightningEntityManager::Instance();
        if (lem.HasPendingFog()) {
            static int fogDensityLoc = GetShaderLocation(OmegaTechData.Lights, "fogDensity");
            static int fogColorLoc = GetShaderLocation(OmegaTechData.Lights, "fogColor");
            float density = lem.PendingFogDensity();
            float color[3] = { lem.PendingFogR(), lem.PendingFogG(), lem.PendingFogB() };
            SetShaderValue(OmegaTechData.Lights, fogDensityLoc, &density, SHADER_UNIFORM_FLOAT);
            SetShaderValue(OmegaTechData.Lights, fogColorLoc, color, SHADER_UNIFORM_VEC3);
            FogEnabled = true;
            FogIntensity = (density > 0) ? density : 0.3f;
            FogTint = { (unsigned char)(color[0]*255), (unsigned char)(color[1]*255),
                        (unsigned char)(color[2]*255), 255 };
            lem.ClearPendingFog();
        }

        // Apply pending Skybox changes from script contexts
        if (lem.HasPendingSkybox()) {
            OZ_INFO("LightningScript: skybox change to '%s' (loading deferred)",
                    lem.PendingSkybox().c_str());
            lem.ClearPendingSkybox();
        }
    }

    UpdateCustom();

    EndMode3D();
    EndTextureMode();

    if (SetSceneFlag)
    {
        OmegaTechData.LevelIndex = SetSceneId;
        LoadWorld();
        SetSceneFlag = false;

        // Place player near the new world's collision surface
        float sy = 8.0f;
        OmegaTechData.MainCamera.position.y = sy;
        OmegaTechData.MainCamera.target.y = sy;
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




