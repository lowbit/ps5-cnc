# Command & Conquer (first generation) on the PS5: research

What it takes to bring Tiberian Dawn (with The Covert Operations) and Red Alert (with Counterstrike
and Aftermath) to the PS5 as native titles, the way DOOM (`lowbit/ps5-doom`) and OpenRCT2
(`lowbit/ps5-openrct2`) were done: which engines exist, which of them need the original game files,
where those files come from and in what forms, and how the importer should take all of them. Red
Alert 2 and Generals are only touched on at the end. Everything here was checked on 2026-10-06 unless
it says otherwise; nothing has been built or run on the console yet.

## Recommendation

1. **Port [Vanilla Conquer](https://github.com/TheAssemblyArmada/Vanilla-Conquer)** (VanillaTD and
   VanillaRA). It is the maintained C++ engine for both games (last commit 2026-07-11), built on SDL2
   and OpenAL, single-threaded, already 64-bit clean and already ported by others to the Switch, the
   Nintendo DSi and the Amiga. It fits the OpenRCT2 port's toolchain, runtime and SDL2 almost as it is,
   and it renders an 8-bit 640x400 picture with a palette, which is exactly what DOOM's GPU presenter
   scales.
2. **Every engine needs Westwood's data files.** None of them can ship playable the way DOOM ships the
   shareware episode. But the two base games are official freeware (EA, 2007 and 2008), so a player
   without discs can still get them for nothing; only the expansions need a copy the player owns.
3. **Build one importer that takes the data in every form it exists in**: the freeware ISOs, the
   original CDs, The First Decade DVD, The Ultimate Collection (Steam, EA app), the Remastered
   Collection's legacy folder and plain game folders, loose or inside ZIP, 7Z and RAR, sent from a
   browser, fetched from a link or picked up from a USB drive. Most of it is DOOM's importer and
   OpenRCT2's send screen as they are; the new parts are ISO images (libarchive already reads them),
   the InstallShield archives on the discs and the expansion patch files.
4. **Leave OpenRA for a second track.** It is C# (.NET 10 on its development branch) and the PS5 had no
   .NET until recently. That changed: SharpProspero links Microsoft's own NativeAOT runtime into PS5
   titles that ship today, and ps5-opengl gives OpenGL 4.6. So OpenRA is now possible, but it needs
   changes to OpenRA itself and a controller interface it has never had. It is also the only open
   engine with a Red Alert 2 mod, so it is worth a spike once Vanilla Conquer runs.

## What exists

### Engines and builds

| Engine | Games | Language, libraries | State | PS5 fit |
| --- | --- | --- | --- | --- |
| [Vanilla Conquer](https://github.com/TheAssemblyArmada/Vanilla-Conquer) | TD + Covert Ops, RA + Counterstrike + Aftermath (two executables, `vanillatd` and `vanillara`) | C++11, CMake, SDL2 (or SDL1), OpenAL | Maintained, last commit 2026-07-11; binary releases for Windows, Linux, macOS | **Best**: same stack as the OpenRCT2 port |
| [OpenRA](https://github.com/OpenRA/OpenRA) | TD, RA, Dune 2000 (Tiberian Sun in development; mods for RA2 and others) | C#, .NET 10 (`net10.0` on bleed), SDL2, OpenGL 3.2 core or GLES 3.0, OpenAL, FreeType, Lua 5.1 | Last release `release-20250330`, playtest `playtest-20260222`, bleed active | Possible now, large: see [OpenRA](#openra-as-a-second-track) |
| EA's original sources ([CnC_Tiberian_Dawn](https://github.com/electronicarts/CnC_Tiberian_Dawn), [CnC_Red_Alert](https://github.com/electronicarts/CnC_Red_Alert)) | TD, RA | C++ with Watcom-era assembly, DOS and Windows 95 code | Released 2025-02-28 under GPL 3 with EA's additional terms; archival | Poor directly; [ra-port](https://github.com/dk8827/ra-port) builds RA from it with SDL2 for iOS, Android, Linux and macOS |
| [CnC_Remastered_Collection](https://github.com/electronicarts/CnC_Remastered_Collection) | TD and RA game logic as DLLs | C++ DLLs for the closed C# client | Released 2020; Vanilla Conquer grew out of it | No: the client and HD art are not open |
| [OpenRA TiberianDawnHD](https://github.com/OpenRA/TiberianDawnHD) | TD with the Remastered HD art | OpenRA mod | Proof of concept, "a dedicated GPU is recommended" | Only after OpenRA itself |

No PS5 build of any of them was found: none in the homebrew catalog (the `ps5-homebrew-catalog` fork,
25 apps), none on GitHub or in searches.

### Why not the others

- **EA's original sources** are what Westwood shipped: DOS and Windows 95 code with assembly,
  DirectDraw and Watcom-specific parts. Vanilla Conquer starts from the cleaner 2020 Remastered DLL
  code and has done the portability work already (SDL, OpenAL, 64-bit, POSIX file system).
- **The Remastered DLLs** are only the simulation; the renderer, interface and HD assets live in the
  closed client.
- **Chronoshift**, The Assembly Armada's Red Alert reimplementation, was archived on 2020-07-23,
  after the source release; the same group maintains Vanilla Conquer.

## Game data

### Where it is needed

Every engine above reads Westwood's MIX files (sprites, maps, sounds, speech, music, movies). None of
it can be committed or put in a release. The difference from game to game is how a player gets it:

| Content | Free? | Where from |
| --- | --- | --- |
| Tiberian Dawn (GDI and Nod campaigns, skirmish) | **Yes**: C&C Gold, freeware since 2007-08-31 | Two ISO images, *GDI* and *Nod*; EA's page is gone, mirrored on [ModDB](https://www.moddb.com/games/cc-gold/downloads/command-conquer-gold-free-game-gdi-iso) and elsewhere |
| The Covert Operations | No | Its CD; The First Decade; The Ultimate Collection; the Remastered Collection |
| Red Alert (Allied and Soviet campaigns, skirmish) | **Yes**: freeware since 2008-08-31 | Two ISO images, *Allied* and *Soviet* ([ModDB](https://www.moddb.com/games/cc-red-alert/downloads/cc-red-alert-full-game-allied-iso)) |
| Red Alert demo | Free download, distribution terms not confirmed | `ra95demo.zip`; Vanilla Conquer plays it fully: skirmish (no interior maps) and one mission per side |
| Counterstrike, Aftermath | No (the EA freeware ISOs do not have them) | Their CDs; The First Decade; The Ultimate Collection; the Remastered Collection. Community packages such as CnCNet's carry them too |

Each game needs only one of its two discs to start; the other disc adds its campaign and movies.
OpenRA treats music and movies as optional; whether Vanilla Conquer starts with a disc folder that
lacks them is still to be checked.

### What Vanilla Conquer reads

From its code (`tiberiandawn/init.cpp`, `tiberiandawn/conquer.cpp`, `redalert/init.cpp`,
`redalert/conquer.cpp`) and its wiki ([TD](https://github.com/TheAssemblyArmada/Vanilla-Conquer/wiki/Installing-VanillaTD),
[RA](https://github.com/TheAssemblyArmada/Vanilla-Conquer/wiki/Installing-VanillaRA)). File names may be
in any case. A disc's folder is found by its name, and the game switches folders as a campaign asks
for "the other CD":

```text
td/                   conquer.mix desert.mix temperat.mix winter.mix sounds.mix      (disc root)
                      cclocal.mix transit.mix speech.mix update.mix updatec.mix
                      deseicnh.mix tempicnh.mix winticnh.mix                         (INSTALL/SETUP.Z)
td/gdi/               general.mix movies.mix scores.mix                              (GDI disc)
td/nod/               general.mix movies.mix scores.mix                              (Nod disc)
td/covertops/         general.mix movies.mix scores.mix                              (Covert Ops disc)

ra/                   redalert.mix                                                   (INSTALL/REDALERT.MIX)
                      expand.mix                                                     (Counterstrike)
                      expand2.mix hires1.mix lores1.mix                              (Aftermath)
ra/allied/            main.mix                                                       (Allied disc)
ra/soviet/            main.mix                                                       (Soviet disc)
ra/counterstrike/     main.mix
ra/aftermath/         main.mix
```

A single `main.mix` holding both campaigns (The First Decade, the Origin Ultimate Collection) goes in
`ra/` itself: when only the data folder has disc files, Vanilla Conquer treats it as every disc (its
DVD handling; how that combines with separate expansion folders is to be checked). `cclocal.mix` must come from `SETUP.Z`: some discs, the freeware images among them,
also carry a loose German one in `INSTALL`.

Vanilla Conquer's README names the retail and freeware discs and the RA demo as working, and says
data from the Remastered Collection, The Ultimate Collection and The First Decade can be used but is
"currently not supported". The importer should take them; the console tests should cover each.

### Every source, mapped to that layout

| Source | Form | What the importer does |
| --- | --- | --- |
| C&C Gold disc, GDI or Nod (freeware image or CD) | ISO 9660 | Root MIX files to `td/`; `general`, `movies`, `scores` to `gdi/` or `nod/` (the disc is told by `DISK.WAV`); unpack `INSTALL/SETUP.Z` (InstallShield 3, PKWare DCL compression) for the eight install-only files |
| Covert Ops disc | ISO 9660 | `general`, `movies`, `scores` to `covertops/`, plus what the wiki takes from its `INSTALL` folder (its `conquer.mix` is newer than Gold's) |
| Red Alert disc, Allied or Soviet | ISO 9660 | `INSTALL/REDALERT.MIX` to `ra/`; `MAIN.MIX` to `allied/` or `soviet/` (told apart by the SHA-1 of its first 4 KB) |
| Counterstrike disc | ISO 9660 | `MAIN.MIX` to `counterstrike/`; `expand.mix` from `SETUP/INSTALL/CSTRIKE.RTP` (an RTPatch file: byte ranges to find, as OpenRA did for Aftermath) |
| Aftermath disc | ISO 9660 | `MAIN.MIX` to `aftermath/`; from `SETUP/INSTALL/PATCH.RTP`: `expand2.mix` (offset 4712984, 469922 bytes), `hires1.mix` (5182981, 90264), `lores1.mix` (5273320, 57076), offsets from OpenRA |
| The First Decade DVD | InstallShield cabinet, `data1.hdr` + `data1.cab` ... `data5.cab` | Read the `CnC\` and `Red Alert\` folders out of the cabinet set (volumes 1 to 3 for TD, 1 to 5 for RA) |
| The Ultimate Collection, Steam (2024) or EA app | Installed folders | TD: the root MIX files, `movies.mix`, `SCORES.MIX` (the Origin version keeps Covert Ops' music in `covert\`). RA: `REDALERT.MIX`, `MAIN1.MIX` to `MAIN4.MIX` (Allied, Soviet, Counterstrike, Aftermath) to the four folders, `EXPAND2.MIX`, `HIRES1.MIX`, `LORES1.MIX` |
| The Remastered Collection, Steam, EA app or Epic | `Data/CNCDATA/` in the install | `TIBERIAN_DAWN/CD1`, `CD2`, `CD3` to `gdi/`, `nod/`, `covertops/` plus the root files; `RED_ALERT/CD1`, `COUNTERSTRIKE`, `AFTERMATH`. It has no Soviet disc |
| Red Alert demo | ZIP: `ra95demo/INSTALL/REDALERT.MIX`, `MAIN.MIX` | Both to `ra/` |
| An installed copy (CnCNet, a 1990s install, someone's Vanilla Conquer folder) | Folder or ZIP of it | Matched by file names and checksums into the layout |
| OpenRA's freeware packages | ZIP | Not enough as they are: the TD one lacks `cclocal`, `update`, `updatec`, two of the three icon sets, movies and music; the RA one has `MAIN.MIX` already taken apart |

### Telling files apart

Names alone are not enough (two kinds of `main.mix`, a German `cclocal.mix`, the Remastered
`conquer.mix` differing from Gold's). OpenRA's content installer has a SHA-1 for every source it
knows, in `mods/cnc-content/installer/*.yaml` and `mods/ra-content/installer/*.yaml`; they make a good
table for the importer. A few:

| File | SHA-1 | Means |
| --- | --- | --- |
| `CONQUER.MIX` | `833e02a09aae694659eb312d3838367f681d1b30` | C&C Gold disc (GDI or Nod) |
| `DISK.WAV` | `8bef9a66...` / `83a02355...` | GDI disc / Nod disc |
| `CONQUER.MIX` | `713b53fa4c188ca9619c6bbeadbfc86513704266` | Covert Ops disc, Ultimate Collection |
| `Data/CNCDATA/TIBERIAN_DAWN/CD1/CONQUER.MIX` | `3f891c8dc0864f654e1710430ea4ff34c3715e97` | Remastered Collection |
| `INSTALL/REDALERT.MIX` | `0e58f4b54f44f6cd29fecf8cf379d33cf2d4caef` | Red Alert (every source has this one) |
| `MAIN.MIX`, first 4096 bytes | `20ebe16f...` / `9d108f18...` | Allied disc / Soviet disc |
| `data1.hdr` | `bef3a08c3fc1b1caf28ca0dbb97c1f900005930e` | The First Decade |

Anything the table does not know is still taken when its name fits the layout, with a note on the
screen and the page, so a patched or translated copy is not refused outright.

### What can ship in the package

Nothing of Westwood's. The Quake II port in the catalog ships the official Quake II demo with its
licence, and DOOM ships the shareware WAD under id's shareware terms; the Red Alert demo would be the
equivalent here, but no redistribution licence for it was found. Shipping it is a decision to make
(below), not a default.

## Importing: every format, the DOOM and OpenRCT2 way

### Ways in

| Way | From | State |
| --- | --- | --- |
| Send screen: a page on `http://<console>:9666/` with a QR code; files or whole folders dropped or chosen in a PC or phone browser, `PUT /upload/<path>`, notes back through `GET /status` | OpenRCT2's `upload.c` and `upload.html`, DOOM's send screen | Reuse; widen what the page accepts |
| Download from a link typed on the console: a file, an archive, or an HTML folder listing; HTTPS through `sceHttp`, range requests so archives are read without downloading twice | DOOM's `import.c` and `listing.c` | Reuse. A player can paste an archive.org link to a freeware ISO and never touch a PC |
| A folder on the console: files put in `/app0/import/` by FTP, ps5upload or USB copy, picked up at start | DOOM (`/app0/wads`) | Reuse |
| A USB drive: ISOs, archives or game folders found on `/mnt/usb0` ... | New | ShadowMountPlus mounts `/mnt` and `/data` into a title's sandbox (its README; ProsperoEden asks for 1.7beta4 or newer for this). Older setups do not have it, so it is an extra, not the main way |

### Formats

| Format | Read with | Notes |
| --- | --- | --- |
| Loose files, folders | The upload server | As OpenRCT2: only paths that fit the layout are stored, each part a plain name, written as `.upload` and renamed when complete |
| ZIP, 7Z, RAR 4 and 5 | libarchive (DOOM's build: zip, 7z, rar, rar5, liblzma, zlib) | As DOOM, including reading over HTTP with ranges |
| ISO 9660 (`.iso`) | libarchive's iso9660 reader, **added to DOOM's build** | libarchive reads ISO images as a stream, so an ISO inside a ZIP or 7Z, or arriving over HTTP, is unpacked without a temporary copy. The freeware downloads are `.iso` files |
| BIN/CUE, IMG (raw 2352-byte sectors) | A small reader that hands libarchive the 2048 user bytes of each sector (MODE1 and MODE2/XA) | Common for old CD rips; the `.cue` itself can be ignored |
| InstallShield 3 (`INSTALL/SETUP.Z` on the C&C discs) | About 150 lines after OpenRA's `InstallShieldPackage.cs` (a header pointing at the directory table, files from offset 255) plus `blast.c` from zlib's `contrib/blast` (zlib licence) | Needs seeking, so `SETUP.Z` is copied out of the ISO first; it is small next to the disc |
| InstallShield cabinets (The First Decade) | [unshield](https://github.com/twogood/unshield) (MIT, C, zlib) or a port of OpenRA's `InstallShieldCABCompression.cs` (461 lines) | The page should send only `data1.hdr` and `data1.cab` to `data5.cab`, not the whole DVD |
| RTPatch (`CSTRIKE.RTP`, `PATCH.RTP`) | Byte ranges at known offsets, checked by SHA-1 | Aftermath's ranges are in OpenRA; Counterstrike's `expand.mix` ranges have to be found |
| MIX | Copied whole | Never taken apart: Vanilla Conquer reads nested MIX files itself |
| CHD, NRG, MDF | Not planned | libchdr (BSD) if anyone asks |

The Ultimate Collection and the Remastered Collection come as installed folders, so they need nothing
beyond the folder rules. No GOG installer of these games was found (they sell on Steam, the EA app
and Epic), so OpenRCT2's Inno Setup reader has nothing to read here, unless a community installer
turns out to be Inno Setup.

### How an import runs

1. A file arrives (upload, link or folder). Archives and images are opened and walked; nested ones are
   opened in turn (a 7Z holding two ISOs holding `SETUP.Z`).
2. Each file is identified by name, size and SHA-1 against the table, and given its place in the
   layout. Files the game does not use (the DOS and Windows executables, `DISK.WAV`, the installer's
   own files) are skipped and named in the notes.
3. It is written as `<path>.part` in 256 KB writes (DOOM's measurement: about 60 MB/s on `/app0` that
   way, against 0.4 MB/s for the 20 KB pieces `recv` hands over), checked and renamed.
4. When a game has what it needs to start (TD: the root files and one disc folder; RA:
   `redalert.mix` and one `main.mix`), the screen says so and the game list shows it; what is still
   missing (the other campaign, music, movies, an expansion) is listed beside it.
5. Uploaded archives and images are deleted once their contents are out, as DOOM does.

Expected times, from the DOOM and OpenRCT2 measurements: a 600 to 700 MB disc image arrives in about
a minute over a wired network at 10 to 11 MB/s and is unpacked in seconds (a few large files, not
thousands of small ones), so all four freeware discs take five minutes or so.

### Page and tools

- The page takes `.iso`, `.bin`, `.cue`, `.img`, `.zip`, `.7z`, `.rar`, `.mix`, `.hdr`, `.cab`,
  `.rtp` and folders; for a dropped Remastered or Ultimate Collection install it sends only
  `Data/CNCDATA` or the game folders, as OpenRCT2's page sends only the game's own folders.
- `tools/send.py` learns the same, for hands-free console tests.
- The game list shows each game with a checklist: base files, each campaign, music, movies,
  expansion.

## The port: Vanilla Conquer on the PS5

### What comes from where

| Part | From |
| --- | --- |
| Toolchain: clang 18 for `x86_64-sie-ps5`, `ps5-cc`, `ps5-c++`, `ps5-link`, the native-app boilerplate's `ps5-native-tool` and `libc.prx`, the build image | OpenRCT2 |
| Runtime: the mspace heap, `dirent` on `sceKernelGetdents`, `realpath`, the missing libc functions, logging to file and klog, 2 MiB thread stacks, exiting through `sceSystemServiceLoadExec("exit")` | OpenRCT2's `src/runtime` |
| SDL2 (ps5-payload-dev's fork) with the patches: frame callback, a pointer the application moves with a drawn cursor, the touchpad, the IME as the only keyboard | OpenRCT2's `patches/sdl` |
| OpenAL | New: OpenAL Soft built with its SDL2 output backend (SDL already drives AudioOut), or a `soundio` backend of our own straight on AudioOut (DOOM has the AudioOut code) |
| Presentation: palette lookup and sharp-bilinear scaling of an 8-bit picture to 1080p on the GPU, CPU fallback | DOOM's `src/ps5/gpu.c` and `present.cl` (the source size is already a parameter) |
| Importer, send screen, link download, game list | DOOM and OpenRCT2, widened as above |
| Console test harness (`test.cfg` plans, frame capture over TCP 9119, the launch payload), deploy, klog, symbolize | Both |

### Changes to Vanilla Conquer

1. The title supplies `main`; `Paths` gets a PS5 version with fixed places (program `/app0`, data
   `/app0/td` or `/app0/ra`, user `/download0/td` or `/download0/ra`) instead of `realpath` on
   `/proc/self/exe` (`common/paths_posix.cpp`).
2. Video: start with the SDL2 backend as it is (`common/video_sdl2.cpp`, SDL's software renderer, as
   OpenRCT2 does), then move presentation to DOOM's GPU path (below).
3. Audio: OpenAL as it is, through OpenAL Soft (`common/soundio_openal.cpp`, `vqaaudio_openal.cpp`).
4. Input: the controller layer (below). Vanilla Conquer already has a plain one in
   `common/wwkeyboard_sdl2.cpp` (left stick moves the pointer, A and B click, X and Y are G and F,
   the d-pad is teams 1 to 4, the right stick scrolls, the right trigger speeds the pointer up), a
   starting point and nothing more.
5. Networking off at first (`NETWORKING=OFF`); LAN play against PCs later. A title can listen on
   most TCP ports but the sandbox refuses some (DOOM's finding), so ports need checking.
6. Text entry (save names, player names) through the IME patch; save slots pre-filled, as DOOM does.
7. Anything else the console's libc or sandbox refuses turns up in the first console runs; the
   OpenRCT2 runtime already covers what DOOM and OpenRCT2 met (`opendir`, `realpath`, `lstat`,
   `localtime_r`, `fnmatch`, small heap, small stacks).

### One title or two

VanillaTD and VanillaRA are two executables built from the same `common/` code with different
defines, so their global names collide and they cannot simply be linked into one `eboot.bin`.

- **One title ("Vanilla Conquer", one icon, one send screen, a game list as in DOOM)**: link each
  game into a relocatable object with only its entry point left global (`ld -r`, then
  `llvm-objcopy --localize-hidden`, compiled with `-fvisibility=hidden`), and link both behind the
  launcher. Both games' static constructors run at start, which looks harmless in their code. This
  is the first thing to prove.
- **Or**: chain executables inside one title with `sceSystemServiceLoadExec` (the OpenRCT2 port
  already calls it to exit); whether a fake-signed title may load a second module from `/app0` needs
  a console check.
- **Fallback: two titles** from one repository sharing `src/port/`, each importing its own game's data
  (and telling the player when files for the other one arrive).

### Presentation

Both games draw 640x400 in 256 colours. On a 1080p screen that is 2.7 times: 1440x1080 at the 4:3 a
CRT showed, or 1728x1080 with square pixels (Vanilla Conquer's default boxing is 16:10). Nearest
neighbour at 2.7 makes uneven pixel columns that shimmer when the map scrolls; DOOM's compute kernel
already does palette lookup and sharp-bilinear scaling of an 8-bit picture into the tiled scanout,
and its source size is a parameter. The 4:3 picture leaves 240 pixels on each side of the screen,
which is where button hints, the selected team and import notes can go without covering the game.

### Controls

RTS on a controller has precedents: the PlayStation versions of both games (cursor on the d-pad,
Cross to act, Circle to cancel, Triangle to the sidebar, team creation and recall on L2 and L1 with
the face buttons, force fire, force move and guard on R1 combinations) and OpenRCT2's pointer layer
(stick and touchpad pointer, jumping between buttons, radial menus, hints). A first proposal:

| Input | On the battlefield | In the sidebar |
| --- | --- | --- |
| Left stick | The pointer, faster after a moment at full tilt (as in OpenRCT2); pushed against the screen's edge it scrolls | The pointer |
| Touchpad | The pointer, like a laptop's; a click is Cross | |
| Right stick | Scroll the map | |
| Cross | Select, give the order; held and moved, a selection box | Build or place |
| Circle | Deselect, cancel placement | Leave the sidebar |
| Triangle | Jump to the sidebar (the d-pad then moves through its two columns of build icons) | Back to the map |
| Square | Radial menu of orders: guard, stop, scatter, formation (RA), deploy, sell, repair, map | |
| D-pad | Recall team 1 to 4; twice to centre on it | Move between icons |
| L2 + d-pad | Make team 1 to 4 from the selection | |
| L1 | Add to the selection (Shift) | Page the column up |
| R1 | Force fire (Ctrl) while held | Page the column down |
| R2 | Force move (Alt) while held | |
| L3 / R3 | Centre on the last event (Space) / on the base (Home) | |
| Options | Game menu | |

USB mouse and keyboard would suit an RTS better than anything above. OpenRCT2's notes list
`libSceKeyboard` and `libSceMouse` among the modules a title loads itself with
`sceSysmoduleLoadModule` (firmware 13.00); PS5SX2 reports the console refusing them on 11.x and
12.00. Worth adding once the controller layer works.

### Saves, settings, size

Settings (`conquer.ini`, `redalert.ini`) and saves go to `/download0`; `downloadDataSize` 256 is
plenty. Game data stays in the title folder as DOOM and OpenRCT2 keep theirs (`/app0/td`,
`/app0/ra`): about 1.2 GB for TD with both campaigns and Covert Ops, about 1.6 GB for RA with both
discs and the expansions (estimates from the disc sizes; to be measured).

### Name, icon, title IDs

EA's additional terms to the GPL (section 7, in Vanilla Conquer's `License.txt`) grant no trademark
rights and forbid distributing a modified version "using any Electronic Arts trademark". So the
home-screen name and icon should not be "Command & Conquer" or "Red Alert" or use EA's or the
factions' logos; upstream calls itself Vanilla Conquer, `vanillatd` and `vanillara` for this reason.
A neutral name such as **Vanilla Conquer** (or Vanilla TD and Vanilla RA) and original icon art are
the safe choice. Free title IDs in the catalog fork today, if wanted: `PPSA99095` and `PPSA99096`
(reserve them in the catalog before use).

### Testing

- A host build for Linux in the build image, as DOOM has, to run the importer against every source
  (freeware ISOs, demo ZIP, folders) and compare the layout it writes with the wiki's.
- Console plans as in both ports: first boot with an empty data folder (the import screen), each
  import route, skirmish, a campaign mission switching discs, movies, music, saves, a long soak.

### Risks and open questions

- Data Vanilla Conquer does not officially support (The First Decade, Ultimate Collection,
  Remastered): possible invisible units or crashes; test each and say so on the import screen.
- The Counterstrike `expand.mix` byte ranges in `CSTRIKE.RTP` are not documented anywhere found.
- `sceSystemServiceLoadExec` with a second module and the `ld -r` route are both unproven.
- OpenAL Soft on the PS5 is new ground; the fallback is a small mixer of our own on AudioOut.
- Whether The Ultimate Collection's TD folder holds `cclocal.mix`, `updatec.mix` and the icon sets
  directly (OpenRA's lists only name the files OpenRA needs).

## OpenRA, as a second track

### Why not first

OpenRA is a C# engine. Until this year a PS5 port meant writing a .NET runtime for the console. Its
interface is mouse and keyboard only (SDL is started for video alone; its "joystick" setting is a
mouse scrolling mode), it is far larger than Vanilla Conquer, and for TD and RA it plays well but
differently from the originals.

### What the PS5 now offers

- **.NET through NativeAOT.** [SharpProspero](https://github.com/SvenGDK/SharpProspero) (GPL-3.0,
  last commit 2026-09-27) compiles C# with `dotnet publish -r linux-x64` and NativeAOT, then links the
  object with the **.NET SDK's own linux-x64 NativeAOT runtime archives** (GC, exceptions, type
  system) into a PS5 module: the console's libc and libkernel already export most of what the runtime
  imports (the `pthread` family, `mmap`, files, time), and a small generated compat object covers the
  rest (the large-file variants, `readdir64`, a few glibc-only names). Prospero Explorer and Prospero
  Vibrate (both in the catalog) are built with it. It targets .NET 10, which is what OpenRA's bleed
  branch uses.
- **OpenGL 4.6 core.** [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl) (Mesa and
  Gallium, EGL, an SDL2 bridge, a static SDK) passed the complete Khronos OpenGL 4.6 conformance run
  on a PS5 (2026-10-01; the project's own run, not Khronos certification); the Quake II port renders
  through it. OpenRA asks for OpenGL 3.2 core or GLES 3.0.
- **JIT only with help.** PS5SX2 runs PCSX2's recompilers on the console, but only after its helper
  payload (based on OnionHEN) has jailbroken the title. Ahead-of-time compilation needs none of
  that.

### What OpenRA would need

1. Mod assemblies loaded with `AssemblyLoadContext.LoadFromAssemblyPath`
   (`OpenRA.Game/Support/AssemblyLoader.cs`) become static references compiled into the one image.
2. `OpenRA.Game/Sync.cs` emits IL (`DynamicMethod`, `ILGenerator`) for the sync hashes; NativeAOT
   cannot. It needs a source generator or plain reflection.
3. 17 `MakeGenericType`/`MakeGenericMethod` calls (`FieldLoader.cs`, `TraitDictionary.cs`,
   `TypeDictionary.cs`, `ScriptContext.cs`) fail at run time for value-type instantiations nobody
   compiled; root them or rewrite them.
4. `Expression.Compile` (condition expressions in `VariableExpression.cs`, `SyncReport.cs`) falls back
   to the expression interpreter: works, slower.
5. Reflection over every trait and YAML field: keep all mod assemblies' metadata (trimming roots).
6. Native libraries for the console: SDL2 with an OpenGL window (ps5-opengl's bridge), OpenAL Soft,
   FreeType, Lua 5.1 (for Eluant), linked statically.
7. A controller layer for OpenRA's whole interface, like OpenRCT2's.
8. Its content installer is replaced by our importer (its YAML source definitions stay useful as data).

### What it would unlock

Dune 2000 (retail data only), Tiberian Sun once OpenRA's TS mod is done, the
[RA2 mod](https://github.com/OpenRA/ra2) (active, last commit 2025-11-28, not complete), TiberianDawnHD
with the Remastered art, and one engine for all of them.

### A spike, after Vanilla Conquer runs

1. Build OpenRA's dedicated server (`OpenRA.Server`, no graphics) with NativeAOT and SharpProspero's
   linking, and run a headless game on the console: this proves the runtime, the mod loading and the
   reflection changes.
2. Then the game with ps5-opengl, mouse-driven first, then the controller layer.

## Later: Red Alert 2 and Generals

- **Red Alert 2 and Tiberian Sun**: EA released no source for either (reportedly lost; only the
  FinalSun and FinalAlert 2 map editors were released). The open routes are OpenRA's RA2 and TS mods,
  which is another reason to have OpenRA running.
- **Generals and Zero Hour**: EA released the source on 2025-02-28; the community continues it as
  [GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode). It is Direct3D 8 and
  Windows code, so a port needs a renderer the PS5 has: OpenGL 4.6 (ps5-opengl) or Vulkan (Mihawk-99's
  PS5_Vulkan driver, which PS5SX2 renders through, and a Mesa RADV port).

## How other PS5 homebrew handles data

Looked at through the native apps in the catalog fork and their READMEs:

| App | Player's data | How it gets in |
| --- | --- | --- |
| DOOM (yours) | IWADs, archives of them | Send screen with QR code, link download with folder listings, folder scan, ZIP/7Z/RAR; ships the shareware WAD |
| OpenRCT2 (yours) | RCT2 folder | Send screen: GOG installer (own Inno Setup reader), folder or ZIP |
| Yamagi Quake II (`PPSA99007`) | `baseq2` PAKs | Ships the official demo with its licence; retail data copied into the title folder by hand |
| PS5SX2 (`PPSA99203`) | BIOS, `.iso`/`.chd`/`.cso`/`.zso` games | `/data/PCSX2/...` or a USB drive's `games/` and `bios/`; settings page on the phone by QR code; a helper payload for its recompilers |
| RetroArch (`PPSA99169`) | ROMs, BIOS | A WebUI on port 6769 with uploads, or FTP |
| ProsperoEden (`PPSA99008`) | Keys, firmware, games | A folder browser over internal, M.2 and USB storage (ShadowMountPlus 1.7beta4+ mounts `/data` and USB into the sandbox); updates itself from the catalog |
| PICO-8 loader (`PPSA99808`) | The player's PICO-8 Linux binary | Copied by FTP; an elevator payload through elfldr on port 9021 for `/data` |
| Prospero Explorer (`PPSA99109`) | Anything | A C# (SharpProspero) file manager with zip and tar, a network file service |

Nobody else takes disc images, installers and archives from a phone the way DOOM and OpenRCT2 do;
that stays the model. Two ideas worth taking: reading straight from a USB drive (needs a recent
ShadowMountPlus) and ProsperoEden's update check against the catalog.

## Decisions for you

1. One title with both games (the `ld -r` route, to be proven) or two titles.
2. A "download the freeware games" button with built-in links, or only links the player types.
   OpenRA downloads freeware content from its own mirrors; our own links would need a host that
   stays up.
3. Ship the Red Alert demo in the package (no licence found that allows it) or only accept it.
4. Data in the title folder (as now) or in `/data/...` (survives deleting the title; needs
   ShadowMountPlus 1.7beta4 or newer).
5. Title name and icon (see the trademark terms) and the title IDs.
6. When to look at USB mouse and keyboard, and whether to spike OpenRA.

## Sources

- Vanilla Conquer: [repository](https://github.com/TheAssemblyArmada/Vanilla-Conquer),
  [Installing VanillaTD](https://github.com/TheAssemblyArmada/Vanilla-Conquer/wiki/Installing-VanillaTD),
  [Installing VanillaRA](https://github.com/TheAssemblyArmada/Vanilla-Conquer/wiki/Installing-VanillaRA);
  ports: [Nintendo DSi](https://www.gamebrew.org/wiki/Vanilla_Conquer_-_Nintendo_DSi_port%E2%80%8B),
  [Amiga](https://arcziii.itch.io/vanilla-conquer-red-alert-amiga-port),
  [Switch (GBAtemp thread)](https://gbatemp.net/threads/c-c-vanilla-conquer.579074/)
- OpenRA: [repository](https://github.com/OpenRA/OpenRA) (content installer definitions in
  `mods/*-content/installer/`), [RA2 mod](https://github.com/OpenRA/ra2),
  [TiberianDawnHD](https://github.com/OpenRA/TiberianDawnHD)
- EA: [GitHub organisation](https://github.com/electronicarts) (TD, RA, Renegade, Generals and Zero
  Hour sources, Remastered DLLs, modding support);
  [TechPowerUp on the 2025 release](https://www.techpowerup.com/333300/ea-makes-command-conquer-source-code-public)
- Freeware: [C&C Gold freeware guide](https://cncnz.com/features/technical-support-help-guides/command-conquer-freeware-release-install-guide/),
  [Red Alert freeware guide](https://cncnz.com/features/technical-support-help-guides/red-alert-freeware-release-install-guide/),
  [Ultimate Collection on Steam](https://www.gamingonlinux.com/2024/03/command-conquer-the-ultimate-collection-launched-on-steam-and-other-classics/page=1/)
- PS5: [SharpProspero](https://github.com/SvenGDK/SharpProspero),
  [ps5-opengl](https://github.com/blackbearreloaded/ps5-opengl),
  [Yamagi Quake II](https://github.com/blackbearreloaded/ps5-yamagi-quake2),
  [PS5SX2](https://github.com/Swordpdf/PS5SX2), [RetroArch](https://github.com/mihawk-99/PS5_RetroArch),
  [ProsperoEden](https://github.com/blackbearreloaded/ProsperoEden),
  [PICO-8 loader](https://github.com/RafaelNGP/pico8-ps5),
  [Prospero Explorer](https://github.com/SvenGDK/Prospero-Explorer),
  [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus)
- Libraries: [libarchive formats](https://github.com/libarchive/libarchive/blob/master/libarchive/libarchive-formats.5)
  (ISO 9660 read as a stream), [unshield](https://github.com/twogood/unshield),
  [zlib contrib/blast](https://github.com/madler/zlib/tree/develop/contrib/blast)
- PlayStation controls of the originals: [PSX Data Center, Command & Conquer](https://psxdatacenter.com/games/U/C/SLUS-00379.html)
