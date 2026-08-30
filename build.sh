#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BUILDROOT_DIR="${BUILDROOT_DIR:-}"

clean() {
    rm -rf "$ROOT_DIR/build/"
 
    echo "==> Clean complete"
}

usage() {
    cat <<EOF
Usage:

  Native build (host kernel + host compiler):
    ./build.sh

  Buildroot build (cross-compile kernel module):
    ./build.sh --buildroot /path/to/buildroot

Optional commands:

  clean   Remove build directory
    ./build.sh clean

Examples:

  # Native build
  ./build.sh

  # Buildroot ARM build
  ./build.sh --buildroot /opt/arm-buildroot

  # Clean build
  ./build.sh clean

EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --buildroot)
            BUILDROOT_DIR="$2"
            shift 2
            ;;
        clean)
            clean
            exit 0
            ;;
        -h)
            usage
            exit 0
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1"
            exit 1
            ;;
    esac
done

if [[ "${1:-}" == "clean" ]]; then
    rm -rf "$BUILD_DIR"
    shift
fi

echo "==> Configuring"

if [[ -z "$BUILDROOT_DIR" ]]; then
    BUILD_DIR="$ROOT_DIR/build/$(uname -m)-linux-$(uname -r)"
else
    LINUX_VER=$(ls "$BUILDROOT_DIR/output/build/" | grep '^linux')
    if [[ $? -ne 0 || -z "$LINUX_VER" ]]; then
        echo "Could not find Linux version for buildroot environment"
        exit 1
    fi

    BUILDROOT_ARCH=$(grep '^BR2_ARCH=' "$BUILDROOT_DIR/.config" | sed 's/BR2_ARCH="\(.*\)"/\1/')
    if [[ $? -ne 0 || -z "$BUILDROOT_ARCH" ]]; then
        echo "Could not determine Buildroot architecture"
        exit 1
    fi
    BUILD_DIR="$ROOT_DIR/build/$BUILDROOT_ARCH-$LINUX_VER"
fi

# Configure CMake build directory
cmake -B "$BUILD_DIR" \
    -DBUILDROOT_DIR="${BUILDROOT_DIR:-}" \
    "$ROOT_DIR"

echo "==> Building"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "==> Done"
