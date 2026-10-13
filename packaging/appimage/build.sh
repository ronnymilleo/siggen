#!/usr/bin/env bash
# Builds siggen as an AppImage inside an Ubuntu 24.04 container, the oldest base it supports, so the image runs on
# distributions with glibc 2.39 or newer. Run it from the repository root, with the submodules checked out:
#
#   docker run --rm -v "$PWD":/src -w /src ubuntu:24.04 packaging/appimage/build.sh
#
# The release workflow runs the same script. The AppImage lands in out/appimage/.
set -euo pipefail

SOURCE_DIR=$(pwd)
WORK_DIR=/tmp/siggen-appimage
LINUXDEPLOY_URL=https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
mkdir -p "$WORK_DIR"

echo "== System packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update
# The SDL3 build dependencies are those of the Ubuntu sdl3 package, since SDL fails to configure when an X11
# extension it enables is missing; SDL3 is compiled from the vendor submodule and opens the Vulkan loader at run
# time, so neither needs anything else
apt-get install -y --no-install-recommends \
    ca-certificates wget file git ninja-build cmake pkg-config g++-14 gcc-14 make \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \
    libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl-dev libdbus-1-dev libudev-dev
# GCC 14, the newest Ubuntu 24.04 offers; CMake looks for the plain names, so they point to it
ln -sf /usr/bin/gcc-14 /usr/local/bin/gcc
ln -sf /usr/bin/g++-14 /usr/local/bin/g++
ln -sf /usr/bin/gcc-14 /usr/local/bin/cc
ln -sf /usr/bin/g++-14 /usr/local/bin/c++
export CC=gcc CXX=g++

echo "== siggen"
# The C++ runtime is linked statically, so the image does not depend on the libstdc++ of the host
cmake -S "$SOURCE_DIR" -B "$WORK_DIR/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build "$WORK_DIR/build"
rm -rf "$WORK_DIR/AppDir"
# The install rules lay out the image: the programs, the desktop entry, the icon, the notices and the licenses
DESTDIR="$WORK_DIR/AppDir" cmake --install "$WORK_DIR/build"

echo "== AppImage"
wget -q "$LINUXDEPLOY_URL" -O "$WORK_DIR/linuxdeploy"
chmod +x "$WORK_DIR/linuxdeploy"
VERSION=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$SOURCE_DIR/CMakeLists.txt" | head -n 1)
mkdir -p "$SOURCE_DIR/out/appimage"
cd "$SOURCE_DIR/out/appimage"
# Containers have no FUSE, so linuxdeploy runs extracted. SDL3 is linked statically and the Vulkan loader stays on
# the host, next to its drivers
APPIMAGE_EXTRACT_AND_RUN=1 LINUXDEPLOY_OUTPUT_VERSION="$VERSION" "$WORK_DIR/linuxdeploy" \
    --appdir "$WORK_DIR/AppDir" \
    --executable "$WORK_DIR/AppDir/usr/bin/siggen" \
    --desktop-file "$WORK_DIR/AppDir/usr/share/applications/siggen.desktop" \
    --icon-file "$WORK_DIR/AppDir/usr/share/icons/hicolor/256x256/apps/siggen.png" \
    --exclude-library 'libvulkan.so*' \
    --output appimage
ls -l "$SOURCE_DIR/out/appimage"
