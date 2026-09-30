#include "EditorIcons.hpp"
#include <filesystem>
#include <cstdio>
#include <algorithm>

namespace fs = std::filesystem;

// Scan a directory for *.bmp icons. `stripPrefix` (when non-empty) is removed
// from the stem so packed names like "effect-effect-ModeAdd" map to "ModeAdd".
static int ScanIconDir(const fs::path& dir, const std::string& stripPrefix,
                       std::unordered_map<std::string, Texture2D>& out) {
    if (!fs::exists(dir)) return 0;
    int count = 0;
    for (auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".bmp") continue;
        std::string name = entry.path().stem().string();
        if (!stripPrefix.empty() && name.rfind(stripPrefix, 0) == 0)
            name = name.substr(stripPrefix.size());
        if (name.empty() || out.count(name)) continue; // first source wins
        Texture2D tex = LoadTexture(entry.path().string().c_str());
        if (tex.id > 0) {
            out[name] = tex;
            count++;
        }
    }
    return count;
}

void EditorIcons::Load() {
    int count = 0;

    // 1) AngelEd/UI (repo) — primary source with clean names.
    fs::path iconDir = fs::current_path() / "AngelEd" / "UI";
    if (!fs::exists(iconDir))
        iconDir = fs::path("..") / "AngelEd" / "UI";
    count += ScanIconDir(iconDir, "", m_icons);

    // 2) GameData/Global/Engine/UI — packed engine UI icons
    //    ("effect-effect-ModeAdd.bmp" -> "ModeAdd").
    const char* gd = "GameData/Global/Engine/UI";
    if (fs::exists(gd))
        count += ScanIconDir(gd, "effect-effect-", m_icons);
    else if (fs::exists(fs::path("..") / gd))
        count += ScanIconDir(fs::path("..") / gd, "effect-effect-", m_icons);

    if (count == 0)
        fprintf(stderr, "EditorIcons: no icons loaded\n");
    else
        fprintf(stdout, "EditorIcons: loaded %d icons\n", count);
}

void EditorIcons::Unload() {
    for (auto& [name, tex] : m_icons) {
        if (tex.id > 0) UnloadTexture(tex);
    }
    m_icons.clear();
}

const Texture2D* EditorIcons::Get(const std::string& name) const {
    auto it = m_icons.find(name);
    if (it != m_icons.end() && it->second.id > 0)
        return &it->second;
    return nullptr;
}

void EditorIcons::Draw(const std::string& name, int x, int y, int size, Color tint) const {
    const Texture2D* tex = Get(name);
    if (!tex) return;
    Rectangle src = {0, 0, (float)tex->width, (float)tex->height};
    Rectangle dst = {(float)x, (float)y, (float)size, (float)size};
    DrawTexturePro(*tex, src, dst, (Vector2){0, 0}, 0, tint);
}
