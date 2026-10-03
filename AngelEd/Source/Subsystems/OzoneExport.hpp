// ============================================================================
// Subsystems/OzoneExport.hpp
//
// ONE declaration from the OZONE exporter, not a real interface yet.
//
// Subsystems/History.cpp calls ExportToOzone to snapshot the document for undo, and it
// is a real translation unit. That is the whole reason this header exists: a `static`
// function in a unity fragment cannot satisfy an external call.
//
// The exporter itself is still a unity fragment. It calls UI file dialogs
// (ChooseOpenWorldFile / ChooseSaveWorldFile) and uses Core statics
// (g_documentPath, SetWorldDirectory), so promoting it means cutting those seams first
// rather than declaring them across a layer boundary.
#ifndef ANGEL_ED_SUBSYSTEMS_OZONEEXPORT_HPP
#define ANGEL_ED_SUBSYSTEMS_OZONEEXPORT_HPP

#include <ostream>

// Serialise the whole document to `output`.
void ExportToOzone(std::ostream& output);

#endif // ANGEL_ED_SUBSYSTEMS_OZONEEXPORT_HPP