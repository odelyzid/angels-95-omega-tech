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
where windres >nul 2>&1
if errorlevel 1 (
    echo ERROR: windres not found. Expected in %W64DEVKIT%\bin
    exit /b 1
)
where mingw32-make >nul 2>&1
if errorlevel 1 (
    echo ERROR: mingw32-make not found. Expected in %W64DEVKIT%\bin
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

REM --- 4. Build AngelEd (level editor) via Makefile ---
echo.
echo === Building AngelEd ===
cd /d "%ROOT%AngelEd"
mingw32-make -j %NUMBER_OF_PROCESSORS% AngelEd
if errorlevel 1 ( echo ERROR: AngelEd build failed & exit /b 1 )
cd /d "%ROOT%"
echo AngelEd.exe built.

REM --- 5. Assemble System/ directory ---
echo.
echo === Assembling System/ release ===

if exist "%OUT_DIR%" (
    rmdir /s /q "%OUT_DIR%" 2>nul
)
mkdir "%OUT_DIR%" 2>nul
mkdir "%OUT_DIR%\Data" 2>nul
mkdir "%OUT_DIR%\Cache" 2>nul

REM Move EXEs from build locations into System/ (fail if any missing)
if not exist "%ROOT%Angels95.exe"  echo ERROR: Angels95.exe missing & exit /b 1
if not exist "%ROOT%AngelServ.exe" echo ERROR: AngelServ.exe missing & exit /b 1
if not exist "%ROOT%OzPack.exe"    echo ERROR: OzPack.exe missing & exit /b 1
if not exist "%ROOT%AngelEd\AngelEd.exe" echo ERROR: AngelEd.exe missing & exit /b 1
move /y "%ROOT%Angels95.exe"  "%OUT_DIR%\Angels95.exe"  >nul
move /y "%ROOT%AngelServ.exe" "%OUT_DIR%\AngelServ.exe" >nul
move /y "%ROOT%OzPack.exe"    "%OUT_DIR%\OzPack.exe"    >nul
move /y "%ROOT%AngelEd\AngelEd.exe" "%OUT_DIR%\AngelEd.exe" >nul

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
