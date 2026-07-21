CFLAGS = -O3 -g --std=c++20 -fPIC -Wno-narrowing
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
COMP = g++

OBJS = raygui.o \
       Main.o

all: oz_editor

# 1. Compile Main Editor Source
Main.o: Source/Main.cpp Source/*.hpp
	$(COMP) $(CFLAGS) -c Source/Main.cpp

# 2. Compile Custom Engine Behavior (checking root Source/ folder)
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

# 3. Compile raygui layer safely for C++ / Linux
raygui.o: Source/raygui/raygui.c
	$(COMP) -fpermissive $(CFLAGS) -c Source/raygui/raygui.c -shared -DRAYGUI_IMPLEMENTATION -DBUILD_LIBTYPE_SHARED -lGL -lX11 -lpthread -lrt -lm -ldl

# 4. Link into final oz_editor binary
oz_editor: $(OBJS) Custom.so
	$(COMP) Main.o raygui.o -o oz_editor $(CFLAGS) -L. -l:Custom.so $(LDFLAGS) -Wl,-rpath=.

clean:
	rm -f *.o *.so oz_editor OTEditor