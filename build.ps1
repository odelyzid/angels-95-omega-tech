# Angels95 Windows Build Script (PowerShell)
# Requires: MinGW-w64 (g++), CMake, Git
# Run from the repo root.

$RAYLIB_VERSION = "5.5"
$RAYLIB_DIR = "$env:USERPROFILE\raylib-$RAYLIB_VERSION"

function Write-Step { Write-Host "==> $args" -ForegroundColor Cyan }
function Write-Error { Write-Host "ERROR: $args" -ForegroundColor Red }

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

# --- Build ---
Write-Step "Building Angels95..."
$env:Path += ";$RAYLIB_DIR\lib"
& mingw32-make -j $env:NUMBER_OF_PROCESSORS OTENGINE
if (-not $?) { Write-Error "Game build failed"; exit 1 }

Write-Step "Building oz_server..."
& mingw32-make -j $env:NUMBER_OF_PROCESSORS oz_server
if (-not $?) { Write-Error "Server build failed"; exit 1 }

Write-Step "Done."
Write-Host "Run .\Angels95.exe to launch, or .\oz_server.exe to start the server." -ForegroundColor Green
