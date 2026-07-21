# build-native-win.ps1 — Native Windows build using C:\raylib w64devkit + raylib
# Run from repo root: powershell -File build-native-win.ps1

$W64DEVKIT = "C:\raylib\w64devkit"
$RAYLIB_SRC = "C:\raylib\raylib"
$OUT_DIR    = "$PSScriptRoot\System"

function Write-Step  { Write-Host "==> $args" -ForegroundColor Cyan }
function Write-Error { Write-Host "ERROR: $args" -ForegroundColor Red; exit 1 }

# --- PATH ---
$env:Path = "$W64DEVKIT\bin;$env:Path"

# --- Prerequisites ---
Write-Step "Checking prerequisites..."
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    Write-Error "g++ not found. Expected in $W64DEVKIT\bin"
}
Write-Step "g++ $(& g++ --version | Select-Object -First 1)"

# --- raylib headers ---
Write-Step "Ensuring raylib headers are available..."
if (-not (Test-Path "$W64DEVKIT\include\raymath.h")) {
    if (Test-Path "$RAYLIB_SRC\src\raymath.h") {
        Copy-Item "$RAYLIB_SRC\src\raymath.h" "$W64DEVKIT\include\raymath.h"
        Copy-Item "$RAYLIB_SRC\src\rcamera.h"  "$W64DEVKIT\include\rcamera.h" -ErrorAction SilentlyContinue
        Copy-Item "$RAYLIB_SRC\src\rgestures.h" "$W64DEVKIT\include\rgestures.h" -ErrorAction SilentlyContinue
        Write-Step "Copied missing raylib headers to $W64DEVKIT\include"
    } else {
        Write-Error "raylib source not found at $RAYLIB_SRC\src\raylib.h"
    }
}

# --- Clean previous artifacts ---
Write-Step "Cleaning previous build..."
Push-Location $PSScriptRoot
& mingw32-make clean 2>&1 | Out-Null
if (Test-Path "$PSScriptRoot\OTEditor\*.o") { Remove-Item "$PSScriptRoot\OTEditor\*.o" -Force }

# --- 1. Build OTENGINE (game client) ---
Write-Step "Building Angels95 (OTENGINE)..."
& mingw32-make -j $env:NUMBER_OF_PROCESSORS OTENGINE 2>&1
if (-not $?) { Write-Error "OTENGINE build failed" }
Write-Step "Angels95.exe built."

# --- 2. Build oz_server (dedicated server) ---
Write-Step "Building oz_server..."
& mingw32-make oz_server 2>&1
if (-not $?) { Write-Error "oz_server build failed" }
Write-Step "oz_server.exe built."

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
Write-Step "oz_editor.exe built."

# --- Gather outputs ---
Write-Step "Moving binaries to $OUT_DIR..."
if (-not (Test-Path $OUT_DIR)) { New-Item -ItemType Directory -Force -Path $OUT_DIR | Out-Null }
Move-Item -Force "$PSScriptRoot\Angels95.exe"  "$OUT_DIR\Angels95.exe"
Move-Item -Force "$PSScriptRoot\oz_server.exe" "$OUT_DIR\oz_server.exe"
Move-Item -Force "$PSScriptRoot\OTEditor\oz_editor.exe" "$OUT_DIR\oz_editor.exe"

# --- Cleanup intermediate objects ---
Remove-Item "$PSScriptRoot\*.o" -Force -ErrorAction SilentlyContinue

Pop-Location

# --- Done ---
Write-Step "Build complete."
Write-Host "Binaries in $OUT_DIR" -ForegroundColor Green
Write-Host "  Angels95.exe  — Game client" -ForegroundColor Green
Write-Host "  oz_server.exe — Dedicated server" -ForegroundColor Green
Write-Host "  oz_editor.exe — Level editor" -ForegroundColor Green