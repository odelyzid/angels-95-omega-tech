#include "OzoneParser.hpp"

std::vector<OzonePrimitive> OzoneParser::parse_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        fprintf(stderr, "OZONE: cannot open %s\n", path.c_str());
        return {};
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return parse_string(content);
}

std::vector<OzonePrimitive> OzoneParser::parse_string(const std::string& content) {
    std::vector<OzonePrimitive> prims;
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("OZONE", 0) == 0 ||
            line.rfind("brushes", 0) == 0)
            continue;

        std::istringstream ls(line);
        std::string type_name;
        ls >> type_name;

        OzonePrimitive prim;
        prim.type = OzonePrimitiveType::UNKNOWN;

        if (type_name == "box") {
            prim.type = OzonePrimitiveType::BOX;
            // box x y z w h d rot
            std::string s;
            while (ls >> s) {
                try { prim.args.push_back(std::stof(s)); }
                catch (...) { break; }
            }
        } else if (type_name == "cyl") {
            prim.type = OzonePrimitiveType::CYLINDER;
            // cyl x y z r_top r_bot h slices rot
            std::string s;
            while (ls >> s) {
                try { prim.args.push_back(std::stof(s)); }
                catch (...) { break; }
            }
        } else if (type_name == "sph") {
            prim.type = OzonePrimitiveType::SPHERE;
            // sph x y z r [segments]
            std::string s;
            while (ls >> s) {
                try { prim.args.push_back(std::stof(s)); }
                catch (...) { break; }
            }
        } else if (type_name == "pyr") {
            prim.type = OzonePrimitiveType::PYRAMID;
            // pyr x y z w d h
            std::string s;
            while (ls >> s) {
                try { prim.args.push_back(std::stof(s)); }
                catch (...) { break; }
            }
        } else if (type_name == "pln") {
            prim.type = OzonePrimitiveType::PLANE;
            // pln x y z nx ny nz dist
            std::string s;
            while (ls >> s) {
                try { prim.args.push_back(std::stof(s)); }
                catch (...) { break; }
            }
        }

        if (prim.type != OzonePrimitiveType::UNKNOWN)
            prims.push_back(std::move(prim));
    }

    return prims;
}