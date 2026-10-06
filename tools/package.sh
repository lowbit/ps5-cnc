#!/usr/bin/env bash
# Assembles the title folder dist/PPSA99096 from the PS5 build: the signed eboot.bin, the
# boilerplate's libc.prx, sce_sys, an empty ra/ folder for the game data and the licences. The
# player's Red Alert data is not part of it: the title's launcher gets it.
set -euo pipefail

title=PPSA99096
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
native="$root/.deps/native-app"
tool="$native/build/host/ps5-native-tool"
sources="$root/.deps/src"
app="$root/dist/$title"

rm -rf "$app"
mkdir -p "$app/sce_sys" "$app/sce_module" "$app/licenses" "$app/ra"
"$tool" self --sign --in "$root/build/ps5/vanillara" --out "$app/eboot.bin" --magic 0x1D3D154F >/dev/null
cp "$native/runtime/libc.prx" "$app/sce_module/libc.prx"
cp "$root/sce_sys/param.json" "$root/sce_sys/icon0.png" "$app/sce_sys/"
cp "$root/data/ra-readme.txt" "$app/ra/README.txt"

cp "$root/THIRD_PARTY_NOTICES.md" "$app/licenses/"
cp "$root/LICENSE" "$app/licenses/PS5-Native-RA-GPL-3.0.txt"
# GPL 3 with EA's additional terms (section 7).
cp "$root/vanilla-conquer/License.txt" "$app/licenses/VanillaConquer-GPL-3.0-EA-terms.txt"
cp "$native/LICENSE" "$app/licenses/libc-prx-GPL-3.0.txt"
cp "$sources/SDL-ee4c47dc/LICENSE.txt" "$app/licenses/SDL2.txt"
cp "$sources/openal-soft-1.24.3/COPYING" "$app/licenses/OpenAL-Soft-LGPL-2.txt"
cp "$sources/openal-soft-1.24.3/BSD-3Clause" "$app/licenses/OpenAL-Soft-BSD-3Clause.txt"
cp "$sources/openal-soft-1.24.3/LICENSE-pffft" "$app/licenses/OpenAL-Soft-pffft.txt"
cp "$sources/openal-soft-1.24.3/fmt-11.1.1/LICENSE" "$app/licenses/OpenAL-Soft-fmt.txt"
# What the importer and the launcher compile in.
cp "$sources/libarchive-3.8.9/COPYING" "$app/licenses/libarchive.txt"
cp "$sources/xz-5.8.4/COPYING.0BSD" "$app/licenses/xz-liblzma-0BSD.txt"
cp "$sources/zlib-1.3.2/LICENSE" "$app/licenses/zlib.txt"
cp "$sources/unshield-1.6.2/LICENSE" "$app/licenses/unshield-MIT.txt"
sed '/^#include/,$d' "$sources/unshield-1.6.2/lib/md5/md5c.c" >"$app/licenses/unshield-md5-RSA.txt"
sed '/^#include/,$d' "$sources/QR-Code-generator-1.8.0/c/qrcodegen.c" >"$app/licenses/qrcodegen-MIT.txt"
if [[ -f /usr/share/doc/fonts-dejavu-core/copyright ]]; then
    cp /usr/share/doc/fonts-dejavu-core/copyright "$app/licenses/DejaVu-fonts.txt"
fi
"$tool" self --inspect --file "$app/eboot.bin" | grep -E "integrity|sha256"
du -sh "$app"
