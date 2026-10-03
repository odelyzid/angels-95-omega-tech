// ============================================================================
// Subsystems/Selection.hpp
//
// Picking, selection and the per-entity delete/duplicate operations.
//
// A REAL translation unit as of Phase F (the .cpp is still a unity fragment for now;
// this header exists now because PropsApply.cpp needs two of its helpers and cannot
// reach into a fragment).
// ============================================================================
#ifndef ANGEL_ED_SUBSYSTEMS_SELECTION_HPP
#define ANGEL_ED_SUBSYSTEMS_SELECTION_HPP

#include "../Core/EditorState.hpp"

#include <vector>

// Find a playerstart by its id. Used by the properties apply to write position and yaw.
PlayerStartNode* FindPlayerStartById(int id);

// Clamp an int into [lo, hi], logging when it had to. Shared with the properties apply
// so a row cannot write an out-of-range value by whatever route it was typed.
int ClampPropInt(int v, int lo, int hi);

#endif // ANGEL_ED_SUBSYSTEMS_SELECTION_HPP