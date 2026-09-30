#pragma once
#include "raylib.h"
#include "raymath.h"
#include <cmath>

// PlayerMovement — per-frame physics and movement state
// Stat fields (Health, Mana, PsychicEnergy, Level, XP) are in LightningEntityManager player entity
constexpr float PLAYER_EYE_HEIGHT = 2.0f;

// ---------------------------------------------------------------------------
// Custom first-person controller state.
//
// Movement used to be delegated to raylib's UpdateCamera(); it exposes no
// speed/sensitivity knobs, so look + WASD are integrated here instead. This is
// what lets us honour per-entity `movement_speed` and support sprint/crouch.
// Defaults mirror raylib: CAMERA_MOVE_SPEED (5.4) and mouse sensitivity 0.003.
// ---------------------------------------------------------------------------
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

    // --- Controller tuning ---
    float BaseSpeed = 5.4f;          // units/sec at movement_speed == 1
    float SprintMultiplier = 1.6f;   // hold Shift
    float CrouchMultiplier = 0.5f;   // hold Ctrl
    float MouseSensitivity = 0.003f; // radians per pixel
    float LookDistance = 10.0f;      // camera target distance (matches old cam)

    // --- Stance state ---
    bool isSprinting = false;
    bool isCrouching = false;
    float StandEyeHeight = PLAYER_EYE_HEIGHT;
    float CrouchEyeHeight = 1.2f;
    float EyeHeight = PLAYER_EYE_HEIGHT; // current, smoothed toward the stance

    float OldX = 0.0f, OldY = 0.0f, OldZ = 0.0f;

    BoundingBox PlayerBounds;

    // Read mouse + WASD/gamepad, apply the per-entity speed multiplier plus
    // sprint/crouch, and move the camera horizontally. Y is owned by the
    // caller's jump/gravity code. When `inputBlocked` (inventory/skill/pause/
    // console) look and move are skipped, but the stance still settles.
    void UpdateLookAndMove(Camera3D& cam, float dt, float speedScalar, bool inputBlocked) {
        if (dt <= 0.0f) dt = 1.0f / 60.0f;

        if (!inputBlocked) {
            isCrouching = IsKeyDown(KEY_LEFT_CONTROL);
            isSprinting = IsKeyDown(KEY_LEFT_SHIFT) && !isCrouching;
        } else {
            isSprinting = false;
        }

        float targetEye = isCrouching ? CrouchEyeHeight : StandEyeHeight;
        EyeHeight += (targetEye - EyeHeight) * fminf(1.0f, 10.0f * dt);

        if (inputBlocked) return;

        // --- Look: yaw/pitch derived from the live camera each frame so that
        //     portal / SetCamera / respawn teleports are respected. ---
        Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        float yaw = atan2f(fwd.x, fwd.z);
        float pitch = asinf(fmaxf(-1.0f, fminf(1.0f, fwd.y)));

        Vector2 md = GetMouseDelta();
        yaw += md.x * MouseSensitivity;
        pitch -= md.y * MouseSensitivity;

        // Gamepad right stick look (raylib's first-person camera provided this)
        float lookX = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X);
        float lookY = GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y);
        if (fabsf(lookX) > 0.15f) yaw += lookX * 2.4f * dt;
        if (fabsf(lookY) > 0.15f) pitch -= lookY * 1.8f * dt;

        if (pitch > 1.5533f) pitch = 1.5533f;   // clamp ~89 degrees
        if (pitch < -1.5533f) pitch = -1.5533f;

        // --- Move input relative to yaw ---
        float f = 0.0f, r = 0.0f;
        if (IsKeyDown(KEY_W)) f += 1.0f;
        if (IsKeyDown(KEY_S)) f -= 1.0f;
        if (IsKeyDown(KEY_D)) r += 1.0f;
        if (IsKeyDown(KEY_A)) r -= 1.0f;
        float gx = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
        float gy = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
        if (fabsf(gx) > 0.15f) r += gx;
        if (fabsf(gy) > 0.15f) f -= gy;

        float len = sqrtf(f * f + r * r);
        if (len > 1.0f) { f /= len; r /= len; }

        float speed = BaseSpeed * (speedScalar > 0.0f ? speedScalar : 1.0f);
        if (isSprinting) speed *= SprintMultiplier;
        if (isCrouching) speed *= CrouchMultiplier;

        Vector3 forward = {sinf(yaw), 0.0f, cosf(yaw)};
        Vector3 right   = {cosf(yaw), 0.0f, -sinf(yaw)};
        Vector3 move = {forward.x * f + right.x * r, 0.0f, forward.z * f + right.z * r};
        float dist = speed * dt;

        // Split into sub-steps so a sprint step cannot tunnel through a thin
        // brush AABB between the per-frame collision tests.
        int steps = (int)(dist / 0.5f) + 1;
        for (int i = 0; i < steps; i++) {
            float s = dist / (float)steps;
            cam.position.x += move.x * s;
            cam.position.z += move.z * s;
        }

        // Rebuild the look target from the final yaw/pitch.
        Vector3 dir = {cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)};
        cam.target = Vector3Add(cam.position, Vector3Scale(dir, LookDistance));
    }

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
