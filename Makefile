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
  LDFLAGS := $(RAYLIB_DIR) -lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 -lwinhttp -lm
  RPATH :=
  EXE := .exe
  SERVER_LIBS := -lm -lws2_32 -lwinhttp
endif

# Windows resource files (icon embedding) only apply to Windows builds;
# GNU windres is not guaranteed on Linux runners.
ifneq ($(EXE),)
RES_95  = $(BUILD_DIR)/Angels95.res
RES_SRV = $(BUILD_DIR)/AngelServ.res
else
RES_95  =
RES_SRV =
endif

# --- Build mode: release (default) or debug ---
#   release: -O3          shipping build
#   debug:   -O0 -g       fast compiles + symbols for iteration
# Usage:  make MODE=debug OTENGINE
MODE ?= release
ifeq ($(MODE),debug)
  OPTFLAGS := -O0 -g
else ifeq ($(MODE),release)
  OPTFLAGS := -O3
else
  $(error MODE must be 'release' or 'debug' (got '$(MODE)'))
endif

# Optional compiler cache (ccache) and faster linker (LLD), auto-detected via
# `command -v`. Only probed when a POSIX shell is present (UNAME_S is set by the
# `uname` probe above); this keeps cmd.exe-based make quiet.
# Disable with CCACHE= / LDEXTRA=, or force with CCACHE=<path> / LDEXTRA=-fuse-ld=lld.
ifneq ($(UNAME_S),)
  CCACHE ?= $(shell command -v ccache 2>/dev/null)
  LLD_BIN := $(shell command -v ld.lld 2>/dev/null || command -v lld 2>/dev/null)
endif

ifneq ($(CCACHE),)
  CCACHE_PREFIX := $(CCACHE)
else
  CCACHE_PREFIX :=
endif

ifneq ($(LLD_BIN),)
  LDEXTRA ?= -fuse-ld=lld
else
  LDEXTRA ?=
endif

CFLAGS := $(OPTFLAGS) --std=c++20 $(PIC) $(RAYLIB_INC)
COMP := $(CCACHE_PREFIX) g++
CC := $(CCACHE_PREFIX) gcc
SERVER_CXX := $(CCACHE_PREFIX) g++
SERVER_FLAGS := $(OPTFLAGS) --std=c++20 $(RAYLIB_INC)

BUILD_DIR := build
OBJS := $(addprefix $(BUILD_DIR)/, \
          raygui.o Main.o Network.o Log.o Client.o \
          OzAssetMapper.o OzPawnSystem.o GameUi.o SlotBar.o InventoryBehaviour.o WeaponBehaviour.o PlayerController.o PickupPawns.o \
          OzOzoneLoader.o OzoneParser.o OzBsp.o WorldChunk.o PlayerPhysics.o \
          ZoneManager.o SoundManager.o \
          LightningScriptContext.o LightningScriptParser.o \
          LightningEntityRegistry.o LightningEntityManager.o \
          LitLightning.o Mesh.o SkeletalMesh.o MeshCache.o AnimatedMesh.o OzAnimFormat.o \
          ViewModel.o OzParticleSimulationManager.o rlights.o UiHandler.o)

.PHONY: all clean test help
all: OTENGINE AngelServ AngelMaster ozpack

# Build-speed helpers:
#   MODE=debug     -O0 -g instead of -O3 (much faster compiles for iteration)
#   CCACHE=...     compiler cache; auto-detected when 'ccache' is on PATH
#   LDEXTRA=...    extra linker driver flags; -fuse-ld=lld auto-set when available
help:
	@echo "Targets: OTENGINE AngelServ AngelMaster ozpack test clean"
	@echo "MODE      = $(MODE)      (release | debug)"
	@echo "OPTFLAGS  = $(OPTFLAGS)"
	@echo "ccache    = $(if $(CCACHE),$(CCACHE),<not found>)"
	@echo "LDEXTRA   = $(if $(LDEXTRA),$(LDEXTRA),<none>)"
	@echo ""
	@echo "Fast iteration:  make -j$$(nproc) MODE=debug OTENGINE"

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 1. Compile Main Game Logic
$(BUILD_DIR)/Main.o: Source/Main.cpp Source/*.hpp Source/UI/*.hpp Source/Package/*.hpp Source/Pawn/*.hpp Source/Pawn/AngelPlayer/*.hpp Source/Renderer/*.hpp Source/Audio/*.hpp Source/Menu/*.hpp Source/Server/Master/*.hpp Source/Physics/*.hpp Source/Client/*.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Main.cpp -o $@

# 4. Compile raygui helper
$(BUILD_DIR)/raygui.o: Source/Renderer/raygui/raygui.c | $(BUILD_DIR)
	$(COMP) -fpermissive $(CFLAGS) -c Source/Renderer/raygui/raygui.c -DRAYGUI_IMPLEMENTATION -o $@

# 4b. Compile miniz (public-domain zlib substitute used by OzPackage).
# Must be compiled as C (gcc): g++ would C++-mangle the definitions while
# consumers see extern "C" prototypes from miniz.h.
$(BUILD_DIR)/miniz.o: Source/miniz/miniz.c Source/miniz/miniz.h | $(BUILD_DIR)
	$(CC) $(OPTFLAGS) -std=c99 $(PIC) -ISource/miniz -c Source/miniz/miniz.c -o $@

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
$(BUILD_DIR)/OzAssetMapper.o: Source/Renderer/OzAssetMapper.cpp Source/Renderer/OzAssetMapper.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Renderer/OzAssetMapper.cpp -o $@

$(BUILD_DIR)/OzPawnSystem.o: Source/Pawn/OzPawnSystem.cpp Source/Pawn/OzPawnSystem.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Pawn/OzPawnSystem.cpp -o $@

$(BUILD_DIR)/GameUi.o: Source/Pawn/AngelPlayer/GameUi.cpp Source/Pawn/AngelPlayer/GameUi.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Pawn/AngelPlayer/GameUi.cpp -o $@

$(BUILD_DIR)/InventoryBehaviour.o: Source/Pawn/AngelPlayer/InventoryBehaviour.cpp Source/Pawn/AngelPlayer/InventoryBehaviour.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Pawn/AngelPlayer/InventoryBehaviour.cpp -o $@

$(BUILD_DIR)/SlotBar.o: Source/Pawn/AngelPlayer/SlotBar.cpp Source/Pawn/AngelPlayer/SlotBar.hpp Source/Pawn/AngelPlayer/GameUi.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Pawn/AngelPlayer/SlotBar.cpp -o $@

$(BUILD_DIR)/WeaponBehaviour.o: Source/Pawn/AngelPlayer/WeaponBehaviour.cpp Source/Pawn/AngelPlayer/WeaponBehaviour.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Pawn/AngelPlayer/WeaponBehaviour.cpp -o $@

$(BUILD_DIR)/PlayerController.o: Source/Pawn/AngelPlayer/PlayerController.cpp Source/Pawn/AngelPlayer/PlayerController.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Pawn/AngelPlayer/PlayerController.cpp -o $@

$(BUILD_DIR)/PickupPawns.o: Source/Pawn/PickupPawns.cpp Source/Pawn/PickupPawns.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Pawn/PickupPawns.cpp -o $@

$(BUILD_DIR)/LitLightning.o: Source/Renderer/LitLightning.cpp Source/Renderer/LitLightning.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Renderer/LitLightning.cpp -o $@

# 5d-mesh. Compile the oz::Mesh taxonomy render primitives
$(BUILD_DIR)/Mesh.o: Source/Renderer/Mesh/Mesh.cpp Source/Renderer/Mesh/Mesh.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Renderer/Mesh/Mesh.cpp -o $@

$(BUILD_DIR)/SkeletalMesh.o: Source/Renderer/Mesh/SkeletalMesh.cpp Source/Renderer/Mesh/SkeletalMesh.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Renderer/Mesh/SkeletalMesh.cpp -o $@

$(BUILD_DIR)/MeshCache.o: Source/Renderer/Mesh/MeshCache.cpp Source/Renderer/Mesh/MeshCache.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Renderer/Mesh/MeshCache.cpp -o $@

# 5d-anim. Vertex-keyframe (morph) animation format + runtime mesh
$(BUILD_DIR)/OzAnimFormat.o: Source/Package/Anim/OzAnimFormat.cpp Source/Package/Anim/OzAnimFormat.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Package/Anim/OzAnimFormat.cpp -o $@

$(BUILD_DIR)/AnimatedMesh.o: Source/Renderer/Mesh/AnimatedMesh.cpp Source/Renderer/Mesh/AnimatedMesh.hpp Source/Package/Anim/OzAnimFormat.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Renderer/Mesh/AnimatedMesh.cpp -o $@

$(BUILD_DIR)/ViewModel.o: Source/Renderer/ViewModel.cpp Source/Renderer/ViewModel.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Renderer/ViewModel.cpp -o $@

$(BUILD_DIR)/PlayerModel.o: Source/Renderer/PlayerModel.cpp Source/Renderer/PlayerModel.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Renderer/PlayerModel.cpp -o $@

# 5d-ui. Native menu bar / platform UI handler
$(BUILD_DIR)/UiHandler.o: Source/UI/UiHandler.cpp Source/UI/UiHandler.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/UI/UiHandler.cpp -o $@

# 5d-particle. Isolated ParticleEmitter simulation
$(BUILD_DIR)/OzParticleSimulationManager.o: Source/Particle/OzParticleSimulationManager.cpp Source/Particle/OzParticleSimulationManager.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Particle/OzParticleSimulationManager.cpp -o $@

$(BUILD_DIR)/rlights.o: Source/Renderer/rlights/rlights.cpp Source/Renderer/rlights/rlights.h | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Renderer/rlights/rlights.cpp -o $@

# 5i. OzOzoneLoader (OzWorld format loader)
$(BUILD_DIR)/OzOzoneLoader.o: Source/World/OzOzoneLoader.cpp Source/World/OzOzoneLoader.hpp Source/World/OzoneParser.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/World/OzOzoneLoader.cpp -o $@

$(BUILD_DIR)/OzoneFrustum.o: Source/World/OzoneFrustum.cpp Source/World/OzoneFrustum.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/World/OzoneFrustum.cpp -o $@

$(BUILD_DIR)/OzoneHeightmap.o: Source/World/OzoneHeightmap.cpp Source/World/OzOzoneLoader.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/World/OzoneHeightmap.cpp -o $@

# 5e. Compile OzoneParser (used by both client and server)
$(BUILD_DIR)/OzoneParser.o: Source/World/OzoneParser.cpp Source/World/OzoneParser.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/World/OzoneParser.cpp -o $@

# 5e2. Zone volume storage + runtime queries (extracted from PawnSystem)
$(BUILD_DIR)/ZoneManager.o: Source/World/ZoneManager.cpp Source/World/ZoneManager.hpp Source/World/ZoneTypes.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/World/ZoneManager.cpp -o $@

# 5e3. Audio facade (one-shots, world music, sound zones, reverb, script SFX)
$(BUILD_DIR)/SoundManager.o: Source/Audio/SoundManager.cpp Source/Audio/SoundManager.hpp Source/Audio/DspReverb.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Audio/SoundManager.cpp -o $@

# 5f. Compile CSG/BSP processor
$(BUILD_DIR)/OzBsp.o: Source/Physics/OzBsp.cpp Source/Physics/OzBsp.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Physics/OzBsp.cpp -o $@

# 5g. Compile WorldChunk spatial partition
$(BUILD_DIR)/WorldChunk.o: Source/Physics/WorldChunk.cpp Source/Physics/WorldChunk.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Physics/WorldChunk.cpp -o $@

# 5g-b. Player/collision physics module
$(BUILD_DIR)/PlayerPhysics.o: Source/Physics/PlayerPhysics.cpp Source/Physics/PlayerPhysics.hpp Source/Physics/PhysicsInfo.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Physics/PlayerPhysics.cpp -o $@

# 5h. Compile LightningScript system
$(BUILD_DIR)/LightningScriptContext.o: Source/Script/LightningScriptContext.cpp Source/Script/LightningScriptContext.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Script/LightningScriptContext.cpp -o $@

$(BUILD_DIR)/LightningScriptParser.o: Source/Script/LightningScriptParser.cpp Source/Script/LightningScriptParser.hpp Source/Script/LightningEntityDef.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Script/LightningScriptParser.cpp -o $@

$(BUILD_DIR)/LightningEntityRegistry.o: Source/Script/LightningEntityRegistry.cpp Source/Script/LightningEntityRegistry.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Script/LightningEntityRegistry.cpp -o $@

$(BUILD_DIR)/LightningEntityManager.o: Source/Script/LightningEntityManager.cpp Source/Script/LightningEntityManager.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Script/LightningEntityManager.cpp -o $@

# 6a. Compile Windows resource files
ifneq ($(EXE),)
$(BUILD_DIR)/Angels95.res: Source/Angels95.rc GameData/Global/Icon/angels95.ico | $(BUILD_DIR)
	windres Source/Angels95.rc -O coff -o $@

$(BUILD_DIR)/AngelServ.res: Source/AngelServ.rc GameData/Global/Icon/AngelServ.ico | $(BUILD_DIR)
	windres Source/AngelServ.rc -O coff -o $@
endif

# 6b. Build Game Binary
OTENGINE: $(RES_95) $(addprefix $(BUILD_DIR)/, raygui.o miniz.o Main.o Network.o Log.o Client.o OzAssetMapper.o OzPawnSystem.o GameUi.o SlotBar.o InventoryBehaviour.o WeaponBehaviour.o PlayerController.o PickupPawns.o OzOzoneLoader.o OzoneFrustum.o OzoneHeightmap.o OzoneParser.o ZoneManager.o SoundManager.o OzBsp.o WorldChunk.o PlayerPhysics.o LightningScriptContext.o LightningScriptParser.o LightningEntityRegistry.o LightningEntityManager.o LitLightning.o Mesh.o SkeletalMesh.o MeshCache.o AnimatedMesh.o OzAnimFormat.o ViewModel.o PlayerModel.o OzParticleSimulationManager.o rlights.o UiHandler.o)
	$(COMP) $(LDEXTRA) $^ -o Angels95$(EXE) $(CFLAGS) $(LDFLAGS) $(RPATH)

# 7. Build AngelServ (dedicated server, no raylib; miniz for OZWN package reads)
$(BUILD_DIR)/GameState.o: Source/Server/GameState.cpp Source/Server/GameState.hpp | $(BUILD_DIR)
	$(SERVER_CXX) $(SERVER_FLAGS) -c Source/Server/GameState.cpp -o $@

AngelServ: $(RES_SRV) $(BUILD_DIR)/Network.o $(BUILD_DIR)/GameState.o $(BUILD_DIR)/Log.o $(BUILD_DIR)/miniz.o Source/Server/Server.cpp Source/Server/ServerHttp.cpp Source/Server/ServerInternal.hpp Source/Network/Network.hpp Source/World/OzoneParser.hpp Source/World/OzoneParser.cpp Source/Server/Master/MasterClient.hpp Source/Network/MasterProtocol.hpp Source/Network/MasterHttp.hpp
	$(SERVER_CXX) $(SERVER_FLAGS) $(RES_SRV) $(BUILD_DIR)/Network.o $(BUILD_DIR)/GameState.o $(BUILD_DIR)/Log.o $(BUILD_DIR)/miniz.o Source/Server/Server.cpp Source/Server/ServerHttp.cpp Source/World/OzoneParser.cpp -o AngelServ$(EXE) $(SERVER_LIBS) $(LDEXTRA)

# 7b. Build AngelMaster (standalone master server, no raylib)
AngelMaster: $(BUILD_DIR)/Log.o Source/Server/Master/Master.cpp Source/Network/MasterProtocol.hpp Source/Network/MasterHttp.hpp
	$(SERVER_CXX) $(SERVER_FLAGS) $(BUILD_DIR)/Log.o Source/Server/Master/Master.cpp -o AngelMaster$(EXE) $(SERVER_LIBS) $(LDEXTRA)

# 8. Build OzPack (standalone packer/unpacker, no raylib)
ozpack: Source/OzPack.cpp Source/Package/OzPackage.hpp Source/miniz/miniz.h $(BUILD_DIR)/miniz.o
	$(SERVER_CXX) $(SERVER_FLAGS) Source/OzPack.cpp $(BUILD_DIR)/miniz.o -o OzPack$(EXE) $(SERVER_LIBS) $(LDEXTRA)

# 9. Unit tests (no raylib dependency)
TEST_FLAGS := -O0 -g --std=c++20 -DOMEGA_TEST_ENV
test_context: tests/LightningScriptContext.test.cpp Source/Script/LightningScriptContext.cpp Source/Log.cpp
	$(SERVER_CXX) $(TEST_FLAGS) -ISource $^ -o $@

test_parser: tests/LightningScriptParser.test.cpp Source/Script/LightningScriptParser.cpp
	$(SERVER_CXX) $(TEST_FLAGS) -ISource $^ -o $@

test_registry: tests/LightningEntityRegistry.test.cpp Source/Script/LightningEntityRegistry.cpp Source/Script/LightningScriptParser.cpp Source/Script/LightningScriptContext.cpp Source/Log.cpp
	$(SERVER_CXX) $(TEST_FLAGS) $(RAYLIB_INC) -ISource $^ $(BUILD_DIR)/miniz.o -o $@

test_entity_manager: tests/LightningEntityManager.test.cpp Source/Script/LightningEntityManager.cpp Source/Script/LightningEntityRegistry.cpp Source/Script/LightningScriptContext.cpp Source/Script/LightningScriptParser.cpp Source/Audio/SoundManager.cpp Source/World/ZoneManager.cpp Source/Log.cpp
	$(COMP) $(TEST_FLAGS) $(RAYLIB_INC) -ISource $^ $(BUILD_DIR)/miniz.o -o $@ $(LDFLAGS) $(LDEXTRA)

test_pawn_system: tests/OzPawnSystem.test.cpp Source/Pawn/OzPawnSystem.cpp Source/World/ZoneManager.cpp Source/Audio/SoundManager.cpp Source/Physics/OzBsp.cpp Source/Physics/WorldChunk.cpp Source/Physics/PlayerPhysics.cpp Source/Log.cpp Source/Renderer/OzAssetMapper.cpp Source/Script/LightningEntityManager.cpp Source/Script/LightningEntityRegistry.cpp Source/Script/LightningScriptContext.cpp Source/Script/LightningScriptParser.cpp Source/Renderer/Mesh/Mesh.cpp Source/Renderer/Mesh/SkeletalMesh.cpp Source/Renderer/Mesh/MeshCache.cpp Source/Renderer/Mesh/AnimatedMesh.cpp Source/Package/Anim/OzAnimFormat.cpp Source/Particle/OzParticleSimulationManager.cpp
	$(COMP) $(TEST_FLAGS) $(RAYLIB_INC) -ISource $^ $(BUILD_DIR)/miniz.o -o $@ $(LDFLAGS) $(LDEXTRA)

test_ozone_parser: tests/OzoneParser.test.cpp Source/World/OzoneParser.cpp
	$(SERVER_CXX) $(TEST_FLAGS) -ISource $^ $(BUILD_DIR)/miniz.o -o $@

# Static auditor for .ozone worlds: geometry bounds, lighting, mesh scale,
# asset references, playerstart sanity and zone naming. Pass a world file:
#   make worldcheck && ./worldcheck GameData/Worlds/<World>/World.ozone
# Exact model bounds (so mesh scale errors are caught) come from
# tools/glb_bounds.py; see the Makefile-adjacent README note in the tool.
worldcheck: tools/worldcheck.cpp Source/World/OzoneParser.cpp
	$(SERVER_CXX) $(TEST_FLAGS) -ISource tools/worldcheck.cpp Source/World/OzoneParser.cpp $(BUILD_DIR)/miniz.o -o $@

test_join_uri: tests/JoinUri.test.cpp Source/Client/JoinUri.hpp
	$(SERVER_CXX) $(TEST_FLAGS) -ISource tests/JoinUri.test.cpp -o $@

test_master: tests/Master.test.cpp Source/Network/MasterProtocol.hpp
	$(SERVER_CXX) $(TEST_FLAGS) -ISource tests/Master.test.cpp -o $@

test_network: tests/Network.test.cpp Source/Network/Network.cpp Source/Log.cpp
	$(SERVER_CXX) $(TEST_FLAGS) -ISource $^ -o $@ $(SERVER_LIBS)

test_game_state: tests/GameState.test.cpp Source/Server/GameState.cpp Source/World/OzoneParser.cpp Source/Network/Network.cpp Source/Log.cpp
	$(SERVER_CXX) $(TEST_FLAGS) -ISource $^ $(BUILD_DIR)/miniz.o -o $@ $(SERVER_LIBS)

test_ozanim: tests/OzAnim.test.cpp Source/Package/Anim/OzAnimFormat.cpp
	$(SERVER_CXX) $(TEST_FLAGS) -ISource $^ -o $@

test: test_parser test_context test_registry test_entity_manager test_pawn_system test_ozone_parser test_join_uri test_master test_network test_game_state test_ozanim
	@echo "=== LightningScriptParser Tests ==="
	-./test_parser
	@echo ""
	@echo "=== LightningScriptContext Tests ==="
	-./test_context
	@echo ""
	@echo "=== LightningEntityRegistry Tests ==="
	-./test_registry
	@echo ""
	@echo "=== LightningEntityManager Tests ==="
	-./test_entity_manager
	@echo ""
	@echo "=== OzPawnSystem Tests ==="
	-./test_pawn_system
	@echo ""
	@echo "=== OzoneParser Tests ==="
	-./test_ozone_parser
	@echo ""
	@echo "=== JoinUri Tests ==="
	-./test_join_uri
	@echo ""
	@echo "=== Master Protocol Tests ==="
	-./test_master
	@echo ""
	@echo "=== Network Packet Tests ==="
	-./test_network
	@echo ""
	@echo "=== GameState Tests ==="
	-./test_game_state
	@echo ""
	@echo "=== OzAnim Tests ==="
	-./test_ozanim

clean:
	rm -rf $(BUILD_DIR) *.exe AngelServ Angels95 AngelMaster OzPack *.o AngelEd/*.o AngelEd/Source/*.o test_context test_parser test_registry test_ozone_parser test_join_uri test_master test_network test_game_state test_ozanim worldcheck
