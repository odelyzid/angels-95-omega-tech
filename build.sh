#!/usr/bin/env bash
set -euo pipefail

# Angels95 Linux Build Script
# Installs dependencies, compiles raylib, builds game + server.

RAYLIB_VERSION="5.5"

# --- Detect package manager ---
install_raylib_system() {
    echo "==> Installing raylib system-wide..."
    git clone --depth 1 --branch "$RAYLIB_VERSION" https://github.com/raysan5/raylib.git /tmp/raylib
    cd /tmp/raylib
    mkdir -p build && cd build
    cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF ..
    cmake --build . --parallel "$(nproc)"
    sudo cmake --install .
    sudo ldconfig
    cd /tmp && rm -rf raylib
    echo "==> raylib installed."
}

# --- Prerequisites ---
if ! command -v g++ &>/dev/null; then
    echo "ERROR: g++ not found. Install build-essential / base-devel."
    exit 1
fi

if ! command -v cmake &>/dev/null; then
    echo "ERROR: cmake not found. Install cmake."
    exit 1
fi

if ! command -v git &>/dev/null; then
    echo "ERROR: git not found. Install git."
    exit 1
fi

# --- raylib ---
if ! ldconfig -p 2>/dev/null | grep -q libraylib &&
   [ ! -f /usr/local/lib/libraylib.a ]; then
    echo "==> raylib not found. Building from source..."
    install_raylib_system
else
    echo "==> raylib found."
fi

# --- Game ---
echo "==> Building Angels95..."
make -j"$(nproc)" OTENGINE

# --- Server ---
echo "==> Building oz_server..."
make -j"$(nproc)" oz_server

echo ""
echo "Done. Run ./Angels95 to launch, or ./oz_server to start the server."
