#!/bin/sh
# Builds Area Play (Vita Edition) -> build/AreaPlay.vpk  (Linux / macOS / WSL)
# Needs VitaSDK (https://vitasdk.org) with $VITASDK set, CMake and Ninja (or make).
set -e
cd "$(dirname "$0")"
[ -n "$VITASDK" ] || { echo "ERROR: VITASDK is not set"; exit 1; }
export PATH="$VITASDK/bin:$PATH"
[ "$1" = clean ] && rm -rf build
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake"
rm -f build/AreaPlay.vpk build/AreaPlay.vpk.out
cmake --build build
echo "Done: build/AreaPlay.vpk"
