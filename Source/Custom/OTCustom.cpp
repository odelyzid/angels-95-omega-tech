#include "OTCustom.hpp"
#include <raylib.h>
#include <sstream>
#include <fstream>
#include <string>
#include <algorithm>

void UpdateCustom(){
    
}

// Helper function to render a pyramid (base centered at baseCenter)
void DrawPyramidPrimitive(Vector3 baseCenter, float width, float depth, float height, Color color) {
    float hw = width / 2.0f;
    float hd = depth / 2.0f;

    Vector3 apex  = { baseCenter.x, baseCenter.y + height, baseCenter.z };
    Vector3 base1 = { baseCenter.x - hw, baseCenter.y, baseCenter.z - hd };
    Vector3 base2 = { baseCenter.x + hw, baseCenter.y, baseCenter.z - hd };
    Vector3 base3 = { baseCenter.x + hw, baseCenter.y, baseCenter.z + hd };
    Vector3 base4 = { baseCenter.x - hw, baseCenter.y, baseCenter.z + hd };

    // Draw side triangles
    DrawTriangle3D(base1, base2, apex, color);
    DrawTriangle3D(base2, base3, apex, color);
    DrawTriangle3D(base3, base4, apex, color);
    DrawTriangle3D(base4, base1, apex, color);

    // Draw base
    DrawTriangle3D(base2, base1, base4, color);
    DrawTriangle3D(base3, base2, base4, color);
}

void ParseOzonePlane(const std::string& line) {
    std::stringstream ss(line);
    std::string type;
    float x, y, z, nx, ny, nz, dist;
    
    ss >> type >> x >> y >> z >> nx >> ny >> nz >> dist;

    // Convert Z-Up (.ozone) to Y-Up (Raylib)
    Vector3 anchorPos = { x, z, y };
    Vector3 normal    = { nx, nz, ny }; // Re-orient normal vector

    // Define size for visual representation of the plane
    float size = 10.0f; 

    // Compute rotation quaternion from default UP vector (0,1,0) to plane normal
    Quaternion rot = QuaternionFromVector3ToVector3(Vector3{0.0f, 1.0f, 0.0f}, Vector3Normalize(normal));

    // Render as a 3D Quad/Plane using Raylib
    rlPushMatrix();
        rlTranslatef(anchorPos.x, anchorPos.y, anchorPos.z);
        
        // Convert Quaternion to Euler angles to rotate the quad
        Vector3 euler = QuaternionToEuler(rot);
        rlRotatef(euler.z * RAD2DEG, 0, 0, 1);
        rlRotatef(euler.y * RAD2DEG, 0, 1, 0);
        rlRotatef(euler.x * RAD2DEG, 1, 0, 0);

        // Draw flat quad surface
        DrawPlane(Vector3{0, 0, 0}, Vector2{size, size}, DARKGRAY);
        DrawGrid(10, 1.0f); // Wireframe grid overlay for low-poly look
    rlPopMatrix();
}

// Function to parse the lines directly from your .ozone file
void ParseOzonePrimitive(const std::string& line) {
    std::stringstream ss(line);
    std::string type;
    ss >> type;

    if (type == "cyl") {
        float x, y, z, rTop, rBot, h, rot;
        int slices;
        ss >> x >> y >> z >> rTop >> rBot >> h >> slices >> rot;

        Vector3 pos = { x, z, y }; // Swap Y and Z if your map uses Z-up orientation
        
        // Raylib draws cylinders centered, so we adjust height position
        DrawCylinderEx(pos, Vector3{pos.x, pos.y + h, pos.z}, rBot, rTop, slices, LIGHTGRAY);
        DrawCylinderWiresEx(pos, Vector3{pos.x, pos.y + h, pos.z}, rBot, rTop, slices, DARKGRAY);
    } 
    else if (type == "sph") {
        float x, y, z, r;
        int ringsSlices;
        ss >> x >> y >> z >> r >> ringsSlices;

        Vector3 pos = { x, z, y }; // Swap Y and Z if your map uses Z-up orientation

        DrawSphereEx(pos, r, ringsSlices, ringsSlices, PURPLE);
        DrawSphereWires(pos, r, ringsSlices, ringsSlices, RAYWHITE); // Wireframe overlay for retro PSX look
    } 
    else if (type == "pyr") {
        float x, y, z, w, d, h;
        ss >> x >> y >> z >> w >> d >> h;

        Vector3 pos = { x, z, y }; // Swap Y and Z if your map uses Z-up orientation

        DrawPyramidPrimitive(pos, w, d, h, SKYBLUE);
    }
}

void LoadOzoneMap(const char* filepath){
	std::ifstream file(filepath);
    if (!file.is_open()) return;

    std::string line;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("OZONE", 0) == 0 || line.rfind("brushes", 0) == 0) {
            continue; // Skip headers, comments, and empty lines
        }

        std::stringstream ss(line);
        std::string type;
        ss >> type;

        // BOX: box x y z w h d rot
        if (type == "box") {
            float x, y, z, w, h, d, rot;
            ss >> x >> y >> z >> w >> h >> d >> rot;

            // Swap Y and Z for Raylib Y-Up coordinate space
            Vector3 pos = { x, z, y };
            
            DrawCube(pos, w, d, h, LIGHTGRAY);
            DrawCubeWires(pos, w, d, h, DARKGRAY); // Retro wireframe outline
        }
        
        // SPHERE: sph x y z r rings_slices
        else if (type == "sph") {
            float x, y, z, r;
            int segments = 16;
            ss >> x >> y >> z >> r;
            if (ss >> segments) {} // Read optional segment count if present

            Vector3 pos = { x, z, y };

            DrawSphereEx(pos, r, segments, segments, PURPLE);
            DrawSphereWires(pos, r, segments, segments, WHITE);
        }

        // CYLINDER: cyl x y z r_top r_bot h slices rot
        else if (type == "cyl") {
            float x, y, z, rTop, rBot, h, rot;
            int slices = 16;
            ss >> x >> y >> z >> rTop >> rBot >> h >> slices >> rot;

            Vector3 startPos = { x, z, y };
            Vector3 endPos   = { x, z + h, y }; // Extrude vertically along Y

            DrawCylinderEx(startPos, endPos, rBot, rTop, slices, SKYBLUE);
            DrawCylinderWiresEx(startPos, endPos, rBot, rTop, slices, WHITE);
        }

        // PYRAMID: pyr x y z w d h
        else if (type == "pyr") {
            float x, y, z, w, d, h;
            ss >> x >> y >> z >> w >> d >> h;

            Vector3 pos = { x, z, y };

            DrawPyramidPrimitive(pos, w, d, h, GOLD);
        }

        // PLANE: pln x y z nx ny nz dist
        else if (type == "pln") {
            float x, y, z, nx, ny, nz, dist;
            ss >> x >> y >> z >> nx >> ny >> nz >> dist;

            Vector3 pos    = { x, z, y };
            Vector3 normal = Vector3Normalize(Vector3{ nx, nz, ny });

            // Calculate rotation towards normal vector
            Quaternion rot = QuaternionFromVector3ToVector3(Vector3{ 0.0f, 1.0f, 0.0f }, normal);
            Vector3 euler  = QuaternionToEuler(rot);

            rlPushMatrix();
                rlTranslatef(pos.x, pos.y, pos.z);
                rlRotatef(euler.z * RAD2DEG, 0, 0, 1);
                rlRotatef(euler.y * RAD2DEG, 0, 1, 0);
                rlRotatef(euler.x * RAD2DEG, 1, 0, 0);

                DrawPlane(Vector3{ 0, 0, 0 }, Vector2{ 10.0f, 10.0f }, DARKGRAY);
            rlPopMatrix();
        }
    }
}

