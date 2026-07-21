#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

// LightningScriptContext — per-instance script execution engine
// Refactored from Source/Parasite/ParasiteScript.hpp
// Original ParasiteScript by ODeLyZiD / OmegaTech

class LightningScriptContext {
public:
    static constexpr int MAX_FLAGS = 64;
    static constexpr int MAX_VARS  = 128;

    LightningScriptContext() = default;

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

    // Internal: evaluate a condition string (e.g. "$x > 5")
    bool EvalCondition(const std::string& cond);
};
