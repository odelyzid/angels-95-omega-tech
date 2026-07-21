# build-native-win.ps1 — Native Windows build using C:\raylib w64devkit + raylib
# Run from repo root: powershell -File build-native-win.ps1

param(
    [switch]$SkipData,
    [switch]$SkipClean
)

$W64DEVKIT = "C:\raylib\w64devkit"
$RAYLIB_SRC = "C:\raylib\raylib"
$OUT_DIR    = "$PSScriptRoot\System"

function Write-Step  { Write-Host "==> $args" -ForegroundColor Cyan }
function Fail { Write-Host "ERROR: $args" -ForegroundColor Red; exit 1 }

# --- PATH ---
$env:Path = "$W64DEVKIT\bin;$env:Path"

# --- Prerequisites ---
Write-Step "Checking prerequisites..."
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
    Fail "g++ not found. Expected in $W64DEVKIT\bin"
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
        Fail "raylib source not found at $RAYLIB_SRC\src\raylib.h"
    }
}

# --- Clean previous artifacts ---
if (-not $SkipClean) {
    Write-Step "Cleaning previous build..."
    Push-Location $PSScriptRoot
    & mingw32-make clean 2>&1 | Out-Null
    if (Test-Path "$PSScriptRoot\OTEditor\*.o") { Remove-Item "$PSScriptRoot\OTEditor\*.o" -Force }
    Pop-Location
}

# --- 1. Build OTENGINE (game client) ---
Write-Step "Building Angels95 (OTENGINE)..."
Push-Location $PSScriptRoot
& mingw32-make -j $env:NUMBER_OF_PROCESSORS OTENGINE 2>&1
if ($LASTEXITCODE -ne 0) { Fail "OTENGINE build failed" }
Write-Step "Angels95.exe built."

# --- 2. Build oz_server (dedicated server) ---
Write-Step "Building oz_server..."
& mingw32-make oz_server 2>&1
if ($LASTEXITCODE -ne 0) { Fail "oz_server build failed" }
Write-Step "oz_server.exe built."

# --- 3. Build OzPack (asset packer) ---
Write-Step "Building OzPack..."
& mingw32-make ozpack 2>&1
if ($LASTEXITCODE -ne 0) { Fail "OzPack build failed" }
Write-Step "OzPack.exe built."
Pop-Location

# --- 4. Build oz_editor (level editor) ---
Write-Step "Building oz_editor..."
Push-Location "$PSScriptRoot\OTEditor"
$EDITOR_FLAGS = @('-O3', '-g', '--std=c++20', '-Wno-narrowing')
$EDITOR_INC   = @('-I', '../Source', '-I', 'Source', '-I', "$W64DEVKIT\include")
$EDITOR_LIBS  = @('-lraylib', '-lopengl32', '-lgdi32', '-lwinmm', '-lcomctl32', '-lcomdlg32', '-lws2_32', '-lm')

# Compile raygui
g++ -fpermissive @EDITOR_FLAGS @EDITOR_INC -c Source/raygui/raygui.c -DRAYGUI_IMPLEMENTATION -o raygui.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "raygui compilation failed" }

# Compile editor sources
g++ @EDITOR_FLAGS @EDITOR_INC -c Source/Main.cpp -o Main.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "Editor Main.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c Source/Win32Dialogs.cpp -o Win32Dialogs.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "Editor Win32Dialogs.cpp compilation failed" }

# Compile engine sources needed by editor
g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/oz_pawn_system.cpp -o oz_pawn_system.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "oz_pawn_system.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/oz_assetmapper.cpp -o oz_assetmapper.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "oz_assetmapper.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/oz_ozone_loader.cpp -o oz_ozone_loader.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "oz_ozone_loader.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Server/OzoneParser.cpp -o OzoneParser.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "OzoneParser.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Server/WDLParser.cpp -o WDLParser.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "WDLParser.cpp compilation failed" }

# Compile OTCustom stub
$stub = 'int main_custom() { return 0; }'
$stub | g++ @EDITOR_FLAGS @EDITOR_INC -x c++ -c - -o OTCustom_stub.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "OTCustom stub compilation failed" }

# Link editor
g++ Main.o Win32Dialogs.o oz_pawn_system.o oz_assetmapper.o oz_ozone_loader.o OzoneParser.o WDLParser.o raygui.o OTCustom_stub.o -o oz_editor.exe @EDITOR_FLAGS @EDITOR_INC @EDITOR_LIBS 2>&1
if ($LASTEXITCODE -ne 0) { Fail "oz_editor link failed" }

# Cleanup editor objects
Remove-Item Main.o, Win32Dialogs.o, oz_pawn_system.o, oz_assetmapper.o, oz_ozone_loader.o, OzoneParser.o, WDLParser.o, raygui.o, OTCustom_stub.o -Force -ErrorAction SilentlyContinue
Pop-Location
Write-Step "oz_editor.exe built."

# --- 5. Assemble System/ directory ---
Write-Step "Assembling System/ release..."
if (-not (Test-Path $OUT_DIR)) { New-Item -ItemType Directory -Force -Path $OUT_DIR | Out-Null }
if (-not (Test-Path "$OUT_DIR\Data")) { New-Item -ItemType Directory -Force -Path "$OUT_DIR\Data" | Out-Null }

# Move EXEs
Move-Item -Force "$PSScriptRoot\Angels95.exe"  "$OUT_DIR\Angels95.exe"
Move-Item -Force "$PSScriptRoot\oz_server.exe" "$OUT_DIR\oz_server.exe"
Move-Item -Force "$PSScriptRoot\OzPack.exe"    "$OUT_DIR\OzPack.exe"
Move-Item -Force "$PSScriptRoot\OTEditor\oz_editor.exe" "$OUT_DIR\oz_editor.exe"

# Copy/create INI files
if (-not (Test-Path "$OUT_DIR\Angels95.ini")) {
    @"
[Video]
Width=1280
Height=720
Fullscreen=0
VSync=1

[Audio]
MasterVolume=1.0
MusicVolume=0.7
SFXVolume=1.0

[Game]
ServerIP=127.0.0.1
ServerPort=27015
"@ | Set-Content "$OUT_DIR\Angels95.ini" -Encoding UTF8
}

if (-not (Test-Path "$OUT_DIR\oz_editor.ini")) {
    @"
[Editor]
GridSize=1.0
SnapToGrid=1
ShowGrid=1

[Video]
Width=1600
Height=900
"@ | Set-Content "$OUT_DIR\oz_editor.ini" -Encoding UTF8
}

# Create run scripts
@"
@echo off
cd /d "%~dp0.."
start "" "%~dp0Angels95.exe"
"@ | Set-Content "$OUT_DIR\run.bat" -Encoding ASCII

@"
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
& "$PSScriptRoot\Angels95.exe"
"@ | Set-Content "$OUT_DIR\run.ps1" -Encoding UTF8

# --- 6. Package assets (optional) ---
if (-not $SkipData) {
    Write-Step "Packaging assets..."
    Push-Location $PSScriptRoot
    & .\build-data.ps1 2>&1
    if ($LASTEXITCODE -ne 0) { Write-Host "WARNING: Asset packaging failed" -ForegroundColor Yellow }
    Pop-Location
} else {
    Write-Step "Skipping asset packaging (-SkipData)"
}

# --- 7. Cleanup intermediate objects ---
Remove-Item "$PSScriptRoot\*.o" -Force -ErrorAction SilentlyContinue
Remove-Item "$PSScriptRoot\OTEditor\*.o" -Force -ErrorAction SilentlyContinue

# --- 8. Verify outputs ---
Write-Step "Verifying outputs..."
$missing = @()
foreach ($exe in @("Angels95.exe", "oz_server.exe", "oz_editor.exe", "OzPack.exe")) {
    if (-not (Test-Path "$OUT_DIR\$exe")) { $missing += $exe }
}
if ($missing.Count -gt 0) {
    Fail "Missing EXEs: $($missing -join ', ')"
}

$dataFiles = Get-ChildItem "$OUT_DIR\Data" -Filter "*.oz*" -ErrorAction SilentlyContinue
if ($dataFiles.Count -eq 0 -and -not $SkipData) {
    Write-Host "WARNING: No .oz* packages found in System/Data" -ForegroundColor Yellow
}

Write-Step "Build complete."
Write-Host "System/ release in $OUT_DIR" -ForegroundColor Green
Write-Host "  Angels95.exe  - Game client" -ForegroundColor Green
Write-Host "  oz_server.exe - Dedicated server" -ForegroundColor Green
Write-Host "  oz_editor.exe - Level editor" -ForegroundColor Green
Write-Host "  OzPack.exe    - Asset packer" -ForegroundColor Green
Write-Host "  Data/*.oz*    - Packaged assets ($($dataFiles.Count) files)" -ForegroundColor Green
