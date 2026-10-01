#pragma once
#include "raylib.h"
#include "../../Physics/PhysicsInfo.hpp"
#include <functional>

// PlayerController — AngelPlayer input/mode state that used to live inline in
// Main.cpp: overlay key toggles (K/Tab), the fixed-step accumulator, the
// Jump/Fly/Noclip/Water vertical integrator, and the game-over overlay.
//
// It owns no engine globals; UI visibility flags are passed by reference and
// the world-reload + return-to-menu actions are injected by the host.
class PlayerController {
public:
    static PlayerController& Instance() {
        static PlayerController instance;
        return instance;
    }

    // K toggles the skill tree, Tab toggles the inventory (mutually exclusive,
    // both ignored while the console is open). Handles the cursor state.
    void HandleKeyToggles(bool& showInventory, bool& showSkillTree, bool consoleOpen);

    // Fixed 60 Hz timestep: advance the accumulator and return how many sim
    // steps to run this frame (at most `maxSteps`).
    int ConsumeMoveSteps(float frameTime, int maxSteps = 4);
    static constexpr float MoveDeltaSeconds() { return 1.0f / 60.0f; }

    // Jump / Fly / Noclip / Water vertical integration for one fixed step.
    // Gravity/water/ladder constants come from the active zone's PhysicsInfo.
    void UpdateVertical(float dt, Camera3D& cam, float savedCamY, bool uiBlocked,
                        const oz::physics::PhysicsInfo& phys);

    // Play the jump one-shot (called by UpdateVertical when a jump is detected).
    void PlayJump();

    // Draws the "3 deaths" game-over overlay. Returns true when the overlay is
    // shown (caller should EndDrawing + continue). `restartWorld` re-loads the
    // world on Restart.
    bool DrawGameOver(int& deaths,
                      Texture2D btnClicked, Texture2D btnHover, Texture2D btnNormal,
                      bool& returnToMenu,
                      const std::function<void()>& restartWorld);

private:
    PlayerController() = default;
    double m_moveAccumulator = 0.0;
};
