#include "OzAnimFormat.hpp"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <cctype>

namespace ozanim {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Strip a trailing comment ('#' or "//") unless inside a quoted string.
static std::string StripComment(const std::string& s) {
    bool inQuote = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '"') inQuote = !inQuote;
        else if (!inQuote && c == '#') return s.substr(0, i);
        else if (!inQuote && c == '/' && i + 1 < s.size() && s[i + 1] == '/')
            return s.substr(0, i);
    }
    return s;
}

// Read a (possibly quoted) token, advancing the stream.
static std::string ReadToken(std::istringstream& ls) {
    std::string t;
    if (!(ls >> t)) return "";
    if (!t.empty() && t[0] == '"') {
        // Quoted string: accumulate until closing quote.
        if (t.size() >= 2 && t.back() == '"') return t.substr(1, t.size() - 2);
        std::string out = t.substr(1);
        std::string part;
        while (ls >> part) {
            if (!part.empty() && part.back() == '"') {
                out += " " + part.substr(0, part.size() - 1);
                break;
            }
            out += " " + part;
        }
        return out;
    }
    return t;
}

static std::string FormatFloat(float v) {
    std::ostringstream os;
    os << std::setprecision(6) << v;
    std::string s = os.str();
    // Trim trailing zeros / dot for compactness.
    if (s.find('.') != std::string::npos && s.find('e') == std::string::npos) {
        size_t last = s.find_last_not_of('0');
        if (last != std::string::npos && s[last] == '.') last--;
        s = s.substr(0, last + 1);
    }
    return s;
}

static void FillDense(const Keyframe& k, int vertexCount, std::vector<float>& dense) {
    dense.assign((size_t)vertexCount * 3, 0.0f);
    for (const auto& o : k.offsets) {
        if (o.index < 0 || o.index >= vertexCount) continue;
        dense[(size_t)o.index * 3 + 0] = o.dx;
        dense[(size_t)o.index * 3 + 1] = o.dy;
        dense[(size_t)o.index * 3 + 2] = o.dz;
    }
}

bool Clip::SampleOffsets(float time, int vertexCount, std::vector<float>& out) const {
    if (vertexCount <= 0) return false;
    out.assign((size_t)vertexCount * 3, 0.0f);
    if (keys.empty()) return true;

    float dur = Duration();
    if (loop && dur > 0.0f) {
        time = std::fmod(time, dur);
        if (time < 0.0f) time += dur;
    } else {
        if (time < keys.front().time) time = keys.front().time;
        if (time > keys.back().time) time = keys.back().time;
    }

    // Find the first key at/after `time`.
    size_t i1 = 0;
    while (i1 < keys.size() && keys[i1].time < time) i1++;
    if (i1 == 0) { FillDense(keys.front(), vertexCount, out); return true; }
    if (i1 >= keys.size()) { FillDense(keys.back(), vertexCount, out); return true; }

    const Keyframe& k0 = keys[i1 - 1];
    const Keyframe& k1 = keys[i1];
    float span = k1.time - k0.time;
    float t = (span > 1e-6f) ? (time - k0.time) / span : 0.0f;

    std::vector<float> a, b;
    FillDense(k0, vertexCount, a);
    FillDense(k1, vertexCount, b);
    for (size_t k = 0; k < out.size(); k++)
        out[k] = a[k] + (b[k] - a[k]) * t;
    return true;
}

const Clip* Animation::FindClip(const std::string& name) const {
    for (const auto& c : clips)
        if (c.name == name) return &c;
    return nullptr;
}

Clip* Animation::FindClip(const std::string& name) {
    for (auto& c : clips)
        if (c.name == name) return &c;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Parse
// ---------------------------------------------------------------------------
Animation Parse(const std::string& text) {
    Animation anim;
    std::istringstream input(text);
    std::string raw;
    Clip* cur = nullptr;
    Keyframe* key = nullptr;

    while (std::getline(input, raw)) {
        std::string line = Trim(StripComment(raw));
        if (line.empty()) continue;
        std::istringstream ls(line);
        std::string tok;
        ls >> tok;

        if (tok == "ozanim") {
            ls >> anim.version;
        } else if (tok == "clip") {
            Clip c;
            c.name = ReadToken(ls);
            std::string w;
            while (ls >> w) {
                if (w == "fps") { ls >> c.fps; }
                else if (w == "loop") { int v = 1; ls >> v; c.loop = (v != 0); }
            }
            anim.clips.push_back(std::move(c));
            cur = &anim.clips.back();
            key = nullptr;
        } else if (tok == "key" && cur) {
            Keyframe kf;
            ls >> kf.time;
            cur->keys.push_back(std::move(kf));
            key = &cur->keys.back();
        } else if (tok == "v" && key) {
            VertexOffset o;
            if (ls >> o.index >> o.dx >> o.dy >> o.dz)
                key->offsets.push_back(o);
        }
    }

    // Normalize: sort keys by time, offsets by index.
    for (auto& c : anim.clips) {
        std::sort(c.keys.begin(), c.keys.end(),
                  [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
        for (auto& k : c.keys)
            std::sort(k.offsets.begin(), k.offsets.end(),
                      [](const VertexOffset& a, const VertexOffset& b) { return a.index < b.index; });
    }
    return anim;
}

// ---------------------------------------------------------------------------
// Serialize
// ---------------------------------------------------------------------------
std::string Serialize(const Animation& anim) {
    std::ostringstream os;
    os << "ozanim " << anim.version << "\n";
    for (const auto& c : anim.clips) {
        os << "clip \"" << c.name << "\" fps " << FormatFloat(c.fps)
           << " loop " << (c.loop ? 1 : 0) << "\n";
        for (const auto& k : c.keys) {
            os << "key " << FormatFloat(k.time) << "\n";
            for (const auto& o : k.offsets) {
                os << "  v " << o.index << " "
                   << FormatFloat(o.dx) << " " << FormatFloat(o.dy) << " " << FormatFloat(o.dz)
                   << "\n";
            }
        }
    }
    return os.str();
}

} // namespace ozanim
