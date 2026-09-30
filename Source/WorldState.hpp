#pragma once

// Shared world/scene transition state. These globals drive LoadWorld()
// (scene switch, saved-camera restore) and the toggle-flag save/load path.
// They used to live in the legacy Parasite script data header.

#include "raylib.h"

bool SetSceneFlag = false;
int SetSceneId = 0;

bool SetCameraFlag = false;
Vector3 SetCameraPos = {0, 0, 0};

#define MaxTFlag 100
struct Flags
{
    bool Value;
};
static Flags ToggleFlags[MaxTFlag];
