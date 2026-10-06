# PS5 Native RA (native title)

Red Alert as an installable PS5 title, title ID `PPSA99096`: Vanilla Conquer's VanillaRA at
`ce83b59`, on SDL2 from ps5-payload-dev and OpenAL Soft, linked into a PS5 module with the
native-app boilerplate's tools and the OpenRCT2 port's runtime. The player brings the game data;
none is in this repository or its package.

Status (2026-10-06): the title builds, links (every import is from a module a game process loads)
and packages into a signed `dist/PPSA99096` of 5.2 MB; the same sources build for Linux. Nothing has
run on a console yet. First console test: copy a data folder into `ra/` (README), launch, and read
`/data/homebrew/PPSA99096/vanillara.log`.

## Layout

| Path | What |
| --- | --- |
| `vanilla-conquer/` | Vanilla Conquer at `ce83b59` with `patches/vanilla-conquer` applied as commits on a local `ps5` branch (not tracked; `tools/fetch-vanilla-conquer.sh`) |
| `patches/vanilla-conquer/` | The port's changes to Vanilla Conquer (`tools/export-patches.sh` writes them from the `ps5` branch) |
| `patches/sdl/` | Changes to ps5-payload-dev's SDL, from the OpenRCT2 port: no dynamic API, the frame callback, the pointer and touchpad, the IME as the only keyboard |
| `src/runtime/` | What every PS5 link gets (`libps5runtime.a`), the linker script, stubs the SDK lacks; from the OpenRCT2 port, plus `getpwuid_r` |
| `src/port/` | PS5 code compiled into the game: `main.cpp` |
| `data/ra-readme.txt` | The `ra/README.txt` of the title folder: where the game files go |
| `sce_sys/` | `param.json` and the icon (`tools/make-icon.py`, no artwork of the game's own) |
| `cmake/ps5.cmake` | CMake toolchain for the console |
| `cmake/ps5-vanillara.cmake` | Hooked into Vanilla Conquer's project: adds `src/port` and SDL2 after OpenAL |
| `tools/ps5-cc`, `tools/ps5-c++`, `tools/ps5-link` | Compiler drivers for the console; any link produces a PS5 module |
| `tools/build-*.sh`, `tools/package.sh` | Runtime, dependencies, PS5 build, Linux build, title folder |
| `tools/deploy.py`, `klog.py`, `symbolize.sh`, `launcher/launch.c` | Console side: upload, kernel log, crash frames, the `ralaunch` payload |
| `docker/` | The build image |
| `RESEARCH.md` | Engines, game data sources and formats, the import plan, OpenRA |

## Build

```bash
docker build -t ps5-native-ra-build docker/
MSYS_NO_PATHCONV=1 docker run --rm -v "$(cygpath -w "$PWD"):/src" -w /src ps5-native-ra-build make package host
```

`make package` builds, in order:

1. `tools/fetch-native-tools.sh`: ps5-native-app-boilerplate at `b1315a9` (as DOOM and OpenRCT2):
   the PS5 payload SDK v0.42 with its static libc++, `ps5-native-tool` and the clean-room `libc.prx`.
2. `tools/build-runtime.sh`: the CRT, `libps5runtime.a` and the system module stubs into
   `.deps/ps5`.
3. `tools/build-deps.sh`: SDL2 (ps5-payload-dev's fork at `ee4c47dc`, fetched with git as it has
   no release archives, plus `patches/sdl`) and OpenAL Soft 1.24.3 (static, SDL2 as its only
   output, no `dlopen`, no RTKit, no EAX). Each has a stamp in `build/deps`.
4. `tools/fetch-vanilla-conquer.sh` once, then `tools/build-ps5.sh`: VanillaRA with SDL2 and OpenAL,
   no network play, into `build/ps5/vanillara` (the unsigned module) and `build/ps5/vanillara.pie`
   (the linked ELF, for symbols). About a minute on four cores.
5. `tools/package.sh`: `dist/PPSA99096`: the signed `eboot.bin`, `sce_module/libc.prx`, `sce_sys`,
   an empty `ra/` with its README, and the licences.

`make host` builds `build/host/vanillara` for Linux with the system SDL2 and OpenAL. `make release`
writes `dist/PPSA99096.zip` and its `.sha256`.

Outside the build image the same works on Ubuntu 24.04 with the image's packages; without ccache,
the boilerplate's scripts want `USE_CCACHE=0`.

## Changes to Vanilla Conquer (`patches/vanilla-conquer`)

1. Fixed folders on the PS5 (`common/paths_posix.cpp`): program `/app0`, data `/app0/ra`, user
   `/download0/ra` (`td` for Tiberian Dawn). The sandbox refuses the `sysctl` that finds the
   executable on FreeBSD, and there is no home folder. The title supplies `main` and calls
   `VanillaRA_Main` (`redalert/startup.cpp`).
2. Options presses Escape instead of Return (`common/wwkeyboard_sdl2.cpp`): the PS5's SDL passes no
   Create (back) button to a game, so the controller had no Escape.

Everything else builds unchanged: the PS5 target defines `__FreeBSD__`, so Vanilla Conquer takes
its POSIX paths throughout.

## The port (`src/port`)

`main.cpp` opens the log (`/app0/vanillara.log`, else `/download0/vanillara.log`), writes a
first-run `redalert.ini` in `/download0/ra` (the controller as the pointer, the picture boxed, the
software cursor), selects SDL's software renderer (the only one the PS5 SDL has), checks for
`ra/redalert.mix` and shows a system notification without it, and runs the game.

## Game data

Vanilla Conquer reads the MIX files of the Red Alert discs, laid out as `data/ra-readme.txt` says:
`redalert.mix` (the discs' `INSTALL/REDALERT.MIX`), one folder per disc holding its `MAIN.MIX`
(`allied`, `soviet`, `counterstrike`, `aftermath`), and the expansions' `expand.mix`, `expand2.mix`,
`hires1.mix`, `lores1.mix`. The freeware Allied and Soviet discs (2008) are the whole base game: both
campaigns, every movie and every music track. What a player's own copy adds is Counterstrike and
Aftermath (missions, maps, music; Aftermath also units). The demo's `REDALERT.MIX` and `MAIN.MIX` go
straight in `ra/`. Never commit or publish any of it.

## On the console

`uv run --no-project python tools/deploy.py` uploads `dist/PPSA99096` to `/data/homebrew/PPSA99096`
(PS5Upload helper running, as for the other ports); `--ra path/to/ra` also uploads a prepared data
folder, `--eboot` only the executable, and `tools/deploy.py cat /data/homebrew/PPSA99096/vanillara.log`
prints the log. `build/launcher/ralaunch.elf` launches the title from the Payload Manager
(`/data/pldmgr/payloads/ralaunch/`). Crashes: klogsrv and `tools/klog.py`, then
`tools/symbolize.sh` against `build/ps5/vanillara.pie`, as in the OpenRCT2 port.

## Next

1. **First console run** with hand-copied data: boot, title screen, a skirmish, sound, the
   controller, a mission with its movie, saving. Watch for: OpenAL Soft's start-up (its config
   and data searches go through `std::filesystem`; the sandbox refuses `lstat`, and `getcwd` is
   untested),
   the speed of SDL's software scaling from 640x400 to 1728x1080, the renderer's texture format.
2. **Importing the game files** (RESEARCH.md): the send screen, link download and folder pickup from
   the DOOM and OpenRCT2 ports, with libarchive reading ZIP, 7Z, RAR and ISO images, Red Alert's
   disc layouts, The Ultimate Collection and Remastered folders, and the expansions' patch files.
   A first-start screen offers the freeware download alongside.
3. **The controller layer**: a sidebar mode, radial menus for orders and teams, button hints in the
   space beside the 16:10 picture.
4. **Presentation through DOOM's GPU kernel** (palette lookup and sharp-bilinear scaling), if SDL's
   nearest-neighbour scaling shimmers as expected.
5. Console test harness (`test.cfg` plans, frame capture) from the OpenRCT2 port.

## External code (pinned)

- [Vanilla Conquer](https://github.com/TheAssemblyArmada/Vanilla-Conquer) `ce83b59` (GPL-3.0 with
  EA's additional terms)
- [ps5-payload-dev/SDL](https://github.com/ps5-payload-dev/SDL) `ee4c47dc` (zlib)
- [OpenAL Soft](https://github.com/kcat/openal-soft) 1.24.3 (LGPL-2.0-or-later)
- [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  `b1315a9` (GPL-3.0-or-later): `ps5-native-tool`, `libc.prx`, `app_crt.cpp`, the PIE linker script,
  and the PS5 payload SDK v0.42 it fetches
- The runtime, compiler drivers, SDL patches and console tools come from the OpenRCT2 port
