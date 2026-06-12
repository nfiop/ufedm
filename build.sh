#!/usr/bin/env bash
set -e

# -----------------------------
# Project root
# -----------------------------
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# -----------------------------
# Buildroot path (edit default or override via env)
# -----------------------------
BUILDROOT_DIR="${BUILDROOT_DIR:-}"

if [ -z "$BUILDROOT_DIR" ]; then
    echo "ERROR: BUILDROOT_DIR not set"
    echo "Usage: BUILDROOT_DIR=/path/to/buildroot ./build.sh"
    exit 1
fi

if [ -z "$KERNEL_ARCH" ]; then
    echo "ERROR: KERNEL_ARCH not set"
    echo "Set KERNEL_ARCH to arm/amd64/riscv ..."
    exit 1
fi

TOOLCHAIN_FILE="$BUILDROOT_DIR/output/host/share/buildroot/toolchainfile.cmake"

if [ ! -f "$TOOLCHAIN_FILE" ]; then
    echo "ERROR: toolchain file not found:"
    echo "  $TOOLCHAIN_FILE"
    exit 1
fi

# -----------------------------
# Build directory
# -----------------------------
BUILD_DIR="$ROOT_DIR/build"

# Optional: clean build
if [ "$1" == "clean" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    shift
fi

# -----------------------------
# Configure (only once or if missing)
# -----------------------------
if [ ! -f "$BUILD_DIR/Makefile" ]; then
    echo "Configuring CMake..."

    cmake -B "$BUILD_DIR" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DBUILDROOT_DIR="$BUILDROOT_DIR" -DKERNEL_ARCH="$KERNEL_ARCH" \
        "$ROOT_DIR"
fi

# -----------------------------
# Build
# -----------------------------
cmake --build "$BUILD_DIR" -j"$(nproc)"

# pushd build

# make clean
# make

# popd


echo "Build complete"
