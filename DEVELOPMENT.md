# PS5 Native RA (native title)

Red Alert as an installable PS5 title, title ID `PPSA99096`: Vanilla Conquer's VanillaRA at
`ce83b59`, on SDL2 from ps5-payload-dev and OpenAL Soft, linked into a PS5 module with the
native-app boilerplate's tools and the OpenRCT2 port's runtime. The player brings the game data;
none is in this repository or its package.

Status (2026-10-06): the title builds, links (every import is from a module a game process loads)
and packages into a signed `dist/PPSA99096` of 6 MB; the same sources build for Linux. It starts on
a launcher that gets the game files (the free download, the upload page, links, USB, the import
folder), whose importer passes its tests on a PC (`make test`). Nothing has run on a console yet.
First console test: launch, get the files one of the ways, play, and read
`/data/homebrew/PPSA99096/vanillara.log`.

## Layout

| Path | What |
| --- | --- |
| `vanilla-conquer/` | Vanilla Conquer at `ce83b59` with `patches/vanilla-conquer` applied as commits on a local `ps5` branch (not tracked; `tools/fetch-vanilla-conquer.sh`) |
| `patches/vanilla-conquer/` | The port's changes to Vanilla Conquer (`tools/export-patches.sh` writes them from the `ps5` branch) |
| `patches/sdl/` | Changes to ps5-payload-dev's SDL, from the OpenRCT2 port: no dynamic API, the frame callback, the pointer and touchpad, the IME as the only keyboard |
| `src/runtime/` | What every PS5 link gets (`libps5runtime.a`), the linker script, stubs the SDK lacks; from the OpenRCT2 port, plus `getpwuid_r` |
| `src/port/` | PS5 code compiled into the game: `main.cpp`, the launcher, the importer, the upload server and page (below) |
| `data/ra-readme.txt` | The `ra/README.txt` of the title folder: where the game files go |
| `data/freeware.txt` | Where the free discs are downloaded from; the title reads it from this repository |
| `third_party/` | Build settings for libarchive, liblzma and unshield (the first two from the DOOM port) |
| `test/` | The importer's tests and the launcher on a PC (`make test`) |
| `sce_sys/` | `param.json` and the icon (`tools/make-icon.py`, no artwork of the game's own) |
| `cmake/ps5.cmake` | CMake toolchain for the console |
| `cmake/ps5-vanillara.cmake` | Hooked into Vanilla Conquer's project: adds `src/port`, its generated headers, the import libraries, and SDL2 after OpenAL |
| `cmake/import-libs.cmake` | The importer's libraries, compiled from source with the game (and with the tests) |
| `tools/ps5-cc`, `tools/ps5-c++`, `tools/ps5-link` | Compiler drivers for the console; any link produces a PS5 module |
| `tools/build-*.sh`, `tools/package.sh` | Runtime, dependencies, PS5 build, Linux build, title folder |
| `tools/make-font.py`, `tools/embed-file.py` | The launcher's fonts and the upload page, as C headers (from the OpenRCT2 port) |
| `tools/test-import.sh` | Builds and runs the tests on a PC; `--shots` also saves the launcher's views |
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
   output, no `dlopen`, no RTKit, no EAX); and the sources the importer compiles in with the game:
   libarchive 3.8.9, xz 5.8.4, zlib 1.3.2, qrcodegen 1.8.0 and unshield 1.6.2. Each has a stamp in
   `build/deps`.
4. `tools/fetch-vanilla-conquer.sh` once, then `tools/build-ps5.sh`: VanillaRA with SDL2 and OpenAL,
   no network play, into `build/ps5/vanillara` (the unsigned module) and `build/ps5/vanillara.pie`
   (the linked ELF, for symbols). About a minute on four cores.
5. `tools/package.sh`: `dist/PPSA99096`: the signed `eboot.bin`, `sce_module/libc.prx`, `sce_sys`,
   an empty `ra/` with its README, and the licences.

`make host` builds `build/host/vanillara` for Linux with the system SDL2 and OpenAL. `make test`
builds the importer and the launcher for Linux and runs the importer's tests. `make release` writes
`dist/PPSA99096.zip` and its `.sha256`.

Outside the build image the same works on Ubuntu 24.04 with the image's packages; without ccache,
the boilerplate's scripts want `USE_CCACHE=0`.

## Changes to Vanilla Conquer (`patches/vanilla-conquer`)

1. Fixed folders on the PS5 (`common/paths_posix.cpp`): program `/app0`, data `/app0/ra`, user
   `/download0/ra` (`td` for Tiberian Dawn). The sandbox refuses the `sysctl` that finds the
   executable on FreeBSD, and there is no home folder. The title supplies `main` and calls
   `VanillaRA_Main` (`redalert/startup.cpp`).
2. Options presses Escape instead of Return (`common/wwkeyboard_sdl2.cpp`): the PS5's SDL passes no
   Create (back) button to a game, so the controller had no Escape.
3. Disc folders before the data folder (`redalert/conquer.cpp`, `Change_Local_Dir`): a disc counts
   when its folder (`allied`, `soviet`, `counterstrike`, `aftermath`) has a `MAIN.MIX` of its own,
   and its folder is searched before the data folder, where a `main.mix` holding every disc may
   also be (The First Decade's, the Origin version's). A disc the player has no copy of is replaced
   by one they have: the one holding every disc, else the disc in use, else the first one there is;
   Counterstrike's missions take whichever expansion disc is there. Upstream, a `main.mix` in the
   data folder hid every disc folder, and a missing disc left the game asking for it.

Everything else builds unchanged: the PS5 target defines `__FreeBSD__`, so Vanilla Conquer takes
its POSIX paths throughout.

## The port (`src/port`)

`main.cpp` opens the log (`/app0/vanillara.log`, else `/download0/vanillara.log`), writes a
first-run `redalert.ini` in `/download0/ra` (the controller as the pointer, the picture boxed, the
software cursor), selects SDL's software renderer (the only one the PS5 SDL has), runs the launcher
and, when the player chooses Play, the game.

| File | What |
| --- | --- |
| `launcher.c` | The first screen (SDL, its own fonts, 1920x1080): what is installed, Play, the free download, sending, a link with folder listings, USB drives (`/mnt/usb0` to `7`), and an import's progress and notes. It imports what waits in `/app0/import` when it starts, and destroys its window without `SDL_Quit` so that the game opens its own. |
| `importer.c` | Reads sources on a thread of its own (below). |
| `rafiles.c` | What the importer knows about Red Alert's files: where each goes, OpenRA's checksums of the known copies, how to tell the discs apart, Aftermath's ranges in `PATCH.RTP`. |
| `tfd.c` | The First Decade's InstallShield cabinets through unshield, from a folder or straight out of a disc image (its ISO 9660 folder is looked up and unshield reads the cabinets' bytes in place). |
| `upload.c`, `upload.html` | The upload server and page (from the OpenRCT2 port): files keep their folders below `/app0/import`, only those the importer reads are taken, and the page shows the console's notes. |
| `http.c` | `sceHttp` with `sceSsl` on the console (from the DOOM port: redirects followed by hand, ranges), libcurl on a PC. |
| `listing.c`, `url.c`, `sha1.c` | Folder listings and links (from the DOOM port), SHA-1. |

### The importer

A source is a link, a file or a folder on the console, or a free disc (`freeware:allied`,
`freeware:soviet`: the addresses in `data/freeware.txt`, fetched from this repository, then the
built-in Wayback Machine copy of EA's download; the first that answers with an archive wins). Each
source is read once, as a stream: a file, a download (ranges when the server has them, read in order
otherwise; a server that ignores a range request does not lose the download), an archive's entry,
or a raw image's sectors (2352 or 2448 bytes, mode 1 or 2, of which 2048 are data). The first
64 KiB tell what it holds: an ISO 9660 image, a raw image, ZIP, 7Z, RAR (2.9 and 5), a web page
(a folder listing, when it is the only link), or one of the game's files by its name. Containers
nest: EA's RAR holds an ISO, which libarchive reads in order without unpacking it; a 7Z inside
another archive is copied out first, as 7Z has to be read from its end. Only the file names the
game uses are written, to `ra/.import` with their SHA-1 (of the whole file, and of its first
4096 bytes, OpenRA's way of telling the discs' `MAIN.MIX` apart).

Once a source has been read, its files are told apart and renamed into place, so a source that
fails halfway changes nothing:

- `REDALERT.MIX`, `EXPAND*.MIX`, `HIRES1.MIX`, `LORES1.MIX` by name; The Ultimate Collection's
  `MAIN1.MIX` to `MAIN4.MIX` are the four discs.
- A `MAIN.MIX` by its checksum (the Allied and Soviet discs), else by the nearest folder or image
  whose name speaks of one disc ("RedAlert1_SovietDisc.iso", the Remastered Collection's `CD1`,
  `COUNTERSTRIKE`, `AFTERMATH`; "Counterstrike & Aftermath" says nothing), else by the files beside
  it on its disc (a known `README.TXT`, `CSTRIKE.RTP`, the known `PATCH.RTP`), else it goes in the
  data folder itself, as a `main.mix` holding every disc.
- The Aftermath disc's `PATCH.RTP` gives `expand2.mix`, `hires1.mix` and `lores1.mix` at OpenRA's
  offsets. Counterstrike's `CSTRIKE.RTP` holds its missions compressed; the player is asked for an
  installed `EXPAND.MIX`.
- A copy in the table wins over one that is not (a demo's `REDALERT.MIX` never replaces the full
  game's); a small `main.mix` in the data folder (the demo's) goes once a disc's arrives, and is not
  added over them.
- Sources in the incoming folder are deleted once read; folders elsewhere (USB) are left alone.

## Testing on a PC

`make test` (or `tools/test-import.sh`) builds `build/test/import-test` (the importer with libcurl,
under AddressSanitizer) and runs `test/run-tests.py`: it makes stand-ins for the discs (ISO images
with genisoimage, raw BIN images in three sector layouts, RAR 2.9 written by the script as rar 7
writes no RAR4, RAR 5, 7Z, ZIP, nested archives, the collections' folders, the demo), serves them
over HTTP with and without ranges, and checks every file that ends up in the game folder. The
stand-ins' checksums go into the importer's table with `--known`. `tools/test-import.sh --shots`
also runs `build/test/launcher-test` with scripted key presses (`RA_LAUNCHER_KEYS`) and saves its
views (`RA_LAUNCHER_SHOTS`) to `build/test/shots`.

## Game data

Vanilla Conquer reads the MIX files of the Red Alert discs, laid out as `data/ra-readme.txt` says:
`redalert.mix` (the discs' `INSTALL/REDALERT.MIX`), one folder per disc holding its `MAIN.MIX`
(`allied`, `soviet`, `counterstrike`, `aftermath`), and the expansions' `expand.mix`, `expand2.mix`,
`hires1.mix`, `lores1.mix`. The freeware Allied and Soviet discs (2008) are the whole base game: both
campaigns, every movie and every music track. What a player's own copy adds is Counterstrike and
Aftermath (missions, maps, music; Aftermath also units). The demo's `REDALERT.MIX` and `MAIN.MIX` go
straight in `ra/`. The launcher puts all of it in place (above). Never commit or publish any of it.

## On the console

`uv run --no-project python tools/deploy.py` uploads `dist/PPSA99096` to `/data/homebrew/PPSA99096`
(PS5Upload helper running, as for the other ports); `--ra path/to/ra` also uploads a prepared data
folder (anything put in `/data/homebrew/PPSA99096/import` goes through the importer instead), `--eboot` only the executable, and `tools/deploy.py cat /data/homebrew/PPSA99096/vanillara.log`
prints the log. `build/launcher/ralaunch.elf` launches the title from the Payload Manager
(`/data/pldmgr/payloads/ralaunch/`). Crashes: klogsrv and `tools/klog.py`, then
`tools/symbolize.sh` against `build/ps5/vanillara.pie`, as in the OpenRCT2 port.

## Next

1. **First console run**: the launcher (fonts, controller, the on-screen keyboard for links), the
   free download (whether `sceSsl` trusts web.archive.org's certificate, and its speed), the upload
   page, a USB drive (whether `/mnt/usb*` is visible to the title), then the game: title screen, a
   skirmish, sound, the controller, a mission with its movie, saving. Watch for: OpenAL Soft's
   start-up (its config and data searches go through `std::filesystem`; the sandbox refuses
   `lstat`, and `getcwd` is untested), the speed of SDL's software scaling from 640x400 to
   1728x1080, the renderer's texture format.
2. **The controller layer**: a sidebar mode, radial menus for orders and teams, button hints in the
   space beside the 16:10 picture.
3. **Presentation through DOOM's GPU kernel** (palette lookup and sharp-bilinear scaling), if SDL's
   nearest-neighbour scaling shimmers as expected.
4. Console test harness (`test.cfg` plans, frame capture) from the OpenRCT2 port.

## External code (pinned)

- [Vanilla Conquer](https://github.com/TheAssemblyArmada/Vanilla-Conquer) `ce83b59` (GPL-3.0 with
  EA's additional terms)
- [ps5-payload-dev/SDL](https://github.com/ps5-payload-dev/SDL) `ee4c47dc` (zlib)
- [OpenAL Soft](https://github.com/kcat/openal-soft) 1.24.3 (LGPL-2.0-or-later)
- [ps5-native-app-boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
  `b1315a9` (GPL-3.0-or-later): `ps5-native-tool`, `libc.prx`, `app_crt.cpp`, the PIE linker script,
  and the PS5 payload SDK v0.42 it fetches
- [libarchive](https://github.com/libarchive/libarchive) 3.8.9 (BSD 2-Clause): the ZIP, 7Z, RAR,
  RAR5 and ISO 9660 readers
- [xz](https://github.com/tukaani-project/xz) 5.8.4 (0BSD): liblzma's decoders, for 7Z
- [zlib](https://github.com/madler/zlib) 1.3.2 (zlib): inflate, for ZIP and unshield
- [QR-Code-generator](https://github.com/nayuki/QR-Code-generator) 1.8.0 `720f62b` (MIT)
- [unshield](https://github.com/twogood/unshield) 1.6.2 `51de441` (MIT, with RSA's MD5)
- The runtime, compiler drivers, SDL patches and console tools come from the OpenRCT2 port; the
  HTTP client, link and folder listing code from the DOOM port
