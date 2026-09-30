#pragma once

// Cross-translation-unit debug flag.
//
// Settings.hpp declares `static bool Debug`, so every .cpp that includes it
// gets its own private copy and only the copy in the file that calls
// LoadClientSettings() ever sees the ini value. Anything outside that
// translation unit that needs to know whether debug / authoring gizmos should be
// drawn reads this instead (C++17 inline variable => exactly one instance).
//
// This is its own header on purpose: Settings.hpp also declares non-static
// globals (AudioSlider, MuteToggle, MXAAToggle, ...), so including it from a
// second .cpp produces "multiple definition" link errors.
inline bool g_debugEnabled = false;