#include "raylib.h"

class Player{
    public:
        float Height = 10.0f;
        float Width = 2.0f;

        int HeadBob = 0;
        int HeadBobDirection = 1;

        float Health = 100.0f;
        float MaxHealth = 100.0f;
        float Mana = 0.0f;
        float MaxMana = 100.0f;
        float PsychicEnergy = 0.0f;
        float MaxPsychicEnergy = 100.0f;
        int Level = 1;
        int XP = 0;
        int XPToNext = 100;

        float OldX = 0.0f;
        float OldY = 0.0f;
        float OldZ = 0.0f;

        // Jump / fly / noclip
        float velocityY = 0.0f;
        bool onGround = false;
        bool isFlying = false;
        bool isNoClip = false;

        // Zone state
        bool inWater = false;

        BoundingBox PlayerBounds;
    };

static Player OmegaPlayer;