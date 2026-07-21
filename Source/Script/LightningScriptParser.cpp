#include "LightningScriptParser.hpp"
#include <cstdio>
#include <cctype>
#include <algorithm>
#include <sstream>

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------
void LightningScriptParser::SkipWhitespace(ParseState& s) {
    while (s.pos < s.content->size() && ((*s.content)[s.pos] == ' ' ||
           (*s.content)[s.pos] == '\t' || (*s.content)[s.pos] == '\r')) s.pos++;
    while (s.pos < s.content->size() && (*s.content)[s.pos] == '\n') { s.pos++; s.line++; SkipWhitespace(s); }
    // Skip single-line comments
    if (s.pos + 1 < s.content->size() && (*s.content)[s.pos] == '/' && (*s.content)[s.pos+1] == '/') {
        while (s.pos < s.content->size() && (*s.content)[s.pos] != '\n') s.pos++;
        if (s.pos < s.content->size()) { s.pos++; s.line++; }
        SkipWhitespace(s);
    }
    if (s.pos < s.content->size() && (*s.content)[s.pos] == '#') {
        while (s.pos < s.content->size() && (*s.content)[s.pos] != '\n') s.pos++;
        if (s.pos < s.content->size()) { s.pos++; s.line++; }
        SkipWhitespace(s);
    }
}

std::string LightningScriptParser::ReadToken(ParseState& s) {
    SkipWhitespace(s);
    size_t start = s.pos;
    if (start >= s.content->size()) return "";
    // Check for string literal
    if ((*s.content)[start] == '"') return ReadString(s);
    // Check for block characters
    if ((*s.content)[start] == '{' || (*s.content)[start] == '}' ||
        (*s.content)[start] == '=' || (*s.content)[start] == ':') {
        s.pos++;
        return std::string(1, (*s.content)[start]);
    }
    while (s.pos < s.content->size() && !std::isblank((*s.content)[s.pos]) &&
           (*s.content)[s.pos] != '{' && (*s.content)[s.pos] != '}' &&
           (*s.content)[s.pos] != '=' && (*s.content)[s.pos] != ':' &&
           (*s.content)[s.pos] != '\n' && (*s.content)[s.pos] != '\r') s.pos++;
    return s.content->substr(start, s.pos - start);
}

std::string LightningScriptParser::ReadString(ParseState& s) {
    SkipWhitespace(s);
    if (s.pos >= s.content->size() || (*s.content)[s.pos] != '"') return "";
    s.pos++; // skip opening quote
    size_t start = s.pos;
    while (s.pos < s.content->size() && (*s.content)[s.pos] != '"') s.pos++;
    std::string result = s.content->substr(start, s.pos - start);
    if (s.pos < s.content->size()) s.pos++; // skip closing quote
    return result;
}

float LightningScriptParser::ReadNumber(ParseState& s) {
    SkipWhitespace(s);
    size_t start = s.pos;
    bool dot = false;
    while (s.pos < s.content->size() &&
           (std::isdigit((*s.content)[s.pos]) || (*s.content)[s.pos] == '.' || (*s.content)[s.pos] == '-')) {
        if ((*s.content)[s.pos] == '.') { if (dot) break; dot = true; }
        s.pos++;
    }
    return std::stof(s.content->substr(start, s.pos - start));
}

void LightningScriptParser::Expect(ParseState& s, const std::string& expected) {
    std::string tok = ReadToken(s);
    if (tok != expected) {
        fprintf(stderr, "[LightningParser] %s:%d: expected '%s', got '%s'\n",
                s.sourcePath.c_str(), s.line, expected.c_str(), tok.c_str());
    }
}

void LightningScriptParser::SkipBlock(ParseState& s) {
    int depth = 1;
    while (s.pos < s.content->size() && depth > 0) {
        std::string tok = ReadToken(s);
        if (tok == "{") depth++;
        else if (tok == "}") depth--;
        if (tok.empty()) break;
    }
}

// ---------------------------------------------------------------------------
// ParseStatBlock — parses key = value / key = (r,g,b) inside { }
// ---------------------------------------------------------------------------
EntityStatBlock LightningScriptParser::ParseStatBlock(ParseState& s) {
    EntityStatBlock block;
    Expect(s, "{");
    while (s.pos < s.content->size()) {
        std::string key = ReadToken(s);
        if (key == "}") break;
        std::string eq = ReadToken(s);
        if (eq == "}") { /* unexpected close */ break; }
        if (eq != "=") {
            fprintf(stderr, "[LightningParser] %s:%d: expected '=', got '%s'\n",
                    s.sourcePath.c_str(), s.line, eq.c_str());
            break;
        }
        // Check for vec3: (r, g, b)
        SkipWhitespace(s);
        if (s.pos < s.content->size() && (*s.content)[s.pos] == '(') {
            s.pos++; // skip (
            float v[3];
            v[0] = ReadNumber(s);
            ReadToken(s); // skip comma or close
            v[1] = ReadNumber(s);
            ReadToken(s);
            v[2] = ReadNumber(s);
            ReadToken(s); // skip )
            block.vec3s[key][0] = v[0];
            block.vec3s[key][1] = v[1];
            block.vec3s[key][2] = v[2];
        } else {
            SkipWhitespace(s);
            std::string valStr;
            size_t valStart = s.pos;
            while (s.pos < s.content->size() && !std::isblank((*s.content)[s.pos]) &&
                   (*s.content)[s.pos] != '}' && (*s.content)[s.pos] != '\n') s.pos++;
            valStr = s.content->substr(valStart, s.pos - valStart);
            // Try as float first
            char* end = nullptr;
            float fv = std::strtof(valStr.c_str(), &end);
            if (end && *end == '\0')
                block.floats[key] = fv;
            else
                block.strings[key] = valStr;
        }
    }
    return block;
}

// ---------------------------------------------------------------------------
// ParseAction — reads name, then parses block
// ---------------------------------------------------------------------------
EntityAction LightningScriptParser::ParseAction(ParseState& s) {
    std::string name = ReadToken(s);
    return ParseActionWithName(name, s);
}

// ParseActionWithName — parses { ... lines ... } with name already known
// ---------------------------------------------------------------------------
EntityAction LightningScriptParser::ParseActionWithName(const std::string& name, ParseState& s) {
    EntityAction action;
    action.name = name;
    Expect(s, "{");
    // Extract raw content between braces as script lines
    int depth = 1;
    size_t blockStart = s.pos;
    // First pass: find the closing brace at matching depth
    size_t tmp = blockStart;
    while (tmp < s.content->size() && depth > 0) {
        char c = (*s.content)[tmp];
        if (c == '{') depth++;
        else if (c == '}') depth--;
        if (depth > 0) tmp++;
    }
    // Extract the raw block text (excluding outer braces)
    std::string rawBlock = s.content->substr(blockStart, tmp - blockStart);
    // Split into lines
    std::istringstream stream(rawBlock);
    std::string line;
    while (std::getline(stream, line)) {
        size_t a = line.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t\r\n");
        std::string trimmed = line.substr(a, b - a + 1);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        action.scriptLines.push_back(trimmed);
    }
    // Advance parser state past the closing brace
    s.pos = tmp + 1;
    return action;
}

// ---------------------------------------------------------------------------
// Parse — main entry: parse a single .ozls file into EntityDef
// ---------------------------------------------------------------------------
EntityDef LightningScriptParser::Parse(const std::string& content, const std::string& sourcePath) {
    EntityDef def;
    ParseState s;
    s.content = &content;
    s.pos = 0;
    s.line = 1;
    s.sourcePath = sourcePath;
    def.sourcePath = sourcePath;

    // Expected format: entity "name" : type { ... }
    std::string kw = ReadToken(s);
    if (kw != "entity") {
        fprintf(stderr, "[LightningParser] %s: expected 'entity', got '%s'\n", sourcePath.c_str(), kw.c_str());
        return def;
    }

    def.name = ReadString(s);
    if (def.name.empty()) {
        fprintf(stderr, "[LightningParser] %s: expected entity name string\n", sourcePath.c_str());
        return def;
    }

    std::string colon = ReadToken(s);
    if (colon != ":") {
        fprintf(stderr, "[LightningParser] %s: expected ':', got '%s'\n", sourcePath.c_str(), colon.c_str());
        return def;
    }

    std::string typeName = ReadToken(s);
    def.type = EntityTypeFromName(typeName);
    if (def.type == EntityType::UNKNOWN) {
        fprintf(stderr, "[LightningParser] %s: unknown entity type '%s'\n", sourcePath.c_str(), typeName.c_str());
        return def;
    }

    Expect(s, "{");

    // Parse body
    while (s.pos < s.content->size()) {
        std::string tok = ReadToken(s);
        if (tok == "}") break;
        if (tok.empty()) break;

        if (tok == "mesh") {
            Expect(s, "=");
            def.mesh = ReadToken(s);
        } else if (tok == "texture") {
            Expect(s, "=");
            def.texture = ReadToken(s);
        } else if (tok == "icon") {
            Expect(s, "=");
            def.icon = ReadToken(s);
        } else if (tok == "skybox") {
            Expect(s, "=");
            def.skybox = ReadToken(s);
        } else if (tok == "music") {
            Expect(s, "=");
            def.music = ReadToken(s);
        } else if (tok == "stats") {
            def.stats = ParseStatBlock(s);
        } else if (tok == "actions") {
            Expect(s, "{");
            while (s.pos < s.content->size()) {
                std::string atok = ReadToken(s);
                if (atok == "}") break;
                // atok is the action name (on_fire, on_enter, etc.)
                EntityAction act = ParseActionWithName(atok, s);
                def.actions.push_back(act);
            }
        } else if (tok == "variants") {
            Expect(s, "{");
            while (s.pos < s.content->size()) {
                std::string vtok = ReadToken(s);
                if (vtok == "}") break;
                // vtok is the variant name (e.g. "lvl1")
                EntityVariant var;
                var.name = vtok;
                Expect(s, "{");
                while (s.pos < s.content->size()) {
                    std::string vkey = ReadToken(s);
                    if (vkey == "}") break;
                    std::string veq = ReadToken(s);
                    std::string vval = ReadToken(s);
                    if (vkey == "mesh_override") var.meshOverride = vval;
                    else if (vkey == "texture_override") var.textureOverride = vval;
                }
                def.variants.push_back(var);
            }
        } else if (tok == "fog_color" || tok == "ambient_light") {
            // skyzone-specific vec3 fields stored in stats.vec3s
            Expect(s, "=");
            SkipWhitespace(s);
            if (s.pos < s.content->size() && (*s.content)[s.pos] == '(') {
                s.pos++;
                float v[3];
                v[0] = ReadNumber(s); ReadToken(s);
                v[1] = ReadNumber(s); ReadToken(s);
                v[2] = ReadNumber(s); ReadToken(s);
                def.stats.vec3s[tok][0] = v[0];
                def.stats.vec3s[tok][1] = v[1];
                def.stats.vec3s[tok][2] = v[2];
            }
        } else if (tok == "fog_density") {
            Expect(s, "=");
            float v = ReadNumber(s);
            def.stats.floats["fog_density"] = v;
        } else {
            // Unknown key — skip value
            std::string eq = ReadToken(s);
            if (eq == "=") { ReadToken(s); }
        }
    }

    return def;
}

// ---------------------------------------------------------------------------
// ParseAll — parse multiple files
// ---------------------------------------------------------------------------
std::vector<EntityDef> LightningScriptParser::ParseAll(
    const std::vector<std::string>& fileContents,
    const std::vector<std::string>& sourcePaths)
{
    std::vector<EntityDef> result;
    for (size_t i = 0; i < fileContents.size(); i++) {
        std::string path = i < sourcePaths.size() ? sourcePaths[i] : "";
        EntityDef def = Parse(fileContents[i], path);
        if (def.type != EntityType::UNKNOWN)
            result.push_back(def);
    }
    return result;
}
