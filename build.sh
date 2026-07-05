#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"

BUILDROOT_DIR=""

configure_cmake_build_directory() {
   cmake -B "$BUILD_DIR" \
    -DBUILDROOT_DIR="${BUILDROOT_DIR:-}" \
    "$ROOT_DIR"
}

clean() {
    rm -rf $"BUILD_DIR" 
    
    echo "==> Creating mock build environment before cleaning"
    configure_cmake_build_directory

    echo "==> Cleaning kernel module artifacts"
    cmake --build ${BUILD_DIR} --target kernel_module_clean
    
    echo "==> Cleaning build directory: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
 
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

configure_cmake_build_directory()

echo "==> Building"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "==> Done"
