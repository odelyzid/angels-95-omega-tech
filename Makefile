CFLAGS = -O3 --std=c++20 -fPIC
LDFLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
COMP = g++

SERVER_CXX = g++
SERVER_FLAGS = -O3 --std=c++20 -fPIC
SERVER_LIBS = -lm -lpthread

# Track our internal object files
OBJS = raygui.o Encoder.o Main.o Network.o Log.o Client.o

all: OTENGINE oz_server

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
	
# 5. Compile Network library
Network.o: Source/Network/Network.cpp Source/Network/Network.hpp
	$(SERVER_CXX) $(SERVER_FLAGS) -c Source/Network/Network.cpp

# 5b. Compile Log system (also used by server, so compile with SERVER_FLAGS too)
Log.o: Source/Log.cpp Source/Log.hpp
	$(SERVER_CXX) $(SERVER_FLAGS) -c Source/Log.cpp

# 5c. Compile Client networking
Client.o: Source/Client/Client.cpp Source/Client/Client.hpp
	$(COMP) $(CFLAGS) -c Source/Client/Client.cpp

# 6. Build Game Binary
OTENGINE: $(OBJS) Custom.so
	$(COMP) $(OBJS) -o OmegaTech $(CFLAGS) -L. -l:Custom.so $(LDFLAGS) -Wl,-rpath=.

# 7. Build oz_server (dedicated server, no raylib)
GameState.o: Source/Server/GameState.cpp Source/Server/GameState.hpp
	$(SERVER_CXX) $(SERVER_FLAGS) -c Source/Server/GameState.cpp

oz_server: Network.o GameState.o Log.o Source/Server/Server.cpp Source/Network/Network.hpp Source/Server/WDLParser.hpp Source/Server/OzoneParser.hpp Source/Server/WDLParser.cpp Source/Server/OzoneParser.cpp
	$(SERVER_CXX) $(SERVER_FLAGS) Network.o GameState.o Log.o Source/Server/Server.cpp Source/Server/WDLParser.cpp Source/Server/OzoneParser.cpp -o oz_server $(SERVER_LIBS)

clean:
	rm -f *.o *.so OmegaTech oz_server