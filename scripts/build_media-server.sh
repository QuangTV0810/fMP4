#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

MEDIA_SERVER_DIR="${MEDIA_SERVER_DIR:-$PROJECT_ROOT/3rd/media-server}"
BUILD_DIR="${BUILD_DIR:-$MEDIA_SERVER_DIR/build}"
INSTALL_DIR="${INSTALL_DIR:-$BUILD_DIR/install}"

CROSS_COMPILE="${CROSS_COMPILE:-arm-linux-}"
CC="${CC:-${CROSS_COMPILE}gcc}"
CXX="${CXX:-${CROSS_COMPILE}g++}"
AR="${AR:-${CROSS_COMPILE}ar}"
STRIP="${STRIP:-${CROSS_COMPILE}strip}"
PLATFORM="${PLATFORM:-arm-linux}"
RELEASE="${RELEASE:-1}"

LIBMOV_OUT_DIR="$MEDIA_SERVER_DIR/libmov/release.$PLATFORM"
LIBHLS_OUT_DIR="$MEDIA_SERVER_DIR/libhls/release.$PLATFORM"

echo "========================================"
echo "Build media-server subset"
echo "PROJECT_ROOT      = $PROJECT_ROOT"
echo "MEDIA_SERVER_DIR  = $MEDIA_SERVER_DIR"
echo "BUILD_DIR         = $BUILD_DIR"
echo "INSTALL_DIR       = $INSTALL_DIR"
echo "CROSS_COMPILE     = $CROSS_COMPILE"
echo "CC                = $CC"
echo "CXX               = $CXX"
echo "AR                = $AR"
echo "STRIP             = $STRIP"
echo "PLATFORM          = $PLATFORM"
echo "RELEASE           = $RELEASE"
echo "========================================"

if [ ! -d "$MEDIA_SERVER_DIR" ]; then
    echo "Error: MEDIA_SERVER_DIR does not exist: $MEDIA_SERVER_DIR"
    exit 1
fi

if [ ! -f "$MEDIA_SERVER_DIR/Makefile" ]; then
    echo "Error: Makefile not found in: $MEDIA_SERVER_DIR"
    exit 1
fi

if [ ! -x "$CC" ]; then
    echo "Error: C compiler not found or not executable: $CC"
    exit 1
fi

if [ ! -x "$CXX" ]; then
    echo "Error: C++ compiler not found or not executable: $CXX"
    exit 1
fi

if [ ! -x "$AR" ]; then
    echo "Error: archiver not found or not executable: $AR"
    exit 1
fi

echo "[1/6] Clean previous outputs"
make -C "$MEDIA_SERVER_DIR/libhls" clean || true
make -C "$MEDIA_SERVER_DIR/libmov" clean || true
rm -rf "$BUILD_DIR"
mkdir -p "$INSTALL_DIR/include" "$INSTALL_DIR/lib"

echo "[2/6] Build libmov"
make -C "$MEDIA_SERVER_DIR/libmov" \
    RELEASE="$RELEASE" \
    PLATFORM="$PLATFORM" \
    CC="$CC" \
    CXX="$CXX" \
    AR="$AR"

echo "[3/6] Build libhls"
make -C "$MEDIA_SERVER_DIR/libhls" \
    RELEASE="$RELEASE" \
    PLATFORM="$PLATFORM" \
    CC="$CC" \
    CXX="$CXX" \
    AR="$AR"

echo "[4/6] Install headers"
rm -rf "$INSTALL_DIR/include/libmov" "$INSTALL_DIR/include/libhls"
mkdir -p "$INSTALL_DIR/include/libmov" "$INSTALL_DIR/include/libhls"
cp -a "$MEDIA_SERVER_DIR/libmov/include/." "$INSTALL_DIR/include/libmov/"
cp -a "$MEDIA_SERVER_DIR/libhls/include/." "$INSTALL_DIR/include/libhls/"

echo "[5/6] Install static libraries"
cp -f "$LIBMOV_OUT_DIR/libmov.a" "$INSTALL_DIR/lib/"
cp -f "$LIBHLS_OUT_DIR/libhls.a" "$INSTALL_DIR/lib/"

echo "[6/6] Strip static libraries"
if [ -x "$STRIP" ]; then
    "$STRIP" --strip-debug "$INSTALL_DIR/lib/libmov.a" || true
    "$STRIP" --strip-debug "$INSTALL_DIR/lib/libhls.a" || true
else
    echo "Warning: strip tool not found: $STRIP"
    echo "Skip strip"
fi

echo "========================================"
echo "media-server build done"
echo "libmov  : $INSTALL_DIR/lib/libmov.a"
echo "libhls  : $INSTALL_DIR/lib/libhls.a"
echo "headers : $INSTALL_DIR/include/libmov"
echo "headers : $INSTALL_DIR/include/libhls"
echo "========================================"
