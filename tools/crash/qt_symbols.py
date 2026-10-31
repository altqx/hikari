#!/usr/bin/env python3
"""Collects Qt's debug info for the Qt modules a package ships, for posthog_symbols.py.

qt_symbols.py PLATFORM PACKAGE OUT [--cache DIR]

PLATFORM is linux or windows, PACKAGE the unpacked tar.gz or zip. Downloads the debug-symbol archives that
qt-symbols.lock.json pins from Qt's online repository, and puts in OUT each shipped module's debug file:
the ELF .debug with the module's build-id, or the module beside the PDB with the module's GUID. Needs 7z.
"""

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

import posthog_symbols

HERE = Path(__file__).resolve().parent
LOCK = HERE / "qt-symbols.lock.json"
QT_LOCKS = HERE.parent.parent / "cmake" / "locks"
PDB_MAGIC = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0"


def pdb_guid(data):
    """The GUID in an MSF 7 PDB's info stream, or None."""
    if not data.startswith(PDB_MAGIC):
        return None
    block, _, _, directory_size, _, map_at = struct.unpack_from("<6I", data, len(PDB_MAGIC))
    count = -(-directory_size // block)
    directory = b"".join(data[at * block:(at + 1) * block]
                         for at in struct.unpack_from(f"<{count}I", data, map_at * block))
    streams, = struct.unpack_from("<I", directory)
    if streams < 2:
        return None
    sizes = struct.unpack_from(f"<{streams}I", directory, 4)
    first_blocks = 0 if sizes[0] == 0xFFFFFFFF else -(-sizes[0] // block)
    info, = struct.unpack_from("<I", directory, 4 + 4 * streams + 4 * first_blocks)
    return bytes(data[info * block + 12:info * block + 28])


def elf_debug_name(module):
    """Qt's name for a library's debug file: libQt6Core.so.6.12.0 is Qt6Core.debug."""
    name = module.name.removeprefix("lib")
    return name[:name.index(".so")] + ".debug" if ".so" in name else None


def shipped(package, platform):
    """{debug file name in Qt's archives: [(module, its debug id or PDB GUID)]} for the package's modules."""
    found = {}
    for path in sorted(package.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        if platform == "windows":
            if path.suffix.lower() != ".dll":
                continue
            codeview = posthog_symbols.pe_codeview(path.read_bytes())
            if codeview:
                found.setdefault(codeview[2].lower(), []).append((path, codeview[0]))
            continue
        name = elf_debug_name(path)
        if not name:
            continue
        with path.open("rb") as file:
            if file.read(4) != b"\x7fELF":
                continue
        ident = posthog_symbols.elf_debug_id(path.read_bytes())
        if ident:
            found.setdefault(name.lower(), []).append((path, ident))
    return found


def fetch(entry, cache):
    """The archive in cache, downloaded unless it is there with the pinned length and SHA-256."""
    path = cache / entry["url"].rsplit("/", 1)[-1]

    def pinned():
        if not path.is_file() or path.stat().st_size != entry["length"]:
            return False
        digest = hashlib.sha256()
        with path.open("rb") as file:
            for chunk in iter(lambda: file.read(1 << 20), b""):
                digest.update(chunk)
        return digest.hexdigest() == entry["sha256"]

    if not pinned():
        with urllib.request.urlopen(entry["url"], timeout=600) as response, path.open("wb") as file:
            shutil.copyfileobj(response, file, 1 << 20)
        if not pinned():
            path.unlink()
            raise OSError(f"{entry['url']} is not the pinned archive")
    return path


def collect(platform, package, out, cache):
    """Puts each shipped Qt module's debug file in out; returns how many."""
    lock = json.loads(LOCK.read_text())
    qt = json.loads((QT_LOCKS / f"qt-{platform}.lock.json").read_text())["qt_version"]
    if lock["qt_version"] != qt:
        raise OSError(f"{LOCK.name} is for Qt {lock['qt_version']}, the build uses {qt}")
    modules = shipped(package, platform)
    collected = 0
    for entry in lock["platforms"][platform]:
        archive = fetch(entry, cache)
        with tempfile.TemporaryDirectory() as scratch:
            subprocess.run(["7z", "x", "-y", "-ssc-", f"-o{scratch}", str(archive), "-r", *modules],
                           stdout=subprocess.DEVNULL, check=True)
            for debug in sorted(Path(scratch).rglob("*")):
                if debug.is_file():
                    collected += place(debug, modules.get(debug.name.lower(), []), package, out, platform)
    return collected


def place(debug, candidates, package, out, platform):
    """Copies the debug file to out beside or after the module it matches; 1 if one matched, else 0."""
    data = debug.read_bytes()
    own = pdb_guid(data) if platform == "windows" else posthog_symbols.elf_debug_id(data)
    for module, ident in candidates:
        if ident != own:
            continue
        target = out / module.relative_to(package)
        target.parent.mkdir(parents=True, exist_ok=True)
        if platform == "windows":
            shutil.copyfile(module, target)
            shutil.copyfile(debug, target.with_suffix(".pdb"))
        else:
            shutil.copyfile(debug, target.with_name(target.name + ".debug"))
        return 1
    return 0


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("platform", choices=("linux", "windows"))
    parser.add_argument("package", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--cache", type=Path, help="keep the downloaded archives here")
    args = parser.parse_args(argv)
    if not shutil.which("7z"):
        print("::warning::7z is needed")
        return 1
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as scratch:
        cache = args.cache or Path(scratch)
        cache.mkdir(parents=True, exist_ok=True)
        try:
            collected = collect(args.platform, args.package, args.out, cache)
        except (OSError, subprocess.CalledProcessError) as error:
            print(f"::warning::Qt symbols for {args.platform}: {error}")
            return 1
    print(f"Qt symbols for {args.platform}: {collected} modules")
    return 0 if collected else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
