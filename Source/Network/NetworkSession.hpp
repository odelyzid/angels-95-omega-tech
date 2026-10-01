#pragma once

// Network session state shared between Main.cpp (which owns the OmegaClient and
// the CLI/connect flow) and Core.hpp (which is included *before* those statics
// are declared, so it cannot reference them directly).
//
// Both are set once during connect/handshake and read every frame; no locking is
// needed because only the game thread touches them.

// True when a connection has been established and the session is live.
extern bool g_network_enabled;

// Index of the world the server has us in: the position of the active world in
// the "worlds":[...] array sent during the scene handshake. Pickup and NPC
// packets carry this id, and the server rejects a collect whose world_index does
// not match the player's own world. -1 means "not yet known".
extern int g_network_world_index;