#include "OzoneParser.hpp"
#include <cstdio>

// Particle type names -> numeric index (0=none 1=snow 2=rain 3=void 4=psychic)
static int ParticleTypeNameToIndex(const std::string& s) {
    if (s == "NONE" || s == "none") return 0;
    if (s == "SNOW" || s == "snow") return 1;
    if (s == "RAIN" || s == "rain") return 2;
    if (s == "VOID" || s == "VOID_REALM" || s == "void") return 3;
    if (s == "PSYCHIC" || s == "PSYCHIC_REALM" || s == "psychic") return 4;
    try { return std::stoi(s); } catch (...) { return 0; }
}

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
        prim.csgOp = 0;

        // Check for CSG operation prefix: add/sub/intersect/deresc
        if (type_name == "add" || type_name == "sub" ||
            type_name == "intersect" || type_name == "deresc") {
            if (type_name == "add")        prim.csgOp = 1;
            else if (type_name == "sub")         prim.csgOp = 2;
            else if (type_name == "intersect")   prim.csgOp = 3;
            else if (type_name == "deresc")      prim.csgOp = 4;
            // Read the actual primitive type after the operation
            if (!(ls >> type_name)) continue;
        }

        auto parseFloatsAndFlags = [&]() {
            std::string s;
            while (ls >> s) {
                try { prim.args.push_back(std::stof(s)); }
                catch (...) {
                    if (s.rfind("flags=", 0) == 0)
                        prim.surfaceFlags = std::stoi(s.substr(6));
                    else if (s.rfind("texScaleU=", 0) == 0)
                        prim.texScaleU = std::stof(s.substr(10));
                    else if (s.rfind("texScaleV=", 0) == 0)
                        prim.texScaleV = std::stof(s.substr(10));
                    else if (s.rfind("texOffsetU=", 0) == 0)
                        prim.texOffsetU = std::stof(s.substr(11));
                    else if (s.rfind("texOffsetV=", 0) == 0)
                        prim.texOffsetV = std::stof(s.substr(11));
                    else if (s.rfind("texPath=", 0) == 0)
                        prim.texPath = s.substr(8);
                    else if (s.rfind("texPath=\"", 0) == 0)
                        prim.texPath = s.substr(9, s.size() - 10);
                }
            }
        };

        if (type_name == "box") {
            prim.type = OzonePrimitiveType::BOX;
            // box x y z w h d rot [texSlot] [flags:N]
            parseFloatsAndFlags();
        } else if (type_name == "cyl") {
            prim.type = OzonePrimitiveType::CYLINDER;
            // cyl x y z r_top r_bot h slices rot [texSlot] [flags:N]
            parseFloatsAndFlags();
        } else if (type_name == "sph") {
            prim.type = OzonePrimitiveType::SPHERE;
            // sph x y z r [segments] [flags:N]
            parseFloatsAndFlags();
        } else if (type_name == "pyr") {
            prim.type = OzonePrimitiveType::PYRAMID;
            // pyr x y z w d h [texSlot] [flags:N]
            parseFloatsAndFlags();
        } else if (type_name == "pln") {
            prim.type = OzonePrimitiveType::PLANE;
            // pln x y z nx ny nz dist [flags:N]
            parseFloatsAndFlags();
        } else if (type_name == "playerstart") {
            prim.type = OzonePrimitiveType::ENTITY_PLAYERSTART;
            // playerstart x y z yaw
            std::string s;
            while (ls >> s) {
                try { prim.args.push_back(std::stof(s)); }
                catch (...) { break; }
            }
        } else if (type_name == "pickup") {
            prim.type = OzonePrimitiveType::ENTITY_PICKUP;
            // pickup type x y z [respawnTime]
            if (ls >> prim.entityType) {
                std::string s;
                while (ls >> s) {
                    try { prim.args.push_back(std::stof(s)); }
                    catch (...) { break; }
                }
            }
        } else if (type_name == "zone") {
            prim.type = OzonePrimitiveType::ENTITY_ZONE;
            // zone zonetype minX minY minZ maxX maxY maxZ [intensity] [fog...] [name=label]
            if (ls >> prim.entitySubType) {
                std::string s;
                while (ls >> s) {
                    if (s.rfind("name=", 0) == 0) {
                        prim.name = s.substr(5);
                        continue;
                    }
                    try { prim.args.push_back(std::stof(s)); }
                    catch (...) { break; }
                }
            }
        } else if (type_name == "npc") {
            prim.type = OzonePrimitiveType::ENTITY_NPC;
            // npc npctype x y z
            if (ls >> prim.entityType) {
                std::string s;
                while (ls >> s) {
                    try { prim.args.push_back(std::stof(s)); }
                    catch (...) { break; }
                }
            }
        } else if (type_name == "light") {
            prim.type = OzonePrimitiveType::ENTITY_LIGHT;
            // light point x y z r g b intensity radius [effect]
            // light spot x y z tx ty tz r g b intensity radius innerCone outerCone [effect]
            // light directional tx ty tz r g b intensity
            if (ls >> prim.entityType) {
                std::string s;
                while (ls >> s) {
                    try { prim.args.push_back(std::stof(s)); }
                    catch (...) { break; }
                }
            }
        } else if (type_name == "portal") {
            prim.type = OzonePrimitiveType::ENTITY_PORTAL;
            // portal targetWorld minX minY minZ maxX maxY maxZ [spawnX spawnY spawnZ] [bidir]
            // Coordinates are OZONE Z-up; loaders convert to engine Y-up.
            if (ls >> prim.entityType) {
                std::string s;
                while (ls >> s) {
                    if (s == "bidir" || s == "1") { prim.args.push_back(1.0f); continue; }
                    if (s == "0") { prim.args.push_back(0.0f); continue; }
                    try { prim.args.push_back(std::stof(s)); }
                    catch (...) { break; }
                }
            }
        } else if (type_name == "levelinfo") {
            prim.type = OzonePrimitiveType::ENTITY_LEVELINFO;
            // levelinfo gameType maxPlayers respawnTime timeLimitEnabled timeLimitMinutes
            //           scoreLimit friendlyFire skyboxPath
            for (int c = 0; c < 7; c++) {
                std::string s;
                if (!(ls >> s)) break;
                try { prim.args.push_back(std::stof(s)); }
                catch (...) { prim.args.push_back(0.0f); }
            }
            std::string skybox;
            if (ls >> skybox) prim.entityType = skybox;
            std::string sideSkybox;
            if (ls >> sideSkybox) prim.entitySubType = sideSkybox;
        } else if (type_name == "particles") {
            prim.type = OzonePrimitiveType::ENTITY_PARTICLES;
            // particles type density speed r g b windX windZ
            // type is numeric (0=none 1=snow 2=rain 3=void 4=psychic); names accepted for robustness
            std::string s;
            bool first = true;
            while (ls >> s) {
                if (first) {
                    first = false;
                    prim.args.push_back((float)ParticleTypeNameToIndex(s));
                    continue;
                }
                try { prim.args.push_back(std::stof(s)); }
                catch (...) { break; }
            }
        } else if (type_name == "heightmap") {
            prim.type = OzonePrimitiveType::HEIGHTMAP;
            // heightmap imagePath texturePath x y z scale sizeX sizeY sizeZ
            ls >> prim.entityType;   // image path
            std::string texPath;
            ls >> texPath;           // texture path
            prim.entitySubType = texPath;
            {
                std::string s;
                while (ls >> s) {
                    try { prim.args.push_back(std::stof(s)); }
                    catch (...) { break; }
                }
            }
        }

        if (prim.type != OzonePrimitiveType::UNKNOWN)
            prims.push_back(std::move(prim));
    }

    return prims;
}