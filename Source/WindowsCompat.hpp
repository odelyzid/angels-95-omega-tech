#ifndef OMEGA_WINDOWS_COMPAT_HPP
#define OMEGA_WINDOWS_COMPAT_HPP

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef NOGDI
        #define NOGDI
    #endif
    #ifndef NOMB
        #define NOMB
    #endif

    // Rename conflicting Windows function declarations so they don't clash with raylib.
    // raylib declares its own CloseWindow(), ShowCursor(), LoadImage(), DrawTextEx(), etc.
    // The rename macro causes the preprocessor to rename the Windows declarations, then we
    // #undef so the rest of the code can use the original names for raylib's versions.
    #define CloseWindow  __WIN32_CloseWindow
    #define ShowCursor   __WIN32_ShowCursor

    #include <windows.h>

    #undef CloseWindow
    #undef ShowCursor

    #ifdef LoadImage
        #undef LoadImage
    #endif
    #ifdef DrawTextEx
        #undef DrawTextEx
    #endif
    #ifdef DrawText
        #undef DrawText
    #endif
    #ifdef byte
        #undef byte
    #endif
#endif

#endif
