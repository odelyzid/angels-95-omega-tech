# Angels95 Windows Build Script (PowerShell)
# For MSYS2/MINGW64 or similar MinGW-w64 environments.
# For native w64devkit builds, use build-native-win.ps1 instead.
# Requires: MinGW-w64 (g++), CMake, Git
# Run from the repo root.

param(
    [switch]$SkipData,
    [switch]$SkipClean
)

$RAYLIB_VERSION = "5.5"
$RAYLIB_DIR = "$env:USERPROFILE\raylib-$RAYLIB_VERSION"
$OUT_DIR       = "$PSScriptRoot\System"

function Write-Step { Write-Host "==> $args" -ForegroundColor Cyan }
function Fail { Write-Host "ERROR: $args" -ForegroundColor Red; exit 1 }

# --- Prerequisites ---
Write-Step "Checking prerequisites..."

$haveGpp = Get-Command g++ -ErrorAction SilentlyContinue
$haveCmake = Get-Command cmake -ErrorAction SilentlyContinue
$haveGit = Get-Command git -ErrorAction SilentlyContinue

if (-not $haveGpp) {
    Fail "g++ not found in PATH. Install MinGW-w64 (https://winlibs.com) and add to PATH."
}
if (-not $haveCmake) {
    Fail "cmake not found. Install from https://cmake.org/download/"
}
if (-not $haveGit) {
    Fail "git not found. Install from https://git-scm.com/downloads"
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
    if ($LASTEXITCODE -ne 0) { Fail "CMake configuration failed" }
    cmake --build . --parallel
    if ($LASTEXITCODE -ne 0) { Fail "Build failed" }
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
if (-not $SkipClean) {
    Write-Step "Cleaning previous build..."
    Push-Location $PSScriptRoot
    & mingw32-make clean 2>&1 | Out-Null
    Pop-Location
}

# --- 1. Build OTENGINE (game client) ---
Write-Step "Building Angels95 (OTENGINE)..."
Push-Location $PSScriptRoot
$env:Path += ";$RAYLIB_DIR\lib"
& mingw32-make -j $env:NUMBER_OF_PROCESSORS OTENGINE
if ($LASTEXITCODE -ne 0) { Fail "Game build failed" }

# --- 2. Build AngelServ (dedicated server) ---
Write-Step "Building AngelServ..."
& mingw32-make -j $env:NUMBER_OF_PROCESSORS AngelServ
if ($LASTEXITCODE -ne 0) { Fail "Server build failed" }

# --- 3. Build OzPack (asset packer) ---
Write-Step "Building OzPack..."
& mingw32-make ozpack
if ($LASTEXITCODE -ne 0) { Fail "OzPack build failed" }
Pop-Location

# --- 4. Build AngelEd (level editor) ---
Write-Step "Building AngelEd..."
Push-Location "$PSScriptRoot\AngelEd"
$EDITOR_FLAGS = @('-O3', '-g', '--std=c++20', '-Wno-narrowing')
$EDITOR_INC   = @('-I', '../Source', '-I', 'Source')
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
g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Pawn/OzPawnSystem.cpp -o OzPawnSystem.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "OzPawnSystem.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Package/OzAssetMapper.cpp -o OzAssetMapper.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "OzAssetMapper.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/OzOzoneLoader.cpp -o OzOzoneLoader.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "OzOzoneLoader.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Server/OzoneParser.cpp -o OzoneParser.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "OzoneParser.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Server/WDLParser.cpp -o WDLParser.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "WDLParser.cpp compilation failed" }

# Compile Log system (used by engine modules)
g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Log.cpp -o Log.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "Log.cpp compilation failed" }

# Compile Physics system (CSG + Chunking)
g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Physics/OzBsp.cpp -o OzBsp.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "OzBsp.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Physics/WorldChunk.cpp -o WorldChunk.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "WorldChunk.cpp compilation failed" }

# Compile LightningScript system
g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Script/LightningScriptContext.cpp -o LightningScriptContext.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "LightningScriptContext.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Script/LightningScriptParser.cpp -o LightningScriptParser.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "LightningScriptParser.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Script/LightningEntityRegistry.cpp -o LightningEntityRegistry.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "LightningEntityRegistry.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Script/LightningEntityManager.cpp -o LightningEntityManager.o 2>&1g++ @EDITOR_FLAGS @EDITOR_INC -c Source/EditorIcons.cpp -o EditorIcons.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail ""EditorIcons.cpp compilation failed"" }
if ($LASTEXITCODE -ne 0) { Fail "LightningEntityManager.cpp compilation failed" }

# Compile OTCustom stub
$stub = 'int main_custom() { return 0; }'
$stub | g++ @EDITOR_FLAGS @EDITOR_INC -x c++ -c - -o OTCustom_stub.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "OTCustom stub compilation failed" }

# Link editor
g++ Main.o Win32Dialogs.o OzPawnSystem.o OzAssetMapper.o OzOzoneLoader.o OzoneParser.o WDLParser.o Log.o OzBsp.o WorldChunk.o LightningScriptContext.o LightningScriptParser.o LightningEntityRegistry.o LightningEntityManager.o EditorIcons.o raygui.o OTCustom_stub.o -o AngelEd.exe @EDITOR_FLAGS @EDITOR_INC @EDITOR_LIBS 2>&1
if ($LASTEXITCODE -ne 0) { Fail "AngelEd link failed" }

# Cleanup editor objects
Remove-Item Main.o, Win32Dialogs.o, OzPawnSystem.o, OzAssetMapper.o, OzOzoneLoader.o, OzoneParser.o, WDLParser.o, Log.o, OzBsp.o, WorldChunk.o, LightningScriptContext.o, LightningScriptParser.o, LightningEntityRegistry.o, LightningEntityManager.o, EditorIcons.o, raygui.o, OTCustom_stub.o -Force -ErrorAction SilentlyContinue
Pop-Location
Write-Step "AngelEd.exe built."

# --- 5. Assemble System/ directory ---
Write-Step "Assembling System/ release..."
if (-not (Test-Path $OUT_DIR)) { New-Item -ItemType Directory -Force -Path $OUT_DIR | Out-Null }
if (-not (Test-Path "$OUT_DIR\Data")) { New-Item -ItemType Directory -Force -Path "$OUT_DIR\Data" | Out-Null }

# Move EXEs
Move-Item -Force "$PSScriptRoot\Angels95.exe"  "$OUT_DIR\Angels95.exe"
Move-Item -Force "$PSScriptRoot\AngelServ.exe" "$OUT_DIR\AngelServ.exe"
Move-Item -Force "$PSScriptRoot\OzPack.exe"    "$OUT_DIR\OzPack.exe"
Move-Item -Force "$PSScriptRoot\AngelEd\AngelEd.exe" "$OUT_DIR\AngelEd.exe"

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

if (-not (Test-Path "$OUT_DIR\AngelEd.ini")) {
    @"
[Editor]
GridSize=1.0
SnapToGrid=1
ShowGrid=1

[Video]
Width=1600
Height=900
"@ | Set-Content "$OUT_DIR\AngelEd.ini" -Encoding UTF8
}

 # Create OzServer.ini
if (-not (Test-Path "$OUT_DIR\OzServer.ini")) {
    @"
[Server]
Port=27015
HttpPort=8080
MaxPlayers=16
WorldDir=GameData

[Game]
ServerName=Angels95 Server
"@ | Set-Content "$OUT_DIR\OzServer.ini" -Encoding UTF8
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
    if (Test-Path ".\build-data.ps1") {
        & .\build-data.ps1 2>&1
        if ($LASTEXITCODE -ne 0) { Write-Host "WARNING: Asset packaging failed" -ForegroundColor Yellow }
    } else {
        Write-Host "WARNING: build-data.ps1 not found, skipping asset packaging" -ForegroundColor Yellow
    }
    Pop-Location
} else {
    Write-Step "Skipping asset packaging (-SkipData)"
}

# --- 7. Verify outputs ---
Write-Step "Verifying outputs..."
$missing = @()
foreach ($exe in @("Angels95.exe", "AngelServ.exe", "AngelEd.exe", "OzPack.exe")) {
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
Write-Host "  AngelServ.exe - Dedicated server" -ForegroundColor Green
Write-Host "  AngelEd.exe - Level editor" -ForegroundColor Green
Write-Host "  OzPack.exe    - Asset packer" -ForegroundColor Green
Write-Host "  Data/*.oz*    - Packaged assets ($($dataFiles.Count) files)" -ForegroundColor Green
