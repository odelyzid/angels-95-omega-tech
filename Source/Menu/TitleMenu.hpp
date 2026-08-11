#pragma once
#include "raylib.h"
#include "../raygui/raygui.h"
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

static const Color TEAM_COLORS[7] = {
    {200, 40, 40, 255},   {40, 100, 220, 255},  {40, 180, 60, 255},
    {200, 170, 30, 255},  {60, 180, 200, 255},  {160, 50, 200, 255},
    {220, 130, 30, 255}
};
static const char* TEAM_COLOR_NAMES[7] = {"Red","Blue","Green","Gold","Azure","Purple","Orange"};

struct WorldEntry { std::string name; std::string dirName; };

struct TitleMenuTextures {
    Texture2D titleBg{0};
    Texture2D fTL{0}, fT{0}, fTR{0};
    Texture2D fL{0}, fArea{0}, fR{0};
    Texture2D fBL{0}, fB{0}, fBR{0};
    Texture2D barL{0}, barTile{0}, barWin{0}, barMax{0};
    Texture2D menuLine{0}, clientArea{0};
    Texture2D btnNormal{0}, btnHover{0}, btnClicked{0};
    bool loaded = false;

    void Load() {
        if (loaded) return;
        titleBg = LoadTextureWithFallback("GameData/Global/Title/Title.png");
        fTL = LoadTexture("GameData/Global/Menu/BlueMenuTL.png");
        fT  = LoadTexture("GameData/Global/Menu/BlueMenuT.png");
        fTR = LoadTexture("GameData/Global/Menu/BlueMenuTR.png");
        fL  = LoadTexture("GameData/Global/Menu/BlueMenuL.png");
        fArea = LoadTexture("GameData/Global/Menu/BlueMenuArea.png");
        fR  = LoadTexture("GameData/Global/Menu/BlueMenuR.png");
        fBL = LoadTexture("GameData/Global/Menu/BlueMenuBL.png");
        fB  = LoadTexture("GameData/Global/Menu/BlueMenuB.png");
        fBR = LoadTexture("GameData/Global/Menu/BlueMenuBR.png");
        barL    = LoadTexture("GameData/Global/Menu/BlueBarL.png");
        barTile = LoadTexture("GameData/Global/Menu/BlueBarTile.png");
        barWin  = LoadTexture("GameData/Global/Menu/BlueBarWin.png");
        barMax  = LoadTexture("GameData/Global/Menu/BlueBarMax.png");
        menuLine = LoadTexture("GameData/Global/Menu/BlueMenuLine.png");
        clientArea = LoadTexture("GameData/Global/Menu/BlueClientArea.png");
        btnNormal  = LoadTexture("GameData/Global/Title/menu_button.png");
        btnHover   = LoadTexture("GameData/Global/Title/menu_button_hover.png");
        btnClicked = LoadTexture("GameData/Global/Title/menu_button_clicked.png");
        loaded = true;
    }

    void Unload() {
        if (!loaded) return;
        auto u = [](Texture2D& t) { if (t.id) UnloadTexture(t); t = Texture2D{0}; };
        u(titleBg); u(fTL); u(fT); u(fTR); u(fL); u(fArea); u(fR); u(fBL); u(fB); u(fBR);
        u(barL); u(barTile); u(barWin); u(barMax);
        u(menuLine); u(clientArea); u(btnNormal); u(btnHover); u(btnClicked);
        loaded = false;
    }
};

static TitleMenuTextures& GetMenuTex() { static TitleMenuTextures tex; tex.Load(); return tex; }

static std::vector<WorldEntry> ScanWorlds() {
    std::vector<WorldEntry> worlds;
    const char* base = "GameData/Worlds";
    if (!fs::exists(base)) return worlds;
    for (auto& entry : fs::directory_iterator(base)) {
        if (!entry.is_directory()) continue;
        std::string dirName = entry.path().filename().string();
        std::string wdl = entry.path().string() + "/World.wdl";
        std::string oz  = entry.path().string() + "/World.ozone";
        if (IsPathFile(wdl.c_str()) || IsPathFile(oz.c_str())) {
            worlds.push_back({dirName, dirName});
        }
    }
    return worlds;
}

enum PaneType : int {
    PANE_CAMPAIGN = 0,
    PANE_PRACTICE,
    PANE_SERAPHIC,
    PANE_STATS,
    PANE_SETTINGS,
    PANE_CHARACTER,
    PANE_HELP,
    PANE_MULTIPLAYER
};

struct Pane {
    Rectangle rect;
    PaneType type;
    bool dragging = false;
    Vector2 dragOffset{0};
};

class TitleMenu {
public:
    TitleMenu() {
        GetMenuTex();
        m_worlds = ScanWorlds();
        snprintf(m_charName, sizeof(m_charName), "%s", "Player");
    }

    bool Tick() {
        if (WindowShouldClose()) { m_exit = true; return true; }

        UpdateMusicStream(OmegaTechData.HomeScreenMusic);

        m_sw = GetScreenWidth();
        m_sh = GetScreenHeight();
        Vector2 mp = GetMousePosition();

        UpdateLayout();
        UpdateInput(mp);

        BeginTextureMode(Target);
        ClearBackground(BLACK);

        DrawBackground();
        Draw9SliceFrame();
        DrawTitleBar();
        DrawSeparator(m_titleBar);
        DrawMenuBar();
        DrawSeparator(m_menuBar);
        DrawContent(m_contentArea);
        DrawSeparator(m_helpBar);
        DrawHelpBar();

        if (m_dropdownOpen && m_openTab >= 0)
            DrawDropdown(m_openTab);

        EndTextureMode();

        BeginDrawing();
        DrawTexturePro(Target.texture,
            (Rectangle){0, 0, Target.texture.width, -Target.texture.height},
            (Rectangle){0, 0, m_sw, m_sh},
            (Vector2){0, 0}, 0.f, WHITE);
        EndDrawing();

        return m_exit;
    }

    bool ShouldExit() const { return m_exit; }
    bool ShouldLoadGame() const { return m_loadGame; }
    bool ShouldJoinServer() const { return m_joinServer; }
    bool ShouldStartServer() const { return m_startServer; }
    const char* GetSelectedWorld() const { return m_selectedWorld.c_str(); }
    const char* GetJoinIP() const { return m_joinIP; }

private:
    int m_sw = 1280, m_sh = 720;
    static constexpr int BORDER = 4, TITLE_H = 24, MENU_H = 24, HELP_H = 22;
    static constexpr int SEP_H = 2, CELL_W = 160, CELL_H = 48, CELL_GAP = 8, FRAME_M = 8;

    Rectangle m_frameRect, m_innerRect, m_titleBar, m_menuBar, m_contentArea, m_helpBar;

    static constexpr int TAB_COUNT = 8;
    static const char* TAB_NAMES[TAB_COUNT];
    Rectangle m_tabRects[TAB_COUNT];
    int m_hoveredTab = -1, m_openTab = -1, m_hoveredItem = -1;
    bool m_dropdownOpen = false;
    Rectangle m_dropdownRect{0};
    std::vector<Rectangle> m_itemRects;

    bool m_exit = false, m_loadGame = false, m_joinServer = false, m_startServer = false;
    char m_joinIP[32] = "127.0.0.1";
    std::string m_selectedWorld;
    std::vector<WorldEntry> m_worlds;

    char m_charName[64];
    int m_teamColor = 0, m_gameTypeFilter = 0, m_settingsSubPage = 0;
    char m_joinIPBuffer[64] = "127.0.0.1";
    char m_hostPortBuffer[16] = "27015";
    std::string m_selectedServerWorld;
    int m_multiplayerSubPage = 0; // 0=Join, 1=Host

    Rectangle m_minBtn, m_maxBtn, m_closeBtn;
    bool m_dragging = false;
    bool m_dragPane = false;
    int m_dragPaneIdx = -1;
    Vector2 m_dragStart{0};

    // Pane system
    std::vector<Pane> m_panes;
    int m_cascadeX = 40, m_cascadeY = 40;
    static constexpr int PANE_TITLE_H = 20;
    static constexpr int PANE_BORDER = 4;

    void UpdateLayout() {
        m_frameRect = {(float)FRAME_M, (float)FRAME_M, (float)(m_sw - FRAME_M*2), (float)(m_sh - FRAME_M*2)};
        m_innerRect = {m_frameRect.x + BORDER, m_frameRect.y + BORDER, m_frameRect.width - BORDER*2, m_frameRect.height - BORDER*2};

        float iy = m_innerRect.y;
        m_titleBar = {m_innerRect.x, iy, m_innerRect.width, TITLE_H}; iy += TITLE_H;
        m_menuBar = {m_innerRect.x, iy + SEP_H, m_innerRect.width, MENU_H}; iy += SEP_H + MENU_H;
        m_contentArea = {m_innerRect.x, iy + SEP_H, m_innerRect.width, m_innerRect.height - TITLE_H - SEP_H - MENU_H - SEP_H - SEP_H - HELP_H};
        iy += SEP_H + m_contentArea.height;
        m_helpBar = {m_innerRect.x, iy + SEP_H, m_innerRect.width, HELP_H};

        float bx = m_titleBar.x + m_titleBar.width - 16;
        m_closeBtn = {bx, m_titleBar.y, 16, 16}; bx -= 18;
        m_maxBtn = {bx, m_titleBar.y, 16, 16}; bx -= 18;
        m_minBtn = {bx, m_titleBar.y, 16, 16};

        float tabW = m_menuBar.width / TAB_COUNT;
        for (int i = 0; i < TAB_COUNT; i++)
            m_tabRects[i] = {m_menuBar.x + tabW * i, m_menuBar.y, tabW, m_menuBar.height};
    }

    void UpdateInput(Vector2 mp) {
        m_hoveredTab = -1;
        for (int i = 0; i < TAB_COUNT; i++)
            if (CheckCollisionPointRec(mp, m_tabRects[i])) { m_hoveredTab = i; break; }

        m_hoveredItem = -1;

        // Main window chrome buttons
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            if (CheckCollisionPointRec(mp, m_closeBtn)) { PostMessage(GetActiveWindow(), WM_CLOSE, 0, 0); return; }
            if (CheckCollisionPointRec(mp, m_maxBtn)) { ToggleFullscreen(); return; }
            if (CheckCollisionPointRec(mp, m_minBtn)) { ShowWindow(GetActiveWindow(), SW_MINIMIZE); return; }
            if (CheckCollisionPointRec(mp, m_titleBar)) { m_dragging = true; m_dragStart = mp; return; }
        }
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) { m_dragging = false; m_dragPane = false; }
        if (m_dragging) {
            Vector2 d = {mp.x - m_dragStart.x, mp.y - m_dragStart.y};
            if (d.x != 0 || d.y != 0) {
                RECT r; GetWindowRect(GetActiveWindow(), &r);
                SetWindowPos(GetActiveWindow(), 0, r.left + (int)d.x, r.top + (int)d.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
                m_dragStart = mp;
            }
            return;
        }

        // Pane dragging
        if (m_dragPane && m_dragPaneIdx >= 0 && m_dragPaneIdx < (int)m_panes.size()) {
            m_panes[m_dragPaneIdx].rect.x = mp.x - m_panes[m_dragPaneIdx].dragOffset.x;
            m_panes[m_dragPaneIdx].rect.y = mp.y - m_panes[m_dragPaneIdx].dragOffset.y;
            return;
        }

        // Dropdown tab clicks
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            if (m_hoveredTab >= 0) {
                PlaySound(OmegaTechSoundData.UIClick);
                if (m_openTab == m_hoveredTab && m_dropdownOpen) { m_dropdownOpen = false; m_openTab = -1; }
                else { m_openTab = m_hoveredTab; m_dropdownOpen = true; BuildDropdownItems(); }
                return;
            }
        }

        // Dropdown menu clicks
        if (m_dropdownOpen && m_openTab >= 0) {
            if (CheckCollisionPointRec(mp, m_dropdownRect)) {
                for (size_t i = 0; i < m_itemRects.size(); i++)
                    if (CheckCollisionPointRec(mp, m_itemRects[i])) { m_hoveredItem = (int)i; break; }
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && m_hoveredItem >= 0) {
                    PlaySound(OmegaTechSoundData.UIClick);
                    HandleDropdownClick(m_openTab, m_hoveredItem);
                    return;
                }
            } else if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { m_dropdownOpen = false; m_openTab = -1; }
        }

        // Pane input handling (front to back)
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
            HandlePaneInput(mp);
    }

    void BuildDropdownItems() {
        m_itemRects.clear();
        if (m_openTab < 0) return;
        float x = m_tabRects[m_openTab].x;
        float y = m_tabRects[m_openTab].y + m_tabRects[m_openTab].height;
        int count = GetDropdownItemCount(m_openTab);
        m_dropdownRect = {x, y, 180, count * 26.0f + 4};
        for (int i = 0; i < count; i++)
            m_itemRects.push_back({x + 2, y + 2 + i * 26.0f, 176, 24});
    }

    int GetDropdownItemCount(int tab) {
        switch (tab) {
            case 0: return 2; case 1: return 3; case 4: return 3;
            case 2: case 3: case 5: case 6: case 7: return 1;
            default: return 0;
        }
    }

    const char* GetDropdownItemName(int tab, int item) {
        switch (tab) {
            case 0: return item == 0 ? "Start New Campaign" : "Load Campaign Game";
            case 1: return item == 0 ? "Start New Game" : (item == 1 ? "Load Game" : "Quick Match");
            case 2: return "Enter Seraphic Realm";
            case 3: return "View Statistics";
            case 4: return item == 0 ? "Video Settings" : (item == 1 ? "Audio Settings" : "Controls");
            case 5: return "Configure Character";
            case 6: return "About Angels95";
            case 7: return "Multiplayer Lobby";
            default: return "";
        }
    }

    static const char* GetPaneTitle(PaneType t) {
        switch (t) {
            case PANE_CAMPAIGN:  return "Campaign Missions";
            case PANE_PRACTICE:  return "Practice Session";
            case PANE_SERAPHIC:  return "Seraphic Realm";
            case PANE_STATS:     return "Player Statistics";
            case PANE_SETTINGS:  return "Settings";
            case PANE_CHARACTER: return "Character Configuration";
            case PANE_HELP:      return "About Angels95";
            case PANE_MULTIPLAYER: return "Multiplayer Lobby";
            default:             return "";
        }
    }

    void OpenPane(PaneType type) {
        // If a pane of this type already exists, bring it to front
        for (size_t i = 0; i < m_panes.size(); i++) {
            if (m_panes[i].type == type) {
                BringPaneToFront((int)i);
                return;
            }
        }
        int pw, ph;
        GetPaneSize(type, pw, ph);
        Rectangle r;
        r.width = (float)pw;
        r.height = (float)ph;
        r.x = m_contentArea.x + m_cascadeX;
        r.y = m_contentArea.y + m_cascadeY;
        // Clamp inside content area
        if (r.x + r.width > m_contentArea.x + m_contentArea.width - 10)
            r.x = m_contentArea.x + 20;
        if (r.y + r.height > m_contentArea.y + m_contentArea.height - 10)
            r.y = m_contentArea.y + 20;
        // Cascade position for next pane
        m_cascadeX += 24;
        m_cascadeY += 24;
        if (m_cascadeX + pw > (int)m_contentArea.width - 60) m_cascadeX = 40;
        if (m_cascadeY + ph > (int)m_contentArea.height - 60) m_cascadeY = 40;
        // For settings pane, reset sub-page
        if (type == PANE_SETTINGS) m_settingsSubPage = 0;
        m_panes.push_back({r, type, false, {0, 0}});
    }

    void GetPaneSize(int type, int& w, int& h) {
        switch (type) {
            case PANE_CAMPAIGN:  w = 640; h = 380; break;
            case PANE_PRACTICE:  w = 640; h = 380; break;
            case PANE_SERAPHIC:  w = 460; h = 260; break;
            case PANE_STATS:     w = 360; h = 240; break;
            case PANE_SETTINGS:  w = 480; h = 340; break;
            case PANE_CHARACTER: w = 440; h = 260; break;
            case PANE_HELP:      w = 520; h = 360; break;
            case PANE_MULTIPLAYER: w = 480; h = 340; break;
            default:             w = 400; h = 300; break;
        }
    }

    void BringPaneToFront(int idx) {
        if (idx < 0 || idx >= (int)m_panes.size()) return;
        Pane p = m_panes[idx];
        m_panes.erase(m_panes.begin() + idx);
        m_panes.push_back(p);
    }

    int GetPaneAtPoint(Vector2 mp) {
        for (int i = (int)m_panes.size() - 1; i >= 0; i--)
            if (CheckCollisionPointRec(mp, m_panes[i].rect))
                return i;
        return -1;
    }

    Rectangle GetPaneTitleBarRect(const Pane& p) {
        return {p.rect.x + PANE_BORDER, p.rect.y + PANE_BORDER, p.rect.width - PANE_BORDER * 2, PANE_TITLE_H};
    }

    Rectangle GetPaneCloseBtnRect(const Pane& p) {
        Rectangle tb = GetPaneTitleBarRect(p);
        return {tb.x + tb.width - 16, tb.y, 14, tb.height};
    }

    Rectangle GetPaneContentRect(const Pane& p) {
        Rectangle tb = GetPaneTitleBarRect(p);
        return {
            p.rect.x + PANE_BORDER + 2,
            tb.y + tb.height + 2,
            p.rect.width - PANE_BORDER * 2 - 4,
            p.rect.height - PANE_BORDER - tb.height - 6
        };
    }

    void HandlePaneInput(Vector2 mp) {
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            // Check close buttons first (front to back)
            for (int i = (int)m_panes.size() - 1; i >= 0; i--) {
                Rectangle closeBtn = GetPaneCloseBtnRect(m_panes[i]);
                if (CheckCollisionPointRec(mp, closeBtn)) {
                    PlaySound(OmegaTechSoundData.UIClick);
                    m_panes.erase(m_panes.begin() + i);
                    return;
                }
            }

            // Check title bar for drag (front to back)
            for (int i = (int)m_panes.size() - 1; i >= 0; i--) {
                Rectangle tb = GetPaneTitleBarRect(m_panes[i]);
                if (CheckCollisionPointRec(mp, tb)) {
                    m_dragPane = true;
                    m_dragPaneIdx = i;
                    m_panes[i].dragging = true;
                    m_panes[i].dragOffset = {mp.x - m_panes[i].rect.x, mp.y - m_panes[i].rect.y};
                    BringPaneToFront(i);
                    return;
                }
            }

            // Check content area clicks (front to back)
            int paneIdx = GetPaneAtPoint(mp);
            if (paneIdx >= 0) {
                BringPaneToFront(paneIdx);
                Pane& p = m_panes[paneIdx];
                Rectangle cr = GetPaneContentRect(p);
                Vector2 localMp = {mp.x - cr.x, mp.y - cr.y};
                if (CheckCollisionPointRec(mp, cr)) {
                    HandlePaneContentClick(p.type, mp, cr);
                }
                return;
            }
        }
    }

    void HandlePaneContentClick(PaneType type, Vector2 mp, Rectangle contentRect) {
        switch (type) {
            case PANE_MULTIPLAYER: {
                float bx = contentRect.x + 12, by = contentRect.y + 40;
                auto checkBtn = [&](int idx, Rectangle r) {
                    if (CheckCollisionPointRec(mp, r)) {
                        PlaySound(OmegaTechSoundData.UIClick);
                        m_multiplayerSubPage = idx;
                    }
                };
                checkBtn(0, {bx, by, 140, 32});
                checkBtn(1, {bx, by + 40, 140, 32});
                break;
            }
            case PANE_CAMPAIGN:
            case PANE_PRACTICE: {
                bool campaign = (type == PANE_CAMPAIGN);
                int cols = std::max(1, (int)((contentRect.width - 24) / (CELL_W + CELL_GAP)));
                int sx = (int)(contentRect.x + 12);
                int sy = (int)(contentRect.y + 64);
                for (size_t i = 0; i < m_worlds.size(); i++) {
                    int r = (int)(i / cols), c = (int)(i % cols);
                    Rectangle cell = {(float)(sx + c * (CELL_W + CELL_GAP)), (float)(sy + r * (CELL_H + CELL_GAP)), (float)CELL_W, (float)CELL_H};
                    if (CheckCollisionPointRec(mp, cell)) {
                        PlaySound(OmegaTechSoundData.UIClick);
                        m_selectedWorld = m_worlds[i].dirName;
                        if (!campaign) { m_loadGame = false; }
                        m_exit = true;
                        return;
                    }
                }
                break;
            }
            case PANE_SETTINGS: {
                // Settings sub-page buttons
                float bx = contentRect.x + 12, by = contentRect.y + 40;
                auto checkBtn = [&](int idx, Rectangle r) {
                    if (CheckCollisionPointRec(mp, r)) {
                        PlaySound(OmegaTechSoundData.UIClick);
                        m_settingsSubPage = idx;
                    }
                };
                checkBtn(0, {bx, by, 140, 32});
                checkBtn(1, {bx, by + 40, 140, 32});
                checkBtn(2, {bx, by + 80, 140, 32});
                break;
            }
            case PANE_CHARACTER: {
                // Team color swatches
                float x = contentRect.x + 16;
                float y = contentRect.y + 76; // after name and label
                float sx2 = x + 100;
                for (int i = 0; i < 7; i++) {
                    Rectangle sw = {sx2 + i * 30, y, 24, 24};
                    if (CheckCollisionPointRec(mp, sw)) {
                        PlaySound(OmegaTechSoundData.UIClick);
                        m_teamColor = i;
                        break;
                    }
                }
                break;
            }
            default: break;
        }
    }

    void HandleDropdownClick(int tab, int item) {
        m_dropdownOpen = false; m_openTab = -1;
        switch (tab) {
            case 0:
                if (item == 0) OpenPane(PANE_CAMPAIGN);
                else if (item == 1) {
                    if (IsPathFile("GameData/Saves/TF.sav")) { LoadSave(); LoadFlag = true; m_exit = true; }
                }
                break;
            case 1:
                if (item == 0) OpenPane(PANE_PRACTICE);
                else if (item == 1) {
                    if (IsPathFile("GameData/Saves/TF.sav")) { LoadSave(); LoadFlag = true; m_exit = true; }
                }
                else if (item == 2 && !m_worlds.empty()) {
                    m_selectedWorld = m_worlds[0].dirName; m_exit = true;
                }
                break;
            case 2: OpenPane(PANE_SERAPHIC); break;
            case 3: OpenPane(PANE_STATS); break;
            case 4: OpenPane(PANE_SETTINGS); break;
            case 5: OpenPane(PANE_CHARACTER); break;
            case 6: OpenPane(PANE_HELP); break;
            case 7: OpenPane(PANE_MULTIPLAYER); break;
            default: break;
        }
    }

    void DrawBackground() {
        auto& tex = GetMenuTex();
        if (tex.titleBg.id)
            for (int x = 0; x < m_sw; x += tex.titleBg.width)
                for (int y = 0; y < m_sh; y += tex.titleBg.height)
                    DrawTexture(tex.titleBg, x, y, WHITE);
    }

    void Draw9SlicePanel(Rectangle r, Texture2D tl, Texture2D t, Texture2D tr,
                         Texture2D l, Texture2D area, Texture2D rgt,
                         Texture2D bl, Texture2D b, Texture2D br) {
        if (r.width < 8 || r.height < 8) return;
        auto d9 = [](Texture2D tex, Rectangle src, Rectangle dst) {
            if (tex.id) DrawTexturePro(tex, src, dst, {0,0}, 0, WHITE);
        };
        d9(tl, {0,0,(float)tl.width,(float)tl.height}, {r.x,r.y,4,4});
        d9(tr, {0,0,(float)tr.width,(float)tr.height}, {r.x+r.width-4,r.y,4,4});
        d9(bl, {0,0,(float)bl.width,(float)bl.height}, {r.x,r.y+r.height-4,4,4});
        d9(br, {0,0,(float)br.width,(float)br.height}, {r.x+r.width-4,r.y+r.height-4,4,4});
        d9(t, {0,0,(float)t.width,(float)t.height}, {r.x+4,r.y,r.width-8,4});
        d9(b, {0,0,(float)b.width,(float)b.height}, {r.x+4,r.y+r.height-4,r.width-8,4});
        d9(l, {0,0,(float)l.width,(float)l.height}, {r.x,r.y+4,4,r.height-8});
        d9(rgt,{0,0,(float)rgt.width,(float)rgt.height}, {r.x+r.width-4,r.y+4,4,r.height-8});
        d9(area,{0,0,(float)area.width,(float)area.height}, {r.x+4,r.y+4,r.width-8,r.height-8});
    }

    void Draw9SliceFrame() {
        auto& t = GetMenuTex();
        Draw9SlicePanel(m_frameRect, t.fTL, t.fT, t.fTR, t.fL, t.fArea, t.fR, t.fBL, t.fB, t.fBR);
    }

    void DrawTitleBar() {
        auto& t = GetMenuTex();
        Rectangle r = m_titleBar;
        DrawRectangleRec(r, (Color){0,16,48,255});
        if (t.barL.id) DrawTexturePro(t.barL, {0,0,16,16}, {r.x, r.y, 16, r.height}, {0,0}, 0, WHITE);
        if (t.barTile.id) {
            float tw = r.width - 16 - 16*3 - 8;
            if (tw > 0) DrawTexturePro(t.barTile, {0,0,1,16}, {r.x+16, r.y, tw, r.height}, {0,0}, 0, WHITE);
        }
        DrawText("Angels95", (int)(r.x + 20), (int)(r.y + 4), 14, WHITE);
        DrawWinButton(m_minBtn, 0);
        DrawWinButton(m_maxBtn, 1);
        DrawWinButton(m_closeBtn, 2);
        if (t.barWin.id)
            DrawTexturePro(t.barWin, {0,0,16,16}, {r.x + r.width - 16*3 - 8 - 20, r.y, 16, r.height}, {0,0}, 0, WHITE);
    }

    void DrawWinButton(Rectangle r, int type) {
        bool hover = CheckCollisionPointRec(GetMousePosition(), r);
        Color bg = hover ? (Color){60,80,120,255} : (Color){20,30,60,255};
        if (type == 2) bg = hover ? (Color){180,40,40,255} : (Color){100,20,20,255};
        DrawRectangleRec(r, bg);
        DrawRectangleLinesEx(r, 1, (Color){100,130,180,255});
        int cx = (int)(r.x + r.width/2), cy = (int)(r.y + r.height/2);
        if (type == 0) DrawRectangle(cx-4, cy, 8, 2, WHITE);
        else if (type == 1) DrawRectangleLines(cx-4, cy-3, 8, 6, WHITE);
        else { DrawLine(cx-3, cy-3, cx+3, cy+3, WHITE); DrawLine(cx+3, cy-3, cx-3, cy+3, WHITE); }
    }

    void DrawSeparator(Rectangle ref) {
        Rectangle s = {m_innerRect.x, ref.y - SEP_H, m_innerRect.width, SEP_H};
        auto& t = GetMenuTex();
        if (t.menuLine.id) DrawTexturePro(t.menuLine, {0,0,128,2}, s, {0,0}, 0, WHITE);
        else DrawRectangleRec(s, (Color){60,100,160,200});
    }

    void DrawMenuBar() {
        DrawRectangleRec(m_menuBar, (Color){8,16,40,255});
        for (int i = 0; i < TAB_COUNT; i++) {
            Rectangle tr = m_tabRects[i];
            bool hover = (i == m_hoveredTab);
            bool active = (i == m_openTab);
            if (active) DrawRectangleRec(tr, (Color){30,60,100,255});
            else if (hover) DrawRectangleRec(tr, (Color){20,40,70,255});
            Color tc = active ? WHITE : (hover ? WHITE : (Color){180,200,220,255});
            int fs = 12;
            DrawText(TAB_NAMES[i], (int)(tr.x + (tr.width - MeasureText(TAB_NAMES[i], fs))/2), (int)(tr.y + (tr.height - fs)/2), fs, tc);
        }
    }

    void DrawDropdown(int tab) {
        auto& t = GetMenuTex();
        Draw9SlicePanel(m_dropdownRect, t.fTL, t.fT, t.fTR, t.fL, t.fArea, t.fR, t.fBL, t.fB, t.fBR);
        int count = GetDropdownItemCount(tab);
        for (int i = 0; i < count && i < (int)m_itemRects.size(); i++) {
            Rectangle ir = m_itemRects[i];
            bool hover = (i == m_hoveredItem);
            if (hover) DrawRectangleRec(ir, (Color){40,80,140,200});
            DrawText(GetDropdownItemName(tab, i), (int)(ir.x + 6), (int)(ir.y + 6), 12, hover ? WHITE : (Color){200,210,230,255});
            if (i < count - 1) DrawLine((int)ir.x, (int)(ir.y + ir.height - 1), (int)(ir.x + ir.width), (int)(ir.y + ir.height - 1), (Color){60,80,120,150});
        }
    }

    // ── Pane rendering ──────────────────────────────────────────────

    void DrawContent(Rectangle area) {
        auto& t = GetMenuTex();
        if (t.clientArea.id) DrawTexturePro(t.clientArea, {0,0,128,1}, area, {0,0}, 0, WHITE);
        else DrawRectangleRec(area, (Color){4,8,24,220});

        for (auto& p : m_panes) {
            DrawPane(p);
        }

        if (m_panes.empty()) {
            const char* msg = "Select an action from the menu above";
            DrawText(msg, (int)(area.x + (area.width - MeasureText(msg, 16)) / 2),
                     (int)(area.y + area.height / 2 - 10), 16, (Color){100,130,160,150});
        }
    }

    void DrawPane(const Pane& p) {
        auto& t = GetMenuTex();
        // Draw 9-slice frame
        Draw9SlicePanel(p.rect, t.fTL, t.fT, t.fTR, t.fL, t.fArea, t.fR, t.fBL, t.fB, t.fBR);

        // Draw pane title bar
        Rectangle tb = GetPaneTitleBarRect(p);
        DrawRectangleRec(tb, (Color){0,16,48,255});
        if (t.barL.id)
            DrawTexturePro(t.barL, {0,0,16,16}, {tb.x, tb.y, 16, tb.height}, {0,0}, 0, WHITE);
        if (t.barTile.id && tb.width > 32) {
            float tw = tb.width - 16 - 16 - 2;
            if (tw > 0)
                DrawTexturePro(t.barTile, {0,0,1,16}, {tb.x + 16, tb.y, tw, tb.height}, {0,0}, 0, WHITE);
        }
        DrawWinButton(GetPaneCloseBtnRect(p), 2);
        const char* title = GetPaneTitle(p.type);
        DrawText(title, (int)(tb.x + 20), (int)(tb.y + 4), 12, WHITE);

        // Draw pane content with scissoring
        Rectangle cr = GetPaneContentRect(p);
        BeginScissorMode((int)cr.x, (int)cr.y, (int)cr.width, (int)cr.height);
        DrawPaneContent(p.type, cr);
        EndScissorMode();
    }

    void DrawPaneContent(PaneType type, Rectangle area) {
        switch (type) {
            case PANE_CAMPAIGN:  DrawCampaignPage(area); break;
            case PANE_PRACTICE:  DrawPracticeSessionPage(area); break;
            case PANE_SERAPHIC:  DrawSeraphicPage(area); break;
            case PANE_STATS:     DrawStatsPage(area); break;
            case PANE_SETTINGS:  DrawSettingsPage(area); break;
            case PANE_CHARACTER: DrawCharacterPage(area); break;
            case PANE_HELP:      DrawHelpPage(area); break;
            case PANE_MULTIPLAYER: DrawMultiplayerPage(area); break;
        }
    }

    // ── Page drawing functions (no input handling) ──────────────────

    void DrawPageHeader(Rectangle area, const char* title) {
        DrawText(title, (int)(area.x + 12), (int)(area.y + 8), 18, WHITE);
        DrawLine((int)(area.x + 12), (int)(area.y + 30), (int)(area.x + area.width - 12), (int)(area.y + 30), (Color){60,100,160,150});
    }

    void DrawLevelSelectGrid(Rectangle area, bool campaignMode) {
        DrawPageHeader(area, campaignMode ? "Campaign Missions" : "Practice Session");
        DrawText("Game Type: ", (int)(area.x + 12), (int)(area.y + 38), 12, LIGHTGRAY);
        float fx = area.x + 12 + MeasureText("Game Type: ", 12) + 4;
        Rectangle filterRect = {fx, (float)(area.y + 36), 120, 20};
        GuiDropdownBox(filterRect, "All Types;Campaign;Deathmatch;CTF", &m_gameTypeFilter, false);

        int cols = std::max(1, (int)((area.width - 24) / (CELL_W + CELL_GAP)));
        int sx = (int)(area.x + 12), sy = (int)(area.y + 64);
        Vector2 mp = GetMousePosition();

        for (size_t i = 0; i < m_worlds.size(); i++) {
            int r = (int)(i / cols), c = (int)(i % cols);
            Rectangle cell = {(float)(sx + c * (CELL_W + CELL_GAP)), (float)(sy + r * (CELL_H + CELL_GAP)), (float)CELL_W, (float)CELL_H};
            bool hover = CheckCollisionPointRec(mp, cell);
            DrawRectangleRounded(cell, 0.15f, 8, hover ? (Color){40,80,140,180} : (Color){12,24,50,200});
            DrawRectangleRoundedLinesEx(cell, 0.15f, 8, 1.0f, hover ? (Color){100,160,220,200} : (Color){40,60,100,150});
            const char* wn = m_worlds[i].name.c_str();
            int fs = 13;
            DrawText(wn, (int)(cell.x + (cell.width - MeasureText(wn, fs))/2), (int)(cell.y + (cell.height - fs)/2), fs, hover ? WHITE : (Color){180,200,230,255});
        }
        if (m_worlds.empty())
            DrawText("No worlds found in GameData/Worlds/", (int)(area.x + 12), (int)(area.y + 64), 14, (Color){180,180,180,200});
    }

    void DrawCampaignPage(Rectangle area) { DrawLevelSelectGrid(area, true); }
    void DrawPracticeSessionPage(Rectangle area) { DrawLevelSelectGrid(area, false); }

    void DrawSeraphicPage(Rectangle area) {
        DrawPageHeader(area, "Seraphic Realm");
        const char* l[] = {"The Seraphic Realm awaits...","","A celestial battlefield where angels descend","to challenge the worthy.","","Coming Soon","","Prepare yourself for the ascent."};
        int y = (int)(area.y + 40);
        for (int i = 0; i < 8; i++) { DrawText(l[i], (int)(area.x + 16), y, 14, (Color){160,180,200,220}); y += 22; }
    }

    void DrawStatsPage(Rectangle area) {
        DrawPageHeader(area, "Player Statistics");
        char buf[128]; int y = (int)(area.y + 40);
        snprintf(buf, sizeof(buf), "Deaths: %d", OmegaTechData.Deaths); DrawText(buf, (int)(area.x + 16), y, 14, LIGHTGRAY); y += 24;
        snprintf(buf, sizeof(buf), "Level: %d", OmegaTechData.LevelIndex); DrawText(buf, (int)(area.x + 16), y, 14, LIGHTGRAY); y += 24;
        DrawText("More statistics coming soon...", (int)(area.x + 16), y + 20, 13, (Color){120,140,160,180});
    }

    void DrawSettingsPage(Rectangle area) {
        DrawPageHeader(area, "Settings");
        auto drawBtn = [&](int idx, const char* label, Rectangle r) {
            bool h = CheckCollisionPointRec(GetMousePosition(), r);
            auto& t = GetMenuTex();
            DrawTexturePro(h ? t.btnHover : t.btnNormal, {0,0,64,64}, r, {0,0}, 0, h ? WHITE : (Color){200,200,200,255});
            DrawText(label, (int)(r.x + (r.width - MeasureText(label, 13))/2), (int)(r.y + 9), 13, h ? WHITE : LIGHTGRAY);
        };
        float bx = area.x + 12, by = area.y + 40;
        drawBtn(0, "Video", {bx, by, 140, 32});
        drawBtn(1, "Audio", {bx, by + 40, 140, 32});
        drawBtn(2, "Controls", {bx, by + 80, 140, 32});

        float sx = bx + 160, sw = area.x + area.width - sx - 12;
        Rectangle sub = {sx, by, sw, area.y + area.height - by - 8};
        DrawRectangleRec(sub, (Color){8,16,36,180});
        int y = (int)(sub.y + 4);
        switch (m_settingsSubPage) {
            case 0: {
                DrawText("Video Settings", (int)(sx + 8), y, 14, WHITE); y += 24;
                bool fs = IsWindowFullscreen();
                if (GuiButton({sx+8, (float)y, 140, 22}, fs ? "Fullscreen: ON" : "Fullscreen: OFF")) ToggleFullscreen();
                y += 30;
                DrawText("Press Alt+Enter to toggle fullscreen", (int)(sx + 8), y, 11, (Color){140,160,180,200});
                break;
            }
            case 1: {
                DrawText("Audio Settings", (int)(sx + 8), y, 14, WHITE); y += 24;
                float vol = GetMasterVolume();
                vol = GuiSlider({sx+8, (float)y, 200, 20}, "Volume", NULL, vol, 0.0f, 1.0f);
                SetMasterVolume(vol);
                break;
            }
            case 2: {
                DrawText("Controls", (int)(sx + 8), y, 14, WHITE); y += 24;
                const char* c[] = {"WASD - Move","Mouse - Look","Left Click - Fire","E - Interact","Tab - Inventory","Escape - Pause","` - Console"};
                for (int i = 0; i < 7; i++) { DrawText(c[i], (int)(sx + 8), y, 13, LIGHTGRAY); y += 20; }
                break;
            }
        }
    }

    void DrawCharacterPage(Rectangle area) {
        DrawPageHeader(area, "Character Configuration");
        float x = area.x + 16, y = area.y + 42;
        DrawText("Name:", (int)x, (int)y, 14, WHITE);
        GuiTextBox({x + 60, y - 2, 200, 22}, m_charName, sizeof(m_charName), true);
        y += 34;

        DrawText("Team Color:", (int)x, (int)(y + 4), 14, WHITE);
        float sx2 = x + 100;
        for (int i = 0; i < 7; i++) {
            Rectangle sw = {sx2 + i * 30, y, 24, 24};
            bool h = CheckCollisionPointRec(GetMousePosition(), sw);
            DrawRectangleRec(sw, TEAM_COLORS[i]);
            if (i == m_teamColor) DrawRectangleLinesEx(sw, 2, WHITE);
            else if (h) DrawRectangleLinesEx(sw, 1, LIGHTGRAY);
        }
        y += 36;
        DrawText(TEAM_COLOR_NAMES[m_teamColor], (int)x, (int)y, 12, LIGHTGRAY); y += 24;

        DrawText("Model:", (int)x, (int)(y + 2), 14, WHITE);
        DrawText("(not yet implemented)", (int)(x + 64), (int)(y + 2), 12, (Color){120,140,160,180}); y += 30;

        Rectangle vp = {x, y, area.width - 32, area.height - (y - area.y) - 16};
        if (vp.height > 100) {
            DrawRectangleRec(vp, (Color){10,20,40,200});
            DrawRectangleLinesEx(vp, 1, (Color){40,60,100,150});
            int cx = (int)(vp.x + vp.width/2), cy = (int)(vp.y + vp.height/2);
            DrawText("3D Model Preview", cx - MeasureText("3D Model Preview", 16)/2, cy - 10, 16, (Color){80,100,140,180});
            DrawText("(coming soon)", cx - MeasureText("(coming soon)", 13)/2, cy + 12, 13, (Color){60,80,120,150});
        }
    }

    void DrawMultiplayerPage(Rectangle area) {
        DrawPageHeader(area, "Multiplayer Lobby");
        float bx = area.x + 12, by = area.y + 40;
        auto drawTabBtn = [&](int idx, const char* label, Rectangle r) {
            bool h = CheckCollisionPointRec(GetMousePosition(), r);
            auto& t = GetMenuTex();
            DrawTexturePro(h ? t.btnHover : t.btnNormal, {0,0,64,64}, r, {0,0}, 0, h ? WHITE : (Color){200,200,200,255});
            DrawText(label, (int)(r.x + (r.width - MeasureText(label, 13))/2), (int)(r.y + 9), 13, h ? WHITE : LIGHTGRAY);
        };
        drawTabBtn(0, "Join Game", {bx, by, 140, 32});
        drawTabBtn(1, "Host Game", {bx, by + 40, 140, 32});

        float sx = bx + 160, sw = area.x + area.width - sx - 12;
        Rectangle sub = {sx, by, sw, area.y + area.height - by - 8};
        DrawRectangleRec(sub, (Color){8,16,36,180});
        int y = (int)(sub.y + 4);

        if (m_multiplayerSubPage == 0) {
            DrawText("Join a Game Server", (int)(sx + 8), y, 14, WHITE); y += 24;
            DrawText("Server IP:", (int)(sx + 8), y + 4, 12, LIGHTGRAY);
            GuiTextBox({sx + 80, (float)y, 160, 22}, m_joinIPBuffer, sizeof(m_joinIPBuffer), true);
            y += 30;
            DrawText("Port:", (int)(sx + 8), y + 4, 12, LIGHTGRAY);
            GuiTextBox({sx + 80, (float)y, 80, 22}, m_hostPortBuffer, sizeof(m_hostPortBuffer), true);
            y += 34;
            if (GuiButton({sx + 8, (float)y, 140, 24}, "Connect")) {
                PlaySound(OmegaTechSoundData.UIClick);
                std::strncpy(m_joinIP, m_joinIPBuffer, sizeof(m_joinIP) - 1);
                m_joinIP[sizeof(m_joinIP) - 1] = '\0';
                m_joinServer = true;
                m_exit = true;
            }
            y += 30;
            DrawText("Enter the server IP address and port,", (int)(sx + 8), y, 11, (Color){140,160,180,200}); y += 14;
            DrawText("then click Connect to join.", (int)(sx + 8), y, 11, (Color){140,160,180,200});
        } else {
            DrawText("Host a Game Server", (int)(sx + 8), y, 14, WHITE); y += 24;
            DrawText("World:", (int)(sx + 8), y + 4, 12, LIGHTGRAY);
            // World dropdown
            {
                static int worldSel = 0;
                if (worldSel >= (int)m_worlds.size()) worldSel = 0;
                std::string wl;
                for (size_t i = 0; i < m_worlds.size(); i++) {
                    if (i) wl += ";";
                    wl += m_worlds[i].name;
                }
                if (!wl.empty()) {
                    GuiDropdownBox({sx + 60, (float)y, sw - 68, 22}, wl.c_str(), &worldSel, false);
                    if (worldSel >= 0 && worldSel < (int)m_worlds.size())
                        m_selectedServerWorld = m_worlds[worldSel].dirName;
                }
            }
            y += 30;
            DrawText("Port:", (int)(sx + 8), y + 4, 12, LIGHTGRAY);
            GuiTextBox({sx + 80, (float)y, 80, 22}, m_hostPortBuffer, sizeof(m_hostPortBuffer), true);
            y += 34;
            if (GuiButton({sx + 8, (float)y, 140, 24}, "Start Server")) {
                PlaySound(OmegaTechSoundData.UIClick);
                if (!m_selectedServerWorld.empty()) {
                    m_selectedWorld = m_selectedServerWorld;
                    m_startServer = true;
                    m_exit = true;
                }
            }
            y += 30;
            DrawText("Select a world and port, then click", (int)(sx + 8), y, 11, (Color){140,160,180,200}); y += 14;
            DrawText("Start Server to host a game.", (int)(sx + 8), y, 11, (Color){140,160,180,200});
        }
    }

    void DrawHelpPage(Rectangle area) {
        DrawPageHeader(area, "About Angels95 / OmegaTech Engine");
        const char* l[] = {
            "Angels95 - Powered by the OzWorld Engine - based on OmegaTech Engine","","A first-person shooter built with raylib 5.5 and C++20.","",
            "Controls:","  WASD  - Movement","  Mouse - Look around","  Left Click - Fire weapon",
            "  E     - Interact / Use","  Tab   - Inventory","  Escape - Pause menu","  `     - Console","",
            "Features:","  - LightningScript scripting system","  - OZONE world format with CSG brushes",
            "  - Dynamic NPC system with FSM AI","  - Custom UDP networking","  - Particle effects system","  - OzPackage asset management"
        };
        int y = (int)(area.y + 40);
        for (int i = 0; i < 21; i++) { DrawText(l[i], (int)(area.x + 16), y, 13, (Color){160,180,200,220}); y += 18; }
    }

    void DrawHelpBar() {
        DrawRectangleRec(m_helpBar, (Color){6,12,32,230});
        const char* h = GetContextHelp();
        DrawText(h, (int)(m_helpBar.x + 8), (int)(m_helpBar.y + 5), 12, (Color){160,190,220,220});
    }

    const char* GetContextHelp() {
        if (m_dropdownOpen && m_openTab >= 0 && m_hoveredItem >= 0) {
            if (m_openTab == 0) return m_hoveredItem == 0 ? "Begin a new campaign mission" : "Load a previously saved campaign game";
            if (m_openTab == 1) return m_hoveredItem == 0 ? "Start a new practice session" : (m_hoveredItem == 1 ? "Load a saved game" : "Jump straight into a random world");
            if (m_openTab == 2) return "Enter the Seraphic Realm - celestial PvP arena (coming soon)";
            if (m_openTab == 3) return "View your player statistics and achievements";
            if (m_openTab == 4) return m_hoveredItem == 0 ? "Configure video and graphics" : (m_hoveredItem == 1 ? "Adjust audio volume" : "Configure keyboard and mouse");
            if (m_openTab == 5) return "Customize your character";
            if (m_openTab == 6) return "About Angels95 and the OmegaTech Engine";
            if (m_openTab == 7) return "Join or host a multiplayer game";
        }
        if (!m_panes.empty()) {
            int idx = GetPaneAtPoint(GetMousePosition());
            if (idx >= 0) {
                switch (m_panes[idx].type) {
                    case PANE_CAMPAIGN:  return "Campaign mission selection";
                    case PANE_PRACTICE:  return "Practice session world selection";
                    case PANE_SERAPHIC:  return "The Seraphic Realm - coming soon";
                    case PANE_STATS:     return "Your player statistics";
                    case PANE_SETTINGS:  return "Configure game settings";
                    case PANE_CHARACTER: return "Customize your character";
                    case PANE_HELP:      return "About Angels95 / OmegaTech Engine";
                    case PANE_MULTIPLAYER: return "Join or host a multiplayer game";
                }
            }
        }
        return "Select an action from the menu above";
    }
};

const char* TitleMenu::TAB_NAMES[TitleMenu::TAB_COUNT] = {
    "Campaign", "Practice Session", "Seraphic Realm",
    "Stats", "Settings", "Character", "Help", "Multiplayer"
};
