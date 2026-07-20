CFLAGS = -O3 --std=c++20 -fPIC
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
COMP = g++

# Track our internal object files
OBJS = raygui.o Encoder.o Main.o

all: OTENGINE

# 1. Compile Main Game Logic
Main.o: Source/Main.cpp Source/*.hpp Source/Parasite/*.hpp
	$(COMP) $(CFLAGS) -c Source/Main.cpp

# 2. Compile Custom Engine Shared Object Behavior
Custom.so: Source/Custom/OTCustom.cpp 
	$(COMP) $(CFLAGS) -c Source/Custom/OTCustom.cpp
	$(COMP) $(CFLAGS) -shared OTCustom.o -o Custom.so
	rm OTCustom.o

# 3. Compile the asset Encoder
Encoder.o: Source/Encoder/Encoder.cpp
	$(COMP) $(CFLAGS) -c Source/Encoder/Encoder.cpp

# 4. Compile Linux-Safe RayGUI Layer (With C++ Type-Safety Override)
raygui.o: Source/raygui/raygui.c
	$(COMP) -fpermissive $(CFLAGS) -c Source/raygui/raygui.c -shared -DRAYGUI_IMPLEMENTATION -DBUILD_LIBTYPE_SHARED -lGL -lX11 -lpthread -lrt -lm -ldl
	
# 5. Build Final Binary by stitching objects and Custom.so together
OTENGINE: $(OBJS) Custom.so
	$(COMP) $(OBJS) -o OmegaTech $(CFLAGS) -L. -l:Custom.so $(LDFLAGS) -Wl,-rpath=.

clean:
	rm -f *.o *.so OmegaTech