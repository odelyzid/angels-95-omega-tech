#include "GameUi.hpp"
#include "../../Script/LightningEntityManager.hpp"
#include "../../Script/LightningEntityRegistry.hpp"
#include "../../Log.hpp"
#include <cstdlib>

void GameUi::Init() {
    auto& lem = LightningEntityManager::Instance();

    // Keep an already-resolved instance if it is still alive and correct.
    if (m_instanceIndex >= 0) {
        EntityInstance* inst = lem.Get(m_instanceIndex);
        if (inst && inst->def && inst->def->type == EntityType::GAMEUI) return;
        m_instanceIndex = -1;
    }

    // Reuse any GameUI instance that was spawned elsewhere.
    for (int i = 0; i < lem.Count(); ++i) {
        EntityInstance* inst = lem.Get(i);
        if (inst && inst->def && inst->def->type == EntityType::GAMEUI) {
            m_instanceIndex = i;
            return;
        }
    }

    if (!LightningEntityRegistry::Instance().Find("GameUI")) {
        OZ_WARN("GameUi: no 'GameUI' def found; HUD falls back to C++ defaults");
        return;
    }

    int idx = lem.Spawn("GameUI");
    if (idx >= 0) {
        m_instanceIndex = idx;
        OZ_INFO("GameUi: spawned GameUI def at idx %d", idx);
    }
}

float GameUi::StatFloat(const std::string& key, float def) const {
    if (m_instanceIndex < 0) return def;
    EntityInstance* inst = LightningEntityManager::Instance().Get(m_instanceIndex);
    if (!inst) return def;

    // runtimeStats wins (script-writable), then the authored def stats.
    auto rit = inst->runtimeStats.find(key);
    if (rit != inst->runtimeStats.end()) return rit->second;
    if (inst->def) {
        auto it = inst->def->stats.floats.find(key);
        if (it != inst->def->stats.floats.end()) return it->second;
    }
    return def;
}

std::string GameUi::StatString(const std::string& key, const std::string& def) const {
    if (m_instanceIndex < 0) return def;
    EntityInstance* inst = LightningEntityManager::Instance().Get(m_instanceIndex);
    if (!inst || !inst->def) return def;
    auto it = inst->def->stats.strings.find(key);
    if (it != inst->def->stats.strings.end()) return it->second;
    return def;
}

Texture2D GameUi::Texture() const {
    if (m_instanceIndex < 0) return Texture2D{0};
    EntityInstance* inst = LightningEntityManager::Instance().Get(m_instanceIndex);
    if (!inst || inst->textureIdx < 0) return Texture2D{0};
    void* p = LightningEntityManager::Instance().GetTexture(inst->textureIdx);
    if (!p) return Texture2D{0};
    return *static_cast<Texture2D*>(p);
}

// ---------------------------------------------------------------------------
// Slotbar geometry
// ---------------------------------------------------------------------------
void GameUi::ParseSlots() const {
    m_slots.clear();
    m_slotsParsed = true;
    m_slotsForInstance = m_instanceIndex;

    if (m_instanceIndex < 0) return;

    // The stats parser stores the raw token verbatim: no quote handling, and a
    // single embedded space truncates the value. Strip any quotes the author
    // (or a future parser fix) leaves behind before splitting.
    std::string raw = StatString("slot_rects");
    size_t b = raw.find_first_not_of(" \t\"");
    size_t e = raw.find_last_not_of(" \t\"");
    if (b == std::string::npos) return;
    raw = raw.substr(b, e - b + 1);

    // Flat list of floats -> groups of four become cells.
    std::vector<float> nums;
    nums.reserve(64);
    const char* p = raw.c_str();
    while (*p) {
        char* end = nullptr;
        float v = std::strtof(p, &end);
        if (end == p) {           // skip the separator
            ++p;
            continue;
        }
        nums.push_back(v);
        p = end;
    }

    const size_t n = nums.size() / 4;
    m_slots.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        Rectangle r{nums[i * 4 + 0], nums[i * 4 + 1], nums[i * 4 + 2], nums[i * 4 + 3]};
        if (r.width > 0.0f && r.height > 0.0f) m_slots.push_back(r);
    }
}

int GameUi::SlotCount() const {
    if (!m_slotsParsed || m_slotsForInstance != m_instanceIndex) ParseSlots();
    return (int)m_slots.size();
}

bool GameUi::SlotRect(int i, Rectangle& out) const {
    if (i < 0 || i >= SlotCount()) return false;
    out = m_slots[(size_t)i];
    return true;
}

bool GameUi::HasBar() const {
    Texture2D t = Texture();
    return t.id != 0 && SlotCount() > 0;
}

float GameUi::WidthPct() const {
    float v = StatFloat("hud_width_pct", 0.42f);
    if (v <= 0.0f || v > 1.0f) return 0.42f;
    return v;
}

float GameUi::BottomMargin() const {
    float v = StatFloat("hud_bottom_margin", 18.0f);
    return v < 0.0f ? 0.0f : v;
}

float GameUi::IconInset() const {
    float v = StatFloat("icon_inset", 16.0f);
    return v < 0.0f ? 0.0f : v;
}

bool GameUi::ShowSlotNumbers() const {
    return StatFloat("show_slot_numbers", 1.0f) != 0.0f;
}

bool GameUi::ShowSlotName() const {
    return StatFloat("show_slot_name", 1.0f) != 0.0f;
}
