#include "PlayerController.hpp"
#include "../../Physics/PlayerPhysics.hpp"
#include "../PlayerMovement.hpp"
#include "../../Audio/SoundManager.hpp"
#include <cmath>

// Reads the movement-state global defined once in the client TU (Main).
extern PlayerMovement g_playerMovement;

namespace pphys = oz::physics;

// ---------------------------------------------------------------------------
// Overlay key toggles (K = skill tree, Tab = inventory)
// ---------------------------------------------------------------------------
void PlayerController::HandleKeyToggles(bool& showInventory, bool& showSkillTree, bool consoleOpen) {
    // K key toggles the ethereal (angelic) skill tree
    if (IsKeyPressed(KEY_K)) {
        if (!consoleOpen) {
            showSkillTree = !showSkillTree;
            if (showSkillTree) {
                showInventory = false;
                ShowCursor();
                EnableCursor();
            } else {
                HideCursor();
                DisableCursor();
            }
        }
    }

    // Tab key toggles inventory
    if (IsKeyPressed(KEY_TAB)) {
        if (!consoleOpen) {
            showInventory = !showInventory;
            if (showInventory) {
                showSkillTree = false;
                ShowCursor();
                EnableCursor();
            } else {
                HideCursor();
                DisableCursor();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Fixed 60 Hz timestep accumulator
// ---------------------------------------------------------------------------
int PlayerController::ConsumeMoveSteps(float frameTime, int maxSteps) {
    static constexpr double kMoveTickS = 1.0 / 60.0;
    m_moveAccumulator += frameTime;
    int steps = 0;
    while (m_moveAccumulator >= kMoveTickS && steps < maxSteps) {
        m_moveAccumulator -= kMoveTickS;
        ++steps;
    }
    if (steps >= maxSteps && m_moveAccumulator >= kMoveTickS)
        m_moveAccumulator = 0.0; // drift guard
    return steps;
}

// ---------------------------------------------------------------------------
// Jump / Fly / Noclip / Water vertical integration (one fixed step)
// ---------------------------------------------------------------------------
void PlayerController::UpdateVertical(float dt, Camera3D& cam, float savedCamY, bool uiBlocked,
                                      const oz::physics::PhysicsInfo& phys) {
    pphys::PlayerPhysics physics;
    pphys::PlayerPhysics::Motion motion;
    motion.onGround = g_playerMovement.onGround;
    motion.velocityY = g_playerMovement.velocityY;

    const bool wasOnGround = motion.onGround;

    if (g_playerMovement.isNoClip || g_playerMovement.isFlying) {
        // Noclip / flying: direct vertical control (Space up / Ctrl down)
        physics.UpdateFlyVertical(cam, g_playerMovement.BaseSpeed, phys, dt, true, true);
    } else if (g_playerMovement.isClimbing) {
        // Ladder: W/S climb at ladderSpeed, no gravity. Checked before water so
        // a ladder volume inside a pool still behaves as a ladder.
        physics.UpdateLadderVertical(cam, motion, savedCamY, uiBlocked, phys, dt);
    } else if (g_playerMovement.inWater) {
        // Water: restore Y, reduced gravity, dampen fall
        physics.UpdateWaterVertical(cam, motion, savedCamY, g_playerMovement.EyeHeight,
                                    uiBlocked, phys, dt);
    } else {
        // Normal / grounded: restore Y, jump impulse, gravity
        physics.UpdateGroundVertical(cam, motion, savedCamY, uiBlocked, phys, dt);
    }

    if (wasOnGround && !motion.onGround && motion.velocityY > 0.0f)
        PlayJump();

    g_playerMovement.onGround = motion.onGround;
    g_playerMovement.velocityY = motion.velocityY;
}

void PlayerController::PlayJump() {
    SoundManager::Instance().PlayJump();
}

// ---------------------------------------------------------------------------
// Game-over overlay (after 3 deaths)
// ---------------------------------------------------------------------------
bool PlayerController::DrawGameOver(int& deaths,
                                    Texture2D btnClicked, Texture2D btnHover, Texture2D btnNormal,
                                    bool& returnToMenu,
                                    const std::function<void()>& restartWorld) {
    if (deaths < 3) return false;

    int sw = GetScreenWidth(), sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 200});

    const char* gameOverText = "GAME OVER";
    int fontSize = 48;
    int textW = MeasureText(gameOverText, fontSize);
    DrawText(gameOverText, (sw - textW) / 2, sh / 2 - 100, fontSize, RED);

    DrawText("You have perished three times...",
             (sw - MeasureText("You have perished three times...", 16)) / 2,
             sh / 2 - 40, 16, LIGHTGRAY);

    const char* labels[] = {"Restart", "Main Menu"};
    int btnCount = 2;
    int btnW = 220, btnH = 50, gap = 10;
    int totalH = btnCount * btnH + (btnCount - 1) * gap;
    int startY = sh / 2 + 10;

    for (int i = 0; i < btnCount; i++) {
        int bx = (sw - btnW) / 2;
        int by = startY + i * (btnH + gap);
        Rectangle r = {(float)bx, (float)by, (float)btnW, (float)btnH};
        bool hover = CheckCollisionPointRec(GetMousePosition(), r);
        bool clicked = hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

        Texture2D tex = clicked ? btnClicked : hover ? btnHover : btnNormal;
        if (tex.id > 0) {
            DrawTexturePro(tex,
                (Rectangle){0, 0, (float)tex.width, (float)tex.height},
                r, (Vector2){0, 0}, 0, WHITE);
        } else {
            DrawRectangleRec(r, (Color){80, 20, 20, 220});
            DrawRectangleLinesEx(r, 2, (Color){180, 60, 60, 255});
        }
        DrawText(labels[i], bx + (btnW - MeasureText(labels[i], 18)) / 2,
                 by + (btnH - 18) / 2, 18, WHITE);

        if (clicked) {
            if (i == 0) {
                deaths = 1;
                ShowCursor();
                EnableCursor();
                if (restartWorld) restartWorld();
                HideCursor();
                DisableCursor();
            } else if (i == 1) {
                // Main Menu. The gameplay loop calls HideCursor/DisableCursor
                // every frame and PlayHomeScreen otherwise inherits that state,
                // so the cursor stays invisible and clicks do nothing. Show it
                // here, then let g_returnToMenu fall out of the game loop and
                // enter the home screen with the cursor already visible.
                returnToMenu = true;
                ShowCursor();
                EnableCursor();
            }
        }
    }
    return true;
}
