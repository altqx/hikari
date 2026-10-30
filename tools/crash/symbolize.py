#!/usr/bin/env python3
# Windows modules need llvm-symbolizer; addr2line reads ELF only.

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

REPO_DEFAULT = "altqx/hikari"
RECORD_FRAME = re.compile(r"^frame ([A-Za-z0-9_.+-]{1,80}) 0x([0-9a-f]{1,16})(?: 0x[0-9a-f]{1,16} [0-9a-f-]{36,45})?$")
REPORT_FRAME = re.compile(r"^\s*#\s*(\d+)\s+([A-Za-z0-9_.+-]{1,80})\s+\+0x([0-9a-f]{1,16})\s*$")
VERSION = r"(\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.]+)?)"


def parse(text):
    """The frames [(module, offset)] and the version a report names."""
    text = text.lstrip("﻿")
    try:
        data = json.loads(text)
    except ValueError:
        data = None
    if isinstance(data, dict):
        props = data.get("properties", data)
        exc = (props.get("$exception_list") or [{}])[0]
        frames = [(f["module"], int(f["offset"], 16)) for f in exc.get("stacktrace", {}).get("frames", [])]
        return frames, props.get("crashed_version")
    frames, version = [], None
    for line in text.splitlines():
        m = RECORD_FRAME.match(line.strip()) or REPORT_FRAME.match(line)
        if m:
            module, offset = m.groups()[-2:]
            frames.append((module, int(offset, 16)))
            continue
        m = re.match(r"^version " + VERSION + r"$", line.strip())
        m = m or re.search(r"\) in " + VERSION + r"\s*$", line)
        m = m or re.search(r"\b\d+ s, " + VERSION + r"\s*$", line)
        if m and not version:
            version = m.group(1)
    return frames, version


def windows(frames):
    return any(m.lower().endswith((".exe", ".dll")) for m, _ in frames)


def download(version, platform, repo):
    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "hikari-symbols" / version / platform
    if cache.is_dir():
        return cache
    name = (f"HikariSub-{version}-windows-x64-symbols.zip" if platform == "windows"
            else f"hikarisub-symbols-{version}-linux-x86_64.tar.xz")
    cache.parent.mkdir(parents=True, exist_ok=True)
    partial = Path(tempfile.mkdtemp(prefix=f".{platform}-", dir=cache.parent))
    try:
        subprocess.run(["gh", "release", "download", f"v{version}", "--repo", repo, "--pattern", name,
                        "--dir", str(partial), "--clobber"], check=True)
        archive = partial / name
        if name.endswith(".zip"):
            with zipfile.ZipFile(archive) as z:
                for member in z.namelist():
                    if os.path.isabs(member) or ".." in Path(member).parts:
                        sys.exit(f"unsafe path in {name}: {member}")
                z.extractall(partial)
        else:
            with tarfile.open(archive) as t:
                t.extractall(partial, filter="data")
        archive.unlink()
        partial.rename(cache)
    finally:
        shutil.rmtree(partial, ignore_errors=True)
    return cache


def find(symbols, name):
    for path in Path(symbols).rglob(name):
        if path.is_file():
            return path
    return None


def first_load_vaddr(path):
    """Page-aligned vaddr of the first PT_LOAD; a load-base offset plus this is the symbol address."""
    with open(path, "rb") as f:
        head = f.read(64)
        if head[:4] != b"\x7fELF" or head[4] != 2:
            return 0
        endian = "<" if head[5] == 1 else ">"
        phoff, = struct.unpack_from(endian + "Q", head, 0x20)
        phentsize, phnum = struct.unpack_from(endian + "HH", head, 0x36)
        lowest = None
        for i in range(phnum):
            f.seek(phoff + i * phentsize)
            ptype, _, _, vaddr, _, _, _, align = struct.unpack(endian + "IIQQQQQQ", f.read(56))
            if ptype == 1:
                base = vaddr & ~((align or 1) - 1)
                lowest = base if lowest is None else min(lowest, base)
        return lowest or 0


def run(args):
    out = subprocess.run(args, capture_output=True, text=True)
    return out.stdout if out.returncode == 0 else ""


def lookup(tool, path, address, relative):
    """[(function, location)] for an address, innermost inlined first."""
    if tool == "llvm-symbolizer":
        args = [tool, f"--obj={path}", "--inlining", "--demangle", "--output-style=GNU"]
        if relative:
            args.append("--relative-address")
        lines = run(args + [hex(address)]).splitlines()
    else:
        lines = run([tool, "-f", "-C", "-i", "-e", str(path), hex(address)]).splitlines()
    pairs = [(lines[i], lines[i + 1]) for i in range(0, len(lines) - 1, 2)]
    return [(fn, loc) for fn, loc in pairs if fn not in ("??", "")]


def symbolize(frames, symbols, out):
    tool = shutil.which("llvm-symbolizer") or shutil.which("addr2line")
    if not tool:
        sys.exit("neither llvm-symbolizer nor addr2line is on PATH")
    # The name alone: Windows gives it with PATHEXT's case (llvm-symbolizer.EXE).
    tool = Path(tool).stem.lower() if tool.lower().endswith(".exe") else os.path.basename(tool)
    if windows(frames) and tool != "llvm-symbolizer":
        sys.exit("Windows symbols need llvm-symbolizer")
    resolved = 0
    for i, (module, offset) in enumerate(frames):
        # A return address points after its call: look the call up.
        target = offset if i == 0 or offset == 0 else offset - 1
        head = f"#{i:<2} {module:<24} +0x{offset:x}"
        found = []
        if module != "other":
            if module.lower().endswith((".exe", ".dll")):
                binary = find(symbols, module)
                if binary:
                    found = lookup(tool, binary, target, relative=True)
            else:
                debug = find(symbols, module + ".debug") or find(symbols, module)
                if debug:
                    found = lookup(tool, debug, target + first_load_vaddr(debug), relative=False)
        if not found:
            print(f"{head}  ?", file=out)
            continue
        resolved += 1
        for j, (fn, loc) in enumerate(found):
            print(f"{head if j == 0 else ' ' * len(head)}  {fn}  {loc}" + ("" if j == 0 else "  (inlined)"), file=out)
    return resolved


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("report")
    ap.add_argument("--symbols", help="folder with the symbols (default: download the release's)")
    ap.add_argument("--version", help="the version that crashed (default: read from the report)")
    ap.add_argument("--repo", default=REPO_DEFAULT)
    a = ap.parse_args()
    frames, version = parse(Path(a.report).read_text(encoding="utf-8", errors="replace"))
    version = a.version or version
    if not frames:
        sys.exit("no frames in the report")
    symbols = a.symbols
    if not symbols:
        if not version:
            sys.exit("no version in the report; give --version")
        symbols = download(version, "windows" if windows(frames) else "linux", a.repo)
    print(f"HikariSub {version or '(version unknown)'}, {len(frames)} frames")
    resolved = symbolize(frames, symbols, sys.stdout)
    print(f"{resolved} of {len(frames)} frames resolved")
    return 0 if resolved else 1


if __name__ == "__main__":
    sys.exit(main())
