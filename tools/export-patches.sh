#!/usr/bin/env bash
# Writes the commits on vanilla-conquer/'s ps5 branch since the pinned commit to
# patches/vanilla-conquer.
set -euo pipefail

commit=ce83b59edd99cccac6cebaae054ca04962c744b7
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

rm -f "$root"/patches/vanilla-conquer/*.patch
git -C "$root/vanilla-conquer" format-patch -q --no-signature --zero-commit -N "$commit..ps5" \
    -o "$root/patches/vanilla-conquer"
ls "$root/patches/vanilla-conquer"
