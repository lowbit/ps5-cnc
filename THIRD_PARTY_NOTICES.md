# Third-party notices

Everything in this project that came from someone else, what it is used for, and its license. The
release package carries this file and the license texts in `PPSA99096/licenses/`.

Red Alert's game data is not part of this project or its package: the title's launcher downloads
the 2008 freeware release from its source when the player asks for it, or imports the files of the
player's own copy.

## Shipped in the release

| Component | Used as | License |
| --- | --- | --- |
| [Vanilla Conquer](https://github.com/TheAssemblyArmada/Vanilla-Conquer) at `ce83b59`, The Assembly Armada, from the source Electronic Arts released | The game (VanillaRA), with the changes in `patches/vanilla-conquer` (listed in `DEVELOPMENT.md`) | GNU GPL v3 with EA's additional terms under section 7 (`licenses/VanillaConquer-GPL-3.0-EA-terms.txt`) |
| [SDL](https://github.com/ps5-payload-dev/SDL) 2, ps5-payload-dev's PS5 fork at `ee4c47dc`, with the changes in `patches/sdl` (from the OpenRCT2 port) | Video, audio, controller and on-screen keyboard | zlib (`licenses/SDL2.txt`) |
| [OpenAL Soft](https://github.com/kcat/openal-soft) 1.24.3, Chris Robinson and contributors | Sound, speech, music and movie audio, played through SDL | GNU LGPL v2 or later (`licenses/OpenAL-Soft-LGPL-2.txt`); parts under BSD 3-Clause, the pffft and {fmt} licenses (`licenses/OpenAL-Soft-*.txt`) |
| [libarchive](https://github.com/libarchive/libarchive) 3.8.9, Tim Kientzle and contributors | The importer's ZIP, 7Z, RAR, RAR5 and ISO 9660 readers | BSD 2-Clause, with some files under other permissive terms (`licenses/libarchive.txt`) |
| [xz](https://github.com/tukaani-project/xz) 5.8.4 (liblzma), Lasse Collin and contributors | LZMA and LZMA2 decoding for 7Z | BSD Zero Clause (`licenses/xz-liblzma-0BSD.txt`) |
| [zlib](https://github.com/madler/zlib) 1.3.2, Jean-loup Gailly and Mark Adler | Inflate, for ZIP and The First Decade's cabinets | zlib (`licenses/zlib.txt`) |
| [unshield](https://github.com/twogood/unshield) 1.6.2, David Eriksson and contributors | Reading The First Decade's InstallShield cabinets | MIT (`licenses/unshield-MIT.txt`); its MD5 is the RSA Data Security, Inc. MD5 Message-Digest Algorithm (`licenses/unshield-md5-RSA.txt`) |
| [QR Code generator](https://github.com/nayuki/QR-Code-generator) 1.8.0, Project Nayuki | The upload page's address as a QR code | MIT (`licenses/qrcodegen-MIT.txt`) |
| [DejaVu fonts](https://dejavu-fonts.github.io/) | The launcher's letters, drawn into the title by `tools/make-font.py` | Bitstream Vera and public domain (`licenses/DejaVu-fonts.txt`) |
| `libc.prx` from [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate), BlackBearReloaded | Runtime module the PS5 loader requires next to a native title (`sce_module/libc.prx`), built from its source at commit `b1315a9` | GNU GPL v3 or later (`licenses/libc-prx-GPL-3.0.txt`) |

## Used to build

| Component | Used as | License |
| --- | --- | --- |
| [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) `b1315a9` | `ps5-native-tool` (PIE to PS5 module conversion, FSELF signing), its PIE linker script and C runtime start file, fetched by `tools/fetch-native-tools.sh` | GNU GPL v3 or later |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) v0.42, John Törnblom | Headers, system import stubs, `prospero-lld`, libc++, libc++abi and libunwind | GNU GPL v3, with the LLVM runtime libraries under Apache 2.0 with LLVM exception |
| [LLVM/Clang](https://llvm.org/) 18 | C and C++ compiler for the PS5 target | Apache 2.0 with LLVM exception |
| [DejaVu fonts](https://dejavu-fonts.github.io/) | The letters of the icon, drawn by `tools/make-icon.py` | Bitstream Vera and public domain |
| [libcurl](https://curl.se/), genisoimage, 7-Zip, RAR | The importer's tests on a PC (`make test`): downloads, and making the stand-in discs and archives | curl; GPL v2; LGPL; RAR's own licence (optional) |

The PS5 runtime (`src/runtime`), the compiler drivers and build scripts in `tools/`, the SDL
changes, the upload server and page and the font tools come from the OpenRCT2 port
(`lowbit/ps5-openrct2`, GPL v3); the HTTP client, links and folder listings, and the libarchive and
liblzma build settings from the DOOM port (`lowbit/ps5-doom`), by the same author. The checksums
of Red Alert's files and the Aftermath patch offsets come from OpenRA's content installer (GPL v3).

## Trademarks

Command & Conquer and Red Alert are trademarks of Electronic Arts Inc. This project is not
affiliated with or endorsed by Electronic Arts, and per EA's additional terms it does not use their
trademarks as its name or icon.
