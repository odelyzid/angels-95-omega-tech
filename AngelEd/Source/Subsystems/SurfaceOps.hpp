// ============================================================================
// Subsystems/SurfaceOps.hpp
//
// Per-face surface-property edits: ApplySurface and ResetSurface.
//
// A REAL translation unit as of Phase F. It was a unity fragment until then, which
// meant it silently depended on Main.cpp's include order for its declarations.
// ============================================================================
#ifndef ANGEL_ED_SUBSYSTEMS_SURFACEOPS_HPP
#define ANGEL_ED_SUBSYSTEMS_SURFACEOPS_HPP

#include <vector>

namespace ed { struct Event; }

// Apply every surface edit in `events`.
//
// Takes the events by parameter and reads NOTHING global. It used to iterate
// `g_editorSurfaceEvents` directly and ignore this argument entirely - harmless while
// the whole editor was one translation unit and the caller happened to pass that exact
// vector, and a lie in the signature the moment anyone called it with anything else.
//
// Deliberately does NOT touch the Surface Properties dialog. It used to call
// SurfacePropsRefresh() twice, which is a Subsystems-to-UI dependency - the layer rule
// runs UI may call Subsystems, never the reverse. The caller refreshes instead.
void ApplySurfaceEdits(const std::vector<ed::Event>& events);

#endif // ANGEL_ED_SUBSYSTEMS_SURFACEOPS_HPP