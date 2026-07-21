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
    Push-Location "$PSScriptRoot\AngelEd"
    & mingw32-make clean 2>&1 | Out-Null
    Pop-Location
}

# --- 1. Build OTENGINE (game client) ---
Write-Step "Building Angels95 (OTENGINE)..."
Push-Location $PSScriptRoot
& mingw32-make -j $env:NUMBER_OF_PROCESSORS OTENGINE 2>&1
if ($LASTEXITCODE -ne 0) { Fail "OTENGINE build failed" }
Write-Step "Angels95.exe built."

# --- 2. Build AngelServ (dedicated server) ---
Write-Step "Building AngelServ..."
& mingw32-make AngelServ 2>&1
if ($LASTEXITCODE -ne 0) { Fail "AngelServ build failed" }
Write-Step "AngelServ.exe built."

# --- 3. Build OzPack (asset packer) ---
Write-Step "Building OzPack..."
& mingw32-make ozpack 2>&1
if ($LASTEXITCODE -ne 0) { Fail "OzPack build failed" }
Write-Step "OzPack.exe built."
Pop-Location

# --- 4. Build AngelEd (level editor) ---
Write-Step "Building AngelEd..."
Push-Location "$PSScriptRoot\AngelEd"
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

# Compile LightningScript system
g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Script/LightningScriptContext.cpp -o LightningScriptContext.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "LightningScriptContext.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Script/LightningScriptParser.cpp -o LightningScriptParser.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "LightningScriptParser.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Script/LightningEntityRegistry.cpp -o LightningEntityRegistry.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "LightningEntityRegistry.cpp compilation failed" }

g++ @EDITOR_FLAGS @EDITOR_INC -c ../Source/Script/LightningEntityManager.cpp -o LightningEntityManager.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "LightningEntityManager.cpp compilation failed" }

# Compile OTCustom stub
$stub = 'int main_custom() { return 0; }'
$stub | g++ @EDITOR_FLAGS @EDITOR_INC -x c++ -c - -o OTCustom_stub.o 2>&1
if ($LASTEXITCODE -ne 0) { Fail "OTCustom stub compilation failed" }

# Link editor
g++ Main.o Win32Dialogs.o OzPawnSystem.o OzAssetMapper.o OzOzoneLoader.o OzoneParser.o WDLParser.o Log.o LightningScriptContext.o LightningScriptParser.o LightningEntityRegistry.o LightningEntityManager.o raygui.o OTCustom_stub.o -o AngelEd.exe @EDITOR_FLAGS @EDITOR_INC @EDITOR_LIBS 2>&1
if ($LASTEXITCODE -ne 0) { Fail "AngelEd link failed" }

# Cleanup editor objects
Remove-Item Main.o, Win32Dialogs.o, OzPawnSystem.o, OzAssetMapper.o, OzOzoneLoader.o, OzoneParser.o, WDLParser.o, Log.o, LightningScriptContext.o, LightningScriptParser.o, LightningEntityRegistry.o, LightningEntityManager.o, raygui.o, OTCustom_stub.o -Force -ErrorAction SilentlyContinue
Pop-Location
Write-Step "AngelEd.exe built."

# --- 5. Assemble System/ directory ---
Write-Step "Assembling System/ release..."
# Ensure clean system directory structure
if (Test-Path $OUT_DIR) { Remove-Item -Recurse -Force "$OUT_DIR\*" -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force -Path $OUT_DIR | Out-Null
New-Item -ItemType Directory -Force -Path "$OUT_DIR\Data" | Out-Null
New-Item -ItemType Directory -Force -Path "$OUT_DIR\Cache" | Out-Null

# Move EXEs
if (Test-Path "$PSScriptRoot\Angels95.exe")  { Move-Item -Force "$PSScriptRoot\Angels95.exe"  "$OUT_DIR\Angels95.exe" }
if (Test-Path "$PSScriptRoot\AngelServ.exe") { Move-Item -Force "$PSScriptRoot\AngelServ.exe" "$OUT_DIR\AngelServ.exe" }
if (Test-Path "$PSScriptRoot\OzPack.exe")    { Move-Item -Force "$PSScriptRoot\OzPack.exe"    "$OUT_DIR\OzPack.exe" }
if (Test-Path "$PSScriptRoot\AngelEd\AngelEd.exe") { Move-Item -Force "$PSScriptRoot\AngelEd\AngelEd.exe" "$OUT_DIR\AngelEd.exe" }

# Copy raylib DLL if present
$raylibDll = "$W64DEVKIT\bin\libraylib.dll"
if (Test-Path $raylibDll) {
    Copy-Item -Force $raylibDll "$OUT_DIR\libraylib.dll"
}

# Always recreate INI files
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

@"
[Editor]
GridSize=1.0
SnapToGrid=1
ShowGrid=1

[Video]
Width=1600
Height=900
"@ | Set-Content "$OUT_DIR\AngelEd.ini" -Encoding UTF8

 # Create OzServer.ini
@"
[Server]
Port=27015
HttpPort=8080
MaxPlayers=16
WorldDir=GameData

[Game]
ServerName=Angels95 Server
"@ | Set-Content "$OUT_DIR\OzServer.ini" -Encoding UTF8

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

# After packaging, copy any loose assets needed at runtime
if (Test-Path "$PSScriptRoot\GameData\Shaders") {
    $shaderDest = "$OUT_DIR\Shaders"
    New-Item -ItemType Directory -Force -Path $shaderDest | Out-Null
    Copy-Item -Recurse -Force "$PSScriptRoot\GameData\Shaders\*" $shaderDest -ErrorAction SilentlyContinue
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
Write-Host "  libraylib.dll - Raylib runtime (if available)" -ForegroundColor Green
Write-Host "  Data/*.oz*    - Packaged assets ($($dataFiles.Count) files)" -ForegroundColor Green
Write-Host "  Cache/        - Temporary runtime cache" -ForegroundColor Green
