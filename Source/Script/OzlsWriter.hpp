#pragma once
// ---------------------------------------------------------------------------
// Script/OzlsWriter.hpp - write .ozls entity definitions back to disk
//
// The counterpart to LightningScriptParser. The parser is a pure reader and the
// AngelEd Entity Properties panel showed `.ozls stats` as read-only rows with
// an "Edit .ozls" button that merely handed the file to the OS editor, so stat
// values could only be changed outside the tool. This adds the write path, and
// it deliberately edits the existing text rather than regenerating it.
//
// PATCH, DO NOT REGENERATE
//
// The alternative - parse the def, mutate it, re-serialise the whole file -
// is much simpler but destroys the things authors actually put in a .ozls:
// the explanatory comments above a stat, the key order they chose, and any
// unknown key the parser only warned about (reverb_mix / reverb_decay in the
// shipped zone defs are exactly this). PatchOzlsStats rewrites individual
// `key = value` lines inside the existing `stats { ... }` span and touches
// nothing else, so an edit in the property panel is a one-line diff.
//
// SerializeEntityDef exists for the case where there is no `stats { }` block
// to patch at all (a brand-new def) and for the round-trip test. It is NOT the
// normal write path, and it cannot preserve comments because it never sees
// them.
//
// Raylib-free on purpose: AngelEd links this, and it is unit-tested through the
// same headless harness as the parser.
// ---------------------------------------------------------------------------

#include "LightningEntityDef.hpp"

#include <string>
#include <vector>

namespace ozls {

// How a stat value is written back. The parser files a stat under whichever
// of the three maps the value parsed as, so an editor has to write back the
// same shape it read or `Parse` would file it differently next time.
enum class StatKind {
    Float,   // damage = 14
    String,  // fire_sound = GameData/x.wav   (never quoted - see note below)
    Vec3,    // viewmodel_offset = (0.2, -0.1, 0.4)
};

// One requested change to the `stats` block.
//
// An empty `value` means "remove this key", which is how the property panel
// clears a field. `key` is matched exactly; there is no aliasing, so what the
// panel shows is what the file gets.
struct StatEdit {
    std::string key;
    std::string value;
    StatKind kind = StatKind::String;
};

// Result of a patch, so a caller can report what actually happened instead of
// assuming success.
struct PatchResult {
    bool ok = false;
    // Why it failed, or "" on success.
    std::string error;
    // True when the file had no `stats { }` block and one was synthesised.
    // Only then can an `insert` add a key; an `update`/`erase` against a
    // missing block fails rather than silently creating an empty one.
    bool createdStatsBlock = false;
    int inserted = 0;
    int updated = 0;
    int erased = 0;
};

// Rewrite selected stats in the `stats { ... }` block of the .ozls at
// `path`. The file is read, the individual `key = value` lines are
// replaced/inserted/erased in place, and it is written back.
//
// Returns false (with `error` set) if the file cannot be read or written, or if
// the edit is not applicable (see PatchResult). Never throws.
//
// Insertion order: new keys are appended just before the block's closing
// brace, so they do not reorder anything the author already wrote.
//
// QUOTING: string values are written bare, never wrapped in quotes. The
// parser stores a stats string verbatim with no quote handling
// (LightningScriptParser::ParseStatBlock), so a quoted path would arrive at
// the runtime still carrying its quotes and fail to resolve. The same applies
// to spaces: a value containing one would be silently truncated, so callers
// should reject such values rather than write a path that cannot work.
PatchResult PatchOzlsStats(const std::string& path,
                           const std::vector<StatEdit>& edits,
                           const std::string& indent = "        ");

// Canonical re-serialisation of a def. Used to create a new file and by the
// round-trip test. Cannot preserve comments, key order or unknown keys.
//
// Only emits the top-level body keys the parser understands, in a fixed order,
// plus `stats` (floats, then strings, then vec3s) and `actions`.
std::string SerializeEntityDef(const EntityDef& def, const std::string& indent = "    ");

}  // namespace ozls