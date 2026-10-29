#!/usr/bin/env python3
"""Collects the AppImage's `full` profile extras.

    collect-runtime.py <app tree> <out lib dir> <host-libs.txt> [<host-side list>]

Copies into <out lib dir>, under its soname, every shared library the tree's ELF files need (transitively),
unless the tree provides it, it matches host-libs.txt, or a host-list library needs it (that stays host-side).
Resolves like the build host's loader (`ldconfig -p`), so run it in the build container; exits 1 when a needed
library is missing. The optional fourth argument receives the host-side sonames, for the package tests.
"""
import fnmatch
import os
import re
import shutil
import subprocess
import sys

ELF_MAGIC = b"\x7fELF"

# Qt's OpenSSL backend dlopens libssl: a bundled libcrypto (Kerberos needs it on the floor) needs its own libssl too.
DLOPEN_COMPANIONS = {"libcrypto.so.3": ("libssl.so.3",)}


def is_elf(path):
    try:
        with open(path, "rb") as f:
            return f.read(4) == ELF_MAGIC
    except OSError:
        return False


def dynamic(path):
    """(needed sonames, own soname) of an ELF file."""
    out = subprocess.run(["readelf", "-dW", path], capture_output=True, text=True, check=False).stdout
    needed = re.findall(r"\(NEEDED\)\s+Shared library: \[([^\]]+)\]", out)
    soname = re.findall(r"\(SONAME\)\s+Library soname: \[([^\]]+)\]", out)
    return needed, (soname[0] if soname else None)


def ldconfig_map():
    out = subprocess.run(["/sbin/ldconfig", "-p"], capture_output=True, text=True, check=True).stdout
    found = {}
    for line in out.splitlines():
        m = re.match(r"\s*(\S+)\s+\(([^)]*)\)\s+=>\s+(\S+)", line)
        if m and "x86-64" in m.group(2):
            found.setdefault(m.group(1), m.group(3))
    return found


def host_globs(path):
    globs = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                globs.append(line)
    return globs


def main():
    if len(sys.argv) not in (4, 5):
        print(__doc__, file=sys.stderr)
        return 2
    tree, out_dir, globs_file = sys.argv[1:4]
    host_side_file = sys.argv[4] if len(sys.argv) == 5 else None
    globs = host_globs(globs_file)
    system = ldconfig_map()

    def on_host_list(name):
        return any(fnmatch.fnmatchcase(name, g) for g in globs)

    provided = set()
    queue = []
    for root, _dirs, files in os.walk(tree):
        for name in files:
            path = os.path.join(root, name)
            if ".so" in name:
                provided.add(name)
            if not os.path.islink(path) and is_elf(path):
                queue.append(path)
    for path in queue:
        _needed, soname = dynamic(path)
        if soname:
            provided.add(soname)

    host_side = set()

    def add_host_closure(name):
        if name in host_side:
            return
        host_side.add(name)
        path = system.get(name)
        if not path:
            return
        out = subprocess.run(["ldd", path], capture_output=True, text=True, check=False).stdout
        for dep in re.findall(r"^\s*(\S+\.so[^\s]*)\s+=>", out, re.M):
            host_side.add(dep)

    collected = {}  # soname -> source path
    missing = set()
    seen = set()
    while queue:
        path = queue.pop()
        if path in seen:
            continue
        seen.add(path)
        needed, _soname = dynamic(path)
        for name in needed:
            if name in provided or name in collected:
                continue
            if on_host_list(name):
                add_host_closure(name)
                continue
            source = system.get(name)
            if not source:
                missing.add(name)
                continue
            collected[name] = source
            queue.append(source)
            # Libraries loaded with dlopen that must match a collected one.
            for companion in DLOPEN_COMPANIONS.get(name, ()):
                if companion not in collected and companion in system and not on_host_list(companion):
                    collected[companion] = system[companion]
                    queue.append(system[companion])

    for name in list(collected):
        if name in host_side:
            del collected[name]
    if missing:
        for name in sorted(missing):
            print(f"error: {name} is needed but not found on the build host", file=sys.stderr)
        return 1

    os.makedirs(out_dir, exist_ok=True)
    if host_side_file:
        with open(host_side_file, "w", encoding="utf-8") as f:
            f.write("".join(f"{name}\n" for name in sorted(host_side)))
    for name in sorted(collected):
        dest = os.path.join(out_dir, name)
        shutil.copyfile(collected[name], dest)
        os.chmod(dest, 0o644)
        print(f"{name} <- {os.path.realpath(collected[name])}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
