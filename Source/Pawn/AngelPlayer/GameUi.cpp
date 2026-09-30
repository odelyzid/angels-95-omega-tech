#include "GameUi.hpp"
#include "../../Script/LightningEntityManager.hpp"
#include "../../Script/LightningEntityRegistry.hpp"
#include "../../Log.hpp"

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
