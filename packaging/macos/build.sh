#!/usr/bin/env bash
# Builds siggen.app and a disk image of it for the architecture of this Mac. Run it from the repository root, with
# the submodules checked out:
#
#   packaging/macos/build.sh
#
# It needs the Xcode Command Line Tools, CMake, Ninja and pkg-config (brew install cmake ninja pkgconf). Every
# dependency comes from the vendor submodules and is linked statically, so the bundle carries no library of its own.
# The disk image lands in out/macos/.
set -euo pipefail

SOURCE_DIR=$(pwd)
OUT_DIR="$SOURCE_DIR/out/macos"
WORK_DIR="${TMPDIR:-/tmp}/siggen-macos"
ARCH=$(uname -m)
VERSION=$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "$SOURCE_DIR/CMakeLists.txt" | head -n 1)
# The deployment target pinned in CMakeLists.txt, so the bundle declares what the program was built for
MACOSX_DEPLOYMENT_TARGET=$(sed -n 's/^set(CMAKE_OSX_DEPLOYMENT_TARGET \([0-9.]*\) .*/\1/p' "$SOURCE_DIR/CMakeLists.txt")
export MACOSX_DEPLOYMENT_TARGET

echo "== siggen"
cmake -S "$SOURCE_DIR" -B "$WORK_DIR/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$WORK_DIR/build"
rm -rf "$WORK_DIR/stage"
# The install rules lay out the bundle: the program, the icon, the notices and the licenses
cmake --install "$WORK_DIR/build" --prefix "$WORK_DIR/stage"
# The signature is ad hoc, which runs on any Mac but is not notarized; it seals the whole bundle, resources included
codesign --force --sign - "$WORK_DIR/stage/siggen.app"
codesign --verify --deep --strict "$WORK_DIR/stage/siggen.app"

echo "== Disk image"
# The image opens on the bundle next to a link to Applications, to drag it into
ln -s /Applications "$WORK_DIR/stage/Applications"
mkdir -p "$OUT_DIR"
IMAGE="$OUT_DIR/siggen-$VERSION-macos-$ARCH.dmg"
rm -f "$IMAGE"
hdiutil create -volname "siggen $VERSION" -srcfolder "$WORK_DIR/stage" -fs HFS+ -format UDZO "$IMAGE"
ls -l "$OUT_DIR"
