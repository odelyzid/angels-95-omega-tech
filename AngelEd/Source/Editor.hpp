#include "PPGIO.hpp"
#include "../../Source/Package/PackageAssetLoader.hpp"
#include "../../Source/Renderer/EngineBillboard.hpp"
#include "../../Source/OzOzoneLoader.hpp"
#include <cstring>
#include <cmath>
#include <filesystem>
#include <algorithm>
namespace fs = std::filesystem;

#define MaxCachedModels 200

RenderTexture2D Target;

enum class LightingMode : uint8_t { LIT, UNLIT, WIREFRAME, DYNAMIC };

class Editor{
    public:
        Camera3D MainCamera = {0}; 
        Camera3D PreviewCamera = {0}; 
        LightingMode ViewMode = LightingMode::LIT;
        char Path[512] = {};
        wstring WorldData;
        wstring OtherData;
        // Environmental settings
        Color FogColor = {200, 200, 210, 255};
        float FogDensity = 0.02f;
        Color AmbientColor = {180, 180, 200, 255};
        float AmbientIntensity = 0.4f;
        // Window flag
        bool ShowEnvPanel = false;
        bool ShowWireframe = false;
        // Lit fog shader
        Shader LitFogShader = {0};
        int FogStartLoc = -1;
        int FogEndLoc = -1;
        int FogDensityLoc = -1;
        int FogColorLoc = -1;
        int FogIntensityLoc = -1;
        int AmbientLoc = -1;
};

static Editor OTEditor;

// Per-model entry in the dynamic model array
struct LoadedModel {
    Model model;
    Texture2D texture;
    std::string name;
    bool loaded = false;
};

class GameModels
{
    public:
        Texture2D Skybox;
        Vector3 HeightMapPosition;
        Vector3 HeightMapSize = {0, 0, 0};
        float HeightMapScale = 1.0f;
        Image HeightMapImage = {0};
        bool HeightMapReady = false;

        Model HeightMap;
        Texture2D HeightMapTexture;

        std::vector<LoadedModel> models;

        // Dynamically load all .obj files from <worldPath>/Models/
        void LoadModels(const std::string& worldPath) {
            for (auto& m : models) {
                if (m.loaded) {
                    if (m.texture.id > 0) UnloadTexture(m.texture);
                    UnloadModel(m.model);
                }
            }
            models.clear();

            fs::path dir = fs::path(worldPath) / "Models";
            if (!fs::exists(dir)) return;

            std::vector<fs::path> objFiles;
            for (auto& entry : fs::directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".obj") objFiles.push_back(entry.path());
                }
            }
            std::sort(objFiles.begin(), objFiles.end());

            for (auto& objPath : objFiles) {
                LoadedModel lm;
                lm.name = objPath.stem().string();
                lm.model = LoadModel(objPath.string().c_str());
                if (lm.model.meshes == nullptr) {
                    lm.loaded = false;
                    models.push_back(lm);
                    continue;
                }
                std::string base = objPath.parent_path().string() + "/" + lm.name;
                std::string texPath = base + "_texture.png";
                if (!fs::exists(texPath)) texPath = base + ".png";
                if (fs::exists(texPath)) {
                    lm.texture = LoadTexture(texPath.c_str());
                    if (lm.texture.id > 0)
                        lm.model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = lm.texture;
                }
                if (OTEditor.LitFogShader.id > 0)
                    lm.model.materials[0].shader = OTEditor.LitFogShader;
                lm.loaded = true;
                models.push_back(lm);
            }
        }

        // Get a loaded model by 1-based WDL ModelId (Model1 â†’ 1, Model2 â†’ 2, ...)
        LoadedModel* GetModelByWDLId(int wdlId) {
            int idx = wdlId - 1;
            if (idx >= 0 && idx < (int)models.size() && models[idx].loaded)
                return &models[idx];
            return nullptr;
        }

        int GetModelCount() const { return (int)models.size(); }
        const char* GetModelName(int index) const {
            if (index < 0 || index >= (int)models.size()) return nullptr;
            return models[index].name.c_str();
        }
};

extern GameModels WDLModels;

class GameData{
    public:
        float X, Y, Z, R, S;
        int ModelId;
        bool Collision;
        void Init(){ X=Y=Z=R=S=0; ModelId=0; Collision=false; }
};

static int CachedModelCounter = 0;
static GameData CachedModels[MaxCachedModels];

class CollisionData{
    public:
        float X, Y, Z, W, H, L;
        void Init(){ X=Y=Z=W=H=L=0; }
};

static int CachedCollisionCounter = 0;
static CollisionData CachedCollision[MaxCachedModels];

// --- Heightmap sampling (terrain following) ---
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
    float ht = h00 * (1-tx)*(1-tz) + h10 * tx*(1-tz) + h01 * (1-tx)*tz + h11 * tx*tz;
    return o.y + ht * WDLModels.HeightMapSize.y * scale;
}

// --- Pickup types for editor placement ---
enum class EditorPickupType {
    HEALTH_VIAL = 0,
    MANA_VIAL = 1,
    ENERGY_CRYSTAL = 2,
    KEY = 3,
    COIN = 4,
    POWERUP = 5
};

static const char* PickupTypeLabel(EditorPickupType t) {
    switch (t) {
        case EditorPickupType::HEALTH_VIAL:    return "Health Vial";
        case EditorPickupType::MANA_VIAL:      return "Mana Vial";
        case EditorPickupType::ENERGY_CRYSTAL: return "Energy Crystal";
        case EditorPickupType::KEY:            return "Key";
        case EditorPickupType::COIN:           return "Coin";
        case EditorPickupType::POWERUP:        return "Powerup";
        default: return "?";
    }
}

// --- Pawn node types ---
enum class EditorNodeType { SPAWN, NPC, LIGHT, ZONE };
static const char* NodeTypeLabel(EditorNodeType t) {
    switch (t) {
        case EditorNodeType::SPAWN: return "Player Spawn";
        case EditorNodeType::NPC:   return "NPC Spawn";
        case EditorNodeType::LIGHT: return "Point Light";
        case EditorNodeType::ZONE:  return "Zone Volume";
        default: return "?";
    }
}

void Init(){
    if (WDLModels.HeightMapImage.data) {
        UnloadImage(WDLModels.HeightMapImage);
        WDLModels.HeightMapImage = (Image){0};
    }
    WDLModels.HeightMapReady = false;

    OTEditor.MainCamera.position = (Vector3){ 18.0f, 10.0f, 18.0f };
    OTEditor.MainCamera.target = (Vector3){ 0.0f, 10.0f, 0.0f };   
    OTEditor.MainCamera.up = (Vector3){ 0.0f, 1.0f, 0.0f };          
    OTEditor.MainCamera.fovy = 60.0f;                         
    OTEditor.MainCamera.projection = CAMERA_PERSPECTIVE;

    OTEditor.PreviewCamera.position = (Vector3){ 5.0f, 4.0f, 5.0f };  
    OTEditor.PreviewCamera.target = (Vector3){ 0.0f, 0.0f, 0.0f };    
    OTEditor.PreviewCamera.up = (Vector3){ 0.0f, 1.0f, 0.0f };         
    OTEditor.PreviewCamera.fovy = 45.0f;                                
    OTEditor.PreviewCamera.projection = CAMERA_PERSPECTIVE;         

    OTEditor.WorldData = LoadFile(TextFormat("%s/World.wdl", OTEditor.Path));

    Target = LoadRenderTexture(320 , 200);

    // Initialize lit fog shader for editor
    // Use OTEditor.Path prefix so shaders resolve from ../GameData/ when cwd=System/
    std::string shaderBase = std::string(OTEditor.Path) + "Shaders/Lights/";
    OTEditor.LitFogShader = LoadShaderWithFallback(
        (shaderBase + "Lighting.vs").c_str(),
        (shaderBase + "LitFog.fs").c_str()
    );
    
    // Initialize fog uniforms for editor
    if (OTEditor.LitFogShader.id > 0) {
        OTEditor.FogStartLoc = GetShaderLocation(OTEditor.LitFogShader, "fogStart");
        OTEditor.FogEndLoc = GetShaderLocation(OTEditor.LitFogShader, "fogEnd");
        OTEditor.FogDensityLoc = GetShaderLocation(OTEditor.LitFogShader, "fogDensity");
        OTEditor.FogColorLoc = GetShaderLocation(OTEditor.LitFogShader, "fogColor");
        OTEditor.FogIntensityLoc = GetShaderLocation(OTEditor.LitFogShader, "fogIntensity");
        
        // Set default fog values
        float fogStart = 10.0f;
        float fogEnd = 100.0f;
        float fogDensity = 1.0f;
        float fogColor[3] = {0.7f, 0.7f, 0.8f};
        float fogIntensity = 1.0f;
        
        SetShaderValue(OTEditor.LitFogShader, OTEditor.FogStartLoc, &fogStart, SHADER_UNIFORM_FLOAT);
        SetShaderValue(OTEditor.LitFogShader, OTEditor.FogEndLoc, &fogEnd, SHADER_UNIFORM_FLOAT);
        SetShaderValue(OTEditor.LitFogShader, OTEditor.FogDensityLoc, &fogDensity, SHADER_UNIFORM_FLOAT);
        SetShaderValue(OTEditor.LitFogShader, OTEditor.FogColorLoc, fogColor, SHADER_UNIFORM_VEC3);
        SetShaderValue(OTEditor.LitFogShader, OTEditor.FogIntensityLoc, &fogIntensity, SHADER_UNIFORM_FLOAT);
        
        // Ambient uniform
        OTEditor.AmbientLoc = GetShaderLocation(OTEditor.LitFogShader, "ambient");
        float ambient[4] = {(float)OTEditor.AmbientColor.r / 255.0f * OTEditor.AmbientIntensity,
                            (float)OTEditor.AmbientColor.g / 255.0f * OTEditor.AmbientIntensity,
                            (float)OTEditor.AmbientColor.b / 255.0f * OTEditor.AmbientIntensity,
                            1.0f};
        if (OTEditor.AmbientLoc >= 0)
            SetShaderValue(OTEditor.LitFogShader, OTEditor.AmbientLoc, ambient, SHADER_UNIFORM_VEC4);
    }

    // Pass lit fog shader to OzoneLoader for OZONE geometry
    OzoneLoader::Instance().SetLitFogShader(OTEditor.LitFogShader);

    // Initialize engine billboard system
    EngineBillboard::Init();

    if (IsPathFile(TextFormat("%s/Models/HeightMap.png", OTEditor.Path)))
    {
        WDLModels.HeightMapTexture = LoadTexture(TextFormat("%sModels/HeightMapTexture.png", OTEditor.Path));
        WDLModels.HeightMapImage = LoadImage(TextFormat("%sModels/HeightMap.png", OTEditor.Path));
        ImageFormat(&WDLModels.HeightMapImage, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE);
        int X = PullConfigValue(TextFormat("%sModels/HeightMapConfig.conf", OTEditor.Path), 0);
        int Y = PullConfigValue(TextFormat("%sModels/HeightMapConfig.conf", OTEditor.Path), 1);
        int Z = PullConfigValue(TextFormat("%sModels/HeightMapConfig.conf", OTEditor.Path), 2);
        WDLModels.HeightMapSize = (Vector3){(float)X, (float)Y, (float)Z};
        Mesh Mesh1 = GenMeshHeightmap(WDLModels.HeightMapImage, WDLModels.HeightMapSize);
        fprintf(stderr, "HM: %dx%d img=%dx%d\n", X, Z,
                WDLModels.HeightMapImage.width, WDLModels.HeightMapImage.height);
        WDLModels.HeightMap = LoadModelFromMesh(Mesh1);
        WDLModels.HeightMap.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.HeightMapTexture;
        WDLModels.HeightMap.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        if (OTEditor.LitFogShader.id > 0) {
            WDLModels.HeightMap.materials[0].shader = OTEditor.LitFogShader;
        }
        WDLModels.HeightMapReady = (WDLModels.HeightMapImage.data != nullptr);
    }

    // Dynamically load all .obj models from the world's Models/ directory
    WDLModels.LoadModels(OTEditor.Path);
}

static int ScriptTimer = 0;
static float X, Y, Z, S, Rotation, W, H, L;
bool NextCollision = false;

void CacheWDL()
{
    wstring WData = OTEditor.WorldData;
    OTEditor.OtherData = L"";
    CachedModelCounter = 0;
    CachedCollisionCounter = 0;
    NextCollision = false;

    for (int i = 0; i <= MaxCachedModels - 1; i++)
    {
        CachedModels[i].Init();
        CachedCollision[i].Init();
    }

    for (int i = 0; i <= GetWDLSize(OTEditor.WorldData, L""); i++)
    {
        if (CachedModelCounter == MaxCachedModels) break;

        wstring Instruction = WSplitValue(WData, i);

        if (WReadValue(Instruction, 0, 4) == L"Model" || WReadValue(Instruction, 0, 8) == L"HeightMap")
        {
            if (WReadValue(Instruction, 0, 8) != L"HeightMap")
                CachedModels[CachedModelCounter].ModelId = int(ToFloat(WReadValue(Instruction, 5, 6)));
            else
                CachedModels[CachedModelCounter].ModelId = -1;
            
            CachedModels[CachedModelCounter].X = ToFloat(WSplitValue(WData, i + 1));
            CachedModels[CachedModelCounter].Y = ToFloat(WSplitValue(WData, i + 2));
            CachedModels[CachedModelCounter].Z = ToFloat(WSplitValue(WData, i + 3));
            CachedModels[CachedModelCounter].S = ToFloat(WSplitValue(WData, i + 4));
            CachedModels[CachedModelCounter].R = ToFloat(WSplitValue(WData, i + 5));
            if (NextCollision) { CachedModels[CachedModelCounter].Collision = true; NextCollision = false; }
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
            CachedCollisionCounter ++;
        }

        if (WReadValue(Instruction, 0, 5) == L"Object" || WReadValue(Instruction, 0, 5) == L"Script")
        {
            OTEditor.OtherData += Instruction + L":" +
                WSplitValue(WData, i + 1) + L":" + WSplitValue(WData, i + 2) + L":" +
                WSplitValue(WData, i + 3) + L":" + WSplitValue(WData, i + 4) + L":" +
                WSplitValue(WData, i + 5) + L":";
            if (WReadValue(Instruction, 0, 6) == L"ClipBox" || WReadValue(Instruction, 0, 11) == L"AdvCollision") {
                OTEditor.OtherData += WSplitValue(WData, i + 6) + L":" + WSplitValue(WData, i + 7) + L":" + WSplitValue(WData, i + 8) + L":";
                i += 3;
            }
            i += 5;
            continue;
        }

        if (Instruction == L"C") { NextCollision = true; }
    }
}

void CWDLProcess()
{
    for (int i = 0; i <= CachedCollisionCounter; i++)
    {
        X = CachedCollision[i].X; Y = CachedCollision[i].Y; Z = CachedCollision[i].Z;
        W = CachedCollision[i].W; H = CachedCollision[i].H; L = CachedCollision[i].L;
        DrawBoundingBox((BoundingBox){(Vector3){X, Y, Z}, (Vector3){W, H, L}}, PURPLE);
    }
    
    for (int i = 0; i <= CachedModelCounter; i++)
    {
        X = CachedModels[i].X; Y = CachedModels[i].Y; Z = CachedModels[i].Z;
        S = CachedModels[i].S; Rotation = CachedModels[i].R;

        if (CachedModels[i].Collision) DrawCubeWires({X, Y, Z}, S, S, S, RED);

        if (OTEditor.MainCamera.position.z - 1000 < Z && OTEditor.MainCamera.position.z + 1000 > Z)
        {
            if (OTEditor.MainCamera.position.x - 1000 < X && OTEditor.MainCamera.position.x + 1000 > X)
            {
                int mid = CachedModels[i].ModelId;
                if (mid == -2) {
                    DrawCubeWires({X, Y, Z}, S, S, S, RED);
                } else if (mid == -1) {
                    DrawModelEx(WDLModels.HeightMap, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE);
                } else if (mid > 0) {
                    LoadedModel* lm = WDLModels.GetModelByWDLId(mid);
                    if (lm) DrawModelEx(lm->model, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE);
                }
            }
        }
    }
}

void WDLProcess()
{
    wstring WData = OTEditor.OtherData;
    int Size = GetWDLSize(OTEditor.OtherData, L"");
    bool Render = false;
    bool FoundPlatform = false;
    float PlatformHeight = 0.0f;

    for (int i = 0; i <= Size; i++)
    {
        wstring Instruction = WSplitValue(WData, i);
        if (Instruction == L"C") { NextCollision = true; }

        if (WReadValue(Instruction, 0, 4) == L"Model" || WReadValue(Instruction, 0, 1) == L"NE" ||
            WReadValue(Instruction, 0, 6) == L"ClipBox" ||
            WReadValue(Instruction, 0, 5) == L"Object" || WReadValue(Instruction, 0, 5) == L"Script" ||
            WReadValue(Instruction, 0, 8) == L"HeightMap" || WReadValue(Instruction, 0, 8) == L"Collision" ||
            WReadValue(Instruction, 0, 11) == L"AdvCollision" ||
            WReadValue(Instruction, 0, 6) == L"Pickup" || WReadValue(Instruction, 0, 5) == L"Spawn" ||
            WReadValue(Instruction, 0, 3) == L"NPC" || WReadValue(Instruction, 0, 5) == L"Light")
        {
            X = ToFloat(WSplitValue(WData, i + 1));
            Y = ToFloat(WSplitValue(WData, i + 2));
            Z = ToFloat(WSplitValue(WData, i + 3));
            S = ToFloat(WSplitValue(WData, i + 4));
            Rotation = ToFloat(WSplitValue(WData, i + 5));
            if (OTEditor.MainCamera.position.z - 1000 < Z && OTEditor.MainCamera.position.z + 1000 > Z &&
                OTEditor.MainCamera.position.x - 1000 < X && OTEditor.MainCamera.position.x + 1000 > X)
                Render = true;
        }

        if (Render)
        {
            if (WReadValue(Instruction, 0, 4) == L"Model")
            {
                int Identifier = ToFloat(WReadValue(Instruction, 5, 6));
                LoadedModel* lm = WDLModels.GetModelByWDLId(Identifier);
                if (lm) DrawModelEx(lm->model, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE);
            }

            if (Instruction == L"Collision") DrawCubeWires({X, Y, Z}, S, S, S, RED);
            if (WReadValue(Instruction, 0, 5) == L"Script") DrawCubeWires({X, Y, Z}, S, S, S, YELLOW);
            if (WReadValue(Instruction, 0, 6) == L"Pickup") {
                Camera3D cam = OTEditor.MainCamera;
                const char* pickupNames[] = {"HealthVial", "ManaVial", "EnergyCrystal", "Key", "Coin", "Powerup"};
                int pickupType = std::stoi(WSplitValue(WData, i + 1));
                if (pickupType >= 0 && pickupType < 6) {
                    EngineBillboard::DrawPickup(cam, pickupNames[pickupType], {X, Y, Z}, 0.8f);
                } else {
                    DrawCubeWires({X, Y, Z}, 0.5f, 0.5f, 0.5f, GREEN);
                }
            }
            if (WReadValue(Instruction, 0, 5) == L"Spawn") {
                EngineBillboard::Draw(OTEditor.MainCamera, "PlayerStart", {X, Y + 0.5f, Z}, 1.2f);
            }
            if (WReadValue(Instruction, 0, 3) == L"NPC") {
                EngineBillboard::Draw(OTEditor.MainCamera, "PawnNode", {X, Y + 1.0f, Z}, 1.5f);
            }
            if (WReadValue(Instruction, 0, 5) == L"Light") {
                EngineBillboard::Draw(OTEditor.MainCamera, "Light", {X, Y + 0.5f, Z}, 1.0f);
            }
            if (WReadValue(Instruction, 0, 5) == L"Sound") {
                EngineBillboard::Draw(OTEditor.MainCamera, "Sound", {X, Y + 0.5f, Z}, 1.0f);
            }
            if (WReadValue(Instruction, 0, 5) == L"Music") {
                EngineBillboard::Draw(OTEditor.MainCamera, "Music", {X, Y + 0.5f, Z}, 1.0f);
            }
            if (WReadValue(Instruction, 0, 8) == L"ZoneInfo") {
                EngineBillboard::Draw(OTEditor.MainCamera, "ZoneInfo", {X, Y + 0.5f, Z}, 1.0f);
            }
        }

        if (Instruction == L"ClipBox") {
            W = ToFloat(WSplitValue(WData, i + 6));
            H = ToFloat(WSplitValue(WData, i + 7));
            L = ToFloat(WSplitValue(WData, i + 8));
            DrawBoundingBox((BoundingBox){(Vector3){X, Y, Z}, (Vector3){W, H - 5, L}}, PURPLE);
            i += 3;
        }
        if (Instruction == L"AdvCollision") {
            if (Render) {
                W = ToFloat(WSplitValue(WData, i + 6));
                H = ToFloat(WSplitValue(WData, i + 7));
                L = ToFloat(WSplitValue(WData, i + 8));
                DrawBoundingBox((BoundingBox){(Vector3){X, Y, Z}, (Vector3){W, H, L}}, PURPLE);
            }
            i += 3;
        }
        if (Instruction == L"HeightMap") {
            WDLModels.HeightMapPosition.x = X;
            WDLModels.HeightMapPosition.y = Y;
            WDLModels.HeightMapPosition.z = Z;
            WDLModels.HeightMapScale = S;
            DrawModelEx(WDLModels.HeightMap, {X, Y, Z}, {0, 1, 0}, 0, {S, S, S}, WHITE);
        }

        if (!NextCollision) i += 5;
        Render = false;
    }
}

class InEditor{
    public:
        bool DrawModel = false;
        float X = 0, Y = 0, Z = 0, S = 1, R = 0, L = 0, H = 0, W = 0;
        EditorPickupType ActivePickupType = EditorPickupType::HEALTH_VIAL;
        EditorNodeType ActiveNodeType = EditorNodeType::SPAWN;
        // CSG operation for OZONE brush placement: 0=SOLID, 1=ADD, 2=SUB, 3=INTERSECT, 4=DE_RESC
        int CSGOperation = 0;
};

static InEditor OmegaTechEditor;

char ScriptEditorBuffer[1200];

void ConvertConstCharToCharArray(const char* constString, char* charArray, int arraySize) {
    std::strncpy(charArray, constString, arraySize - 1);
    charArray[arraySize - 1] = '\0';
}

void LoadEditor(const char* File){
    ifstream file(File);
    string fileContents;
    string line;
    while (getline(file, line)) { fileContents += line + "\n"; }
    file.close();
    ConvertConstCharToCharArray(fileContents.c_str(), ScriptEditorBuffer, 1200);
}

int EMID = 1;
bool CollisionToggle = false;

// --- Helper: draw a WDL instruction line for current placed item ---
static wstring BuildWDLPlaceCommand(const wstring& prefix, int subId) {
    wstring cmd = prefix;
    if (subId >= 0) cmd += to_wstring(subId);
    cmd += L":" + to_wstring(OmegaTechEditor.X) + L":" + to_wstring(OmegaTechEditor.Y) + L":" +
           to_wstring(OmegaTechEditor.Z) + L":" + to_wstring(OmegaTechEditor.S) + L":" + to_wstring(OmegaTechEditor.R) + L":";
    return cmd;
}
