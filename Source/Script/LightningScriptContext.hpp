#pragma once
#include "../Log.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

// LightningScriptContext — per-instance script execution engine
// Refactored from Source/Parasite/ParasiteScript.hpp
// Original ParasiteScript by ODeLyZiD / OmegaTech

// Per-instance logger helper
#define LS_LOG(fmt, ...)  OZ_DEBUG("[LS:%s] " fmt, m_debugTag.c_str(), ##__VA_ARGS__)
#define LS_WARN(fmt, ...) OZ_WARN("[LS:%s] " fmt, m_debugTag.c_str(), ##__VA_ARGS__)

class LightningScriptContext {
public:
    static constexpr int MAX_FLAGS = 64;

    LightningScriptContext() = default;

    // Set a debug tag (typically "entityName:instanceIdx" or "zone:name")
    void SetDebugTag(const std::string& tag) { m_debugTag = tag; }
    const std::string& GetDebugTag() const { return m_debugTag; }

    // Load script lines from a text buffer
    bool Load(const std::string& scriptText);

    // Execute one instruction, advance program counter
    bool ExecuteNext();

    // Rewind program counter to start
    void Reset();

    // Variable I/O
    void SetInt(const std::string& name, int val);
    int  GetInt(const std::string& name, int defaultVal = 0) const;
    void SetFloat(const std::string& name, float val);
    float GetFloat(const std::string& name, float defaultVal = 0.0f) const;
    void SetStr(const std::string& name, const std::string& val);
    std::string GetStr(const std::string& name, const std::string& defaultVal = "") const;

    // Toggle flags
    void SetFlag(int idx, int val);
    int  GetFlag(int idx) const;

    bool HasMore() const { return m_pc < (int)m_lines.size(); }
    int  LineCount() const { return (int)m_lines.size(); }
    int  ProgramCounter() const { return m_pc; }

    // Jump label resolution
    void RegisterJumpLabel(const std::string& label, int line);
    int  FindJumpLabel(const std::string& label) const;

    // Pop pending side-effects (called by host after script execution)
    std::string PopPendingSound();              // returns __last_sound and clears it
    bool PopPendingFog(float& r, float& g, float& b, float& density);
    std::string PopPendingSkybox();             // returns __skybox name or empty

private:
    std::vector<std::string> m_lines;
    int m_pc = 0;

    // Per-instance variable memory
    std::unordered_map<std::string, int> m_intVars;
    std::unordered_map<std::string, float> m_floatVars;
    std::unordered_map<std::string, std::string> m_strVars;
    int m_flags[MAX_FLAGS] = {0};

    // Jump labels: label_name → line_index
    std::unordered_map<std::string, int> m_jumpLabels;
    std::string m_debugTag;

    // Internal: evaluate a condition string (e.g. "$x > 5")
    bool EvalCondition(const std::string& cond);
};
