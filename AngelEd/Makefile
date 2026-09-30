# Detect OS
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  $(error AngelEd requires Win32 API (native panels). Build on Windows with w64devkit or MSYS2.)
  CFLAGS   = -O3 -g --std=c++20 -fPIC -Wno-narrowing
  LDFLAGS  = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
  WIN32_OBJS :=
else
  # Windows (MSYS2/MINGW64)
  CFLAGS   = -O3 -g --std=c++20 -Wno-narrowing -DWIN32_LEAN_AND_MEAN -I../Source
  LDFLAGS  = -lraylib -lopengl32 -lgdi32 -lcomctl32 -lwinmm -lcomdlg32 -lm
  WIN32_OBJS := Win32Dialogs.o
endif

COMP := g++
BUILD_DIR := ../build-ed

.PHONY: all clean

all: AngelEd

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# 1. Compile Main Editor Source
$(BUILD_DIR)/Main.o: Source/Main.cpp Source/*.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Main.cpp -o $@

# 2. Compile Win32 Dialog support (Windows only)
$(BUILD_DIR)/Win32Dialogs.o: Source/Win32Dialogs.cpp Source/Win32Dialogs.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/Win32Dialogs.cpp -o $@

# 3. Compile Custom Engine Behavior
$(BUILD_DIR)/Custom.so:
	@if [ -f Source/OTCustom.cpp ]; then \
		$(COMP) $(CFLAGS) -c Source/OTCustom.cpp -o $(BUILD_DIR)/OTCustom.o; \
	elif [ -f Source/Custom.cpp ]; then \
		$(COMP) $(CFLAGS) -c Source/Custom.cpp -o $(BUILD_DIR)/OTCustom.o; \
	else \
		echo "int main_custom() { return 0; }" | $(COMP) $(CFLAGS) -x c++ -c - -o $(BUILD_DIR)/OTCustom.o; \
	fi
	$(COMP) $(CFLAGS) -shared $(BUILD_DIR)/OTCustom.o -o $@
	rm -f $(BUILD_DIR)/OTCustom.o

# 4a. Compile LightningScript system
$(BUILD_DIR)/LightningScriptContext.o: ../Source/Script/LightningScriptContext.cpp ../Source/Script/LightningScriptContext.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Script/LightningScriptContext.cpp -o $@

$(BUILD_DIR)/LightningScriptParser.o: ../Source/Script/LightningScriptParser.cpp ../Source/Script/LightningScriptParser.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Script/LightningScriptParser.cpp -o $@

$(BUILD_DIR)/LightningEntityRegistry.o: ../Source/Script/LightningEntityRegistry.cpp ../Source/Script/LightningEntityRegistry.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Script/LightningEntityRegistry.cpp -o $@

$(BUILD_DIR)/LightningEntityManager.o: ../Source/Script/LightningEntityManager.cpp ../Source/Script/LightningEntityManager.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -DOMEGA_TEST_ENV -c ../Source/Script/LightningEntityManager.cpp -o $@

# 4. Compile raygui layer
$(BUILD_DIR)/raygui.o: Source/raygui/raygui.c | $(BUILD_DIR)
	$(COMP) -fpermissive $(CFLAGS) -c Source/raygui/raygui.c -shared -DRAYGUI_IMPLEMENTATION -DBUILD_LIBTYPE_SHARED $(LDFLAGS) -o $@

# 5. Link into final AngelEd binary
LIGHTNING_OBJS := $(addprefix $(BUILD_DIR)/, \
    LightningScriptContext.o LightningScriptParser.o \
    LightningEntityRegistry.o LightningEntityManager.o)
$(BUILD_DIR)/EditorIcons.o: Source/EditorIcons.cpp Source/EditorIcons.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/EditorIcons.cpp -o $@
# 5b. Core engine objects needed by the editor
$(BUILD_DIR)/OzPawnSystem.o: ../Source/Pawn/OzPawnSystem.cpp ../Source/Pawn/OzPawnSystem.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Pawn/OzPawnSystem.cpp -o $@

$(BUILD_DIR)/OzOzoneLoader.o: ../Source/World/OzOzoneLoader.cpp ../Source/World/OzOzoneLoader.hpp ../Source/World/OzoneParser.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/World/OzOzoneLoader.cpp -o $@

$(BUILD_DIR)/OzoneFrustum.o: ../Source/World/OzoneFrustum.cpp ../Source/World/OzoneFrustum.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/World/OzoneFrustum.cpp -o $@

$(BUILD_DIR)/OzoneHeightmap.o: ../Source/World/OzoneHeightmap.cpp ../Source/World/OzOzoneLoader.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/World/OzoneHeightmap.cpp -o $@

$(BUILD_DIR)/OzBsp.o: ../Source/Physics/OzBsp.cpp ../Source/Physics/OzBsp.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Physics/OzBsp.cpp -o $@

$(BUILD_DIR)/OzoneParser.o: ../Source/World/OzoneParser.cpp ../Source/World/OzoneParser.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/World/OzoneParser.cpp -o $@

$(BUILD_DIR)/Log.o: ../Source/Log.cpp ../Source/Log.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Log.cpp -o $@

$(BUILD_DIR)/OzAssetMapper.o: ../Source/Renderer/OzAssetMapper.cpp ../Source/Renderer/OzAssetMapper.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Renderer/OzAssetMapper.cpp -o $@

# miniz (C compile; g++ would mangle symbols vs the extern "C" prototypes)
$(BUILD_DIR)/miniz.o: ../Source/miniz/miniz.c ../Source/miniz/miniz.h | $(BUILD_DIR)
	gcc -O3 -std=c99 -I../Source/miniz -c ../Source/miniz/miniz.c -o $@

$(BUILD_DIR)/WorldChunk.o: ../Source/Physics/WorldChunk.cpp ../Source/Physics/WorldChunk.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Physics/WorldChunk.cpp -o $@

$(BUILD_DIR)/LitLightning.o: ../Source/Renderer/LitLightning.cpp ../Source/Renderer/LitLightning.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Renderer/LitLightning.cpp -o $@

$(BUILD_DIR)/Mesh.o: ../Source/Renderer/Mesh/Mesh.cpp ../Source/Renderer/Mesh/Mesh.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Renderer/Mesh/Mesh.cpp -o $@

$(BUILD_DIR)/SkeletalMesh.o: ../Source/Renderer/Mesh/SkeletalMesh.cpp ../Source/Renderer/Mesh/SkeletalMesh.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Renderer/Mesh/SkeletalMesh.cpp -o $@

$(BUILD_DIR)/MeshCache.o: ../Source/Renderer/Mesh/MeshCache.cpp ../Source/Renderer/Mesh/MeshCache.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Renderer/Mesh/MeshCache.cpp -o $@

$(BUILD_DIR)/OzAnimFormat.o: ../Source/Package/Anim/OzAnimFormat.cpp ../Source/Package/Anim/OzAnimFormat.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Package/Anim/OzAnimFormat.cpp -o $@

$(BUILD_DIR)/AnimatedMesh.o: ../Source/Renderer/Mesh/AnimatedMesh.cpp ../Source/Renderer/Mesh/AnimatedMesh.hpp ../Source/Package/Anim/OzAnimFormat.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Renderer/Mesh/AnimatedMesh.cpp -o $@

$(BUILD_DIR)/OzParticleSimulationManager.o: ../Source/Particle/OzParticleSimulationManager.cpp ../Source/Particle/OzParticleSimulationManager.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Particle/OzParticleSimulationManager.cpp -o $@

$(BUILD_DIR)/rlights.o: ../Source/Renderer/rlights/rlights.cpp ../Source/Renderer/rlights/rlights.h | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c ../Source/Renderer/rlights/rlights.cpp -o $@

CORE_OBJS := $(BUILD_DIR)/OzPawnSystem.o $(BUILD_DIR)/OzOzoneLoader.o $(BUILD_DIR)/OzoneFrustum.o $(BUILD_DIR)/OzoneHeightmap.o $(BUILD_DIR)/OzBsp.o \
             $(BUILD_DIR)/OzoneParser.o $(BUILD_DIR)/Log.o $(BUILD_DIR)/OzAssetMapper.o \
             $(BUILD_DIR)/WorldChunk.o $(BUILD_DIR)/LitLightning.o $(BUILD_DIR)/Mesh.o $(BUILD_DIR)/SkeletalMesh.o \
             $(BUILD_DIR)/MeshCache.o $(BUILD_DIR)/AnimatedMesh.o $(BUILD_DIR)/OzAnimFormat.o \
             $(BUILD_DIR)/OzParticleSimulationManager.o $(BUILD_DIR)/rlights.o $(BUILD_DIR)/miniz.o

# 5c. Editor Windows resource
$(BUILD_DIR)/AngelEd.res: AngelEd.rc ../GameData/Global/Icon/angelEd.ico | $(BUILD_DIR)
	windres AngelEd.rc -O coff -o $@

EDITOR_OBJS := $(BUILD_DIR)/raygui.o $(BUILD_DIR)/Main.o $(BUILD_DIR)/EditorIcons.o $(BUILD_DIR)/AngelEd.res $(addprefix $(BUILD_DIR)/,$(WIN32_OBJS)) $(LIGHTNING_OBJS) $(CORE_OBJS)
AngelEd: $(EDITOR_OBJS) $(BUILD_DIR)/Custom.so
	$(COMP) $(EDITOR_OBJS) -o AngelEd $(CFLAGS) -L$(BUILD_DIR) -l:Custom.so $(LDFLAGS) -Wl,-rpath=$(BUILD_DIR)

clean:
	rm -f AngelEd
	rm -rf $(BUILD_DIR)
