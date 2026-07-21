#include "PPGIO.hpp"
#include <cstring>
#include <cmath>

#define MaxCachedModels 200

RenderTexture2D Target;
RenderTexture2D PreviewTarget;
Model CurrentModel;

class Editor{
    public:
        Camera3D MainCamera = {0}; 
        Camera3D PreviewCamera = {0}; 
        char Path[100];
        wstring WorldData;
        wstring OtherData;
        // Environmental settings
        Color FogColor = {200, 200, 210, 255};
        float FogDensity = 0.02f;
        Color AmbientColor = {180, 180, 200, 255};
        float AmbientIntensity = 0.4f;
        // Window flag
        bool ShowEnvPanel = false;
};

static Editor OTEditor;

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

        Model Model1;  Texture2D Model1Texture;
        Model Model2;  Texture2D Model2Texture;
        Model Model3;  Texture2D Model3Texture;
        Model Model4;  Texture2D Model4Texture;
        Model Model5;  Texture2D Model5Texture;
        Model Model6;  Texture2D Model6Texture;
        Model Model7;  Texture2D Model7Texture;
        Model Model8;  Texture2D Model8Texture;
        Model Model9;  Texture2D Model9Texture;
        Model Model10; Texture2D Model10Texture;
        Model Model11; Texture2D Model11Texture;
        Model Model12; Texture2D Model12Texture;
        Model Model13; Texture2D Model13Texture;
        Model Model14; Texture2D Model14Texture;
        Model Model15; Texture2D Model15Texture;
        Model Model16; Texture2D Model16Texture;
        Model Model17; Texture2D Model17Texture;
        Model Model18; Texture2D Model18Texture;
        Model Model19; Texture2D Model19Texture;
        Model Model20; Texture2D Model20Texture;

        Model FastModel1;  Texture2D FastModel1Texture;
        Model FastModel2;  Texture2D FastModel2Texture;
        Model FastModel3;  Texture2D FastModel3Texture;
        Model FastModel4;  Texture2D FastModel4Texture;
        Model FastModel5;  Texture2D FastModel5Texture;
};

static GameModels WDLModels;

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
enum class EditorNodeType { SPAWN, NPC, LIGHT };
static const char* NodeTypeLabel(EditorNodeType t) {
    switch (t) {
        case EditorNodeType::SPAWN: return "Player Spawn";
        case EditorNodeType::NPC:   return "NPC Spawn";
        case EditorNodeType::LIGHT: return "Point Light";
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
    PreviewTarget = LoadRenderTexture(256 , 256);

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
        WDLModels.HeightMapReady = (WDLModels.HeightMapImage.data != nullptr);
    }

    if (IsPathFile(TextFormat("%sModels/Model1.obj", OTEditor.Path)))
    {
        WDLModels.Model1 = LoadModel(TextFormat("%sModels/Model1.obj", OTEditor.Path));
        WDLModels.Model1Texture = LoadTexture(TextFormat("%sModels/Model1Texture.png", OTEditor.Path));
        WDLModels.Model1.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model1Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model2.obj", OTEditor.Path)))
    {
        WDLModels.Model2 = LoadModel(TextFormat("%sModels/Model2.obj", OTEditor.Path));
        WDLModels.Model2Texture = LoadTexture(TextFormat("%sModels/Model2Texture.png", OTEditor.Path));
        WDLModels.Model2.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model2Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model3.obj", OTEditor.Path)))
    {
        WDLModels.Model3 = LoadModel(TextFormat("%sModels/Model3.obj", OTEditor.Path));
        WDLModels.Model3Texture = LoadTexture(TextFormat("%sModels/Model3Texture.png", OTEditor.Path));
        WDLModels.Model3.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model3Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model4.obj", OTEditor.Path)))
    {
        WDLModels.Model4 = LoadModel(TextFormat("%sModels/Model4.obj", OTEditor.Path));
        WDLModels.Model4Texture = LoadTexture(TextFormat("%sModels/Model4Texture.png", OTEditor.Path));
        WDLModels.Model4.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model4Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model5.obj", OTEditor.Path)))
    {
        WDLModels.Model5 = LoadModel(TextFormat("%sModels/Model5.obj", OTEditor.Path));
        WDLModels.Model5Texture = LoadTexture(TextFormat("%sModels/Model5Texture.png", OTEditor.Path));
        WDLModels.Model5.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model5Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model6.obj", OTEditor.Path)))
    {
        WDLModels.Model6 = LoadModel(TextFormat("%sModels/Model6.obj", OTEditor.Path));
        WDLModels.Model6Texture = LoadTexture(TextFormat("%sModels/Model6Texture.png", OTEditor.Path));
        WDLModels.Model6.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model6Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model7.obj", OTEditor.Path)))
    {
        WDLModels.Model7 = LoadModel(TextFormat("%sModels/Model7.obj", OTEditor.Path));
        WDLModels.Model7Texture = LoadTexture(TextFormat("%sModels/Model7Texture.png", OTEditor.Path));
        WDLModels.Model7.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model7Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model8.obj", OTEditor.Path)))
    {
        WDLModels.Model8 = LoadModel(TextFormat("%sModels/Model8.obj", OTEditor.Path));
        WDLModels.Model8Texture = LoadTexture(TextFormat("%sModels/Model8Texture.png", OTEditor.Path));
        WDLModels.Model8.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model8Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model9.obj", OTEditor.Path)))
    {
        WDLModels.Model9 = LoadModel(TextFormat("%sModels/Model9.obj", OTEditor.Path));
        WDLModels.Model9Texture = LoadTexture(TextFormat("%sModels/Model9Texture.png", OTEditor.Path));
        WDLModels.Model9.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model9Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model10.obj", OTEditor.Path)))
    {
        WDLModels.Model10 = LoadModel(TextFormat("%sModels/Model10.obj", OTEditor.Path));
        WDLModels.Model10Texture = LoadTexture(TextFormat("%sModels/Model10Texture.png", OTEditor.Path));
        WDLModels.Model10.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model10Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model11.obj", OTEditor.Path)))
    {
        WDLModels.Model11 = LoadModel(TextFormat("%sModels/Model11.obj", OTEditor.Path));
        WDLModels.Model11Texture = LoadTexture(TextFormat("%sModels/Model11Texture.png", OTEditor.Path));
        WDLModels.Model11.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model11Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model12.obj", OTEditor.Path)))
    {
        WDLModels.Model12 = LoadModel(TextFormat("%sModels/Model12.obj", OTEditor.Path));
        WDLModels.Model12Texture = LoadTexture(TextFormat("%sModels/Model12Texture.png", OTEditor.Path));
        WDLModels.Model12.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model12Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model13.obj", OTEditor.Path)))
    {
        WDLModels.Model13 = LoadModel(TextFormat("%sModels/Model13.obj", OTEditor.Path));
        WDLModels.Model13Texture = LoadTexture(TextFormat("%sModels/Model13Texture.png", OTEditor.Path));
        WDLModels.Model13.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model13Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model14.obj", OTEditor.Path)))
    {
        WDLModels.Model14 = LoadModel(TextFormat("%sModels/Model14.obj", OTEditor.Path));
        WDLModels.Model14Texture = LoadTexture(TextFormat("%sModels/Model14Texture.png", OTEditor.Path));
        WDLModels.Model14.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model14Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model15.obj", OTEditor.Path)))
    {
        WDLModels.Model15 = LoadModel(TextFormat("%sModels/Model15.obj", OTEditor.Path));
        WDLModels.Model15Texture = LoadTexture(TextFormat("%sModels/Model15Texture.png", OTEditor.Path));
        WDLModels.Model15.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model15Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model16.obj", OTEditor.Path)))
    {
        WDLModels.Model16 = LoadModel(TextFormat("%sModels/Model16.obj", OTEditor.Path));
        WDLModels.Model16Texture = LoadTexture(TextFormat("%sModels/Model16Texture.png", OTEditor.Path));
        WDLModels.Model16.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model16Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model17.obj", OTEditor.Path)))
    {
        WDLModels.Model17 = LoadModel(TextFormat("%sModels/Model17.obj", OTEditor.Path));
        WDLModels.Model17Texture = LoadTexture(TextFormat("%sModels/Model17Texture.png", OTEditor.Path));
        WDLModels.Model17.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model17Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model18.obj", OTEditor.Path)))
    {
        WDLModels.Model18 = LoadModel(TextFormat("%sModels/Model18.obj", OTEditor.Path));
        WDLModels.Model18Texture = LoadTexture(TextFormat("%sModels/Model18Texture.png", OTEditor.Path));
        WDLModels.Model18.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model18Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model19.obj", OTEditor.Path)))
    {
        WDLModels.Model19 = LoadModel(TextFormat("%sModels/Model19.obj", OTEditor.Path));
        WDLModels.Model19Texture = LoadTexture(TextFormat("%sModels/Model19Texture.png", OTEditor.Path));
        WDLModels.Model19.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model19Texture;
    }
    if (IsPathFile(TextFormat("%sModels/Model20.obj", OTEditor.Path)))
    {
        WDLModels.Model20 = LoadModel(TextFormat("%sModels/Model20.obj", OTEditor.Path));
        WDLModels.Model20Texture = LoadTexture(TextFormat("%sModels/Model20Texture.png", OTEditor.Path));
        WDLModels.Model20.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = WDLModels.Model20Texture;
    }
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

        if (WReadValue(Instruction, 0, 5) == L"Object" || WReadValue(Instruction, 0, 5) == L"Script" ||
            WReadValue(Instruction, 0, 6) == L"Pickup" || WReadValue(Instruction, 0, 5) == L"Spawn" ||
            WReadValue(Instruction, 0, 3) == L"NPC" || WReadValue(Instruction, 0, 5) == L"Light" ||
            WReadValue(Instruction, 0, 3) == L"Fog" || WReadValue(Instruction, 0, 7) == L"Ambient")
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
                switch (CachedModels[i].ModelId)
                {
                case -2: DrawCubeWires({X, Y, Z}, S, S, S, RED); break;
                case -1: DrawModelEx(WDLModels.HeightMap, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 1:  DrawModelEx(WDLModels.Model1,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 2:  DrawModelEx(WDLModels.Model2,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 3:  DrawModelEx(WDLModels.Model3,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 4:  DrawModelEx(WDLModels.Model4,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 5:  DrawModelEx(WDLModels.Model5,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 6:  DrawModelEx(WDLModels.Model6,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 7:  DrawModelEx(WDLModels.Model7,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 8:  DrawModelEx(WDLModels.Model8,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 9:  DrawModelEx(WDLModels.Model9,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 10: DrawModelEx(WDLModels.Model10, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 11: DrawModelEx(WDLModels.Model11, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 12: DrawModelEx(WDLModels.Model12, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 13: DrawModelEx(WDLModels.Model13, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 14: DrawModelEx(WDLModels.Model14, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 15: DrawModelEx(WDLModels.Model15, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 16: DrawModelEx(WDLModels.Model16, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 17: DrawModelEx(WDLModels.Model17, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 18: DrawModelEx(WDLModels.Model18, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 19: DrawModelEx(WDLModels.Model19, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 20: DrawModelEx(WDLModels.Model20, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                default: break;
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
                switch (Identifier) {
                case 1:  DrawModelEx(WDLModels.Model1,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 2:  DrawModelEx(WDLModels.Model2,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 3:  DrawModelEx(WDLModels.Model3,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 4:  DrawModelEx(WDLModels.Model4,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 5:  DrawModelEx(WDLModels.Model5,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 6:  DrawModelEx(WDLModels.Model6,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 7:  DrawModelEx(WDLModels.Model7,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 8:  DrawModelEx(WDLModels.Model8,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 9:  DrawModelEx(WDLModels.Model9,  {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 10: DrawModelEx(WDLModels.Model10, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 11: DrawModelEx(WDLModels.Model11, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 12: DrawModelEx(WDLModels.Model12, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 13: DrawModelEx(WDLModels.Model13, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 14: DrawModelEx(WDLModels.Model14, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 15: DrawModelEx(WDLModels.Model15, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 16: DrawModelEx(WDLModels.Model16, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 17: DrawModelEx(WDLModels.Model17, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 18: DrawModelEx(WDLModels.Model18, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 19: DrawModelEx(WDLModels.Model19, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                case 20: DrawModelEx(WDLModels.Model20, {X, Y, Z}, {0, Rotation, 0}, Rotation, {S, S, S}, WHITE); break;
                default: break;
                }
            }

            if (Instruction == L"Collision") DrawCubeWires({X, Y, Z}, S, S, S, RED);
            if (WReadValue(Instruction, 0, 5) == L"Script") DrawCubeWires({X, Y, Z}, S, S, S, YELLOW);
            if (WReadValue(Instruction, 0, 6) == L"Pickup")  DrawCubeWires({X, Y, Z}, 0.5f, 0.5f, 0.5f, GREEN);
            if (WReadValue(Instruction, 0, 5) == L"Spawn")  DrawCubeWires({X, Y, Z}, 1.0f, 0.2f, 1.0f, BLUE);
            if (WReadValue(Instruction, 0, 3) == L"NPC")    DrawCubeWires({X, Y, Z}, 1.0f, 2.0f, 1.0f, MAGENTA);
            if (WReadValue(Instruction, 0, 5) == L"Light")  DrawSphereWires({X, Y, Z}, 1.0f, 8, 8, YELLOW);
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
bool OverlayEnabled = false;
bool CollisionToggle = false;

// --- Helper: draw a WDL instruction line for current placed item ---
static wstring BuildWDLPlaceCommand(const wstring& prefix, int subId) {
    wstring cmd = prefix;
    if (subId >= 0) cmd += to_wstring(subId);
    cmd += L":" + to_wstring(OmegaTechEditor.X) + L":" + to_wstring(OmegaTechEditor.Y) + L":" +
           to_wstring(OmegaTechEditor.Z) + L":" + to_wstring(OmegaTechEditor.S) + L":" + to_wstring(OmegaTechEditor.R) + L":";
    return cmd;
}
