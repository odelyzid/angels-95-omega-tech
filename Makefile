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
          OzAssetMapper.o OzSoundLoader.o OzPawnSystem.o \
          OzOzoneLoader.o OzoneParser.o OzBsp.o WorldChunk.o \
          LightningScriptContext.o LightningScriptParser.o \
          LightningEntityRegistry.o LightningEntityManager.o \
          LitLightning.o rlights.o)

.PHONY: all clean test
all: OTENGINE AngelServ ozpack

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 1. Compile Main Game Logic
$(BUILD_DIR)/Main.o: Source/Main.cpp Source/*.hpp Source/Parasite/*.hpp Source/Package/*.hpp Source/Pawn/*.hpp Source/Renderer/*.hpp Source/Audio/*.hpp | $(BUILD_DIR)
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

# 5d. Compile the Oz* subsystem modules
$(BUILD_DIR)/OzAssetMapper.o: Source/Package/OzAssetMapper.cpp Source/Package/OzAssetMapper.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Package/OzAssetMapper.cpp -o $@

$(BUILD_DIR)/OzSoundLoader.o: Source/Audio/OzSoundLoader.cpp Source/Audio/OzSoundLoader.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Audio/OzSoundLoader.cpp -o $@

$(BUILD_DIR)/OzPawnSystem.o: Source/Pawn/OzPawnSystem.cpp Source/Pawn/OzPawnSystem.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Pawn/OzPawnSystem.cpp -o $@

$(BUILD_DIR)/LitLightning.o: Source/Renderer/LitLightning.cpp Source/Renderer/LitLightning.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Renderer/LitLightning.cpp -o $@

$(BUILD_DIR)/rlights.o: Source/rlights/rlights.cpp Source/rlights/rlights.h | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/rlights/rlights.cpp -o $@

$(BUILD_DIR)/OzOzoneLoader.o: Source/OzOzoneLoader.cpp Source/OzOzoneLoader.hpp Source/Server/OzoneParser.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/OzOzoneLoader.cpp -o $@

# 5e. Compile OzoneParser (used by both client and server)
$(BUILD_DIR)/OzoneParser.o: Source/Server/OzoneParser.cpp Source/Server/OzoneParser.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Server/OzoneParser.cpp -o $@

# 5f. Compile CSG/BSP processor
$(BUILD_DIR)/OzBsp.o: Source/Physics/OzBsp.cpp Source/Physics/OzBsp.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Physics/OzBsp.cpp -o $@

# 5g. Compile WorldChunk spatial partition
$(BUILD_DIR)/WorldChunk.o: Source/Physics/WorldChunk.cpp Source/Physics/WorldChunk.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Physics/WorldChunk.cpp -o $@

# 5h. Compile LightningScript system
$(BUILD_DIR)/LightningScriptContext.o: Source/Script/LightningScriptContext.cpp Source/Script/LightningScriptContext.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Script/LightningScriptContext.cpp -o $@

$(BUILD_DIR)/LightningScriptParser.o: Source/Script/LightningScriptParser.cpp Source/Script/LightningScriptParser.hpp Source/Script/LightningEntityDef.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Script/LightningScriptParser.cpp -o $@

$(BUILD_DIR)/LightningEntityRegistry.o: Source/Script/LightningEntityRegistry.cpp Source/Script/LightningEntityRegistry.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Script/LightningEntityRegistry.cpp -o $@

$(BUILD_DIR)/LightningEntityManager.o: Source/Script/LightningEntityManager.cpp Source/Script/LightningEntityManager.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Script/LightningEntityManager.cpp -o $@

# 6. Build Game Binary
OTENGINE: $(addprefix $(BUILD_DIR)/, raygui.o OTCustom.o Encoder.o Main.o Network.o Log.o Client.o OzAssetMapper.o OzSoundLoader.o OzPawnSystem.o OzOzoneLoader.o OzoneParser.o OzBsp.o WorldChunk.o LightningScriptContext.o LightningScriptParser.o LightningEntityRegistry.o LightningEntityManager.o LitLightning.o rlights.o)
	$(COMP) $^ -o Angels95$(EXE) $(CFLAGS) $(LDFLAGS) $(RPATH)

# 7. Build AngelServ (dedicated server, no raylib)
$(BUILD_DIR)/GameState.o: Source/Server/GameState.cpp Source/Server/GameState.hpp | $(BUILD_DIR)
	$(SERVER_CXX) $(SERVER_FLAGS) -c Source/Server/GameState.cpp -o $@

AngelServ: $(BUILD_DIR)/Network.o $(BUILD_DIR)/GameState.o $(BUILD_DIR)/Log.o $(BUILD_DIR)/OzBsp.o $(BUILD_DIR)/WorldChunk.o Source/Server/Server.cpp Source/Network/Network.hpp Source/Server/WDLParser.hpp Source/Server/OzoneParser.hpp Source/Server/WDLParser.cpp Source/Server/OzoneParser.cpp
	$(SERVER_CXX) $(SERVER_FLAGS) $(BUILD_DIR)/Network.o $(BUILD_DIR)/GameState.o $(BUILD_DIR)/Log.o $(BUILD_DIR)/OzBsp.o $(BUILD_DIR)/WorldChunk.o Source/Server/Server.cpp Source/Server/WDLParser.cpp Source/Server/OzoneParser.cpp -o AngelServ$(EXE) $(SERVER_LIBS)

# 8. Build OzPack (standalone packer/unpacker, no raylib)
ozpack: Source/OzPack.cpp Source/Package/OzPackage.hpp
	$(SERVER_CXX) $(SERVER_FLAGS) Source/OzPack.cpp -o OzPack$(EXE) $(SERVER_LIBS)

# 9. Unit tests (no raylib dependency)
TEST_FLAGS := -O0 -g --std=c++20 -DOMEGA_TEST_ENV
test_context: tests/LightningScriptContext.test.cpp Source/Script/LightningScriptContext.cpp Source/Log.cpp
	$(SERVER_CXX) $(TEST_FLAGS) -ISource $^ -o $@

test_parser: tests/LightningScriptParser.test.cpp Source/Script/LightningScriptParser.cpp
	$(SERVER_CXX) $(TEST_FLAGS) -ISource $^ -o $@

test_registry: tests/LightningEntityRegistry.test.cpp Source/Script/LightningEntityRegistry.cpp Source/Script/LightningScriptParser.cpp Source/Script/LightningScriptContext.cpp Source/Log.cpp
	$(SERVER_CXX) $(TEST_FLAGS) -ISource $^ -o $@

test_entity_manager: tests/LightningEntityManager.test.cpp Source/Script/LightningEntityManager.cpp Source/Script/LightningEntityRegistry.cpp Source/Script/LightningScriptContext.cpp Source/Script/LightningScriptParser.cpp Source/Log.cpp
	$(COMP) $(TEST_FLAGS) -ISource $^ -o $@ $(LDFLAGS)

test: test_parser test_entity_manager
	@echo "--- LightningScriptParser Tests ---"
	./test_parser
	@echo "--- LightningEntityManager Tests ---"
	./test_entity_manager

clean:
	rm -rf $(BUILD_DIR) *.exe AngelServ Angels95 OzPack *.o AngelEd/*.o AngelEd/Source/*.o test_context test_parser test_registry
