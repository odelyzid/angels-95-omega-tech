

#include <iostream>
#include <string>
#include <sstream>

#include <fstream>
#include <stdlib.h>
#include <cstring>

#include "../Renderer/TextSystem.hpp"

using namespace std;

#define MaxVaribles 100 
#define MaxJumpPoints 45
#define MaxArraySize 1
#define MaxTFlag 100
#define MaxArrays 1

inline int VaribleCounter = 0;
inline int JumpPointCounter = 0;
inline int ArrayCounter = 0;

inline wstring ExtraWDLInstructions = L"";



inline bool SetSceneFlag = false;
inline int SetSceneId = 0;

inline bool SetCameraFlag = false;
inline Vector3 SetCameraPos = {0,0,0};

typedef struct Memory{
    string Name;
    string Value;
    int IValue;
}Memory;


static Memory VaribleMemory[MaxVaribles];

typedef struct ArrMemory{
    string Name;
    int Array[MaxArraySize];
    int Size;
}ArrMemory;


static ArrMemory ArrayMemory[MaxArrays];

typedef struct JumpPoint{
    string Name;
    int LineNumber;
}JumpPoint;

static JumpPoint JumpPoints[MaxJumpPoints];

typedef struct Flags{
    bool Value;
}Flags;

static Flags ToggleFlags[MaxTFlag];


typedef struct ParasiteScriptData{
   istringstream ProgramData;
   
   string Line[1000];
   
   int LineCounter;
   int ProgramSize;

   bool CompareFlag;

   bool EqualFlag;
   bool GreaterFlag;
   bool LesserFlag;
   bool NotEqualFlag;
   bool ContinueFlag;
   bool ElseFlag;
   bool ErrorFlag;

   int ReturnLine;
   int TextSize;
   

}ParasiteScriptData;

static ParasiteScriptData ParasiteScriptCoreData;


