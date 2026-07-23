@echo off
setlocal enabledelayedexpansion

REM ============================================================================
REM build.cmd -- Clean compile all targets, package assets, copy to System/
REM Requires: C:\raylib\w64devkit (GCC 15.2.0 + raylib 5.5)
REM ============================================================================

set ROOT=%~dp0
set W64DEVKIT=C:\raylib\w64devkit
set RAYLIB_INC=%W64DEVKIT%\include
set RAYLIB_LIB=%W64DEVKIT%\lib
set OUT_DIR=%ROOT%System

set PATH=%W64DEVKIT%\bin;%PATH%

echo === Checking prerequisites ===
where g++ >nul 2>&1
if errorlevel 1 (
    echo ERROR: g++ not found. Expected in %W64DEVKIT%\bin
    exit /b 1
)
g++ --version | findstr /i "g++"

REM --- Clean previous artifacts ---
echo.
echo === Cleaning previous build ===
cd /d "%ROOT%"
mingw32-make clean 2>nul
if exist "%ROOT%AngelEd\*.o" del /q "%ROOT%AngelEd\*.o" 2>nul

REM --- 1. Build OTENGINE (game client) ---
echo.
echo === Building Angels95 (OTENGINE) ===
cd /d "%ROOT%"
mingw32-make -j %NUMBER_OF_PROCESSORS% OTENGINE
if errorlevel 1 (
    echo ERROR: OTENGINE build failed
    exit /b 1
)
echo Angels95.exe built.

REM --- 2. Build AngelServ (dedicated server) ---
echo.
echo === Building AngelServ ===
cd /d "%ROOT%"
mingw32-make AngelServ
if errorlevel 1 (
    echo ERROR: AngelServ build failed
    exit /b 1
)
echo AngelServ.exe built.

REM --- 3. Build OzPack (asset packer) ---
echo.
echo === Building OzPack ===
cd /d "%ROOT%"
mingw32-make ozpack
if errorlevel 1 (
    echo ERROR: OzPack build failed
    exit /b 1
)
echo OzPack.exe built.

REM --- 4. Build AngelEd (level editor) ---
echo.
echo === Building AngelEd ===
cd /d "%ROOT%AngelEd"

set EDITOR_FLAGS=-O3 -g --std=c++20 -Wno-narrowing
set EDITOR_INC=-I ../Source -I Source -I %RAYLIB_INC%

REM Compile raygui
g++ -fpermissive %EDITOR_FLAGS% %EDITOR_INC% -c Source/raygui/raygui.c -DRAYGUI_IMPLEMENTATION -o raygui.o
if errorlevel 1 ( echo ERROR: raygui compilation failed & exit /b 1 )

REM Compile editor sources
g++ %EDITOR_FLAGS% %EDITOR_INC% -c Source/Main.cpp -o Main.o
if errorlevel 1 ( echo ERROR: Main.cpp compilation failed & exit /b 1 )

g++ %EDITOR_FLAGS% %EDITOR_INC% -c Source/Win32Dialogs.cpp -o Win32Dialogs.o
if errorlevel 1 ( echo ERROR: Win32Dialogs.cpp compilation failed & exit /b 1 )

g++ %EDITOR_FLAGS% %EDITOR_INC% -c Source/EditorIcons.cpp -o EditorIcons.o
if errorlevel 1 ( echo ERROR: EditorIcons.cpp compilation failed & exit /b 1 )

REM Compile engine sources needed by editor
g++ %EDITOR_FLAGS% %EDITOR_INC% -c ../Source/Pawn/OzPawnSystem.cpp -o OzPawnSystem.o
if errorlevel 1 ( echo ERROR: OzPawnSystem.cpp compilation failed & exit /b 1 )

g++ %EDITOR_FLAGS% %EDITOR_INC% -c ../Source/Package/OzAssetMapper.cpp -o OzAssetMapper.o
if errorlevel 1 ( echo ERROR: OzAssetMapper.cpp compilation failed & exit /b 1 )

g++ %EDITOR_FLAGS% %EDITOR_INC% -c ../Source/OzOzoneLoader.cpp -o OzOzoneLoader.o
if errorlevel 1 ( echo ERROR: OzOzoneLoader.cpp compilation failed & exit /b 1 )

g++ %EDITOR_FLAGS% %EDITOR_INC% -c ../Source/Server/OzoneParser.cpp -o OzoneParser.o
if errorlevel 1 ( echo ERROR: OzoneParser.cpp compilation failed & exit /b 1 )

g++ %EDITOR_FLAGS% %EDITOR_INC% -c ../Source/Server/WDLParser.cpp -o WDLParser.o
if errorlevel 1 ( echo ERROR: WDLParser.cpp compilation failed & exit /b 1 )

g++ %EDITOR_FLAGS% %EDITOR_INC% -c ../Source/Log.cpp -o Log.o
if errorlevel 1 ( echo ERROR: Log.cpp compilation failed & exit /b 1 )

g++ %EDITOR_FLAGS% %EDITOR_INC% -c ../Source/Physics/OzBsp.cpp -o OzBsp.o
if errorlevel 1 ( echo ERROR: OzBsp.cpp compilation failed & exit /b 1 )

g++ %EDITOR_FLAGS% %EDITOR_INC% -c ../Source/Physics/WorldChunk.cpp -o WorldChunk.o
if errorlevel 1 ( echo ERROR: WorldChunk.cpp compilation failed & exit /b 1 )

g++ %EDITOR_FLAGS% %EDITOR_INC% -c ../Source/Script/LightningScriptContext.cpp -o LightningScriptContext.o
if errorlevel 1 ( echo ERROR: LightningScriptContext.cpp compilation failed & exit /b 1 )

g++ %EDITOR_FLAGS% %EDITOR_INC% -c ../Source/Script/LightningScriptParser.cpp -o LightningScriptParser.o
if errorlevel 1 ( echo ERROR: LightningScriptParser.cpp compilation failed & exit /b 1 )

g++ %EDITOR_FLAGS% %EDITOR_INC% -c ../Source/Script/LightningEntityRegistry.cpp -o LightningEntityRegistry.o
if errorlevel 1 ( echo ERROR: LightningEntityRegistry.cpp compilation failed & exit /b 1 )

g++ %EDITOR_FLAGS% %EDITOR_INC% -c ../Source/Script/LightningEntityManager.cpp -o LightningEntityManager.o
if errorlevel 1 ( echo ERROR: LightningEntityManager.cpp compilation failed & exit /b 1 )

REM OTCustom stub
echo int main_custom() { return 0; } | g++ %EDITOR_FLAGS% %EDITOR_INC% -x c++ -c - -o OTCustom_stub.o
if errorlevel 1 ( echo ERROR: OTCustom stub compilation failed & exit /b 1 )

REM Link editor
set EDITOR_LIBS=-lraylib -lopengl32 -lgdi32 -lwinmm -lcomctl32 -lcomdlg32 -lws2_32 -lm
g++ Main.o Win32Dialogs.o OzPawnSystem.o OzAssetMapper.o OzOzoneLoader.o OzoneParser.o WDLParser.o Log.o OzBsp.o WorldChunk.o LightningScriptContext.o LightningScriptParser.o LightningEntityRegistry.o LightningEntityManager.o EditorIcons.o raygui.o OTCustom_stub.o -o AngelEd.exe %EDITOR_FLAGS% %EDITOR_INC% %EDITOR_LIBS%
if errorlevel 1 ( echo ERROR: AngelEd link failed & exit /b 1 )

REM Cleanup editor objects
del /q Main.o Win32Dialogs.o OzPawnSystem.o OzAssetMapper.o OzOzoneLoader.o OzoneParser.o WDLParser.o Log.o OzBsp.o WorldChunk.o LightningScriptContext.o LightningScriptParser.o LightningEntityRegistry.o LightningEntityManager.o EditorIcons.o raygui.o OTCustom_stub.o 2>nul

echo AngelEd.exe built.
cd /d "%ROOT%"

REM --- 5. Assemble System/ directory ---
echo.
echo === Assembling System/ release ===

if exist "%OUT_DIR%" (
    rmdir /s /q "%OUT_DIR%" 2>nul
)
mkdir "%OUT_DIR%" 2>nul
mkdir "%OUT_DIR%\Data" 2>nul
mkdir "%OUT_DIR%\Cache" 2>nul

REM Move EXEs from build locations into System/
if exist "%ROOT%Angels95.exe"  move /y "%ROOT%Angels95.exe"  "%OUT_DIR%\Angels95.exe"  >nul
if exist "%ROOT%AngelServ.exe" move /y "%ROOT%AngelServ.exe" "%OUT_DIR%\AngelServ.exe" >nul
if exist "%ROOT%OzPack.exe"    move /y "%ROOT%OzPack.exe"    "%OUT_DIR%\OzPack.exe"    >nul
if exist "%ROOT%AngelEd\AngelEd.exe" move /y "%ROOT%AngelEd\AngelEd.exe" "%OUT_DIR%\AngelEd.exe" >nul

REM Copy raylib DLL
if exist "%RAYLIB_LIB%\libraylib.dll" (
    copy /y "%RAYLIB_LIB%\libraylib.dll" "%OUT_DIR%\libraylib.dll" >nul
) else if exist "%W64DEVKIT%\bin\libraylib.dll" (
    copy /y "%W64DEVKIT%\bin\libraylib.dll" "%OUT_DIR%\libraylib.dll" >nul
)

REM Create INI files
echo [Video] > "%OUT_DIR%\Angels95.ini"
echo Width=1280 >> "%OUT_DIR%\Angels95.ini"
echo Height=720 >> "%OUT_DIR%\Angels95.ini"
echo Fullscreen=0 >> "%OUT_DIR%\Angels95.ini"
echo VSync=1 >> "%OUT_DIR%\Angels95.ini"
echo. >> "%OUT_DIR%\Angels95.ini"
echo [Audio] >> "%OUT_DIR%\Angels95.ini"
echo MasterVolume=1.0 >> "%OUT_DIR%\Angels95.ini"
echo MusicVolume=0.7 >> "%OUT_DIR%\Angels95.ini"
echo SFXVolume=1.0 >> "%OUT_DIR%\Angels95.ini"
echo. >> "%OUT_DIR%\Angels95.ini"
echo [Game] >> "%OUT_DIR%\Angels95.ini"
echo ServerIP=127.0.0.1 >> "%OUT_DIR%\Angels95.ini"
echo ServerPort=27015 >> "%OUT_DIR%\Angels95.ini"

echo [Editor] > "%OUT_DIR%\AngelEd.ini"
echo GridSize=1.0 >> "%OUT_DIR%\AngelEd.ini"
echo SnapToGrid=1 >> "%OUT_DIR%\AngelEd.ini"
echo ShowGrid=1 >> "%OUT_DIR%\AngelEd.ini"
echo. >> "%OUT_DIR%\AngelEd.ini"
echo [Video] >> "%OUT_DIR%\AngelEd.ini"
echo Width=1600 >> "%OUT_DIR%\AngelEd.ini"
echo Height=900 >> "%OUT_DIR%\AngelEd.ini"

echo [Server] > "%OUT_DIR%\OzServer.ini"
echo Port=27015 >> "%OUT_DIR%\OzServer.ini"
echo HttpPort=8080 >> "%OUT_DIR%\OzServer.ini"
echo MaxPlayers=16 >> "%OUT_DIR%\OzServer.ini"
echo WorldDir=GameData >> "%OUT_DIR%\OzServer.ini"
echo. >> "%OUT_DIR%\OzServer.ini"
echo [Game] >> "%OUT_DIR%\OzServer.ini"
echo ServerName=Angels95 Server >> "%OUT_DIR%\OzServer.ini"

REM Run scripts
echo @echo off > "%OUT_DIR%\run.bat"
echo cd /d "%%~dp0.." >> "%OUT_DIR%\run.bat"
echo start "" "%%~dp0Angels95.exe" >> "%OUT_DIR%\run.bat"

REM --- 6. Package assets ---
echo.
echo === Packaging assets ===
cd /d "%ROOT%"
if exist "build-data.ps1" (
    powershell -ExecutionPolicy Bypass -File build-data.ps1
    if errorlevel 1 (
        echo WARNING: Asset packaging returned errors
    )
) else (
    echo WARNING: build-data.ps1 not found, skipping asset packaging
)

REM Copy shaders if present
if exist "%ROOT%GameData\Shaders" (
    mkdir "%OUT_DIR%\Shaders" 2>nul
    xcopy /e /y "%ROOT%GameData\Shaders\*" "%OUT_DIR%\Shaders\" >nul
)

REM --- 7. Verify ---
echo.
echo === Verifying outputs ===
set MISSING=
if not exist "%OUT_DIR%\Angels95.exe"  set MISSING=%MISSING% Angels95.exe
if not exist "%OUT_DIR%\AngelServ.exe" set MISSING=%MISSING% AngelServ.exe
if not exist "%OUT_DIR%\AngelEd.exe"   set MISSING=%MISSING% AngelEd.exe
if not exist "%OUT_DIR%\OzPack.exe"    set MISSING=%MISSING% OzPack.exe

if defined MISSING (
    echo WARNING: Missing EXEs:%MISSING%
) else (
    echo All 4 executables present.
)

dir /b "%OUT_DIR%\Data\*.oz*" 2>nul | findstr /r "." >nul
if errorlevel 1 (
    echo WARNING: No .oz* packages found in System/Data
) else (
    dir /b "%OUT_DIR%\Data\*.oz*" 2>nul | find /c /v "" | >nul
    echo Packages found in System/Data.
)

echo.
echo === Build complete ===
echo System/ release in %OUT_DIR%
echo   Angels95.exe   - Game client
echo   AngelServ.exe  - Dedicated server
echo   AngelEd.exe    - Level editor
echo   OzPack.exe     - Asset packer
echo   Data/*.oz*     - Packaged assets
echo   Cache/         - Temporary runtime cache

endlocal
