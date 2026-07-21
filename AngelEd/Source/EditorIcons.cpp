#include "EditorIcons.hpp"
#include <filesystem>
#include <cstdio>
#include <algorithm>

namespace fs = std::filesystem;

void EditorIcons::Load() {
    // Search for icons in AngelEd/UI/ relative to working directory
    fs::path iconDir = fs::current_path() / "AngelEd" / "UI";
    if (!fs::exists(iconDir)) {
        // Try alternate path relative to executable
        iconDir = fs::path("..") / "AngelEd" / "UI";
        if (!fs::exists(iconDir)) {
            fprintf(stderr, "EditorIcons: no icon directory found\n");
            return;
        }
    }

    int count = 0;
    for (auto& entry : fs::directory_iterator(iconDir)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".bmp") {
                std::string name = entry.path().stem().string();
                Texture2D tex = LoadTexture(entry.path().string().c_str());
                if (tex.id > 0) {
                    m_icons[name] = tex;
                    count++;
                }
            }
        }
    }
    fprintf(stdout, "EditorIcons: loaded %d icons from %s\n", count, iconDir.string().c_str());
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
