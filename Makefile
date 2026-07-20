# Detect OS for cross-platform support
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  PIC := -fPIC
  LDFLAGS := -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
  RPATH := -Wl,-rpath=.
  EXE :=
  SERVER_LIBS := -lm -lpthread
else
  # Windows (MINGW/MSYS/CYGWIN)
  PIC :=
  LDFLAGS := -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 -lm
  RPATH :=
  EXE := .exe
  SERVER_LIBS := -lm -lws2_32
endif

CFLAGS := -O3 --std=c++20 $(PIC)
COMP := g++
SERVER_CXX := g++
SERVER_FLAGS := -O3 --std=c++20

# Object files (OTCustom statically linked to avoid DLL cross-platform issues)
OBJS := raygui.o OTCustom.o Encoder.o Main.o Network.o Log.o Client.o

.PHONY: all clean
all: OTENGINE oz_server

# 1. Compile Main Game Logic
Main.o: Source/Main.cpp Source/*.hpp Source/Parasite/*.hpp
	$(COMP) $(CFLAGS) -c Source/Main.cpp

# 2. Compile Custom Engine code (statically linked)
OTCustom.o: Source/Custom/OTCustom.cpp Source/Custom/OTCustom.hpp
	$(COMP) $(CFLAGS) -c Source/Custom/OTCustom.cpp

# 3. Compile the asset Encoder
Encoder.o: Source/Encoder/Encoder.cpp
	$(COMP) $(CFLAGS) -c Source/Encoder/Encoder.cpp

# 4. Compile raygui helper
raygui.o: Source/raygui/raygui.c
	$(COMP) -fpermissive $(CFLAGS) -c Source/raygui/raygui.c -DRAYGUI_IMPLEMENTATION

# 5. Compile Network library (used by both client and server)
Network.o: Source/Network/Network.cpp Source/Network/Network.hpp
	$(SERVER_CXX) $(SERVER_FLAGS) -c Source/Network/Network.cpp

# 5b. Compile Log system
Log.o: Source/Log.cpp Source/Log.hpp
	$(SERVER_CXX) $(SERVER_FLAGS) -c Source/Log.cpp

# 5c. Compile Client networking
Client.o: Source/Client/Client.cpp Source/Client/Client.hpp
	$(COMP) $(CFLAGS) -c Source/Client/Client.cpp

# 6. Build Game Binary
OTENGINE: $(OBJS)
	$(COMP) $(OBJS) -o Angels95$(EXE) $(CFLAGS) $(LDFLAGS) $(RPATH)

# 7. Build oz_server (dedicated server, no raylib)
GameState.o: Source/Server/GameState.cpp Source/Server/GameState.hpp
	$(SERVER_CXX) $(SERVER_FLAGS) -c Source/Server/GameState.cpp

oz_server: Network.o GameState.o Log.o Source/Server/Server.cpp Source/Network/Network.hpp Source/Server/WDLParser.hpp Source/Server/OzoneParser.hpp Source/Server/WDLParser.cpp Source/Server/OzoneParser.cpp
	$(SERVER_CXX) $(SERVER_FLAGS) Network.o GameState.o Log.o Source/Server/Server.cpp Source/Server/WDLParser.cpp Source/Server/OzoneParser.cpp -o oz_server$(EXE) $(SERVER_LIBS)

clean:
	rm -f *.o *.so *.dll Angels95 Angels95.exe oz_server oz_server.exe
