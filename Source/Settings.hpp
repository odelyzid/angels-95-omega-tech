#include "raygui/raygui.h"
#include  "raygui/dark.h"
#include "Log.hpp"
#include <cstddef>

static bool ShowSettings = false;
static bool Debug = false;
static bool HeadBob = true;
static bool PixelShader = true;
static bool ParticlesEnabled = true;
static bool FPSEnabled = false;
static bool ShowLogWindow = false;
static float ResolutionScale = 1.0f;

// --- New graphics settings ---
static int   TextureFilterMode = 1;      // 0=Point, 1=Bilinear, 2=Trilinear, 3=Aniso4x, 4=Aniso8x, 5=Aniso16x
static float PixelSize = 5.0f;           // pixelation block size (pixels)
static bool  JitterEnabled = false;      // vertex / screen jitter toggle
static float JitterIntensity = 1.0f;     // jitter strength
static bool  FogEnabled = false;         // fog post-process toggle
static float FogIntensity = 0.3f;        // fog blend amount
static Color FogTint = {200, 200, 210, 255}; // fog color (R,G,B,A)

// --- Menu bar state ---
static bool ShowMenuBar = false;         // top menu bar visibility
static int  MenuActiveItem = -1;         // which dropdown is open (-1 = none)

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

void ToggleSettings(){
    if (ShowSettings){
        ShowSettings = false;
        HideCursor(); 
        DisableCursor();  
    }
    else {
        ShowSettings = true;
        ShowCursor();
        EnableCursor();
    }
}

void UpdateSettings(){
    if (ShowSettings){
        GuiWindowBox((Rectangle){ 20, 20, 360, 520 }, "Angels95 Developer Settings");
        int x = 40, y = 50, w = 100, h = 30, gap = 5;

        // Row 1 - Debug / HeadBob / PixelShader toggle
        if (GuiButton((Rectangle){ x, y, w, h }, "Toggle Debug")) Debug = !Debug;
        if (GuiButton((Rectangle){ x + w + gap, y, w, h }, "Toggle H.B.")) HeadBob = !HeadBob;
        if (GuiButton((Rectangle){ x + (w + gap) * 2, y, w, h }, "Toggle P.S.")) PixelShader = !PixelShader;

        // Row 2 - Particles / FPS / Log
        y += h + gap;
        if (GuiButton((Rectangle){ x, y, w, h }, "Particles")) ParticlesEnabled = !ParticlesEnabled;
        if (GuiButton((Rectangle){ x + w + gap, y, w, h }, "Show FPS")) FPSEnabled = !FPSEnabled;
        if (GuiButton((Rectangle){ x + (w + gap) * 2, y, w, h }, "Log")) ShowLogWindow = !ShowLogWindow;

        // Row 3 - VSync / Jitter toggle
        y += h + gap;
        if (GuiButton((Rectangle){ x, y, w, h }, "VSync .T")){
            if (IsWindowState(FLAG_VSYNC_HINT)){
                ClearWindowState(FLAG_VSYNC_HINT);
            }
            else {
                SetConfigFlags(FLAG_VSYNC_HINT);
            }
        }
        if (GuiButton((Rectangle){ x + w + gap, y, w, h }, "Jitter")) JitterEnabled = !JitterEnabled;
        if (GuiButton((Rectangle){ x + (w + gap) * 2, y, w, h }, "Fog")) FogEnabled = !FogEnabled;

        // Log window
        if (ShowLogWindow){
            GuiWindowBox((Rectangle){ 380, 20, 500, 500 }, "Log Output");
            int log_line_y = 50;
            for (const auto& ll : Log::get_buffer()) {
                if (log_line_y > 500) break;
                Color c = RAYWHITE;
                DrawText(ll.text, 390, log_line_y, 12, c);
                log_line_y += 16;
            }
        }

        // --- Texture filter group ---
        y += h + gap + 5;
        GuiGroupBox((Rectangle){ x, y, 320, 100 }, "Texture Filter");
        y += 18;
        GuiLabel((Rectangle){ x + 5, y, 100, 20 }, "Mode:");
        Rectangle filterRect = { x + 60, y, 200, 22 };
        if (GuiDropdownBox(filterRect, "Point (Nearest);Bilinear;Trilinear;Aniso x4;Aniso x8;Aniso x16", &TextureFilterMode, MenuActiveItem == 0)){
            MenuActiveItem = (MenuActiveItem == 0) ? -1 : 0;
        }

        // --- Pixelation group ---
        y += 30;
        GuiGroupBox((Rectangle){ x, y, 320, 70 }, "Pixelation");
        y += 18;
        GuiLabel((Rectangle){ x + 5, y, 50, 20 }, "Size:");
        PixelSize = GuiSlider((Rectangle){ x + 55, y, 200, 20 }, "1", "32", PixelSize, 1.0f, 32.0f);

        // --- Jitter group ---
        y += 28;
        GuiGroupBox((Rectangle){ x, y, 320, 70 }, "Vertex Jitter");
        y += 18;
        GuiLabel((Rectangle){ x + 5, y, 50, 20 }, "Intensity:");
        JitterIntensity = GuiSlider((Rectangle){ x + 65, y, 195, 20 }, "0", "10", JitterIntensity, 0.0f, 10.0f);

        // --- Resolution scale group ---
        y += 30;
        GuiGroupBox((Rectangle){ x, y, 320, 80 }, "Resolution Scale");
        y += 18;
        Rectangle sliderRect = { x + 5, y, 250, 20 };
        ResolutionScale = GuiSlider(sliderRect, "Scale", TextFormat("%.2f", ResolutionScale), ResolutionScale, 0.0f, 3.0f);
        y += 28;
        if (GuiButton((Rectangle){ x + 5, y, 120, 22 }, "Set Resolution")){
            UnloadRenderTexture(Target);
            Target = LoadRenderTexture(int(1280 * ResolutionScale), int(720 * ResolutionScale));
        }

        // --- Window size group ---
        y += 30;
        GuiGroupBox((Rectangle){ x, y, 320, 50 }, "Window Size");
        y += 18;
        int bw = 55, bgap = 5;
        if (GuiButton((Rectangle){ x + 5, y, bw, 24 }, "480p")) SetWindowSize(640, 480);
        if (GuiButton((Rectangle){ x + 10 + bw, y, bw, 24 }, "720p")) SetWindowSize(1280, 720);
        if (GuiButton((Rectangle){ x + 15 + bw*2, y, bw, 24 }, "1080p")) SetWindowSize(1980, 1080);
        if (GuiButton((Rectangle){ x + 20 + bw*3, y, bw, 24 }, "1440p")) SetWindowSize(2560, 1440);
        if (GuiButton((Rectangle){ x + 25 + bw*4, y, bw, 24 }, "4k")) SetWindowSize(3840, 2160);
    }
}

bool MenuSettings = false;

bool Spinner003EditMode = false;
int Spinner003Value = 0;
bool Spinner004EditMode = false;
int Spinner004Value = 0;

bool VSYNCToggle = false;
bool MXAAToggle = false;

bool MuteToggle = false;
float AudioSlider = 100.0f;

void ShowMenuSetiings(){
    Rectangle LayoutRecs[15] = {
        (Rectangle){ 72, 85, 120, 24 },
        (Rectangle){ 62, 115, 391, 419 },
        (Rectangle){ 71, 145, 374, 200 },
        (Rectangle){ 94, 186, 120, 24 },
        (Rectangle){ 172, 237, 120, 24 },
        (Rectangle){ 171, 278, 120, 24 },
        (Rectangle){ 311, 258, 120, 24 },
        (Rectangle){ 325, 176, 88, 24 },
        (Rectangle){ 325, 214, 88, 24 },
        (Rectangle){ 336, 399, 88, 24 },
        (Rectangle){ 182, 407, 120, 16 },
        (Rectangle){ 72, 361, 374, 85 },
        (Rectangle){ 73, 452, 120, 24 },
        (Rectangle){ 74, 486, 146, 26 },
        (Rectangle){ 268, 475, 140, 28 },
    };
    GuiWindowBox(LayoutRecs[1], "Settings");
    GuiLabel(LayoutRecs[0], "Angels95 Settings");
    GuiPanel(LayoutRecs[2], "Window");
    if (GuiButton(LayoutRecs[3], "Toggle Fullscreen")) ToggleFullscreen(); 
    VSYNCToggle = GuiToggle(LayoutRecs[7], "VSync", VSYNCToggle);
    MXAAToggle = GuiToggle(LayoutRecs[8], "MXAA x4", MXAAToggle);

    GuiPanel(LayoutRecs[11], "Audio");
    MuteToggle = GuiToggle(LayoutRecs[9], "Mute Audio", MuteToggle);
    AudioSlider = GuiSlider(LayoutRecs[10], "Audio Volume", NULL, AudioSlider, 0, 100);

    GuiLabel(LayoutRecs[12], "ANGELS95 ");
    GuiLabel(LayoutRecs[13], "TribeWarez 2025");
    GuiLabel(LayoutRecs[14], "@EC, JF , ZT , NC , LC");

    if (VSYNCToggle){
        SetConfigFlags(FLAG_VSYNC_HINT);
    }
    else {
        ClearWindowState(FLAG_VSYNC_HINT);
    }

    if (MXAAToggle){
        SetConfigFlags(FLAG_MSAA_4X_HINT);
    }
    else {
        ClearWindowState(FLAG_MSAA_4X_HINT);
    }
    SetMasterVolume(AudioSlider);
    if (MuteToggle){
        SetMasterVolume(0);
    }
    
}
