#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

SRC_DIR="${SRC_DIR:-$PROJECT_DIR/3rd/fdk-aac}"
BUILD_DIR="${BUILD_DIR:-$SRC_DIR/build}"
INSTALL_DIR="${INSTALL_DIR:-$BUILD_DIR/install}"

HOST="${HOST:-arm-linux}"

# Strip tool for target ARM
STRIP="${STRIP:-arm-linux-strip}"

echo "========================================"
echo "Build FDK-AAC"
echo "PROJECT_DIR = $PROJECT_DIR"
echo "SRC_DIR     = $SRC_DIR"
echo "BUILD_DIR   = $BUILD_DIR"
echo "INSTALL_DIR = $INSTALL_DIR"
echo "HOST        = $HOST"
echo "STRIP       = $STRIP"
echo "========================================"

if [ ! -d "$SRC_DIR" ]; then
    echo "Error: SRC_DIR does not exist: $SRC_DIR"
    exit 1
fi

if [ ! -f "$SRC_DIR/autogen.sh" ]; then
    echo "Error: autogen.sh not found in: $SRC_DIR"
    exit 1
fi

cd "$SRC_DIR"

echo "[1/7] Generate configure script"
./autogen.sh

echo "[2/7] Clean previous in-source configure state"
if [ -f Makefile ]; then
    make distclean || make clean || true
else
    echo "No Makefile, skip clean"
fi

echo "[3/7] Clean build directory"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

cd "$BUILD_DIR"

echo "[4/7] Configure"
../configure \
    --host="$HOST" \
    --prefix="$INSTALL_DIR" \
    --disable-shared \
    --enable-static

echo "[5/7] Build"
make -j"$(nproc)"

echo "[6/7] Install"
make install

echo "[7/7] Strip libraries"

if ! command -v "$STRIP" >/dev/null 2>&1; then
    echo "Warning: strip tool not found: $STRIP"
    echo "Skip strip"
else
    if ls "$INSTALL_DIR/lib/"*.so* >/dev/null 2>&1; then
        echo "Strip shared libraries..."
        "$STRIP" --strip-unneeded "$INSTALL_DIR/lib/"*.so* || true
    fi

    if ls "$INSTALL_DIR/lib/"*.a >/dev/null 2>&1; then
        echo "Strip static libraries..."
        "$STRIP" --strip-debug "$INSTALL_DIR/lib/"*.a || true
    fi
fi

echo "========================================"
echo "FDK-AAC build done"
echo "Install path: $INSTALL_DIR"
echo "Library path: $INSTALL_DIR/lib"
echo "Header path : $INSTALL_DIR/include"
echo "========================================"