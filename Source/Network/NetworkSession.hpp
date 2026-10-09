#pragma once

// Network session state shared between Main.cpp (which owns the OmegaClient and
// the CLI/connect flow) and Core.hpp (which is included *before* those statics
// are declared, so it cannot reference them directly).
//
// Both are set once during connect/handshake and read every frame; no locking is
// needed because only the game thread touches them.

// True when a connection has been established and the session is live.
extern bool g_network_enabled;

// Ask the server to move this client into `world_index` — an index into the world
// list the server sent in the scene handshake.
//
// Core.hpp owns the portal and level-transition paths (it cannot reference
// OmegaClient, which Main.cpp declares after Core.hpp is included), and BOTH of them
// change the client's world locally. That made the client and the server disagree
// about which world the player was in, and three authoritative checks then compared
// the client's world against the server's permanently-0 `ServerPlayer::world_index`:
// collect_pickup's WRONG_WORLD reject, MELEE_HIT's world match, and the 15 s
// per-player pickup resync. So walking into a portal rendered a new level while the
// server still believed the player was in world 0, and every collect in the new level
// was rejected.
//
// The request is ADVISORY: the server bounds-checks the index against its own loaded
// worlds and answers with WORLD_CHANGE either way. This function is a no-op when
// offline, so callers do not need to guard on g_network_enabled first.
//
// Declared as a hook rather than exposing OmegaClient, for the same reason the two
// globals above are externs: Core.hpp is textually included before Main.cpp's statics
// are declared. Main.cpp owns g_client and installs the real implementation.
extern void NetworkRequestWorld(int world_index);

// Index of `world_name` in the server's world list, or -1 if the server has not sent
// a list (or the name is not in it). Exists because the portal path knows a world by
// NAME — an OZONE `portal <WorldName>` line — while the protocol is by INDEX.
//
// Returns -1 rather than 0 on a miss: 0 is a real world, and returning it on failure
// would silently move the player into whichever world happens to be first.
extern int NetworkWorldIndexByName(const char* world_name);

// Index of the world the server has us in: the position of the active world in
// the "worlds":[...] array sent during the scene handshake. Pickup and NPC
// packets carry this id, and the server rejects a collect whose world_index does
// not match the player's own world. -1 means "not yet known".
extern int g_network_world_index;