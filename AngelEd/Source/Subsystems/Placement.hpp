// ============================================================================
// Subsystems/Placement.hpp
//
// Entity placement: the ten ed::Ev::Spawn* kinds.
//
// A REAL translation unit as of Phase F. The panel validates and posts
// (UI/Panels/PawnPanel.cpp); this applies.
// ============================================================================
#ifndef ANGEL_ED_SUBSYSTEMS_PLACEMENT_HPP
#define ANGEL_ED_SUBSYSTEMS_PLACEMENT_HPP

#include <vector>

namespace ed { struct Event; }

// Apply every placement in `events`. Reads no live panel or selection state, so a tree
// rebuild or a selection change between the click and the frame cannot place a
// different entity.
//
// Placement is deferred by one frame (the caller drains). That is safe because the
// payload is complete - see UI/Panels/PawnPanel.cpp for the position-capture argument.
void ApplyPlacementSpawns(const std::vector<ed::Event>& events);

#endif // ANGEL_ED_SUBSYSTEMS_PLACEMENT_HPP