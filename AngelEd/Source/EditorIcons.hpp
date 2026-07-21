#pragma once
#include "raylib.h"
#include <string>
#include <unordered_map>

// EditorIcons — loads and caches editor toolbar icons from AngelEd/UI/*.bmp
class EditorIcons {
public:
    static EditorIcons& Instance() {
        static EditorIcons instance;
        return instance;
    }

    void Load();
    void Unload();

    const Texture2D* Get(const std::string& name) const;
    bool Has(const std::string& name) const { return m_icons.find(name) != m_icons.end(); }

    // Draw an icon at position with optional size override
    void Draw(const std::string& name, int x, int y, int size = 20, Color tint = WHITE) const;

private:
    EditorIcons() = default;
    std::unordered_map<std::string, Texture2D> m_icons;
};
