#include "OzlsWriter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace ozls {
namespace {

// ---------------------------------------------------------------------------
// Line helpers
// ---------------------------------------------------------------------------

// Leading whitespace of `line`, preserved when a line is rewritten so an edit
// does not reflow the file's indentation.
std::string LeadingWS(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
    return line.substr(0, i);
}

std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Strip a trailing line comment, but only outside quotes - a path could
// legitimately contain '#'. The parser treats '#' as a comment introducer
// regardless of position, so this mirrors it rather than trying to be cleverer.
std::string StripComment(const std::string& s) {
    bool inQuote = false;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '"') inQuote = !inQuote;
        else if (s[i] == '#' && !inQuote) return s.substr(0, i);
    }
    return s;
}

// The identifier at the start of a `key = value` line, or "" when the line is
// not one (blank, a comment, a bare '}', or a nested block header).
//
// Deliberately line-based rather than brace-aware: inside `stats { }` every
// line is a flat assignment, so this is sufficient and it cannot be fooled by
// braces appearing inside a value.
std::string AssignmentKey(const std::string& line) {
    std::string body = Trim(StripComment(line));
    if (body.empty() || body[0] == '{' || body[0] == '}') return "";

    size_t i = 0;
    while (i < body.size() && (std::isalnum((unsigned char)body[i]) || body[i] == '_')) i++;
    if (i == 0) return "";

    std::string key = body.substr(0, i);
    // Must be followed by '=' to be an assignment rather than a stray token.
    size_t j = i;
    while (j < body.size() && (body[j] == ' ' || body[j] == '\t')) j++;
    if (j >= body.size() || body[j] != '=') return "";
    return key;
}

// The literal text a `key = value` line already carries after the '='.
std::string AssignmentValue(const std::string& line) {
    std::string body = Trim(StripComment(line));
    size_t eq = body.find('=');
    if (eq == std::string::npos) return "";
    return Trim(body.substr(eq + 1));
}

// Find the line range of the `stats { ... }` block. Returns false when there
// is none.
//
// Scans for a line whose first token is exactly `stats` followed by '{', then
// tracks brace depth to its match.
//
// Two things this must get right:
//
//  1. A '}' inside a VALUE must not close the block. `stats` lines are flat
//     `key = value` assignments, so a '}' after the '=' is part of the value.
//     Without that check the block ends early, every later key falls outside
//     it, and an edit silently appends a duplicate key instead of updating in
//     place. Mutation-tested: removing the check fails
//     test_brace_inside_value_does_not_end_block.
//
//  2. The depth tracking keeps the block scan from being confused by braces
//     in the rest of the file, and preserves the nested `actions { on_fire {
//     ... } }` block that follows `stats`
//     (test_preserves_comments_and_order, test_nested_actions_block_not_confused).
//     No shipped .ozls nests a block INSIDE `stats`, so the depth counter
//     never actually exceeds 1 for real input - this is structural robustness,
//     not a case reachable today.
//
// Note the parser cannot currently read a value containing an unquoted '}' -
// it runs off the end of the content - so that case is defensive robustness
// rather than something an author can actually author today. The brace tests
// assert on written text and deliberately do not re-parse.
bool FindStatsBlock(const std::vector<std::string>& lines,
                    size_t& outStart, size_t& outEnd) {
    for (size_t i = 0; i < lines.size(); i++) {
        std::string body = Trim(StripComment(lines[i]));
        if (body.rfind("stats", 0) != 0) continue;

        // The token after `stats` must be '{' - this rejects a key that merely
        // starts with the same letters.
        size_t j = 5;
        while (j < body.size() && (body[j] == ' ' || body[j] == '\t')) j++;
        if (j >= body.size() || body[j] != '{') continue;

        int depth = 0;
        for (size_t k = i; k < lines.size(); k++) {
            const std::string body = Trim(StripComment(lines[k]));
            bool inQuote = false;
            for (size_t c = 0; c < body.size(); c++) {
                if (body[c] == '"') { inQuote = !inQuote; continue; }
                if (inQuote) continue;
                if (body[c] == '{') { depth++; continue; }
                if (body[c] != '}') continue;

                // A '}' only counts when it actually closes a block. Inside
                // `stats { }` every line is a flat `key = value`, so a '}' that
                // appears AFTER an '=' is part of a value (e.g. note = a}b) and
                // must be ignored. Without this the block ends early and every
                // later key silently falls outside it, so an edit appends a
                // duplicate instead of updating in place.
                const size_t eq = body.find('=');
                const bool braceIsInValue = (eq != std::string::npos && c > eq);
                if (braceIsInValue) continue;

                depth--;
                if (depth == 0) {
                    outStart = i;
                    outEnd = k;
                    return true;
                }
            }
        }
        return false;   // unterminated
    }
    return false;
}

// Render a value for its kind. Strings are written bare: the parser stores a
// stats string verbatim with no quote handling, so quotes would survive into
// the runtime path and break resolution.
std::string RenderValue(const StatEdit& e) {
    switch (e.kind) {
        case StatKind::Vec3:
            // Already formatted as "(a, b, c)" by the caller.
            return e.value;
        case StatKind::Float:
        case StatKind::String:
        default:
            return e.value;
    }
}

// "%g"-style float formatting that keeps a decimal point off whole numbers,
// matching FormatStat in the editor panel so the panel and the file agree.
std::string FormatFloat(float v) {
    char buf[32];
    if (v == (float)(int)v && v > -1e9f && v < 1e9f)
        snprintf(buf, sizeof(buf), "%d", (int)v);
    else
        snprintf(buf, sizeof(buf), "%g", (double)v);
    return std::string(buf);
}

}  // namespace

// ---------------------------------------------------------------------------
// PatchOzlsStats
// ---------------------------------------------------------------------------
PatchResult PatchOzlsStats(const std::string& path,
                           const std::vector<StatEdit>& edits,
                           const std::string& indent) {
    PatchResult res;
    if (edits.empty()) {
        res.ok = true;
        return res;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        res.error = "cannot read '" + path + "'";
        return res;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    in.close();

    std::string text = ss.str();
    const bool hadTrailingNewline = !text.empty() && text.back() == '\n';

    // Split on '\n' keeping empty lines, so rejoining reproduces the original
    // byte-for-byte when nothing is edited (CRLF is preserved because '\r'
    // stays at the end of each line and is only touched where we rewrite).
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : text) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else cur += c;
        }
        if (!cur.empty()) lines.push_back(cur);
    }

    size_t start = 0, end = 0;
    const bool haveBlock = FindStatsBlock(lines, start, end);

    if (!haveBlock) {
        // With no stats block there is nothing to patch, so every edit becomes
        // an insert. That is legitimate (a brand-new def), but an ERASE is
        // unambiguously a caller bug: it asserts the key exists, and there is
        // no block for it to exist in. Rejecting it avoids fabricating an empty
        // block to hold a deletion.
        //
        // Note the polarity: a non-empty value is an insert/update, an empty
        // value is an erase.
        for (const StatEdit& e : edits) {
            if (e.key.empty()) continue;
            if (e.value.empty()) {
                res.error = "'" + path + "' has no stats block; cannot erase '" +
                            e.key + "'";
                return res;
            }
        }
    }

    // Collect what must be inserted, in edit order, so appends keep the order
    // the caller supplied rather than the order of a hash map.
    std::vector<std::string> appends;

    if (haveBlock) {
        for (const StatEdit& e : edits) {
            if (e.key.empty()) continue;

            bool found = false;
            for (size_t i = start + 1; i < end && !found; i++) {
                if (AssignmentKey(lines[i]) != e.key) continue;
                found = true;

                const std::string value = RenderValue(e);
                if (value.empty()) {
                    // Erase the whole line, including its newline, rather than
                    // leaving a blank row behind.
                    lines.erase(lines.begin() + (long)i);
                    end--;
                    res.erased++;
                } else {
                    const std::string ws = LeadingWS(lines[i]);
                    const bool hadCR = !lines[i].empty() && lines[i].back() == '\r';
                    std::string rewritten = ws + e.key + " = " + value;
                    if (hadCR) rewritten += "\r";
                    lines[i] = rewritten;
                    res.updated++;
                }
            }

            if (!found && !e.value.empty())
                appends.push_back(indent + e.key + " = " + RenderValue(e));
        }

        if (!appends.empty()) {
            // Insert before the closing brace, after its indentation if it has
            // any, so the block does not end up visually ragged.
            size_t at = end;
            const std::string closeWs = LeadingWS(lines[end]);
            std::vector<std::string> newLines;
            for (const std::string& a : appends) {
                newLines.push_back(a);
                res.inserted++;
            }
            if (closeWs.empty() && !lines[end].empty() && lines[end].back() == '\r')
                newLines.back() += "\r";
            lines.insert(lines.begin() + (long)at, newLines.begin(), newLines.end());
        }
    } else {
        // Synthesise the block immediately before the entity's closing brace.
        // Find the last '}' in the file, which is the entity body close.
        size_t bodyEnd = lines.size();
        while (bodyEnd > 0 && Trim(StripComment(lines[bodyEnd - 1])).empty()) bodyEnd--;
        if (bodyEnd > 0) bodyEnd--;   // step off the closing '}' line

        std::vector<std::string> block;
        block.push_back(indent.substr(0, indent.size() >= 4 ? indent.size() - 4 : 0) + "stats {");
        for (const StatEdit& e : edits) {
            if (e.key.empty() || e.value.empty()) continue;
            block.push_back(indent + e.key + " = " + RenderValue(e));
            res.inserted++;
        }
        block.push_back(indent.substr(0, indent.size() >= 4 ? indent.size() - 4 : 0) + "}");

        lines.insert(lines.begin() + (long)bodyEnd, block.begin(), block.end());
        res.createdStatsBlock = true;
    }

    std::string out;
    for (const std::string& l : lines) { out += l; out += '\n'; }
    // Preserve the absence of a trailing newline rather than adding one.
    if (!hadTrailingNewline && !out.empty() && out.back() == '\n')
        out.pop_back();

    std::ofstream of(path, std::ios::binary | std::ios::trunc);
    if (!of) {
        res.error = "cannot write '" + path + "'";
        return res;
    }
    of << out;
    of.close();
    if (!of) {
        res.error = "write failed for '" + path + "'";
        return res;
    }

    res.ok = true;
    return res;
}

// ---------------------------------------------------------------------------
// SerializeEntityDef
// ---------------------------------------------------------------------------
std::string SerializeEntityDef(const EntityDef& def, const std::string& indent) {
    const std::string in2 = indent + indent;

    std::ostringstream o;
    o << "entity \"" << def.name << "\" : " << EntityTypeName(def.type) << " {\n";

    auto body = [&](const char* k, const std::string& v) {
        if (!v.empty()) o << indent << k << " = \"" << v << "\"\n";
    };
    body("mesh", def.mesh);
    body("texture", def.texture);
    body("icon", def.icon);
    body("skybox", def.skybox);
    body("music", def.music);
    if (!def.meshType.empty())   o << indent << "mesh_type = \"" << def.meshType << "\"\n";
    body("anim_idle", def.animIdle);
    body("anim_patrol", def.animPatrol);
    body("anim_chase", def.animChase);
    body("anim_return", def.animReturn);
    body("anim_death", def.animDeath);
    if (def.animSpeed != 1.0f)
        o << indent << "anim_speed = " << FormatFloat(def.animSpeed) << "\n";
    if (def.movementSpeed != 1.0f)
        o << indent << "movement_speed = " << FormatFloat(def.movementSpeed) << "\n";

    if (!def.stats.floats.empty() || !def.stats.strings.empty() || !def.stats.vec3s.empty()) {
        o << indent << "stats {\n";
        // Sorted so the output is deterministic; unordered_map iteration order
        // is not, and a file that reshuffles itself on every save is unreadable.
        std::vector<std::pair<std::string, float>> fs(def.stats.floats.begin(), def.stats.floats.end());
        std::sort(fs.begin(), fs.end());
        for (const auto& [k, v] : fs)
            o << in2 << k << " = " << FormatFloat(v) << "\n";

        std::vector<std::pair<std::string, std::string>> ss(def.stats.strings.begin(), def.stats.strings.end());
        std::sort(ss.begin(), ss.end());
        for (const auto& [k, v] : ss)
            o << in2 << k << " = " << v << "\n";   // bare: see the header note

        std::vector<std::pair<std::string, std::array<float, 3>>> vs;
        for (const auto& [k, v] : def.stats.vec3s) vs.push_back({k, {v[0], v[1], v[2]}});
        std::sort(vs.begin(), vs.end());
        for (const auto& [k, v] : vs)
            o << in2 << k << " = (" << FormatFloat(v[0]) << ", " << FormatFloat(v[1])
              << ", " << FormatFloat(v[2]) << ")\n";

        o << indent << "}\n";
    }

    if (!def.actions.empty()) {
        o << indent << "actions {\n";
        for (const EntityAction& a : def.actions) {
            o << in2 << a.name << " {\n";
            for (const std::string& l : a.scriptLines)
                o << in2 << indent << l << "\n";
            o << in2 << "}\n";
        }
        o << indent << "}\n";
    }

    o << "}\n";
    return o.str();
}

}  // namespace ozls