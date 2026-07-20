#ifndef OMEGA_WDL_PARSER_HPP
#define OMEGA_WDL_PARSER_HPP

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstdint>

// Standalone WDL parser — no raylib dependency.
// Used by oz_server to serialize .wdl files to JSON.

enum class WDLElementType : uint8_t {
    MODEL,
    COLLISION,
    ADV_COLLISION,
    CLIP_BOX,
    LIGHT,
    HEIGHTMAP,
    SCRIPT,
    OBJECT,
    NOISE_EMITTER,
    ENTITY_WALKER,
    COL_FLAG,
    UNKNOWN
};

struct WDLElement {
    WDLElementType type = WDLElementType::UNKNOWN;
    int model_id = 0;       // for MODEL, SCRIPT, OBJECT, NOISE_EMITTER
    std::vector<float> args; // x,y,z,scale,rotation,... depending on type
};

class WDLParser {
public:
    // Parse a .wdl file. Returns empty vector on failure.
    static std::vector<WDLElement> parse_file(const std::string& path);

    // Parse already-loaded content string.
    static std::vector<WDLElement> parse_string(const std::string& content);

private:
    static WDLElementType classify(const std::string& token);
};

#endif // OMEGA_WDL_PARSER_HPP