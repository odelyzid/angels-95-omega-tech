#pragma once
#include "raylib.h"

// PlayerMovement — per-frame physics and movement state
// Stat fields (Health, Mana, PsychicEnergy, Level, XP) are in LightningEntityManager player entity
class PlayerMovement {
public:
    float Height = 10.0f;
    float Width = 2.0f;

    int HeadBob = 0;
    int HeadBobDirection = 1;

    float velocityY = 0.0f;
    bool onGround = false;
    bool isFlying = false;
    bool isNoClip = false;
    bool inWater = false;

    float OldX = 0.0f, OldY = 0.0f, OldZ = 0.0f;

    BoundingBox PlayerBounds;

    void UpdateBounds(Camera3D& cam) {
        PlayerBounds = (BoundingBox){
            (Vector3){cam.position.x - Width / 2,
                       cam.position.y - Height,
                       cam.position.z - Width / 2},
            (Vector3){cam.position.x + Width / 2,
                       cam.position.y,
                       cam.position.z + Width / 2}};
    }
};
