#include "LightningScriptParser.hpp"
#include <cstdio>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <exception>   // std::exception, caught in ReadNumber

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
    const std::string text = s.content->substr(start, s.pos - start);
    // std::stof throws std::invalid_argument on an empty or non-numeric string and
    // std::out_of_range on a huge one. This is called from the top-level body loop for
    // anim_speed / movement_speed and from ParseStatBlock, so a `.ozls` with a
    // non-numeric value where a number belongs terminated the process inside the
    // parser. Callers now check for a missing component before calling, but the guard
    // belongs here too: ReadNumber is public on the class, and a throwing number reader
    // is a trap for every future caller.
    if (text.empty()) return 0.0f;
    try {
        return std::stof(text);
    } catch (const std::exception&) {
        fprintf(stderr, "[LightningParser] %s:%d: '%s' is not a number — using 0\n",
                s.sourcePath.c_str(), s.line, text.c_str());
        return 0.0f;
    }
}

void LightningScriptParser::Expect(ParseState& s, const std::string& expected) {
    std::string tok = ReadToken(s);
    if (tok != expected) {
        fprintf(stderr, "[LightningParser] %s:%d: expected '%s', got '%s'\n",
                s.sourcePath.c_str(), s.line, expected.c_str(), tok.c_str());
    }
}

// Read a `(a, b, c)` tuple into `out`. Returns false and recovers if it is not one.
//
// TWO call sites need this and there were two hand-written copies that had drifted,
// which is how one got a bounds guard and the other did not. Both now share this.
//
// The failure this exists for: the original read three components by blind position
// (three ReadNumber calls, two ReadToken calls between them) with no check that
// anything was there. A short or unclosed tuple ran ReadNumber past the end of the
// content, and ReadNumber called std::stof on an empty string — std::invalid_argument,
// uncaught, terminating the process. Reproduced at exit 0xC0000409 with `k = (1,2)`,
// `k = (` and `k = ( )`.
//
// Note the original separator handling used ReadToken, which reads until whitespace or
// a block character — so it only worked because every shipped vec3 has a SPACE after
// each comma (`fog_color = (179, 179, 204)`). `(1,2)` lost the second component. This
// consumes exactly one separator character, so both spellings work.
//
// On failure: nothing is written, a warning naming the field is printed, and `s` is
// left on the next plausible boundary so the rest of the file still parses. A bad vec3
// costs one value, not the file.
bool LightningScriptParser::ReadVec3(ParseState& s, float out[3], const char* fieldName) {
    out[0] = out[1] = out[2] = 0.0f;
    SkipWhitespace(s);
    if (s.pos >= s.content->size() || (*s.content)[s.pos] != '(') {
        fprintf(stderr, "[LightningParser] %s:%d: '%s' — expected '(', skipping\n",
                s.sourcePath.c_str(), s.line, fieldName);
        return false;
    }
    const size_t openParen = s.pos;
    s.pos++;   // skip '('

    bool complete = true;
    for (int comp = 0; comp < 3; comp++) {
        if (comp > 0) {
            SkipWhitespace(s);
            if (s.pos >= s.content->size()) { complete = false; break; }
            const char sep = (*s.content)[s.pos];
            if (sep == ',' || sep == ';') {
                s.pos++;
            } else if (sep != ')' && sep != '}' && sep != '\n' && sep != '\r') {
                complete = false;   // missing separator
                break;
            }
        }
        SkipWhitespace(s);
        if (s.pos >= s.content->size()) { complete = false; break; }
        const char c = (*s.content)[s.pos];
        // Refuse to read a component from a closer, a newline, a stray comma or EOF.
        if (c == ')' || c == '}' || c == '\n' || c == '\r' || c == ',') {
            complete = false;
            break;
        }
        // Only a digit, sign or '.' can begin a number — this also rejects a second
        // '(' and any stray word, so `c = (a, b, c)` is malformed rather than 0.
        if (!(std::isdigit((unsigned char)c) || c == '-' || c == '+' || c == '.')) {
            complete = false;
            break;
        }
        out[comp] = ReadNumber(s);
    }

    if (complete) {
        SkipWhitespace(s);
        if (s.pos < s.content->size() && (*s.content)[s.pos] == ')') {
            s.pos++;
        } else {
            fprintf(stderr, "[LightningParser] %s:%d: '%s' is missing its ')' — "
                    "using the three values read\n",
                    s.sourcePath.c_str(), s.line, fieldName);
        }
        return true;
    }

    fprintf(stderr, "[LightningParser] %s:%d: malformed vec3 for '%s' (expected three "
            "numbers) — skipping this value\n",
            s.sourcePath.c_str(), s.line, fieldName);
    // Resume at the closing paren when one exists, else at the end of the line, never
    // past the enclosing block's own '}'.
    size_t scan = s.pos;
    while (scan < s.content->size() && (*s.content)[scan] != ')' &&
           (*s.content)[scan] != '\n') scan++;
    if (scan < s.content->size() && (*s.content)[scan] == ')') scan++;
    s.pos = (scan > openParen) ? scan : openParen + 1;
    return false;
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
        // vec3 form: key = (r, g, b). ReadVec3 owns the recovery and the bounds
        // checks — see its comment for the crash it prevents and why there is only one
        // implementation of this.
        SkipWhitespace(s);
        if (s.pos < s.content->size() && (*s.content)[s.pos] == '(') {
            float v[3];
            if (ReadVec3(s, v, key.c_str())) {
                block.vec3s[key][0] = v[0];
                block.vec3s[key][1] = v[1];
                block.vec3s[key][2] = v[2];
            }
            // On failure ReadVec3 left s on a sane boundary and wrote nothing, so the
            // malformed stat is simply absent rather than defaulted to (0,0,0) —
            // a silent black colour would be worse than a missing one.
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

        // Brace-style script normalization: strip trailing '{'/'}' block
        // delimiters (only when not inside a string literal — an odd number
        // of quotes means an unterminated string keeps the brace).
        // Turns `if ($x == 0) {` into `if ($x == 0)` and drops bare `}` lines.
        size_t quotes = std::count(trimmed.begin(), trimmed.end(), '"');
        if (quotes % 2 == 0) {
            while (!trimmed.empty() && (trimmed.back() == '{' || trimmed.back() == '}'))
                trimmed.pop_back();
            b = trimmed.find_last_not_of(" \t\r\n");
            if (b == std::string::npos) continue; // line was only delimiters
            trimmed = trimmed.substr(0, b + 1);
        }

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
    if (def.type == EntityType::UNKNOWN)
        fprintf(stderr, "[LightningParser] %s: unknown entity type '%s' — parsing body anyway\n", sourcePath.c_str(), typeName.c_str());

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
        } else if (tok == "mesh_type") {
            Expect(s, "=");
            def.meshType = ReadToken(s);
        } else if (tok == "anim_idle") {
            Expect(s, "=");
            def.animIdle = ReadToken(s);
        } else if (tok == "anim_patrol") {
            Expect(s, "=");
            def.animPatrol = ReadToken(s);
        } else if (tok == "anim_chase") {
            Expect(s, "=");
            def.animChase = ReadToken(s);
        } else if (tok == "anim_return") {
            Expect(s, "=");
            def.animReturn = ReadToken(s);
        } else if (tok == "anim_death") {
            Expect(s, "=");
            def.animDeath = ReadToken(s);
        } else if (tok == "anim_speed") {
            Expect(s, "=");
            def.animSpeed = ReadNumber(s);
        } else if (tok == "movement_speed") {
            Expect(s, "=");
            def.movementSpeed = ReadNumber(s);
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
            // skyzone-specific vec3 fields stored in stats.vec3s.
            //
            // This was a SECOND copy of the blind three-component read that crashed in
            // ParseStatBlock (a truncated or unclosed tuple ran ReadNumber off the end
            // and std::stof threw out of the parser). Both copies are now routed
            // through ReadVec3, so a malformed fog_color recovers the same way a
            // malformed stat does instead of being a second crash site.
            Expect(s, "=");
            float v[3];
            if (ReadVec3(s, v, tok.c_str())) {
                def.stats.vec3s[tok][0] = v[0];
                def.stats.vec3s[tok][1] = v[1];
                def.stats.vec3s[tok][2] = v[2];
            }
        } else if (tok == "fog_density") {
            Expect(s, "=");
            float v = ReadNumber(s);
            def.stats.floats["fog_density"] = v;
        } else {
            // Unknown key. Kept reading a value and discarding it so a typo
            // cannot cascade, but warn: without this the shipped
            // reverb_mix / reverb_decay keys in the Dust_Ravine and EngineTest
            // zone defs were dropped in total silence.
            //
            // Uses fprintf rather than OZ_WARN because this TU is linked into
            // several test targets that do not pull in Log.cpp.
            std::string eq = ReadToken(s);
            if (eq == "=") {
                std::string val = ReadToken(s);
                fprintf(stderr, "[LightningParser] %s: ignoring unknown key '%s = %s'\n",
                        def.sourcePath.c_str(), tok.c_str(), val.c_str());
            } else {
                fprintf(stderr, "[LightningParser] %s: ignoring unknown key '%s'\n",
                        def.sourcePath.c_str(), tok.c_str());
            }
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
