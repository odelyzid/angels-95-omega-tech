#include "LightningScriptContext.hpp"
#include <cstring>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <cctype>

// ---------------------------------------------------------------------------
// Load — split text into lines, build jump label index
// ---------------------------------------------------------------------------
bool LightningScriptContext::Load(const std::string& scriptText) {
    m_lines.clear();
    m_intVars.clear();
    m_floatVars.clear();
    m_strVars.clear();
    m_jumpLabels.clear();
    std::memset(m_flags, 0, sizeof(m_flags));
    m_pc = 0;

    std::istringstream stream(scriptText);
    std::string line;
    int lineIdx = 0;
    while (std::getline(stream, line)) {
        // Trim
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) { m_lines.push_back(""); lineIdx++; continue; }
        size_t end = line.find_last_not_of(" \t\r\n");
        std::string trimmed = line.substr(start, end - start + 1);

        // Skip comments
        if (trimmed[0] == '#' || trimmed.rfind("//", 0) == 0) {
            m_lines.push_back("");
            lineIdx++;
            continue;
        }

    m_lines.push_back(trimmed);
    lineIdx++;
        }

    // Build jump label index immediately
    for (int i = 0; i < lineIdx; i++) {
        const std::string& ln = m_lines[i];
        size_t colon = ln.find(':');
        if (colon != std::string::npos && colon > 0 && colon < ln.size() - 1) {
            bool isLabel = true;
            for (size_t c = 0; c < colon; c++) {
                if (!std::isalnum(ln[c]) && ln[c] != '_') { isLabel = false; break; }
            }
            if (isLabel && ln[colon+1] == ' ') {
                m_jumpLabels[ln.substr(0, colon)] = i;
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Jump label registration — scans for "label_name:" at start of line
// ---------------------------------------------------------------------------
void LightningScriptContext::RegisterJumpLabel(const std::string& label, int line) {
    m_jumpLabels[label] = line;
}

int LightningScriptContext::FindJumpLabel(const std::string& label) const {
    auto it = m_jumpLabels.find(label);
    if (it != m_jumpLabels.end()) return it->second;
    return -1;
}

// ---------------------------------------------------------------------------
// ExecuteNext — run one instruction at m_pc, advance if not a jump
// ---------------------------------------------------------------------------
bool LightningScriptContext::ExecuteNext() {
    if (m_pc < 0 || m_pc >= (int)m_lines.size()) return false;

    const std::string& line = m_lines[m_pc];
    if (line.empty()) { m_pc++; return true; }

    // Check for label (skip execution, just advance)
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
        bool allAlpha = true;
        for (size_t c = 0; c < colon; c++) { if (!std::isalnum(line[c]) && line[c] != '_') { allAlpha = false; break; } }
        if (allAlpha && colon < line.size() - 1 && line[colon+1] == ' ') {
            m_pc++;
            return true;
        }
    }

    std::istringstream ls(line);
    std::string opcode;
    ls >> opcode;

    // -- Variable operations --
    if (opcode == "var") {
        // var name = value  (auto-detect int vs float)
        std::string name, eq;
        std::string valueStr;
        ls >> name >> eq;
        if (eq != "=") { m_pc++; return true; }
        ls >> valueStr;
        try {
            if (valueStr.find('.') != std::string::npos)
                m_floatVars[name] = std::stof(valueStr);
            else
                m_intVars[name] = std::stoi(valueStr);
        } catch (const std::exception& e) {
            LS_WARN("Failed to parse value for '%s': %s", name.c_str(), e.what());
            m_intVars[name] = 0;
        }
        m_pc++;

    } else if (opcode.rfind("$", 0) == 0) {
        // $var = value or $var += value etc.
        std::string name = opcode.substr(1);
        std::string op;
        ls >> op;
        std::string valueStr;
        std::getline(ls, valueStr);
        // Trim valueStr
        size_t vs = valueStr.find_first_not_of(" \t");
        if (vs != std::string::npos) valueStr = valueStr.substr(vs);
        size_t ve = valueStr.find_last_not_of(" \t\r\n");
        if (ve != std::string::npos) valueStr = valueStr.substr(0, ve + 1);

        auto getVal = [&](const std::string& s) -> float {
            if (s.rfind("$", 0) == 0) {
                auto it = m_floatVars.find(s.substr(1));
                if (it != m_floatVars.end()) return it->second;
                auto it2 = m_intVars.find(s.substr(1));
                if (it2 != m_intVars.end()) return (float)it2->second;
                return 0;
            }
            try { return std::stof(s); } catch (...) { return 0; }
        };

        float val = getVal(valueStr);

        if (op == "=") {
            if (valueStr.find('.') != std::string::npos || m_floatVars.count(name))
                m_floatVars[name] = val;
            else
                m_intVars[name] = (int)val;
        } else if (op == "+=") { m_intVars[name] = m_intVars[name] + (int)val; }
        else if (op == "-=") { m_intVars[name] = m_intVars[name] - (int)val; }
        else if (op == "*=") { m_intVars[name] = m_intVars[name] * (int)val; }
        else if (op == "/=") { if (val != 0) m_intVars[name] = m_intVars[name] / (int)val; }
        m_pc++;

    } else if (opcode == "if") {
        std::string cond;
        std::getline(ls, cond);
        // Trim
        size_t cs = cond.find_first_not_of(" \t");
        if (cs != std::string::npos) cond = cond.substr(cs);
        size_t ce = cond.find_last_not_of(" \t\r\n");
        if (ce != std::string::npos) cond = cond.substr(0, ce + 1);

        if (!EvalCondition(cond)) {
            // Skip to matching else or endif
            int depth = 1;
            m_pc++;
            while (m_pc < (int)m_lines.size() && depth > 0) {
                const std::string& sl = m_lines[m_pc];
                if (sl.rfind("if", 0) == 0 && sl.size() > 2 && std::isblank(sl[2])) depth++;
                else if (sl.rfind("endif", 0) == 0) depth--;
                else if (sl.rfind("else", 0) == 0 && depth == 1) depth--;
                else if (sl.rfind("}") != std::string::npos) { /* handle block end */ }
                if (depth > 0) m_pc++;
            }
            if (m_pc < (int)m_lines.size()) m_pc++;
            return true;
        }
        m_pc++;

    } else if (opcode == "else") {
        // Skip to endif
        int depth = 1;
        m_pc++;
        while (m_pc < (int)m_lines.size() && depth > 0) {
            const std::string& sl = m_lines[m_pc];
            if (sl.rfind("if", 0) == 0 && sl.size() > 2 && std::isblank(sl[2])) depth++;
            else if (sl.rfind("endif", 0) == 0) depth--;
            if (depth > 0) m_pc++;
        }
        if (m_pc < (int)m_lines.size()) m_pc++;
        return true;

    } else if (opcode == "endif") {
        m_pc++;

    } else if (opcode == "goto") {
        std::string label;
        ls >> label;
        auto it = m_jumpLabels.find(label);
        if (it != m_jumpLabels.end()) m_pc = it->second;
        else m_pc++;

    } else if (opcode == "say") {
        std::string msg;
        std::getline(ls, msg);
        // Trim quotes if present
        size_t qs = msg.find_first_not_of(" \t\"");
        size_t qe = msg.find_last_not_of(" \t\"\r\n");
        if (qs != std::string::npos && qe != std::string::npos)
            msg = msg.substr(qs, qe - qs + 1);
        LS_LOG("say: %s", msg.c_str());
        fprintf(stdout, "[LightningScript] %s\n", msg.c_str());
        m_pc++;

    } else if (opcode == "stop" || opcode == "end") {
        m_pc = (int)m_lines.size(); // Halt execution

    } else if (opcode == "wtflag") {
        int idx, val;
        ls >> idx >> val;
        if (idx >= 0 && idx < MAX_FLAGS) m_flags[idx] = val;
        m_pc++;

    } else if (opcode == "rtflag") {
        // rtflag idx → store flag into $result
        std::string idxStr;
        ls >> idxStr;
        int idx = 0;
        if (idxStr.rfind("$", 0) == 0) idx = (int)m_floatVars[idxStr.substr(1)];
        else idx = std::stoi(idxStr);
        if (idx >= 0 && idx < MAX_FLAGS) m_intVars["result"] = m_flags[idx];
        m_pc++;

    } else if (opcode == "set_cooldown") {
        float sec;
        ls >> sec;
        if (sec > 0) SetFloat("__cooldown", sec);
        m_pc++;

    } else if (opcode == "play_sound") {
        std::string name;
        std::getline(ls, name);
        {
            size_t a = name.find_first_not_of(" \t\"");
            size_t b = name.find_last_not_of(" \t\"\r\n");
            if (a != std::string::npos && b != std::string::npos) name = name.substr(a, b - a + 1);
        }
        // Sound playback handled by caller via message queue; log for now
        SetStr("__last_sound", name);
        m_pc++;

    } else if (opcode == "set_fog") {
        float r, g, b, d;
        ls >> r >> g >> b >> d;
        SetFloat("__fog_r", r); SetFloat("__fog_g", g);
        SetFloat("__fog_b", b); SetFloat("__fog_density", d);
        m_pc++;

    } else if (opcode == "restore_fog") {
        m_floatVars.erase("__fog_r");
        m_floatVars.erase("__fog_g");
        m_floatVars.erase("__fog_b");
        m_floatVars.erase("__fog_density");
        m_pc++;

    } else if (opcode == "set_ambient") {
        float r, g, b;
        ls >> r >> g >> b;
        SetFloat("__ambient_r", r); SetFloat("__ambient_g", g);
        SetFloat("__ambient_b", b);
        m_pc++;

    } else if (opcode == "restore_ambient") {
        m_floatVars.erase("__ambient_r");
        m_floatVars.erase("__ambient_g");
        m_floatVars.erase("__ambient_b");
        m_pc++;

    } else if (opcode == "set_skybox") {
        std::string name;
        ls >> name;
        {
            size_t a = name.find_first_not_of(" \t\"");
            size_t b = name.find_last_not_of(" \t\"\r\n");
            if (a != std::string::npos && b != std::string::npos) name = name.substr(a, b - a + 1);
        }
        SetStr("__skybox", name);
        m_pc++;

    } else if (opcode == "restore_skybox") {
        SetStr("__skybox", "");
        m_pc++;

    } else {
        OZ_WARN("LightningScript: unknown opcode '%s' at line %d", opcode.c_str(), m_pc);
        m_pc++;
    }

    return m_pc < (int)m_lines.size();
}

void LightningScriptContext::Reset() {
    m_pc = 0;
}

// ---------------------------------------------------------------------------
// Variable I/O
// ---------------------------------------------------------------------------
void LightningScriptContext::SetInt(const std::string& name, int val) { m_intVars[name] = val; }
int  LightningScriptContext::GetInt(const std::string& name, int defaultVal) const {
    auto it = m_intVars.find(name);
    return (it != m_intVars.end()) ? it->second : defaultVal;
}
void LightningScriptContext::SetFloat(const std::string& name, float val) { m_floatVars[name] = val; }
float LightningScriptContext::GetFloat(const std::string& name, float defaultVal) const {
    auto it = m_floatVars.find(name);
    return (it != m_floatVars.end()) ? it->second : defaultVal;
}
void LightningScriptContext::SetStr(const std::string& name, const std::string& val) { m_strVars[name] = val; }
std::string LightningScriptContext::GetStr(const std::string& name, const std::string& defaultVal) const {
    auto it = m_strVars.find(name);
    return (it != m_strVars.end()) ? it->second : defaultVal;
}

void LightningScriptContext::SetFlag(int idx, int val) {
    if (idx >= 0 && idx < MAX_FLAGS) m_flags[idx] = val;
}
int LightningScriptContext::GetFlag(int idx) const {
    if (idx >= 0 && idx < MAX_FLAGS) return m_flags[idx];
    return 0;
}

// ---------------------------------------------------------------------------
// EvalCondition — simple condition parser
// Supports: $var == val, $var > val, $var < val, $var >= val, $var <= val, $var != val
// ---------------------------------------------------------------------------
bool LightningScriptContext::EvalCondition(const std::string& cond) {
    // Find the operator
    std::string ops[] = {">=", "<=", "!=", "==", ">", "<"};
    std::string op;
    size_t opPos = std::string::npos;
    for (auto& o : ops) {
        opPos = cond.find(o);
        if (opPos != std::string::npos) { op = o; break; }
    }
    if (opPos == std::string::npos) return false;

    std::string lhs = cond.substr(0, opPos);
    std::string rhs = cond.substr(opPos + op.size());

    // Trim
    auto trim = [](std::string& s) {
        size_t a = s.find_first_not_of(" \t");
        size_t b = s.find_last_not_of(" \t");
        if (a != std::string::npos && b != std::string::npos) s = s.substr(a, b - a + 1);
    };
    trim(lhs); trim(rhs);

    // Resolve variable references
    auto resolve = [&](const std::string& s) -> float {
        if (s.rfind("$", 0) == 0) {
            std::string vn = s.substr(1);
            auto it = m_floatVars.find(vn);
            if (it != m_floatVars.end()) return it->second;
            auto it2 = m_intVars.find(vn);
            if (it2 != m_intVars.end()) return (float)it2->second;
            return 0;
        }
        return std::stof(s);
    };

    float lv = resolve(lhs);
    float rv = resolve(rhs);

    if (op == "==") return lv == rv;
    if (op == "!=") return lv != rv;
    if (op == ">")  return lv > rv;
    if (op == "<")  return lv < rv;
    if (op == ">=") return lv >= rv;
    if (op == "<=") return lv <= rv;
    return false;
}

// ---------------------------------------------------------------------------
// PopPendingSound — return and clear __last_sound
// ---------------------------------------------------------------------------
std::string LightningScriptContext::PopPendingSound() {
    auto it = m_strVars.find("__last_sound");
    if (it == m_strVars.end() || it->second.empty()) return {};
    std::string name = it->second;
    m_strVars.erase(it);
    return name;
}

// ---------------------------------------------------------------------------
// PopPendingFog — read and clear pending fog values
// Returns true if fog values are available.
// ---------------------------------------------------------------------------
bool LightningScriptContext::PopPendingFog(float& r, float& g, float& b, float& density) {
    auto it_r = m_floatVars.find("__fog_r");
    if (it_r == m_floatVars.end()) return false;
    r = it_r->second; m_floatVars.erase(it_r);
    auto it_g = m_floatVars.find("__fog_g");
    auto it_b = m_floatVars.find("__fog_b");
    auto it_d = m_floatVars.find("__fog_density");
    if (it_g != m_floatVars.end()) { g = it_g->second; m_floatVars.erase(it_g); }
    if (it_b != m_floatVars.end()) { b = it_b->second; m_floatVars.erase(it_b); }
    if (it_d != m_floatVars.end()) { density = it_d->second; m_floatVars.erase(it_d); }
    return true;
}

// ---------------------------------------------------------------------------
// PopPendingSkybox — return and clear __skybox
// ---------------------------------------------------------------------------
std::string LightningScriptContext::PopPendingSkybox() {
    auto it = m_strVars.find("__skybox");
    if (it == m_strVars.end()) return {};
    std::string name = it->second;
    m_strVars.erase(it);
    return name;
}

bool LightningScriptContext::PopPendingAmbient(float& r, float& g, float& b) {
    auto it_r = m_floatVars.find("__ambient_r");
    if (it_r == m_floatVars.end()) return false;
    r = it_r->second; m_floatVars.erase(it_r);
    auto it_g = m_floatVars.find("__ambient_g");
    auto it_b = m_floatVars.find("__ambient_b");
    if (it_g != m_floatVars.end()) { g = it_g->second; m_floatVars.erase(it_g); }
    if (it_b != m_floatVars.end()) { b = it_b->second; m_floatVars.erase(it_b); }
    return true;
}
