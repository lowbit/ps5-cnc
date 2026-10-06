#!/usr/bin/env bash
# Clones Vanilla Conquer at the pinned commit into vanilla-conquer/ and applies the port's changes
# (patches/vanilla-conquer) as commits on a local ps5 branch. Work on that branch, then run
# tools/export-patches.sh to write the commits back to patches/vanilla-conquer.
set -euo pipefail

commit=ce83b59edd99cccac6cebaae054ca04962c744b7
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
dest="$root/vanilla-conquer"

if [[ -d $dest/.git ]]; then
    echo "$dest exists" >&2
    exit 1
fi
git -c core.autocrlf=false clone -q https://github.com/TheAssemblyArmada/Vanilla-Conquer.git "$dest"
git -C "$dest" config core.autocrlf false
git -C "$dest" config core.eol lf
git -C "$dest" -c advice.detachedHead=false checkout -q "$commit"
git -C "$dest" switch -q -c ps5
name=$(git config user.name || echo build)
email=$(git config user.email || echo build@localhost)
if compgen -G "$root/patches/vanilla-conquer/*.patch" >/dev/null; then
    git -C "$dest" -c user.name="$name" -c user.email="$email" am -q "$root"/patches/vanilla-conquer/*.patch
fi
