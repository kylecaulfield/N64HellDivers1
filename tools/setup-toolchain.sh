#!/usr/bin/env bash
# One-time setup: build the N64 MIPS toolchain (GCC, binutils, newlib) from
# source using the build script shipped with the *pinned* libdragon submodule,
# so the compiler version always matches the libdragon commit we vendor.
#
# Usage: tools/setup-toolchain.sh
#   N64_INST   install prefix   (default: <repo>/.toolchain/inst)
#   JOBS       parallel jobs    (default: all cores)
#
# Takes 30-60 minutes. Linux/WSL need: build-essential texinfo bison flex
# zlib1g-dev wget. macOS needs Homebrew (the libdragon script installs the rest).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export N64_INST="${N64_INST:-$REPO/.toolchain/inst}"
BUILD_PATH="${BUILD_PATH:-$REPO/.toolchain/build}"

if [ ! -f "$REPO/vendor/libdragon/tools/build-toolchain.sh" ]; then
    echo "vendor/libdragon is missing. Run: git submodule update --init --recursive" >&2
    exit 1
fi

if [ -x "$N64_INST/bin/mips64-elf-gcc" ]; then
    echo "Toolchain already present at $N64_INST ($("$N64_INST/bin/mips64-elf-gcc" -dumpversion))."
    echo "Delete that directory to rebuild."
    exit 0
fi

# build-toolchain.sh tees its log into BUILD_PATH before creating it.
mkdir -p "$BUILD_PATH" "$N64_INST"
export BUILD_PATH
"$REPO/vendor/libdragon/tools/build-toolchain.sh"

# The build tree is ~5 GB and no longer needed.
rm -rf "$BUILD_PATH"
echo "Toolchain installed to $N64_INST"
