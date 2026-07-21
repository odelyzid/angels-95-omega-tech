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
OBJS := raygui.o Main.o $(WIN32_OBJS)

.PHONY: all clean

all: oz_editor

# 1. Compile Main Editor Source
Main.o: Source/Main.cpp Source/*.hpp
	$(COMP) $(CFLAGS) -c Source/Main.cpp

# 2. Compile Win32 Dialog support (Windows only)
Win32Dialogs.o: Source/Win32Dialogs.cpp Source/Win32Dialogs.hpp
	$(COMP) $(CFLAGS) -c Source/Win32Dialogs.cpp

# 3. Compile Custom Engine Behavior
Custom.so:
	@if [ -f Source/OTCustom.cpp ]; then \
		$(COMP) $(CFLAGS) -c Source/OTCustom.cpp -o OTCustom.o; \
	elif [ -f Source/Custom.cpp ]; then \
		$(COMP) $(CFLAGS) -c Source/Custom.cpp -o OTCustom.o; \
	else \
		echo "int main_custom() { return 0; }" | $(COMP) $(CFLAGS) -x c++ -c - -o OTCustom.o; \
	fi
	$(COMP) $(CFLAGS) -shared OTCustom.o -o Custom.so
	rm -f OTCustom.o

# 4. Compile raygui layer
raygui.o: Source/raygui/raygui.c
	$(COMP) -fpermissive $(CFLAGS) -c Source/raygui/raygui.c -shared -DRAYGUI_IMPLEMENTATION -DBUILD_LIBTYPE_SHARED $(LDFLAGS)

# 5. Link into final oz_editor binary
oz_editor: $(OBJS) Custom.so
	$(COMP) $(OBJS) -o oz_editor $(CFLAGS) -L. -l:Custom.so $(LDFLAGS) -Wl,-rpath=.

clean:
	rm -f *.o *.so oz_editor OTEditor
