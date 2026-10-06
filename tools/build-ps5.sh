#!/usr/bin/env bash
# Configures and builds Vanilla Conquer's Red Alert for the PS5 (SDL2 video and input, OpenAL sound,
# no network play yet), against the libraries from tools/build-deps.sh. The result,
# build/ps5/vanillara, is the unsigned PS5 module; build/ps5/vanillara.pie keeps the symbols.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/build/ps5"

cmake -S "$root/vanilla-conquer" -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$root/cmake/ps5.cmake" \
    -DCMAKE_PROJECT_INCLUDE="$root/cmake/ps5-vanillara.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_VANILLATD=OFF -DBUILD_VANILLARA=ON -DBUILD_REMASTERTD=OFF -DBUILD_REMASTERRA=OFF \
    -DBUILD_TOOLS=OFF -DBUILD_TESTS=OFF -DNETWORKING=OFF -DSDL2=ON -DOPENAL=ON
ninja -C "$build" VanillaRA
