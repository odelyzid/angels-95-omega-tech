#include "WDLParser.hpp"
#include <cctype>
#include <cstdio>

static std::string trim(const std::string& value) {
    size_t first = 0;
    while (first < value.size() && std::isspace((unsigned char)value[first])) first++;
    size_t last = value.size();
    while (last > first && std::isspace((unsigned char)value[last - 1])) last--;
    return value.substr(first, last - first);
}

static std::string normalize_content(const std::string& content) {
    std::istringstream stream(content);
    std::string normalized;
    std::string line;
    while (std::getline(stream, line)) {
        size_t comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);
        line = trim(line);
        if (line.empty()) continue;
        normalized += line;
        if (normalized.back() != ':') normalized += ':';
    }
    return normalized;
}

static std::vector<std::string> split_colons(const std::string& s) {
    std::vector<std::string> parts;
    size_t start = 0, end;
    while ((end = s.find(':', start)) != std::string::npos) {
        if (end > start)
            parts.push_back(trim(s.substr(start, end - start)));
        else
            parts.push_back(""); // empty field
        start = end + 1;
    }
    if (start < s.size())
        parts.push_back(trim(s.substr(start)));
    return parts;
}

WDLElementType WDLParser::classify(const std::string& token) {
    if (token == "HeightMap") return WDLElementType::HEIGHTMAP;
    if (token == "Collision") return WDLElementType::COLLISION;
    if (token == "AdvCollision") return WDLElementType::ADV_COLLISION;
    if (token == "ClipBox") return WDLElementType::CLIP_BOX;
    if (token == "Light") return WDLElementType::LIGHT;
    if (token == "C") return WDLElementType::COL_FLAG;
    if (token == "Spawn") return WDLElementType::SPAWN;
    if (token == "Pickup") return WDLElementType::PICKUP;
    if (token == "ZoneInfo") return WDLElementType::ZONE_INFO;
    if (token == "Fog") return WDLElementType::FOG;
    if (token == "Ambient") return WDLElementType::AMBIENT_TYPE;
    if (token == "LightType") return WDLElementType::LIGHT_TYPE;
    if (token == "NPC") return WDLElementType::NPC;

    if (token.compare(0, 6, "Script") == 0) return WDLElementType::SCRIPT;
    if (token.compare(0, 6, "Object") == 0) return WDLElementType::OBJECT;
    if (token.compare(0, 2, "NE") == 0) return WDLElementType::NOISE_EMITTER;
    if (token.rfind("Model", 0) == 0 && token.size() > 5) {
        std::string rest = token.substr(5);
        bool numeric = !rest.empty();
        for (char ch : rest) if (!isdigit(ch)) { numeric = false; break; }
        if (numeric) return WDLElementType::MODEL;
    }
    if (token == "Walker" || token == "Skaarj" || token == "Brute" || token == "Floater")
        return WDLElementType::NPC;

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

    std::vector<std::string> fields = split_colons(normalize_content(content));

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
                // Pattern: Model2:x:y:z:scale:rotation:
                // "Model2" encodes the ID
                std::string num_str = token.substr(5);
                elem.int_id = std::stoi(num_str);
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
                std::string prefix = elem.type == WDLElementType::SCRIPT ? "Script" :
                                     elem.type == WDLElementType::OBJECT ? "Object" : "NE";
                std::string num_str = token.substr(prefix.size());
                try { elem.int_id = std::stoi(num_str); } catch (...) {}
                for (int c = 0; c < 5 && i < fields.size(); c++, i++) {
                    try { elem.args.push_back(std::stof(fields[i])); }
                    catch (...) { elem.args.push_back(0.0f); }
                }
                break;
            }
            case WDLElementType::NPC: {
                // NPC:type:x:y:z: or Walker/Skaarj/etc:x:y:z:
                elem.entityType = token;
                if (token == "NPC" && i < fields.size()) elem.entityType = fields[i++];
                for (int c = 0; c < 3 && i < fields.size(); c++, i++) {
                    try { elem.args.push_back(std::stof(fields[i])); }
                    catch (...) { elem.args.push_back(0.0f); }
                }
                break;
            }
            case WDLElementType::SPAWN: {
                // Spawn:x:y:z:yaw:
                for (int c = 0; c < 4 && i < fields.size(); c++, i++) {
                    try {
                        if (c < 3) elem.args.push_back(std::stof(fields[i]));
                        else elem.yaw = std::stof(fields[i]);
                    }
                    catch (...) {
                        if (c < 3) elem.args.push_back(0.0f);
                        else elem.yaw = 0.0f;
                    }
                }
                break;
            }
            case WDLElementType::PICKUP: {
                // Pickup:type:x:y:z:
                if (i < fields.size()) {
                    elem.pickupType = fields[i];
                    i++;
                }
                for (int c = 0; c < 3 && i < fields.size(); c++, i++) {
                    try { elem.args.push_back(std::stof(fields[i])); }
                    catch (...) { elem.args.push_back(0.0f); }
                }
                break;
            }
            case WDLElementType::ZONE_INFO: {
                // ZoneInfo:type:minX:minY:minZ:maxX:maxY:maxZ:intensity:
                if (i < fields.size()) {
                    elem.zoneType = fields[i];
                    i++;
                }
                for (int c = 0; c < 6 && i < fields.size(); c++, i++) {
                    try { elem.args.push_back(std::stof(fields[i])); }
                    catch (...) { elem.args.push_back(0.0f); }
                }
                if (i < fields.size()) {
                    try { elem.intensity = std::stof(fields[i]); i++; }
                    catch (...) { elem.intensity = 1.0f; i++; }
                }
                break;
            }
            case WDLElementType::FOG:
            case WDLElementType::AMBIENT_TYPE: {
                for (int c = 0; c < 4 && i < fields.size(); c++, i++) {
                    try { elem.args.push_back(std::stof(fields[i])); }
                    catch (...) { elem.args.push_back(0.0f); }
                }
                break;
            }
            case WDLElementType::LIGHT_TYPE: {
                if (i < fields.size()) elem.entityType = fields[i++];
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

const char* WDLParser::GetWDLTypeName(WDLElementType type) {
    switch (type) {
        case WDLElementType::MODEL: return "Model";
        case WDLElementType::COLLISION: return "Collision";
        case WDLElementType::ADV_COLLISION: return "AdvCollision";
        case WDLElementType::CLIP_BOX: return "ClipBox";
        case WDLElementType::LIGHT: return "Light";
        case WDLElementType::HEIGHTMAP: return "HeightMap";
        case WDLElementType::SCRIPT: return "Script";
        case WDLElementType::OBJECT: return "Object";
        case WDLElementType::PICKUP: return "Pickup";
        case WDLElementType::SPAWN: return "Spawn";
        case WDLElementType::NPC: return "NPC";
        case WDLElementType::FOG: return "Fog";
        case WDLElementType::LIGHT_TYPE: return "LightType";
        case WDLElementType::AMBIENT_TYPE: return "Ambient";
        case WDLElementType::ZONE_INFO: return "ZoneInfo";
        case WDLElementType::ENTITY_WALKER: return "Walker";
        case WDLElementType::NOISE_EMITTER: return "NoiseEmitter";
        case WDLElementType::COL_FLAG: return "C";
        default: return "Unknown";
    }
}
