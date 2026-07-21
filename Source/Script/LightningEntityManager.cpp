#include "LightningEntityManager.hpp"
#include "LightningEntityRegistry.hpp"
#include "../Log.hpp"
#include <algorithm>
#include <cstdio>

// ---------------------------------------------------------------------------
// Init — called at startup after registry is populated
// ---------------------------------------------------------------------------
void LightningEntityManager::Init() {
    for (int i = 0; i < HOTBAR_SIZE; i++) m_hotbar[i] = -1;
    m_selectedSlot = 0;
    m_instances.clear();
    m_resources.clear();
    OZ_INFO("LightningEntityManager: ready, registry has %d defs",
            LightningEntityRegistry::Instance().Count());
}

// ---------------------------------------------------------------------------
// Update — tick cooldowns and active instance scripts
// ---------------------------------------------------------------------------
void LightningEntityManager::Update(float dt) {
    for (auto& inst : m_instances) {
        if (inst.def == nullptr) continue;
        if (inst.cooldownRemaining > 0) inst.cooldownRemaining -= dt;
        // Tick instance script context for active behaviors
        if (inst.ctx.HasMore()) {
            // Run up to 10 instructions per frame to avoid stalls
            for (int step = 0; step < 10 && inst.ctx.HasMore(); step++)
                inst.ctx.ExecuteNext();
        }
    }
}

// ---------------------------------------------------------------------------
// Spawn — create a new instance from a registered entity definition
// ---------------------------------------------------------------------------
int LightningEntityManager::Spawn(const std::string& defName) {
    const EntityDef* def = LightningEntityRegistry::Instance().Find(defName);
    if (!def) {
        OZ_WARN("LightningEntityManager: unknown entity '%s'", defName.c_str());
        return -1;
    }

    if ((int)m_instances.size() >= MAX_ENTITIES) {
        OZ_WARN("LightningEntityManager: max entities (%d) reached", MAX_ENTITIES);
        return -1;
    }

    EntityInstance inst;
    inst.def = def;

    // Load resources
    if (!def->mesh.empty()) inst.modelIdx = CacheModel(def->mesh);
    if (!def->texture.empty()) inst.textureIdx = CacheTexture(def->texture);
    if (!def->icon.empty()) inst.iconIdx = CacheTexture(def->icon);

    // Initialize runtime stats from def
    for (auto& [k, v] : def->stats.floats)
        inst.runtimeStats[k] = v;

    // Load instance script from action blocks
    // We build a script that can be triggered by action name
    // For now, prep all action blocks as labels
    std::string scriptText;
    for (auto& action : def->actions) {
        scriptText += action.name + ":\n";
        for (auto& line : action.scriptLines)
            scriptText += line + "\n";
    }
    inst.ctx.Load(scriptText);
    char tag[128];
    snprintf(tag, sizeof(tag), "%s:%d", def->name.c_str(), (int)m_instances.size());
    inst.ctx.SetDebugTag(tag);

    inst.owned = true;
    inst.variantIndex = 0;
    m_instances.push_back(std::move(inst));

    int idx = (int)m_instances.size() - 1;
    OZ_INFO("LightningEntityManager: spawned '%s' at index %d", defName.c_str(), idx);
    return idx;
}

// ---------------------------------------------------------------------------
// Despawn — remove an instance by index
// ---------------------------------------------------------------------------
void LightningEntityManager::Despawn(int index) {
    if (index < 0 || index >= (int)m_instances.size()) return;
    auto& inst = m_instances[index];

    // Free resources
    if (inst.modelIdx >= 0) UncacheResource(inst.modelIdx);
    if (inst.textureIdx >= 0) UncacheResource(inst.textureIdx);
    if (inst.iconIdx >= 0) UncacheResource(inst.iconIdx);

    // Remove from hotbar if present
    for (int s = 0; s < HOTBAR_SIZE; s++) {
        if (m_hotbar[s] == index) m_hotbar[s] = -1;
    }

    // Swap with last to keep array compact
    if (index < (int)m_instances.size() - 1) {
        m_instances[index] = std::move(m_instances.back());
        // Update hotbar references
        for (int s = 0; s < HOTBAR_SIZE; s++) {
            if (m_hotbar[s] == (int)m_instances.size() - 1) m_hotbar[s] = index;
        }
    }
    m_instances.pop_back();
}

EntityInstance* LightningEntityManager::Get(int index) {
    if (index < 0 || index >= (int)m_instances.size()) return nullptr;
    return &m_instances[index];
}

// ---------------------------------------------------------------------------
// Hotbar
// ---------------------------------------------------------------------------
void LightningEntityManager::HotbarAssign(int slot, int instanceIndex) {
    if (slot < 0 || slot >= HOTBAR_SIZE) return;
    m_hotbar[slot] = instanceIndex;
}

void LightningEntityManager::HotbarSwap(int slotA, int slotB) {
    if (slotA < 0 || slotA >= HOTBAR_SIZE || slotB < 0 || slotB >= HOTBAR_SIZE) return;
    std::swap(m_hotbar[slotA], m_hotbar[slotB]);
}

int LightningEntityManager::HotbarAt(int slot) const {
    if (slot < 0 || slot >= HOTBAR_SIZE) return -1;
    return m_hotbar[slot];
}

void LightningEntityManager::SelectSlot(int slot) {
    if (slot >= 0 && slot < HOTBAR_SIZE) m_selectedSlot = slot;
}

EntityInstance* LightningEntityManager::SelectedEntity() const {
    int idx = m_hotbar[m_selectedSlot];
    if (idx < 0 || idx >= (int)m_instances.size()) return nullptr;
    return const_cast<EntityInstance*>(&m_instances[idx]);
}

// ---------------------------------------------------------------------------
// Resource caching
// ---------------------------------------------------------------------------
int LightningEntityManager::CacheModel(const std::string& path) {
    Model model = LoadModel(path.c_str());
    if (model.meshes == nullptr) return -1;
    CachedResource res;
    res.type = 1;
    res.model = model;
    m_resources.push_back(res);
    return (int)m_resources.size() - 1;
}

int LightningEntityManager::CacheTexture(const std::string& path) {
    Texture2D tex = LoadTexture(path.c_str());
    if (tex.id == 0) return -1;
    CachedResource res;
    res.type = 2;
    res.texture = tex;
    m_resources.push_back(res);
    return (int)m_resources.size() - 1;
}

void* LightningEntityManager::GetModel(int idx) const {
    if (idx < 0 || idx >= (int)m_resources.size() || m_resources[idx].type != 1) return nullptr;
    return (void*)&m_resources[idx].model;
}

void* LightningEntityManager::GetTexture(int idx) const {
    if (idx < 0 || idx >= (int)m_resources.size() || m_resources[idx].type != 2) return nullptr;
    return (void*)&m_resources[idx].texture;
}

void* LightningEntityManager::GetIcon(int idx) const {
    return GetTexture(idx);
}

void LightningEntityManager::UncacheResource(int idx) {
    if (idx < 0 || idx >= (int)m_resources.size()) return;
    if (m_resources[idx].type == 1) UnloadModel(m_resources[idx].model);
    else if (m_resources[idx].type == 2) UnloadTexture(m_resources[idx].texture);
    m_resources[idx].type = 0; // Mark as unused instead of erasing (avoids index shift)
    m_resources[idx].model = Model{0};
    m_resources[idx].texture = Texture2D{0};
}

void LightningEntityManager::UnloadAllResources() {
    for (int i = (int)m_resources.size() - 1; i >= 0; i--)
        UncacheResource(i);
}

// ---------------------------------------------------------------------------
// HandleInput — keyboard 1-8 for hotbar
// ---------------------------------------------------------------------------
void LightningEntityManager::HandleInput() {
    if (IsKeyPressed(KEY_ONE))   SelectSlot(0);
    if (IsKeyPressed(KEY_TWO))   SelectSlot(1);
    if (IsKeyPressed(KEY_THREE)) SelectSlot(2);
    if (IsKeyPressed(KEY_FOUR))  SelectSlot(3);
    if (IsKeyPressed(KEY_FIVE))  SelectSlot(4);
    if (IsKeyPressed(KEY_SIX))   SelectSlot(5);
    if (IsKeyPressed(KEY_SEVEN)) SelectSlot(6);
    if (IsKeyPressed(KEY_EIGHT)) SelectSlot(7);

    // Enter/E to use selected item
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_E)) {
        EntityInstance* sel = SelectedEntity();
        if (sel && sel->def) {
            // Trigger on_use action
            sel->ctx.Reset();
            // Find and jump to on_use label
            int labelLine = sel->ctx.FindJumpLabel("on_use");
            if (labelLine >= 0) {
                // Reset and run until on_use, then execute
                sel->ctx.Reset();
                while (sel->ctx.ProgramCounter() < labelLine && sel->ctx.HasMore())
                    sel->ctx.ExecuteNext();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// DrawHotbar — render 8 hotbar slots (ported from Objects.hpp::DrawHotbarSlot)
// ---------------------------------------------------------------------------
void LightningEntityManager::DrawHotbar() {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int slotSize = 50;
    int margin = 4;
    int totalWidth = HOTBAR_SIZE * (slotSize + margin) - margin;
    int startX = (sw - totalWidth) / 2;
    int y = sh - slotSize - 20;

    for (int s = 0; s < HOTBAR_SIZE; s++) {
        int x = startX + s * (slotSize + margin);
        Color bg = (s == m_selectedSlot) ? (Color){60, 60, 120, 200} : (Color){30, 30, 30, 180};
        DrawRectangle(x, y, slotSize, slotSize, bg);
        DrawRectangleLines(x, y, slotSize, slotSize, (s == m_selectedSlot) ? WHITE : GRAY);

        int idx = m_hotbar[s];
        if (idx >= 0 && idx < (int)m_instances.size()) {
            EntityInstance& inst = m_instances[idx];
            if (inst.def) {
                // Draw icon if available
                if (inst.iconIdx >= 0 && inst.iconIdx < (int)m_resources.size() && m_resources[inst.iconIdx].type == 2) {
                    Texture2D& icon = m_resources[inst.iconIdx].texture;
                    if (icon.id > 0) {
                        float iconSize = slotSize - 8;
                        DrawTexturePro(icon,
                            (Rectangle){0,0,(float)icon.width,(float)icon.height},
                            (Rectangle){(float)x+4, (float)y+4, iconSize, iconSize},
                            (Vector2){0,0}, 0, WHITE);
                    }
                }
                // Draw name
                DrawText(inst.def->name.c_str(), x + 2, y + slotSize - 14, 10, LIGHTGRAY);
            }
        }

        // Draw slot number
        char num[2] = {(char)('1' + s), '\0'};
        DrawText(num, x + 2, y + 2, 12, (s == m_selectedSlot) ? WHITE : DARKGRAY);
    }
}

// ---------------------------------------------------------------------------
// TriggerZoneAction — called from PawnSystem when player enters/exits a zone
// ---------------------------------------------------------------------------
void LightningEntityManager::TriggerZoneAction(const std::string& zoneName,
                                                const std::string& actionName) {
    const EntityDef* def = LightningEntityRegistry::Instance().Find(zoneName);
    if (!def || def->type != EntityType::SKYZONE) return;

    // Find existing instance or create one
    int instIdx = -1;
    for (int i = 0; i < (int)m_instances.size(); i++) {
        if (m_instances[i].def == def) { instIdx = i; break; }
    }
    if (instIdx < 0) {
        // Spawn a temporary instance for zone execution
        instIdx = Spawn(zoneName);
    }
    if (instIdx < 0) return;

    EntityInstance* inst = Get(instIdx);
    if (!inst) return;

    // Jump to the action label and execute
    inst->ctx.Reset();
    int labelLine = inst->ctx.FindJumpLabel(actionName);
    if (labelLine >= 0) {
        while (inst->ctx.ProgramCounter() < labelLine && inst->ctx.HasMore())
            inst->ctx.ExecuteNext();
        // Run up to 50 instructions for the action
        for (int step = 0; step < 50 && inst->ctx.HasMore(); step++)
            inst->ctx.ExecuteNext();
    }
}
