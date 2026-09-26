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

  Buildroot build (cross-compile with buildroot environment):
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

# --------------------------------------------------
# Native vs Buildroot mode
# --------------------------------------------------

if [[ -z "$BUILDROOT_DIR" ]]; then
    NATIVE_MODE=true
    BUILD_DIR="$ROOT_DIR/build/$(uname -m)-linux-$(uname -r)"
else
    NATIVE_MODE=false
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

echo "Native mode: ${NATIVE_MODE}"

# --------------------------------------------------
# Toolchain selection
# --------------------------------------------------

if $NATIVE_MODE; then

    echo "Using native toolchain (gcc/g++)"

    CC="${CC:-gcc}"
    CXX="${CXX:-g++}"

    # Equivalent to:
    # execute_process(COMMAND uname -m ...)
    TOOLCHAIN_PREFIX="$(uname -m)"

else

    echo "Using Buildroot toolchain"

    TOOLCHAIN_FILE="${BUILDROOT_DIR}/output/host/share/buildroot/toolchainfile.cmake"

    if [[ ! -f "$TOOLCHAIN_FILE" ]]; then
        echo "ERROR: Buildroot toolchain file not found:"
        echo "       $TOOLCHAIN_FILE"
        exit 1
    fi

    # Find *-gcc in Buildroot's toolchain directory, including symlinks
    GCC="$(find "${BUILDROOT_DIR}/output/host/usr/bin" \
        -maxdepth 1 \
        -type f -o -type l \
        -name '*-gcc' \
        -print -quit)"

    if [[ -z "$GCC" ]]; then
        echo "ERROR: Could not find Buildroot gcc compiler"
        exit 1
    fi

    GCC_NAME="$(basename "$GCC")"

    # Equivalent to removing -gcc
    TOOLCHAIN_PREFIX="${GCC_NAME%-gcc}"

    echo "Toolchain file: ${TOOLCHAIN_FILE}"
    echo "Toolchain prefix: ${TOOLCHAIN_PREFIX}"

    # The Buildroot toolchain file normally handles CC/CXX,
    # so we don't need to explicitly set them here.
fi

# --------------------------------------------------
# Configure CMake
# --------------------------------------------------

CMAKE_ARGS=(
    -S .
    -B "$BUILD_DIR"
)

if $NATIVE_MODE; then
    CMAKE_ARGS+=(
        "-DCMAKE_C_COMPILER=${CC}"
        "-DCMAKE_CXX_COMPILER=${CXX}"
    )
else
    CMAKE_ARGS+=(
        "-DBUILDROOT_DIR=${BUILDROOT_DIR}"
        "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"
    )
fi

CMAKE_ARGS+=(
    "-DTOOLCHAIN_PREFIX=${TOOLCHAIN_PREFIX}"
)

cmake "${CMAKE_ARGS[@]}"

# --------------------------------------------------
# Build
# --------------------------------------------------

cmake --build "$BUILD_DIR"
