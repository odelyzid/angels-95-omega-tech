# Angels95 Windows Build Script (PowerShell)
# Requires: MinGW-w64 (g++), CMake, Git
# Run from the repo root.

$RAYLIB_VERSION = "5.5"
$RAYLIB_DIR = "$env:USERPROFILE\raylib-$RAYLIB_VERSION"
$OUT_DIR       = "$PSScriptRoot\System"

function Write-Step { Write-Host "==> $args" -ForegroundColor Cyan }
function Write-Error { Write-Host "ERROR: $args" -ForegroundColor Red; exit 1 }

# --- Prerequisites ---
Write-Step "Checking prerequisites..."

$haveGpp = Get-Command g++ -ErrorAction SilentlyContinue
$haveCmake = Get-Command cmake -ErrorAction SilentlyContinue
$haveGit = Get-Command git -ErrorAction SilentlyContinue

if (-not $haveGpp) {
    Write-Error "g++ not found in PATH. Install MinGW-w64 (https://winlibs.com) and add to PATH."
    exit 1
}
if (-not $haveCmake) {
    Write-Error "cmake not found. Install from https://cmake.org/download/"
    exit 1
}
if (-not $haveGit) {
    Write-Error "git not found. Install from https://git-scm.com/downloads"
    exit 1
}

Write-Step "g++: $($haveGpp.Source)"
Write-Step "cmake: $($haveCmake.Source)"
Write-Step "git: $($haveGit.Source)"

# --- raylib ---
$raylibInstalled = (Test-Path "$RAYLIB_DIR\raylib\include\raylib.h") -or
                   (Test-Path "C:\raylib\raylib\include\raylib.h")

if (-not $raylibInstalled) {
    Write-Step "raylib not found. Cloning and building v$RAYLIB_VERSION..."
    Push-Location $env:TEMP
    git clone --depth 1 --branch "$RAYLIB_VERSION" https://github.com/raysan5/raylib.git raylib-src
    New-Item -ItemType Directory -Force -Path raylib-src\build | Out-Null
    Set-Location raylib-src\build
    cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF `
             -DCMAKE_INSTALL_PREFIX="$RAYLIB_DIR"
    if (-not $?) { Write-Error "CMake configuration failed"; exit 1 }
    cmake --build . --parallel
    if (-not $?) { Write-Error "Build failed"; exit 1 }
    cmake --install .
    Pop-Location
    Remove-Item "$env:TEMP\raylib-src" -Recurse -Force -ErrorAction SilentlyContinue
    Write-Step "raylib installed to $RAYLIB_DIR"
} else {
    Write-Step "raylib found."
}

# Ensure raylib DLL is on PATH for runtime
$raylibBin = "$RAYLIB_DIR\bin"
if (Test-Path $raylibBin) {
    $currentPath = [Environment]::GetEnvironmentVariable("PATH", "User")
    if ($currentPath -notlike "*$raylibBin*") {
        [Environment]::SetEnvironmentVariable("PATH", "$currentPath;$raylibBin", "User")
        Write-Step "Added $raylibBin to user PATH (reopen shell to take effect)"
    }
}

# --- Clean previous build artifacts from root ---
Write-Step "Cleaning previous build..."
Push-Location $PSScriptRoot
& mingw32-make clean 2>&1 | Out-Null

# --- 1. Build OTENGINE (game client) ---
Write-Step "Building Angels95 (OTENGINE)..."
$env:Path += ";$RAYLIB_DIR\lib"
& mingw32-make -j $env:NUMBER_OF_PROCESSORS OTENGINE
if (-not $?) { Write-Error "Game build failed" }

# --- 2. Build oz_server (dedicated server) ---
Write-Step "Building oz_server..."
& mingw32-make -j $env:NUMBER_OF_PROCESSORS oz_server
if (-not $?) { Write-Error "Server build failed" }

# --- 3. Build oz_editor (level editor) ---
Write-Step "Building oz_editor..."
Push-Location "$PSScriptRoot\OTEditor"
$EDITOR_FLAGS = "-O3 -g --std=c++20 -Wno-narrowing"
$EDITOR_LIBS  = "-lraylib -lopengl32 -lgdi32 -lwinmm -lws2_32 -lm"

g++ -fpermissive $EDITOR_FLAGS -c Source/raygui/raygui.c -DRAYGUI_IMPLEMENTATION -o raygui.o
if (-not $?) { Write-Error "raygui compilation failed" }

g++ $EDITOR_FLAGS -c Source/Main.cpp -o Main.o
if (-not $?) { Write-Error "Editor Main.cpp compilation failed" }

$stub = 'int main_custom() { return 0; }'
$stub | g++ $EDITOR_FLAGS -x c++ -c - -o OTCustom_stub.o
if (-not $?) { Write-Error "OTCustom stub compilation failed" }

g++ Main.o raygui.o OTCustom_stub.o -o oz_editor.exe $EDITOR_FLAGS $EDITOR_LIBS
if (-not $?) { Write-Error "oz_editor link failed" }

Remove-Item Main.o, raygui.o, OTCustom_stub.o -Force
Pop-Location

# --- Gather outputs into System/ ---
Write-Step "Moving binaries to $OUT_DIR..."
if (-not (Test-Path $OUT_DIR)) { New-Item -ItemType Directory -Force -Path $OUT_DIR | Out-Null }
Move-Item -Force "$PSScriptRoot\Angels95.exe"   "$OUT_DIR\Angels95.exe"
Move-Item -Force "$PSScriptRoot\oz_server.exe"  "$OUT_DIR\oz_server.exe"
Move-Item -Force "$PSScriptRoot\OTEditor\oz_editor.exe" "$OUT_DIR\oz_editor.exe"

# --- Cleanup intermediate objects ---
Remove-Item "$PSScriptRoot\*.o" -Force -ErrorAction SilentlyContinue

Pop-Location

# --- Done ---
Write-Step "Build complete."
Write-Host "Binaries in $OUT_DIR" -ForegroundColor Green
Write-Host "  Angels95.exe  - Game client" -ForegroundColor Green
Write-Host "  oz_server.exe - Dedicated server" -ForegroundColor Green
Write-Host "  oz_editor.exe - Level editor" -ForegroundColor Green
