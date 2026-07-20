#include "WDLParser.hpp"

static std::vector<std::string> split_colons(const std::string& s) {
    std::vector<std::string> parts;
    size_t start = 0, end;
    while ((end = s.find(':', start)) != std::string::npos) {
        if (end > start)
            parts.push_back(s.substr(start, end - start));
        else
            parts.push_back(""); // empty field
        start = end + 1;
    }
    if (start < s.size())
        parts.push_back(s.substr(start));
    return parts;
}

WDLElementType WDLParser::classify(const std::string& token) {
    if (token == "HeightMap") return WDLElementType::HEIGHTMAP;
    if (token == "Collision") return WDLElementType::COLLISION;
    if (token == "AdvCollision") return WDLElementType::ADV_COLLISION;
    if (token == "ClipBox") return WDLElementType::CLIP_BOX;
    if (token == "Light") return WDLElementType::LIGHT;
    if (token == "C") return WDLElementType::COL_FLAG;

    if (token.compare(0, 6, "Script") == 0) return WDLElementType::SCRIPT;
    if (token.compare(0, 6, "Object") == 0) return WDLElementType::OBJECT;
    if (token.compare(0, 2, "NE") == 0) return WDLElementType::NOISE_EMITTER;
    if (token.compare(0, 6, "Model[1-20]") == 0) return WDLElementType::MODEL;
    if (token == "Walker") return WDLElementType::ENTITY_WALKER;

    // Check if it matches Model#
    if (token.size() > 5 && token.substr(0, 5) == "Model") {
        std::string rest = token.substr(5);
        bool numeric = !rest.empty();
        for (char ch : rest) if (!isdigit(ch)) { numeric = false; break; }
        if (numeric) return WDLElementType::MODEL;
    }

    return WDLElementType::UNKNOWN;
}

std::vector<WDLElement> WDLParser::parse_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        fprintf(stderr, "WDL: cannot open %s\n", path.c_str());
        return {};
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return parse_string(content);
}

std::vector<WDLElement> WDLParser::parse_string(const std::string& content) {
    std::vector<WDLElement> elements;

    // The WDL format is colon-delimited flat KV. The first token of each
    // "instruction set" is the type, followed by positional args in subsequent
    // colon-separated fields. Count colons to segment:

    std::vector<std::string> fields = split_colons(content);

    // fields[0] = type name, fields[1..N] = args
    // Some lines have empty fields at the end (trailing colons)
    // Walk through fields — skip empty leading fields
    size_t i = 0;
    while (i < fields.size()) {
        // skip empties at start
        while (i < fields.size() && fields[i].empty()) i++;
        if (i >= fields.size()) break;

        std::string& token = fields[i];
        WDLElement elem;
        elem.type = classify(token);
        i++;

        switch (elem.type) {
            case WDLElementType::MODEL: {
                // fields[i] = id, but ModelN type includes the id, skip it
                // Pattern: Model2:x:y:z:scale:rotation:
                // fields: [Model2, x, y, z, scale, rotation]
                // So after the type token, we have x y z scale rotation
                // Actually, the "Model2" itself encodes the ID
                // Let's parse the numeric suffix:
                std::string num_str = token.substr(5);
                elem.model_id = std::stoi(num_str);
                // consume args
                for (int c = 0; c < 5 && i < fields.size(); c++, i++) {
                    try {
                        elem.args.push_back(std::stof(fields[i]));
                    } catch (...) {
                        elem.args.push_back(0.0f);
                    }
                }
                break;
            }
            case WDLElementType::HEIGHTMAP:
            case WDLElementType::COLLISION: {
                // HeightMap/Collision:x:y:z:scale:rotation:
                for (int c = 0; c < 5 && i < fields.size(); c++, i++) {
                    try { elem.args.push_back(std::stof(fields[i])); }
                    catch (...) { elem.args.push_back(0.0f); }
                }
                break;
            }
            case WDLElementType::ADV_COLLISION:
            case WDLElementType::CLIP_BOX: {
                // AdvCollision/ClipBox:x:y:z:scale:rotation:w:h:l:
                // Actually AdvCollision likely has: x y z scale rotation w h l
                for (int c = 0; c < 8 && i < fields.size(); c++, i++) {
                    try { elem.args.push_back(std::stof(fields[i])); }
                    catch (...) { elem.args.push_back(0.0f); }
                }
                break;
            }
            case WDLElementType::LIGHT: {
                // Light:x:y:z:
                for (int c = 0; c < 3 && i < fields.size(); c++, i++) {
                    try { elem.args.push_back(std::stof(fields[i])); }
                    catch (...) { elem.args.push_back(0.0f); }
                }
                break;
            }
            case WDLElementType::SCRIPT:
            case WDLElementType::OBJECT:
            case WDLElementType::NOISE_EMITTER: {
                // Script1/Object1/NE1:x:y:z:scale:rotation:
                std::string prefix;
                if (elem.type == WDLElementType::SCRIPT) prefix = "Script";
                else if (elem.type == WDLElementType::OBJECT) prefix = "Object";
                else prefix = "NE";
                std::string num_str = token.substr(prefix.size());
                try { elem.model_id = std::stoi(num_str); } catch (...) {}
                for (int c = 0; c < 5 && i < fields.size(); c++, i++) {
                    try { elem.args.push_back(std::stof(fields[i])); }
                    catch (...) { elem.args.push_back(0.0f); }
                }
                break;
            }
            case WDLElementType::ENTITY_WALKER: {
                // Walker:x:y:z:
                for (int c = 0; c < 3 && i < fields.size(); c++, i++) {
                    try { elem.args.push_back(std::stof(fields[i])); }
                    catch (...) { elem.args.push_back(0.0f); }
                }
                break;
            }
            case WDLElementType::COL_FLAG:
                // C: flag only, no args
                break;
            default:
                // Unknown — skip to next colon block
                break;
        }

        elements.push_back(std::move(elem));
    }

    return elements;
}