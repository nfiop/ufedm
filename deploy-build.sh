#!/usr/bin/env bash

set -euo pipefail

# Usage: ./copy_build.sh <destination_dir>
DEST="${1:-}"

if [[ -z "$DEST" ]]; then
  echo "Usage: $0 <destination_dir>"
  exit 1
fi

# Create destination directories
mkdir -p "$DEST/kmod"
mkdir -p "$DEST/utests"

# Copy kernel module
if [[ -f build/kmod/ufedm.ko ]]; then
  cp -v build/kmod/ufedm.ko "$DEST/kmod/"
else
  echo "Warning: build/kmod/ufedm.ko not found"
fi

# Copy user tests
for f in test_proxy_ioctls test_proxy_mmap; do
  if [[ -f "build/user/utests/$f" ]]; then
    cp -v "build/user/utests/$f" "$DEST/utests/"
  else
    echo "Warning: build/user/utests/$f not found"
  fi
done

echo "Done copying build artifacts to $DEST"
