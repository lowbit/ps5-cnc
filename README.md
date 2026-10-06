<p align="center"><img src="sce_sys/icon0.png" width="160" alt="PS5 Native RA icon"></p>

# PS5 Native RA

Westwood's Red Alert (1996) as a native PS5 title: its own home-screen icon, launched like a game,
running [Vanilla Conquer](https://github.com/TheAssemblyArmada/Vanilla-Conquer), the maintained
engine built from the source Electronic Arts released, with the DualSense as the pointer. Bring the
game files: the two discs EA released as freeware in 2008 hold the whole base game, both campaigns,
movies and music; Counterstrike and Aftermath come from your own copy.

**Status (2026-10-06): the title builds and packages; it has not run on a console yet.** The
launcher that gets the game files (below) is tested on a PC with stand-ins for every kind of
source; the console test comes next ([DEVELOPMENT.md](DEVELOPMENT.md)).

## Requirements

- A jailbroken PS5 with kstuff (fake-signed executables) and
  [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus).
- A network connection for the free download, sending files or links; or a USB drive.

## Install

1. Build `dist/PPSA99096` ([DEVELOPMENT.md](DEVELOPMENT.md)); there is no release yet.
2. Copy the `PPSA99096` folder to `/data/homebrew/` (FTP, ps5upload or USB).
3. ShadowMountPlus registers it within a few seconds and **PS5 Native RA** appears on the home
   screen.

## Getting the game files

PS5 Native RA starts on a screen that shows which of Red Alert's files are installed and gets the
missing ones. Nothing of the game is in the package.

| Choose | What happens |
| --- | --- |
| **Download the free Red Alert** | In 2008 Electronic Arts made Red Alert free to download, as the Allied and Soviet discs (`RedAlert1_AlliedDisc.rar`, `RedAlert1_SovietDisc.rar`, about 500 MB each). The title downloads them from the sources in [`data/freeware.txt`](data/freeware.txt) (read from this repository at the time, so they can change without a new build; by default the Internet Archive's copy of EA's own download) and keeps the game files, about 1 GB: both campaigns, every movie and all the music. |
| **Send files from a PC or phone** | Shows an address and a QR code. Open it in a browser on the same network and drop your disc images, archives or game folder on the page; the console adds them as they arrive and the page shows what it did. |
| **Download from a link** | Any `http://` or `https://` link to a disc image, an archive or one of the game's files, or to a folder a PC shares over HTTP (its list of files; `python -m http.server` in the folder will do). |
| **Import from a USB drive** | Pick a file or folder on a USB drive (exFAT or FAT32). A title sees USB drives with ShadowMountPlus 1.7 or later. |

Disc images, archives and folders copied over FTP into `/data/homebrew/PPSA99096/import/` are
added the next time the title starts.

What the importer reads, in any combination and nested in each other (a RAR holding an ISO, a ZIP
of a folder of images ...):

| You have | Send |
| --- | --- |
| EA's 2008 freeware | The two RAR files, or the disc images in them |
| The original discs (Allied, Soviet, Counterstrike, Aftermath) | Their images: `.iso`, `.bin` (of a `.cue`), `.img`, `.mdf`, raw or not |
| The Ultimate Collection (EA app, Steam) | The Red Alert folder (`REDALERT.MIX`, `MAIN1.MIX` to `MAIN4.MIX`, `EXPAND*.MIX` ...) |
| The Remastered Collection (Steam) | Its `Data` folder; it has no Soviet disc |
| The First Decade | The DVD's image or files (its `data1.hdr` and `data*.cab`), or the installed Red Alert folder |
| A Red Alert installed from the discs | Its folder |
| The demo | Its archive |

Archives can be ZIP, 7Z or RAR (RAR 2.9 as EA's, and RAR 5). The importer tells the discs apart by
their checksums (from OpenRA's installer), the names of the folders and images they came in, and
the discs' own files, and keeps a known copy over an unknown one. The free discs give the whole base
game; the expansions come from your own copy:

- **Aftermath**: its disc gives everything (the missions and units are cut out of the installer's
  `PATCH.RTP`), as does any installed copy.
- **Counterstrike**: its disc gives the music; its missions are packed inside its installer, so
  send `EXPAND.MIX` from an installed copy: a Red Alert installed with Counterstrike, or The Ultimate
  Collection.

## Controls

Vanilla Conquer's own controller support for now; a layer made for the DualSense comes later.

| Input | Does |
| --- | --- |
| Left stick | The pointer |
| R2 | A faster pointer |
| Right stick | Scroll the map |
| Cross / Circle | Left click / right click |
| Square / Triangle | Guard (G) / formation (F) |
| D-pad | Teams 1 to 4 |
| L1 / R1 | Ctrl / Alt (force fire, force move) |
| Options | Escape: the menu, skipping a movie |

Settings and saves stay on the console (`/download0/ra`). On the launcher: the D-pad or left stick
moves, Cross selects, Circle goes back.

## Building and internals

See [DEVELOPMENT.md](DEVELOPMENT.md) for the build, the changes to Vanilla Conquer and the plan, and
[RESEARCH.md](RESEARCH.md) for why Vanilla Conquer and where the game data comes from. Licenses and
credits: [LICENSE](LICENSE) (GPL v3) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Command & Conquer and Red Alert are trademarks of Electronic Arts; this project is not affiliated
with EA.
