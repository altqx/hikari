#!/usr/bin/env python3
"""Collects the AppImage's `full` profile extras (installers spec 5.2).

    collect-runtime.py <app tree> <out lib dir> <host-libs.txt> [<host-side list>]

Copies into <out lib dir> every shared library that the ELF files of
<app tree> need, directly or through other collected libraries, unless

  - the tree provides it (file name or DT_SONAME),
  - its name matches a glob of host-libs.txt (the host list), or
  - a host-list library needs it, directly or indirectly (the closure rule:
    anything a host library needs is host-side too and is never bundled).

Libraries are resolved the way the build host's loader resolves them
(`ldconfig -p`, x86-64 entries), so run this in the build container. Each
library is copied under its soname. Prints one line per collected library
and exits 1 when a needed library cannot be found. With a fourth argument,
writes there the sonames left to the host (the host-list matches and their
closure), one per line, for the package tests.
"""
import fnmatch
import os
import re
import shutil
import subprocess
import sys

ELF_MAGIC = b"\x7fELF"

# Qt's OpenSSL backend loads libssl with dlopen. When the bundle carries
# libcrypto (Kerberos needs it on the floor), it must carry the libssl of the
# same OpenSSL too, or the host's newer libssl would meet the older libcrypto.
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

    # The closure rule: everything a host-list library needs, as the build
    # host resolves it.
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
