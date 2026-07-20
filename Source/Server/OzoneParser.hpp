#ifndef OMEGA_OZONE_PARSER_HPP
#define OMEGA_OZONE_PARSER_HPP

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <cstdint>

// Standalone .ozone parser — no raylib dependency.
// Parses the OZONE text format used by the OzWorld editor.

enum class OzonePrimitiveType : uint8_t {
    BOX,
    CYLINDER,
    SPHERE,
    PYRAMID,
    PLANE,
    UNKNOWN
};

struct OzonePrimitive {
    OzonePrimitiveType type;
    std::vector<float> args; // content varies by type
};

class OzoneParser {
public:
    static std::vector<OzonePrimitive> parse_file(const std::string& path);
    static std::vector<OzonePrimitive> parse_string(const std::string& content);
};

#endif // OMEGA_OZONE_PARSER_HPP