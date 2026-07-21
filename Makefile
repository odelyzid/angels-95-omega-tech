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
  # raylib library path (detect w64devkit; fall back to default search path)
  RAYLIB_LIB := $(wildcard C:/raylib/w64devkit/lib/libraylib.a)
  ifneq ($(RAYLIB_LIB),)
    RAYLIB_DIR := -LC:/raylib/w64devkit/lib
    RAYLIB_INC := -IC:/raylib/w64devkit/include
  else
    RAYLIB_DIR :=
    RAYLIB_INC :=
  endif
  LDFLAGS := $(RAYLIB_DIR) -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 -lm
  RPATH :=
  EXE := .exe
  SERVER_LIBS := -lm -lws2_32
endif

CFLAGS := -O3 --std=c++20 $(PIC) $(RAYLIB_INC)
COMP := g++
SERVER_CXX := g++
SERVER_FLAGS := -O3 --std=c++20 $(RAYLIB_INC)

BUILD_DIR := build
OBJS := $(addprefix $(BUILD_DIR)/, \
          raygui.o OTCustom.o Encoder.o Main.o Network.o Log.o Client.o \
          oz_assetmapper.o oz_sound_loader.o oz_pawn_system.o \
          oz_ozone_loader.o OzoneParser.o GameState.o)

.PHONY: all clean
all: OTENGINE oz_server ozpack

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 1. Compile Main Game Logic
$(BUILD_DIR)/Main.o: Source/Main.cpp Source/*.hpp Source/Parasite/*.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Main.cpp -o $@

# 2. Compile Custom Engine code (statically linked)
$(BUILD_DIR)/OTCustom.o: Source/Custom/OTCustom.cpp Source/Custom/OTCustom.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Custom/OTCustom.cpp -o $@

# 3. Compile the asset Encoder
$(BUILD_DIR)/Encoder.o: Source/Encoder/Encoder.cpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Encoder/Encoder.cpp -o $@

# 4. Compile raygui helper
$(BUILD_DIR)/raygui.o: Source/raygui/raygui.c | $(BUILD_DIR)
	$(COMP) -fpermissive $(CFLAGS) -c Source/raygui/raygui.c -DRAYGUI_IMPLEMENTATION -o $@

# 5. Compile Network library (used by both client and server)
$(BUILD_DIR)/Network.o: Source/Network/Network.cpp Source/Network/Network.hpp | $(BUILD_DIR)
	$(SERVER_CXX) $(SERVER_FLAGS) -c Source/Network/Network.cpp -o $@

# 5b. Compile Log system
$(BUILD_DIR)/Log.o: Source/Log.cpp Source/Log.hpp | $(BUILD_DIR)
	$(SERVER_CXX) $(SERVER_FLAGS) -c Source/Log.cpp -o $@

# 5c. Compile Client networking
$(BUILD_DIR)/Client.o: Source/Client/Client.cpp Source/Client/Client.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Client/Client.cpp -o $@

# 5d. Compile the oz_* subsystem modules
$(BUILD_DIR)/oz_assetmapper.o: Source/oz_assetmapper.cpp Source/oz_assetmapper.h | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/oz_assetmapper.cpp -o $@

$(BUILD_DIR)/oz_sound_loader.o: Source/oz_sound_loader.cpp Source/oz_sound_loader.h | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/oz_sound_loader.cpp -o $@

$(BUILD_DIR)/oz_pawn_system.o: Source/oz_pawn_system.cpp Source/oz_pawn_system.h | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/oz_pawn_system.cpp -o $@

$(BUILD_DIR)/oz_ozone_loader.o: Source/oz_ozone_loader.cpp Source/oz_ozone_loader.h Source/Server/OzoneParser.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/oz_ozone_loader.cpp -o $@

# 5e. Compile OzoneParser (used by both client and server)
$(BUILD_DIR)/OzoneParser.o: Source/Server/OzoneParser.cpp Source/Server/OzoneParser.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Server/OzoneParser.cpp -o $@

# 6. Build Game Binary
OTENGINE: $(addprefix $(BUILD_DIR)/, raygui.o OTCustom.o Encoder.o Main.o Network.o Log.o Client.o oz_assetmapper.o oz_sound_loader.o oz_pawn_system.o oz_ozone_loader.o OzoneParser.o)
	$(COMP) $^ -o Angels95$(EXE) $(CFLAGS) $(LDFLAGS) $(RPATH)

# 7. Build oz_server (dedicated server, no raylib)
$(BUILD_DIR)/GameState.o: Source/Server/GameState.cpp Source/Server/GameState.hpp | $(BUILD_DIR)
	$(SERVER_CXX) $(SERVER_FLAGS) -c Source/Server/GameState.cpp -o $@

oz_server: $(BUILD_DIR)/Network.o $(BUILD_DIR)/GameState.o $(BUILD_DIR)/Log.o Source/Server/Server.cpp Source/Network/Network.hpp Source/Server/WDLParser.hpp Source/Server/OzoneParser.hpp Source/Server/WDLParser.cpp Source/Server/OzoneParser.cpp
	$(SERVER_CXX) $(SERVER_FLAGS) $(BUILD_DIR)/Network.o $(BUILD_DIR)/GameState.o $(BUILD_DIR)/Log.o Source/Server/Server.cpp Source/Server/WDLParser.cpp Source/Server/OzoneParser.cpp -o oz_server$(EXE) $(SERVER_LIBS)

# 8. Build OzPack (standalone packer/unpacker, no raylib)
ozpack: Source/OzPack.cpp Source/OzPackage.hpp
	$(SERVER_CXX) $(SERVER_FLAGS) Source/OzPack.cpp -o OzPack$(EXE) $(SERVER_LIBS)

clean:
	rm -rf $(BUILD_DIR) *.exe oz_server Angels95 OzPack
