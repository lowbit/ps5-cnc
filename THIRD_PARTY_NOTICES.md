# Third-party notices

Everything in this project that came from someone else, what it is used for, and its license. The
release package carries this file and the license texts in `PPSA99096/licenses/`.

Red Alert's game data is not part of this project or its package: the player puts the files of
their own copy, or of the 2008 freeware release, on the console.

## Shipped in the release

| Component | Used as | License |
| --- | --- | --- |
| [Vanilla Conquer](https://github.com/TheAssemblyArmada/Vanilla-Conquer) at `ce83b59`, The Assembly Armada, from the source Electronic Arts released | The game (VanillaRA), with the changes in `patches/vanilla-conquer` (listed in `DEVELOPMENT.md`) | GNU GPL v3 with EA's additional terms under section 7 (`licenses/VanillaConquer-GPL-3.0-EA-terms.txt`) |
| [SDL](https://github.com/ps5-payload-dev/SDL) 2, ps5-payload-dev's PS5 fork at `ee4c47dc`, with the changes in `patches/sdl` (from the OpenRCT2 port) | Video, audio, controller and on-screen keyboard | zlib (`licenses/SDL2.txt`) |
| [OpenAL Soft](https://github.com/kcat/openal-soft) 1.24.3, Chris Robinson and contributors | Sound, speech, music and movie audio, played through SDL | GNU LGPL v2 or later (`licenses/OpenAL-Soft-LGPL-2.txt`); parts under BSD 3-Clause, the pffft and {fmt} licenses (`licenses/OpenAL-Soft-*.txt`) |
| `libc.prx` from [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate), BlackBearReloaded | Runtime module the PS5 loader requires next to a native title (`sce_module/libc.prx`), built from its source at commit `b1315a9` | GNU GPL v3 or later (`licenses/libc-prx-GPL-3.0.txt`) |

## Used to build

| Component | Used as | License |
| --- | --- | --- |
| [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate) `b1315a9` | `ps5-native-tool` (PIE to PS5 module conversion, FSELF signing), its PIE linker script and C runtime start file, fetched by `tools/fetch-native-tools.sh` | GNU GPL v3 or later |
| [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) v0.42, John Törnblom | Headers, system import stubs, `prospero-lld`, libc++, libc++abi and libunwind | GNU GPL v3, with the LLVM runtime libraries under Apache 2.0 with LLVM exception |
| [LLVM/Clang](https://llvm.org/) 18 | C and C++ compiler for the PS5 target | Apache 2.0 with LLVM exception |
| [DejaVu fonts](https://dejavu-fonts.github.io/) | The letters of the icon, drawn by `tools/make-icon.py` | Bitstream Vera and public domain |

The PS5 runtime (`src/runtime`), the compiler drivers and build scripts in `tools/`, and the SDL
changes come from the OpenRCT2 port (`lowbit/ps5-openrct2`, GPL v3), by the same author.

## Trademarks

Command & Conquer and Red Alert are trademarks of Electronic Arts Inc. This project is not
affiliated with or endorsed by Electronic Arts, and per EA's additional terms it does not use their
trademarks as its name or icon.
