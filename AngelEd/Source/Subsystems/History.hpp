// ============================================================================
// Subsystems/History.hpp
//
// Full-document undo/redo via OZONE text snapshots (ExportToOzone / LoadString).
//
// A REAL translation unit as of Phase F.
// ============================================================================
#ifndef ANGEL_ED_SUBSYSTEMS_HISTORY_HPP
#define ANGEL_ED_SUBSYSTEMS_HISTORY_HPP

#include <string>

// Snapshot the current document as OZONE text. Declared here because OzoneExport
// needs it and History needs ExportToOzone - the only circular edge between these two,
// and it is a declaration rather than a call so there is no link cycle.
std::string HistoryCapture();

void HistoryClear();

// Call BEFORE a mutation: snapshots current state and invalidates redo.
void HistoryPush();

void HistoryRestore(const std::string& text);
void HistoryUndo();
void HistoryRedo();

#endif // ANGEL_ED_SUBSYSTEMS_HISTORY_HPP