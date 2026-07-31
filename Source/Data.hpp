#include "WindowsCompat.hpp"
#include "raylib.h"
#include "Settings.hpp"
#include "Pawn/PlayerMovement.hpp"
#include "Pawn/Player.hpp"
#include "PPGIO.hpp"
#include "Pawn/Items.hpp"
#include "Editor.hpp"
#include "Renderer/Video.hpp"
#include "ParticleDemon/ParticleDemon.hpp"
#include "Parasite/ParasiteScript.hpp"
#include "Encoder/Encoder.hpp"
// Entities.hpp removed

static PlayerMovement g_playerMovement;

#include <string>
#include <iostream>
#include <fstream>
#include <chrono>
#include <utility>

using namespace std;

#define MaxCachedModels 200

static wstring WorldData;
static wstring OtherWDLData;


static int R = 0;
static int G = 0;
static int B = 0;

int Direction = 1;
bool FadeDone = false;
Color FadeColor = (Color){R, G, B, 255};

void PlayFade()
{
    Direction = 1;
    R = 0;
    G = 0;
    B = 0;
    FadeDone = false;
}   



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

        static const int MAX_WDL_MODELS = 20;
        Model wdlModels[MAX_WDL_MODELS + 1]{};
        Texture2D wdlModelTextures[MAX_WDL_MODELS + 1]{};

        // Weapon object models (Object1-5, loaded from EntityDef mesh paths)
        Model objectModels[5]{};
        bool objectModelsLoaded[5] = {false, false, false, false, false};
};

static GameModels WDLModels;

class GameData{
    public:
        float X;
        float Y;
        float Z;
        float R;
        float S;
        int ModelId;
        bool Collision;

        void Init(){
            X = 0;
            Y = 0;
            Z = 0;
            R = 0;
            S = 0;
            ModelId = 0;
            Collision = false;
        }

};

static int CachedModelCounter = 0;
static GameData CachedModels[MaxCachedModels];

class CollisionData{
    public:
        float X;
        float Y;
        float Z;
        float W;
        float H;
        float L;

        void Init(){
            X = 0;
            Y = 0;
            Z = 0;
            W = 0;
            H = 0;
            L = 0;
        }

};

static int CachedCollisionCounter = 0;
static CollisionData CachedCollision[MaxCachedModels];

class GameSounds
{
    public:
        Sound CollisionSound;
        Sound WalkingSound;
        Music BackgroundMusic;
        Sound UIClick;
        Sound ChasingSound;
        Sound Death;

        Music NESound1;
        Music NESound2;
        Music NESound3;

        bool MusicFound = false;
};

static GameSounds OmegaTechSoundData;
