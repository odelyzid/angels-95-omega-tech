#ifndef OTCUSTOM_H 
#define OTCUSTOM_H 

#include <string>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

void UpdateCustom();

void DrawPyramidPrimitive(Vector3 baseCenter, float width, float depth, float height, Color color);

void ParseOzonePlane(const std::string& line);

void ParseOzonePrimitive(const std::string& line);

void LoadOzoneMap(const char* filepath);

#endif 
