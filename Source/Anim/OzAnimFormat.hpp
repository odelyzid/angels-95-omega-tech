#pragma once
#include <string>
#include <vector>
#include <cstdint>

// ---------------------------------------------------------------------------
// OzAnim — vertex-keyframe (morph) animation format.
//
// Raylib-free so it can be parsed/serialized headless (tests) and shared.
// Each keyframe stores sparse per-vertex offset deltas that are ADDED to the
// model's base vertex positions during playback.
//
// Text grammar (case-sensitive keywords):
//
//   ozanim 1
//   clip "Idle" fps 30 loop 1
//   key 0.0
//     v 12  0.1 0.0 0.0
//     v 40  0.0 0.2 0.0
//   key 0.5
//     v 12  -0.1 0.0 0.0
//
// Vertex indices are GLOBAL across the model (all meshes concatenated in order).
// ---------------------------------------------------------------------------
namespace ozanim {

struct VertexOffset {
    int index = 0;
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
};

struct Keyframe {
    float time = 0.0f;                  // seconds
    std::vector<VertexOffset> offsets;  // sparse: only moved vertices
};

struct Clip {
    std::string name;
    float fps = 30.0f;
    bool loop = true;
    std::vector<Keyframe> keys;         // sorted by time

    // Last key time (0 when empty).
    float Duration() const {
        return keys.empty() ? 0.0f : keys.back().time;
    }

    // Fill `out` with the dense (vertexCount*3) offset at `time`, zero where a
    // vertex is untouched. Loops/clamps per the clip flags. Returns false only
    // on invalid input (vertexCount <= 0).
    bool SampleOffsets(float time, int vertexCount, std::vector<float>& out) const;
};

struct Animation {
    int version = 1;
    std::vector<Clip> clips;

    const Clip* FindClip(const std::string& name) const;
    Clip* FindClip(const std::string& name);
};

// Parse a whole file. Malformed lines are skipped; never throws.
Animation Parse(const std::string& text);

// Serialize back to the text format (round-trip stable).
std::string Serialize(const Animation& anim);

} // namespace ozanim
