# Detect OS
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  CFLAGS   = -O3 -g --std=c++20 -fPIC -Wno-narrowing
  LDFLAGS  = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
  WIN32_OBJS :=
else
  # Windows (MSYS2/MINGW64)
  CFLAGS   = -O3 -g --std=c++20 -Wno-narrowing -DWIN32_LEAN_AND_MEAN
  LDFLAGS  = -lraylib -lopengl32 -lgdi32 -lcomctl32 -lwinmm -lm
  WIN32_OBJS := Win32Dialogs.o
endif

COMP := g++
BUILD_DIR := ../build

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
	$(COMP) $(CFLAGS) -c ../Source/Script/LightningEntityManager.cpp -o $@

# 4. Compile raygui layer
$(BUILD_DIR)/raygui.o: Source/raygui/raygui.c | $(BUILD_DIR)
	$(COMP) -fpermissive $(CFLAGS) -c Source/raygui/raygui.c -shared -DRAYGUI_IMPLEMENTATION -DBUILD_LIBTYPE_SHARED $(LDFLAGS) -o $@

# 5. Link into final AngelEd binary
LIGHTNING_OBJS := $(addprefix $(BUILD_DIR)/, \
    LightningScriptContext.o LightningScriptParser.o \
    LightningEntityRegistry.o LightningEntityManager.o)
$(BUILD_DIR)/EditorIcons.o: Source/EditorIcons.cpp Source/EditorIcons.hpp | $(BUILD_DIR)
	$(COMP) $(CFLAGS) -c Source/EditorIcons.cpp -o $@
EDITOR_OBJS := $(BUILD_DIR)/raygui.o $(BUILD_DIR)/Main.o $(BUILD_DIR)/EditorIcons.o $(addprefix $(BUILD_DIR)/,$(WIN32_OBJS)) $(LIGHTNING_OBJS)
AngelEd: $(EDITOR_OBJS) $(BUILD_DIR)/Custom.so
	$(COMP) $(EDITOR_OBJS) -o AngelEd $(CFLAGS) -L$(BUILD_DIR) -l:Custom.so $(LDFLAGS) -Wl,-rpath=$(BUILD_DIR)

clean:
	rm -f AngelEd
	rm -f $(BUILD_DIR)/Main.o $(BUILD_DIR)/Win32Dialogs.o $(BUILD_DIR)/raygui.o $(BUILD_DIR)/EditorIcons.o $(BUILD_DIR)/Custom.so
	rm -f $(LIGHTNING_OBJS)
