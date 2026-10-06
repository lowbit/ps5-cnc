#!/usr/bin/env bash
# Builds the same Vanilla Conquer (with the port's patches) for Linux in the build image, with the
# system's SDL2 and OpenAL: build/host/vanillara. Handy for comparing behaviour with the console and
# for checking a game data folder before it goes to the PS5 (vanillara reads the folder its
# redalert.ini names, see DEVELOPMENT.md).
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/build/host"

cmake -S "$root/vanilla-conquer" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_VANILLATD=OFF -DBUILD_VANILLARA=ON -DBUILD_TOOLS=OFF -DBUILD_TESTS=OFF -DNETWORKING=OFF
ninja -C "$build" VanillaRA
