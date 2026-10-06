#!/usr/bin/env bash
# Builds the importer and the launcher for this PC (test/CMakeLists.txt) and runs the importer's
# tests (test/run-tests.py). Needs libcurl, SDL2, genisoimage and 7z; rar for the RAR5 cases.
# With --shots, also saves the launcher's views to build/test/shots (see src/port/launcher.c).
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build="$root/build/test"

cmake -S "$root/test" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug >/dev/null
ninja -C "$build"
python3 "$root/test/run-tests.py"

if [[ ${1:-} == --shots ]]; then
    shots="$build/shots"
    work=$(mktemp -d)
    trap 'rm -rf -- "$work"' EXIT
    rm -rf "$shots"
    mkdir -p "$shots" "$work/game" "$work/incoming" "$work/settings"
    SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software RA_LAUNCHER_SHOTS="$shots" \
        RA_LAUNCHER_KEYS="wait,cross,circle,down,cross,circle,down,cross,circle,down,cross,circle,down" \
        "$build/launcher-test" "$work/game" "$work/incoming" "$work/settings"
    echo "launcher views in $shots"
fi
