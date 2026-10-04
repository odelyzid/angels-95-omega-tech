#!/usr/bin/env bash
set -euo pipefail

# Angels95 Linux Build Script
# Installs dependencies, compiles raylib, builds game + server.

RAYLIB_VERSION="5.5"

# --- Build mode (release default). Usage: ./build.sh debug | ./build.sh fast ---
#   debug: -O0 -g (fast compiles + symbols)   fast: release, no-op for clean (this
#   script is already incremental - it never runs `make clean`).
MODE="${MODE:-release}"
case "${1:-}" in
    debug|--debug|-d) MODE=debug ;;
    fast|--fast)      MODE=release ;;
esac
echo "==> Build mode: $MODE"

# --- System dev packages ---
# raylib bundles GLFW, whose CMake hard-errors when these headers are absent
# ("Xinerama headers not found", "Xcursor headers not found", ...). Previously this script
# checked only for g++/cmake/git, so on a fresh box install_raylib_system died inside
# raylib's own configure with a message that names neither this repo nor apt. This is the
# same list CI installs (ci.yml) and that Wiki/Building.md documents.
install_dev_packages() {
    local pkgs=(g++ make cmake git libgl1-mesa-dev
                libx11-dev libxrandr-dev libxcursor-dev libxi-dev
                libxinerama-dev libxext-dev
                libasound2-dev libpulse-dev)
    if command -v apt-get &>/dev/null; then
        echo "==> Installing build dependencies (apt-get)..."
        sudo apt-get update -qq
        # DEBIAN_FRONTEND=noninteractive so this cannot block on a tzdata prompt when run
        # from CI or a container.
        sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${pkgs[@]}"
    elif command -v dnf &>/dev/null; then
        echo "==> Installing build dependencies (dnf)..."
        sudo dnf install -y gcc-c++ make cmake git-devel mesa-libGL-devel \
            libX11-devel libXrandr-devel libXcursor-devel libXi-devel \
            libXinerama-devel libXext-devel alsa-lib-devel pulseaudio-libs-devel
    elif command -v pacman &>/dev/null; then
        echo "==> Installing build dependencies (pacman)..."
        sudo pacman -S --needed --noconfirm gcc make cmake git mesa libx11 libxrandr \
            libxcursor libxi libxinerama libxext alsa-lib pulseaudio
    else
        echo "WARNING: no supported package manager (apt-get/dnf/pacman)."
        echo "         Install these manually or raylib's GLFW configure will fail:"
        echo "         ${pkgs[*]}"
    fi
}

# --- raylib ---
install_raylib_system() {
    echo "==> Installing raylib $RAYLIB_VERSION system-wide..."
    # rm first: a leftover /tmp/raylib from an interrupted run makes `git clone` fail,
    # and `set -e` would abort the whole script with a confusing "destination exists".
    rm -rf /tmp/raylib
    git clone --depth 1 --branch "$RAYLIB_VERSION" https://github.com/raysan5/raylib.git /tmp/raylib
    cmake -S /tmp/raylib -B /tmp/raylib/build \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_GAMES=OFF
    cmake --build /tmp/raylib/build --parallel "$(nproc)"
    sudo cmake --install /tmp/raylib/build
    sudo ldconfig
    rm -rf /tmp/raylib
    echo "==> raylib installed."
}

# --- Prerequisites ---
if ! command -v g++ &>/dev/null; then
    echo "==> g++ not found."
    install_dev_packages
fi

if ! command -v cmake &>/dev/null; then
    echo "==> cmake not found."
    install_dev_packages
fi

if ! command -v git &>/dev/null; then
    echo "ERROR: git not found. Install git."
    exit 1
fi

# --- raylib ---
if ! ldconfig -p 2>/dev/null | grep -q libraylib &&
   [ ! -f /usr/local/lib/libraylib.a ]; then
    echo "==> raylib not found. Installing dependencies, then building it from source..."
    install_dev_packages
    install_raylib_system
else
    echo "==> raylib found."
fi

# --- Asset packer: also needed to produce System/Data/*.oz* ---
echo "==> Building OzPack..."
make -j"$(nproc)" MODE="$MODE" ozpack

# --- Game ---
echo "==> Building Angels95..."
make -j"$(nproc)" MODE="$MODE" OTENGINE

# --- Server ---
echo "==> Building AngelServ..."
make -j"$(nproc)" MODE="$MODE" AngelServ

# --- Master server ---
echo "==> Building AngelMaster..."
make -j"$(nproc)" MODE="$MODE" AngelMaster

echo ""
echo "Done. Run ./Angels95 to launch, ./AngelServ to start the server, or ./AngelMaster to host the master server."
echo "Note: the editor (AngelEd) is Windows-only - its panels are Win32 native dialogs."
