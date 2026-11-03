#!/bin/bash
# Build MangoOS inside the MSYS2 UCRT64 environment.
# The i686-elf cross toolchain (opt/cross) is a UCRT64 build: its cc1
# needs the UCRT64 runtime DLLs first on PATH (libwinpthread-1.dll etc.).
#
# Usage:  bash tools/build.sh [make targets...]   (default: all)
#   - parallel: make -jN is applied when N is a number given as "-j4"
#   - interrupt-safe: each object file has its own rule, so re-running
#     the same command resumes where the previous run stopped

set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

export MSYSTEM=UCRT64
export PATH=/ucrt64/bin:/usr/bin:/opt/cross/bin:$PATH

cd "$ROOT"
echo "=== Build environment ==="
echo "ROOT: $ROOT"
echo "PATH: $(echo "$PATH" | tr ':' '\n' | head -3 | tr '\n' ' ')"
which i686-elf-gcc i686-elf-ld i686-elf-objcopy nasm >/dev/null

echo "=== make $* ==="
make "$@"