// ============================================================================
// Subsystems/PropsApply.hpp
//
// Writes the Entity Properties panel's values back into the world (the
// actionApplyProperties field's replacement, ed::Ev::ApplyProperties).
//
// A REAL translation unit as of Phase F.
// ============================================================================
#ifndef ANGEL_ED_SUBSYSTEMS_PROPSAPPLY_HPP
#define ANGEL_ED_SUBSYSTEMS_PROPSAPPLY_HPP

#include <vector>

namespace ed { struct Event; }

// Apply every properties edit in `events`, each carrying the SelRef the panel captured
// when Apply was pressed. The ~87 edited VALUES are still read from g_editorPanels at
// drain time; see the comment at the top of the .cpp for why that is deliberate.
void ApplyPanelProperties(const std::vector<ed::Event>& events);

#endif // ANGEL_ED_SUBSYSTEMS_PROPSAPPLY_HPP