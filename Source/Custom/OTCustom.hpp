#ifndef OTCUSTOM_H 
#define OTCUSTOM_H 

void UpdateCustom();

void DrawPyramidPrimitive(Vector3 baseCenter, float width, float depth, float height, Color color);

void ParseOzonePlane(const std::string& line);

void ParseOzonePrimitive(const std::string& line);

void LoadOzoneMap(const char* filepath);

#endif 
