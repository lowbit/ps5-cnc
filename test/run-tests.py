#!/usr/bin/env python3
"""The importer's tests on a PC (tools/test-import.sh runs them): makes small stand-ins for Red
Alert's discs, archives and folders, runs build/test/import-test on each, and checks what ends up
in the game folder. The stand-ins' checksums are added to the importer's table of known files with
--known, as the real files' cannot be had here. Needs genisoimage and 7z, and rar for the RAR5
cases (RAR 2.9 archives, the format of EA's files, are written here)."""
import hashlib
import http.server
import os
import random
import shutil
import socketserver
import struct
import subprocess
import sys
import tempfile
import threading
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / "build" / "test" / "import-test"
AFTERMATH_RANGES = [("expand2.mix", 4712984, 469922), ("hires1.mix", 5182981, 90264), ("lores1.mix", 5273320, 57076)]


def blob(seed, size):
    return random.Random(seed).randbytes(size)


REDALERT = blob(1, 300_000)
ALLIED = blob(2, 500_000)
SOVIET = blob(3, 520_000)
COUNTERSTRIKE = blob(4, 200_000)
AFTERMATH = blob(5, 210_000)
EXPAND = blob(6, 50_000)
EXPAND2 = blob(11, 60_000)
HIRES1 = blob(12, 30_000)
LORES1 = blob(13, 20_000)
PATCH_RTP = blob(7, 5_400_000)
CSTRIKE_RTP = blob(8, 100_000)
README_CS = b"Command & Conquer Red Alert: Counterstrike readme stand-in\r\n"
README_AM = b"Command & Conquer Red Alert: Aftermath readme stand-in\r\n"
DEMO_REDALERT = blob(9, 100_000)
DEMO_MAIN = blob(10, 80_000)
COMBINED_MAIN = blob(14, 700_000)


def sha1(data):
    return hashlib.sha1(data).hexdigest()


KNOWN = [
    ("redalert", sha1(REDALERT)),
    ("allied", sha1(ALLIED[:4096])),
    ("soviet", sha1(SOVIET[:4096])),
    ("aftermath-patch", sha1(PATCH_RTP)),
    ("counterstrike-readme", sha1(README_CS)),
    ("aftermath-readme", sha1(README_AM)),
]


def write_tree(folder, files):
    for name, data in files.items():
        path = folder / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def make_iso(work, name, files, joliet=True):
    tree = work / (name + ".tree")
    shutil.rmtree(tree, ignore_errors=True)
    write_tree(tree, files)
    out = work / name
    options = ["-J", "-r"] if joliet else []
    subprocess.run(["genisoimage", "-quiet", "-V", "RA", *options, "-o", str(out), str(tree)], check=True)
    shutil.rmtree(tree)
    return out


def make_bin(iso, out, mode=1, sector_size=2352):
    """A raw image of the ISO's sectors, as CD rippers write them."""
    data = iso.read_bytes()
    sync = b"\x00" + b"\xff" * 10 + b"\x00"
    with open(out, "wb") as target:
        for index in range(len(data) // 2048):
            lba = index + 150
            header = bytes([lba // 4500 // 16 * 6 + lba // 4500, 0, 0, mode])
            if mode == 1:
                sector = sync + header + data[index * 2048:(index + 1) * 2048] + bytes(288)
            else:
                sector = sync + header + bytes(8) + data[index * 2048:(index + 1) * 2048] + bytes(280)
            target.write(sector + bytes(sector_size - 2352))
    return out


def make_rar(work, name, inputs, version):
    """RAR5 with the rar tool; RAR 2.9 (the format of EA's files) written here with stored entries,
    as rar 7 writes no RAR4 any more."""
    out = work / name
    out.unlink(missing_ok=True)
    if version == 5:
        subprocess.run(["rar", "a", "-idq", "-ep1", "-ma5", str(out), *map(str, inputs)], check=True)
        return out

    def block(kind, flags, fields):
        body = struct.pack("<BHH", kind, flags, 7 + len(fields)) + fields
        return struct.pack("<H", zlib.crc32(body) & 0xFFFF) + body

    with open(out, "wb") as target:
        target.write(b"Rar!\x1a\x07\x00")
        target.write(block(0x73, 0, bytes(6)))
        for path in map(Path, inputs):
            data = path.read_bytes()
            name_bytes = path.name.encode()
            fields = struct.pack("<IIBIIBBHI", len(data), len(data), 2, zlib.crc32(data), 0x5A000000, 29, 0x30,
                                 len(name_bytes), 0x20) + name_bytes
            target.write(block(0x74, 0x8000, fields))
            target.write(data)
        target.write(block(0x7B, 0x4000, b""))
    return out


def make_7z(work, name, inputs):
    out = work / name
    out.unlink(missing_ok=True)
    subprocess.run(["7z", "a", "-bd", "-bso0", str(out), *map(str, inputs)], check=True)
    return out


def make_zip(work, name, files):
    out = work / name
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as archive:
        for inner, data in files.items():
            archive.writestr(inner, data)
    return out


class RangeHandler(http.server.SimpleHTTPRequestHandler):
    """Serves the work folder, answering Range requests the way download servers do."""

    def log_message(self, *args):
        pass

    def do_GET(self):
        path = Path(self.translate_path(self.path))
        if path.is_dir() or not path.exists():
            return super().do_GET()
        data = path.read_bytes()
        start = 0
        header = self.headers.get("Range")
        if header and header.startswith("bytes=") and not getattr(self.server, "no_ranges", False):
            start = int(header[6:].split("-")[0])
            self.send_response(206)
            self.send_header("Content-Range", f"bytes {start}-{len(data) - 1}/{len(data)}")
        else:
            self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        if not getattr(self.server, "no_ranges", False):
            self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(len(data) - start))
        self.end_headers()
        self.server.requests.append((self.path, start))
        try:
            self.wfile.write(data[start:])
        except (BrokenPipeError, ConnectionResetError):
            pass


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def serve(folder):
    handler = lambda *args, **kwargs: RangeHandler(*args, directory=str(folder), **kwargs)
    server = Server(("127.0.0.1", 0), handler)
    server.requests = []
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


class Result:
    def __init__(self, output):
        self.output = output
        self.notes = [line[5:] for line in output.splitlines() if line.startswith("note ")]
        self.state = next(line[6:] for line in output.splitlines() if line.startswith("state "))
        self.placed = next(line[7:] for line in output.splitlines() if line.startswith("placed")).split()
        self.present = next(line[8:] for line in output.splitlines() if line.startswith("present")).split()
        self.links = [line[5:] for line in output.splitlines() if line.startswith("link ")]


def run(game, *sources, remove=False, known=True, extra=(), env=None):
    command = [str(BINARY)]
    if remove:
        command.append("--remove")
    if known:
        for kind, digest in KNOWN:
            command += ["--known", kind, digest]
    command += [*extra, str(game), *map(str, sources)]
    # The demo's main.mix is told by its size; the stand-ins are smaller than the real files.
    environment = dict(os.environ, ASAN_OPTIONS="detect_leaks=1", RA_DEMO_MAIN_LIMIT="100000", **(env or {}))
    done = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=300)
    if done.returncode != 0:
        raise AssertionError(f"import-test failed ({done.returncode}):\n{done.stdout}\n{done.stderr}")
    return Result(done.stdout)


failures = []


def check(label, condition, result=None):
    if condition:
        print(f"  ok   {label}")
    else:
        print(f"  FAIL {label}")
        if result is not None:
            print("       " + "\n       ".join(result.output.splitlines()))
        failures.append(label)


def same(game, relative, data):
    path = game / relative
    return path.exists() and path.read_bytes() == data


def staging_clean(game):
    return not (game / ".import").exists() or not any((game / ".import").iterdir())


def main():
    if not BINARY.exists():
        sys.exit(f"{BINARY} is missing: build it with tools/test-import.sh")
    work = Path(tempfile.mkdtemp(prefix="ra-import-"))
    games = work / "games"
    games.mkdir()
    counter = iter(range(1000))

    def new_game():
        game = games / f"game{next(counter)}"
        game.mkdir()
        return game

    allied_files = {"MAIN.MIX": ALLIED, "INSTALL/REDALERT.MIX": REDALERT, "README.TXT": b"Red Alert readme\r\n",
                    "SETUP/SETUP.EXE": blob(20, 5000)}
    soviet_files = {"MAIN.MIX": SOVIET, "INSTALL/REDALERT.MIX": REDALERT, "README.TXT": b"Red Alert readme\r\n"}
    allied_iso = make_iso(work, "RedAlert1_AlliedDisc.iso", allied_files)
    soviet_iso = make_iso(work, "RedAlert1_SovietDisc.iso", soviet_files)
    plain_iso = make_iso(work, "CD1.ISO", allied_files, joliet=False)
    aftermath_iso = make_iso(work, "disc4.iso", {"MAIN.MIX": AFTERMATH, "README.TXT": README_AM,
                                                  "SETUP/INSTALL/PATCH.RTP": PATCH_RTP})
    counterstrike_iso = make_iso(work, "disc3.iso", {"MAIN.MIX": COUNTERSTRIKE, "README.TXT": README_CS,
                                                      "SETUP/INSTALL/CSTRIKE.RTP": CSTRIKE_RTP})

    print("Disc images")
    game = new_game()
    result = run(game, allied_iso)
    check("an ISO gives redalert.mix and allied/main.mix",
          result.state == "done" and same(game, "redalert.mix", REDALERT) and same(game, "allied/main.mix", ALLIED),
          result)
    check("the staging folder is cleared", staging_clean(game), result)

    game = new_game()
    result = run(game, plain_iso)
    check("a plain ISO 9660 image (no Joliet or Rock Ridge)", same(game, "allied/main.mix", ALLIED), result)

    for mode, size in ((1, 2352), (2, 2352), (1, 2448)):
        game = new_game()
        image = make_bin(soviet_iso, work / f"soviet-{mode}-{size}.bin", mode, size)
        result = run(game, image)
        check(f"a raw BIN image, mode {mode}, {size}-byte sectors",
              same(game, "soviet/main.mix", SOVIET) and same(game, "redalert.mix", REDALERT), result)

    print("Archives")
    for version in (4, 5) if shutil.which("rar") else (4,):
        game = new_game()
        archive = make_rar(work, f"RedAlert1_SovietDisc-{version}.rar", [soviet_iso], version)
        result = run(game, archive)
        check(f"RAR{version} holding the disc image (as EA gave it away)",
              same(game, "soviet/main.mix", SOVIET) and same(game, "redalert.mix", REDALERT), result)

    game = new_game()
    sample = ROOT / ".deps/src/libarchive-3.8.9/libarchive/test/test_read_format_rar.rar.uu"
    if sample.exists():
        import binascii
        lines = sample.read_text().splitlines()
        data = b"".join(binascii.a2b_uu(line) for line in lines[1:] if line and line != "end")
        (work / "sample.rar").write_bytes(data)
        result = run(game, work / "sample.rar")
        check("a compressed RAR 2.9 archive (libarchive's sample) reads to its end",
              any("Found no Red Alert files" in text for text in result.notes) and result.state == "done", result)

    game = new_game()
    archive = make_7z(work, "allied.7z", [allied_iso])
    result = run(game, archive)
    check("7z holding the disc image", same(game, "allied/main.mix", ALLIED), result)

    game = new_game()
    nested = make_zip(work, "nested.zip", {"discs/allied.7z": archive.read_bytes()})
    result = run(game, nested)
    check("ZIP holding a 7z holding the disc image", same(game, "allied/main.mix", ALLIED), result)
    check("its 7z copy is gone", staging_clean(game), result)

    game = new_game()
    rar_in_zip = make_zip(work, "rar-in-zip.zip", {"RedAlert1_SovietDisc.rar":
                                                    make_rar(work, "inner.rar", [soviet_iso], 4).read_bytes()})
    result = run(game, rar_in_zip)
    check("ZIP holding a RAR holding the disc image", same(game, "soviet/main.mix", SOVIET), result)

    game = new_game()
    tuc = make_zip(work, "tuc.zip", {"Red Alert/REDALERT.MIX": REDALERT, "Red Alert/MAIN1.MIX": ALLIED,
                                      "Red Alert/MAIN2.MIX": SOVIET, "Red Alert/MAIN3.MIX": COUNTERSTRIKE,
                                      "Red Alert/MAIN4.MIX": AFTERMATH, "Red Alert/EXPAND.MIX": EXPAND,
                                      "Red Alert/EXPAND2.MIX": EXPAND2, "Red Alert/HIRES1.MIX": HIRES1,
                                      "Red Alert/LORES1.MIX": LORES1, "Red Alert/ra95.exe": blob(21, 1000)})
    result = run(game, tuc)
    check("The Ultimate Collection's folder in a ZIP fills every slot",
          all(same(game, path, data) for path, data in (
              ("redalert.mix", REDALERT), ("allied/main.mix", ALLIED), ("soviet/main.mix", SOVIET),
              ("counterstrike/main.mix", COUNTERSTRIKE), ("aftermath/main.mix", AFTERMATH),
              ("expand.mix", EXPAND), ("expand2.mix", EXPAND2), ("hires1.mix", HIRES1), ("lores1.mix", LORES1))),
          result)

    print("Expansion discs")
    game = new_game()
    result = run(game, aftermath_iso)
    check("Aftermath disc: its MAIN.MIX by its README.TXT",
          same(game, "aftermath/main.mix", AFTERMATH), result)
    check("Aftermath disc: its three files cut out of PATCH.RTP",
          all(same(game, name, PATCH_RTP[offset:offset + length]) for name, offset, length in AFTERMATH_RANGES),
          result)

    game = new_game()
    result = run(game, counterstrike_iso)
    check("Counterstrike disc: its MAIN.MIX by its README.TXT",
          same(game, "counterstrike/main.mix", COUNTERSTRIKE), result)
    check("Counterstrike disc: a note asks for EXPAND.MIX",
          any("EXPAND.MIX" in text for text in result.notes), result)

    game = new_game()
    unknown_aftermath = make_iso(work, "expansion.iso", {"MAIN.MIX": AFTERMATH, "SETUP/INSTALL/PATCH.RTP": PATCH_RTP})
    result = run(game, unknown_aftermath)
    check("an expansion disc told by its PATCH.RTP alone", same(game, "aftermath/main.mix", AFTERMATH), result)

    print("Folders")
    remastered = work / "Remastered"
    write_tree(remastered, {"Data/CNCDATA/RED_ALERT/CD1/REDALERT.MIX": REDALERT,
                            "Data/CNCDATA/RED_ALERT/CD1/MAIN.MIX": ALLIED,
                            "Data/CNCDATA/RED_ALERT/COUNTERSTRIKE/MAIN.MIX": COUNTERSTRIKE,
                            "Data/CNCDATA/RED_ALERT/COUNTERSTRIKE/EXPAND.MIX": EXPAND,
                            "Data/CNCDATA/RED_ALERT/AFTERMATH/MAIN.MIX": AFTERMATH,
                            "Data/CNCDATA/RED_ALERT/AFTERMATH/EXPAND2.MIX": EXPAND2,
                            "Data/CNCDATA/RED_ALERT/AFTERMATH/HIRES1.MIX": HIRES1,
                            "Data/CNCDATA/RED_ALERT/AFTERMATH/LORES1.MIX": LORES1,
                            "Data/CNCDATA/TIBERIAN_DAWN/CD1/CONQUER.MIX": blob(22, 1000)})
    game = new_game()
    result = run(game, remastered, known=False)
    check("the Remastered Collection's folder, discs named by their folders",
          same(game, "allied/main.mix", ALLIED) and same(game, "counterstrike/main.mix", COUNTERSTRIKE) and
          same(game, "aftermath/main.mix", AFTERMATH) and same(game, "expand.mix", EXPAND), result)
    check("a folder that is not the incoming one is left as it was",
          (remastered / "Data/CNCDATA/RED_ALERT/CD1/MAIN.MIX").exists(), result)

    incoming = work / "incoming"
    write_tree(incoming, {"Counterstrike & Aftermath/MAIN.MIX": COMBINED_MAIN,
                          "Counterstrike & Aftermath/REDALERT.MIX": REDALERT,
                          "discs/RedAlert1_AlliedDisc.iso": allied_iso.read_bytes(),
                          "README.TXT": b"some readme\r\n"})
    game = new_game()
    result = run(game, incoming, remove=True)
    check("a folder named for two discs says nothing: its MAIN.MIX holds every disc",
          same(game, "main.mix", COMBINED_MAIN), result)
    check("the disc image beside it is read too", same(game, "allied/main.mix", ALLIED), result)
    check("the incoming folder is emptied", not any(p.is_file() for p in incoming.rglob("*")), result)

    print("The demo, and upgrading from it")
    demo = make_zip(work, "ra95demo.zip", {"RA95DEMO/INSTALL/REDALERT.MIX": DEMO_REDALERT,
                                            "RA95DEMO/MAIN.MIX": DEMO_MAIN})
    game = new_game()
    result = run(game, demo)
    check("the demo fills redalert.mix and main.mix",
          same(game, "redalert.mix", DEMO_REDALERT) and same(game, "main.mix", DEMO_MAIN), result)
    result = run(game, allied_iso)
    check("the full game replaces the demo's redalert.mix", same(game, "redalert.mix", REDALERT), result)
    check("and removes the demo's main.mix", not (game / "main.mix").exists(), result)
    result = run(game, demo)
    check("the demo afterwards does not replace the known redalert.mix", same(game, "redalert.mix", REDALERT),
          result)
    check("nor adds its main.mix back", not (game / "main.mix").exists(), result)

    game = new_game()
    (game / "REDALERT.MIX").write_bytes(DEMO_REDALERT)
    result = run(game, allied_iso)
    check("a file already there in another case is replaced, not doubled",
          same(game, "redalert.mix", REDALERT) and not (game / "REDALERT.MIX").exists(), result)

    print("Downloads")
    server = serve(work)
    base = f"http://127.0.0.1:{server.server_address[1]}"
    game = new_game()
    rar4 = make_rar(work, "RedAlert1_AlliedDisc.rar", [allied_iso], 4)
    result = run(game, f"{base}/{rar4.name}")
    check("a link to the RAR", same(game, "allied/main.mix", ALLIED), result)

    game = new_game()
    server.requests.clear()
    big = make_iso(work, "big.iso", {"AAA/FILLER.DAT": blob(30, 12_000_000), "MAIN.MIX": SOVIET,
                                     "INSTALL/REDALERT.MIX": REDALERT})
    result = run(game, f"{base}/{big.name}")
    check("a link to an ISO", same(game, "soviet/main.mix", SOVIET), result)
    check("skips what it does not need with ranges", any(start > 0 for _, start in server.requests), result)

    game = new_game()
    server.no_ranges = True
    result = run(game, f"{base}/{big.name}")
    server.no_ranges = False
    check("a server without ranges still works", same(game, "soviet/main.mix", SOVIET), result)

    game = new_game()
    server.no_ranges = True
    server.requests.clear()
    result = run(game, f"{base}/{rar4.name}")
    check("a RAR from a server without ranges is downloaded once",
          same(game, "allied/main.mix", ALLIED) and len(server.requests) == 1, result)
    game = new_game()
    result = run(game, f"{base}/{tuc.name}")
    server.no_ranges = False
    check("a ZIP from a server without ranges is read in order", same(game, "aftermath/main.mix", AFTERMATH), result)

    game = new_game()
    server.requests.clear()
    result = run(game, f"{base}/{tuc.name}")
    check("a ZIP from a server with ranges", same(game, "aftermath/main.mix", AFTERMATH), result)

    game = new_game()
    result = run(game, f"{base}/")
    check("a folder listing lists the files worth importing",
          result.state == "listing" and any(link.startswith("RedAlert1_AlliedDisc.rar ") for link in result.links)
          and not any(link.startswith("big.iso.tree") for link in result.links), result)

    (work / "freeware.txt").write_text(
        f"# test list\nallied {base}/missing.rar\nallied {base}/RedAlert1_AlliedDisc.rar\n"
        f"soviet {base}/{make_rar(work, 'RedAlert1_SovietDisc.rar', [soviet_iso], 4).name}\n")
    game = new_game()
    result = run(game, "freeware:allied", "freeware:soviet", env={"RA_FREEWARE_LIST": f"{base}/freeware.txt"})
    check("the free discs, from the list, past a missing source",
          same(game, "allied/main.mix", ALLIED) and same(game, "soviet/main.mix", SOVIET) and result.state == "done",
          result)

    game = new_game()
    result = run(game, f"{base}/missing.rar")
    check("a broken link fails with a note", result.state == "failed" and result.notes, result)

    print("Failures")
    game = new_game()
    truncated = work / "truncated.rar"
    truncated.write_bytes(rar4.read_bytes()[:400_000])
    result = run(game, truncated)
    check("a cut-off RAR fails and places nothing", result.state == "failed" and not result.placed, result)
    check("its staging is cleared", staging_clean(game), result)

    game = new_game()
    nothing = make_zip(work, "nothing.zip", {"readme.txt": b"hello", "setup.exe": blob(23, 100)})
    result = run(game, nothing)
    check("an archive without game files says so", any("Found no Red Alert files" in text for text in result.notes),
          result)

    game = new_game()
    result = run(game, rar4, extra=["--cancel-after", "0"])
    check("a cancelled import places nothing", result.state == "cancelled" and not result.placed, result)

    game = new_game()
    tfd = make_iso(work, "tfd.iso", {"DATA1.HDR": blob(24, 4000), "DATA1.CAB": blob(25, 9000)})
    result = run(game, tfd)
    check("broken InstallShield cabinets fail with a note", result.state == "failed" and result.notes, result)

    server.shutdown()
    shutil.rmtree(work)
    print(f"{len(failures)} failure(s)" if failures else "all passed")
    sys.exit(1 if failures else 0)


main()
