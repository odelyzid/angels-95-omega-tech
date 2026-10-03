// ============================================================================
// Subsystems/SurfaceOps.cpp
//
// Per-face surface-property edits: ApplySurface and ResetSurface. Extracted from the
// surface dispatch pass in main() in R6, and named per the original plan
// (Subsystems/SurfaceOps) - which the plan wrongly claimed already existed folded into
// EntityOps.cpp. It did not: EntityOps.cpp holds only AutoConvex and the CSG commit.
//
// REAL translation unit as of Phase F.
// ============================================================================
#include "../Core/EditorShell.hpp"
#include "../Core/EditorState.hpp"
#include "SurfaceOps.hpp"

#include "../../../Source/World/OzOzoneLoader.hpp"
#include "../../../Source/World/SurfaceFlags.hpp"

void ApplySurfaceEdits(const std::vector<ed::Event>& events) {
    for (const ed::Event& sev : events) {
        const ed::SurfaceEdit& se = sev.surface();
        if (sev.kind == ed::Ev::ApplySurface) {
            OzoneRenderable* r = OzoneLoader::Instance().Get(se.renderable);
            if (!r) {
                EditorLog("Surface: apply ignored - renderable %d no longer exists",
                          se.renderable);
            } else if (!se.hasFaces()) {
                // Guarded on purpose: an empty mask must never be treated as
                // "apply to all six faces".
                EditorLog("Surface: apply ignored - no face selected");
            } else {
                HistoryPush();
                oz::surface::SurfaceProps p = g_editorPanels.surfaceEdit;
                p.flags      = se.flags;
                p.glowR      = se.glowR;  p.glowG = se.glowG;  p.glowB = se.glowB;
                p.glowScale  = se.glowScale;
                p.alpha      = se.alpha;  p.alphaCutoff = se.alphaCutoff;
                p.uvScaleU   = se.uvScaleU; p.uvScaleV = se.uvScaleV;
                p.uvOffsetU  = se.uvOffsetU; p.uvOffsetV = se.uvOffsetV;
                p.panU       = se.panU;   p.panV = se.panV;
                p.texSlot    = se.texSlot;
                for (int f = 0; f < oz::surface::FACE_COUNT; f++) {
                    if (!(se.faceMask & (1u << f))) continue;
                    OzoneLoader::Instance().SetRenderableFace(
                        se.renderable, (oz::surface::SurfaceFace)f, p);
                }
                EditorLog("Surface: applied to %d face(s) of renderable %d (flags=0x%X)",
                          __builtin_popcount(se.faceMask), se.renderable, se.flags);
                // The Surface Properties dialog is refreshed by the CALLER now, not
                // from here - see SurfaceOps.hpp.
            }
        } else if (sev.kind == ed::Ev::ResetSurface) {
            const int rIdx = se.renderable;
            OzoneRenderable* r = OzoneLoader::Instance().Get(rIdx);
            if (r && se.hasFaces()) {
                HistoryPush();
                for (int f = 0; f < oz::surface::FACE_COUNT; f++) {
                    if (!(se.faceMask & (1u << f))) continue;
                    r->surface.ClearFace((oz::surface::SurfaceFace)f);
                }
                // Face meshes only exist while a brush is decorated, so
                // dropping the last override has to release them.
                OzoneLoader::Instance().RebuildSurfaceMeshes(rIdx);
                EditorLog("Surface: reset %d face(s) of renderable %d",
                          __builtin_popcount(se.faceMask), rIdx);
            }
        }
    }
}