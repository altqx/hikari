#!/usr/bin/env python3
"""Thread stacks from a Windows minidump of the legacy app (legacy-win-dump).

    minidump_stacks.py <HikariSub.dmp> <HikariSub.exe> [frames]

For each thread: its instruction pointer, then every stack slot that points
into a loaded module, from the stack pointer up. That is a scan, not an
unwind, so stale return addresses appear beside the live frames. Addresses in
HikariSub.exe are symbolized with llvm-symbolizer against the release's own
PDB (HikariSub.pdb beside the exe, both in the release zip); others print as
module+offset.
"""
import struct
import subprocess
import sys
from pathlib import Path

THREAD_LIST, MODULE_LIST = 3, 4
IMAGE_BASE = 0x140000000  # the exe's preferred base, as the PDB records it


def main():
    dmp = Path(sys.argv[1]).read_bytes()
    exe = sys.argv[2]
    frames = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    _, _, count, directory = struct.unpack_from("<IIII", dmp, 0)
    streams = {}
    for i in range(count):
        kind, _, rva = struct.unpack_from("<III", dmp, directory + 12 * i)
        streams[kind] = rva

    def name_at(rva):
        n = struct.unpack_from("<I", dmp, rva)[0]
        return dmp[rva + 4:rva + 4 + n].decode("utf-16le").split("\\")[-1]

    rva = streams[MODULE_LIST]
    modules = []
    for i in range(struct.unpack_from("<I", dmp, rva)[0]):
        base, size, _, _, name_rva = struct.unpack_from("<QIIII", dmp, rva + 4 + 108 * i)
        modules.append((base, size, name_at(name_rva)))
    exe_base = next(base for base, _, name in modules if name.lower() == "hikarisub.exe")

    def module_of(address):
        return next(((base, name) for base, size, name in modules if base <= address < base + size), (None, None))

    def symbolize(addresses):
        if not addresses:
            return []
        out = subprocess.run(["llvm-symbolizer", "--obj", exe, "--demangle", "--no-inlines"]
                             + [hex(a - exe_base + IMAGE_BASE) for a in addresses],
                             capture_output=True, text=True).stdout.strip().split("\n\n")
        return [o.replace("\n", " @ ") for o in out]

    rva = streams[THREAD_LIST]
    for i in range(struct.unpack_from("<I", dmp, rva)[0]):
        tid, _, _, _, _, stack_start, stack_size, stack_rva, _, context_rva = struct.unpack_from(
            "<IIIIQQIIII", dmp, rva + 4 + 48 * i)
        rip = struct.unpack_from("<Q", dmp, context_rva + 0xF8)[0]
        rsp = struct.unpack_from("<Q", dmp, context_rva + 0x98)[0]
        stack = dmp[stack_rva:stack_rva + stack_size]
        slots = [rip] + [struct.unpack_from("<Q", stack, o)[0]
                         for o in range(max(0, rsp - stack_start) & ~7, len(stack) - 7, 8)]
        hits = [(a, *module_of(a)) for a in slots]
        hits = [h for h in hits if h[2]][:frames]
        names = iter(symbolize([a for a, _, name in hits if name.lower() == "hikarisub.exe"]))
        print(f"== thread {tid}")
        for address, base, name in hits:
            extra = f"  {next(names)}" if name.lower() == "hikarisub.exe" else ""
            print(f"  {name}+{address - base:#x}{extra}")


if __name__ == "__main__":
    main()
