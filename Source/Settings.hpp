#pragma once
#include "Renderer/raygui/raygui.h"
#include  "Renderer/raygui/dark.h"
#include "Log.hpp"
#include <cstddef>

static bool ShowSettings = false;
static bool Debug = false;
static bool HeadBob = true;
static bool PixelShader = false;
static bool ParticlesEnabled = true;
static bool FPSEnabled = false;
static bool ShowLogWindow = false;
static float ResolutionScale = 1.0f;

// Persisted window size (loaded before InitWindow from System/Angels95.ini).
static int ConfigWindowWidth = 1280;
static int ConfigWindowHeight = 720;

// --- New graphics settings ---
static int   TextureFilterMode = 1;      // 0=Point, 1=Bilinear, 2=Trilinear, 3=Aniso4x, 4=Aniso8x, 5=Aniso16x
static float PixelSize = 5.0f;           // pixelation block size (pixels)
static bool  JitterEnabled = false;      // vertex / screen jitter toggle
static float JitterIntensity = 1.0f;     // jitter strength
static bool  FogEnabled = false;         // fog post-process toggle
static float FogIntensity = 0.3f;        // fog blend amount
static Color FogTint = {200, 200, 210, 255}; // fog color (R,G,B,A)

static RenderTexture2D Target;

static const char* FilterNames[] = { "Point (Nearest)", "Bilinear", "Trilinear", "Aniso x4", "Aniso x8", "Aniso x16" };

// Apply the current TextureFilterMode to a loaded texture
static inline void ApplyTextureFilter(Texture2D tex) {
    if (tex.id == 0) return;
    int fm = TEXTURE_FILTER_BILINEAR;
    switch (TextureFilterMode) {
        case 0: fm = TEXTURE_FILTER_POINT; break;
        case 1: fm = TEXTURE_FILTER_BILINEAR; break;
        case 2: GenTextureMipmaps(&tex); fm = TEXTURE_FILTER_TRILINEAR; break;
        case 3: GenTextureMipmaps(&tex); fm = TEXTURE_FILTER_ANISOTROPIC_4X; break;
        case 4: GenTextureMipmaps(&tex); fm = TEXTURE_FILTER_ANISOTROPIC_8X; break;
        case 5: GenTextureMipmaps(&tex); fm = TEXTURE_FILTER_ANISOTROPIC_16X; break;
    }
    SetTextureFilter(tex, fm);
}

bool MenuSettings = false;

bool Spinner003EditMode = false;
int Spinner003Value = 0;
bool Spinner004EditMode = false;
int Spinner004Value = 0;

bool VSYNCToggle = false;
bool MXAAToggle = false;

bool MuteToggle = false;
float AudioSlider = 100.0f; // persisted as 0..100 in [Settings] audio_volume

// raylib/miniaudio expects the master volume as a linear 0..1 factor; values
// above 1.0 amplify and clip (miniaudio does NOT clamp). AudioSlider is stored
// 0..100, so convert here and let Mute always win. Single source of truth for
// applying the persisted volume.
static inline void ApplyMasterVolume() {
    float v = AudioSlider < 0.0f ? 0.0f : (AudioSlider > 100.0f ? 100.0f : AudioSlider);
    SetMasterVolume(MuteToggle ? 0.0f : v / 100.0f);
}

