#!/usr/bin/env python3
"""Uploads a symbols folder (ELF .debug files, or modules beside their PDBs) to PostHog error tracking.

posthog_symbols.py DIR [--dry-run]

Each goes up as the symbol set posthog-cli makes for an ELF, under the debug id the app sends in `$debug_images`,
unless PostHog already has one under that id.
posthog-cli takes no PDB, and packs at a zstd level that leaves hikarisub.debug over PostHog's 100 MB.
Needs objcopy, dwz and zstd. Reads POSTHOG_CLI_API_KEY, POSTHOG_CLI_PROJECT_ID and POSTHOG_CLI_HOST.
"""

import argparse
import hashlib
import io
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
import uuid
import zipfile
from pathlib import Path

MAGIC = b"posthog_error_tracking"
CONTAINER_VERSION = 2
ELF_DEBUG_INFO = 6
ZSTD = 1
MAX_UPLOAD = 100 * 1024 * 1024
DEFAULT_HOST = "https://eu.posthog.com"
NOT_FOR_SYMBOLS = (".debug_loc", ".debug_loclists")


def debug_id(guid, age=0):
    """symbolic's DebugId text: 16 bytes read as a little-endian GUID, then the age unless it is 0."""
    text = str(uuid.UUID(bytes_le=bytes(guid[:16]).ljust(16, b"\0")))
    return f"{text}-{age:x}" if age else text


def elf_debug_id(data):
    """The debug id of a 64-bit little-endian ELF's GNU build-id note, or None."""
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        return None
    sections_at, = struct.unpack_from("<Q", data, 0x28)
    entry_size, count = struct.unpack_from("<HH", data, 0x3A)
    for i in range(count):
        kind, = struct.unpack_from("<I", data, sections_at + i * entry_size + 4)
        at, size = struct.unpack_from("<QQ", data, sections_at + i * entry_size + 0x18)
        if kind != 7:
            continue
        end = at + size
        while at + 12 <= end:
            name_size, desc_size, note = struct.unpack_from("<III", data, at)
            name_at = at + 12
            desc_at = name_at + (name_size + 3) // 4 * 4
            if note == 3 and data[name_at:name_at + name_size] == b"GNU\0" and desc_size:
                return debug_id(data[desc_at:desc_at + desc_size])
            at = desc_at + (desc_size + 3) // 4 * 4
    return None


def pe_debug_id(data):
    """The debug id of a PE image's RSDS CodeView record, or None."""
    codeview = pe_codeview(data)
    return debug_id(codeview[0], codeview[1]) if codeview else None


def pe_codeview(data):
    """(GUID, age, PDB file name) from a PE image's RSDS CodeView record, or None."""
    if data[:2] != b"MZ":
        return None
    pe, = struct.unpack_from("<I", data, 0x3C)
    if data[pe:pe + 4] != b"PE\0\0":
        return None
    sections, = struct.unpack_from("<H", data, pe + 6)
    optional_size, = struct.unpack_from("<H", data, pe + 20)
    optional = pe + 24
    magic, = struct.unpack_from("<H", data, optional)
    directories = optional + (112 if magic == 0x20B else 96)
    count, = struct.unpack_from("<I", data, directories - 4)
    if count <= 6:
        return None
    rva, size = struct.unpack_from("<II", data, directories + 6 * 8)
    table = optional + optional_size

    def file_offset(address):
        for i in range(sections):
            virtual_size, virtual_address, raw_size, raw_at = struct.unpack_from("<IIII", data, table + i * 40 + 8)
            if virtual_address <= address < virtual_address + max(virtual_size, raw_size):
                return address - virtual_address + raw_at
        return None

    at = file_offset(rva) if rva else None
    if at is None:
        return None
    for entry in range(size // 28):
        kind, data_size, _, raw_at = struct.unpack_from("<IIII", data, at + entry * 28 + 12)
        if kind == 2 and data_size >= 24 and data[raw_at:raw_at + 4] == b"RSDS":
            age, = struct.unpack_from("<I", data, raw_at + 20)
            path = data[raw_at + 24:raw_at + data_size].split(b"\0")[0].decode(errors="replace")
            return bytes(data[raw_at + 4:raw_at + 20]), age, path.replace("\\", "/").rsplit("/", 1)[-1]
    return None


def symbol_sets(folder):
    """[(debug id, file)] for each ELF with a build-id and each module with its PDB beside it."""
    found = {}
    for path in sorted(folder.rglob("*")):
        if not path.is_file() or path.is_symlink() or ".build-id" in path.relative_to(folder).parts:
            continue
        if path.suffix.lower() in (".exe", ".dll"):
            pdb = path.with_suffix(".pdb")
            ident = pe_debug_id(path.read_bytes()) if pdb.is_file() else None
            if ident:
                found.setdefault(ident, pdb)
            continue
        with path.open("rb") as file:
            if file.read(4) != b"\x7fELF":
                continue
        ident = elf_debug_id(path.read_bytes())
        if ident:
            found.setdefault(ident, path)
    return sorted(found.items())


def debug_data(path):
    """The file's bytes; an ELF's uncompressed, so zstd can pack them, deduplicated by dwz, and without location lists."""
    if path.suffix.lower() == ".pdb":
        return path.read_bytes()
    with tempfile.TemporaryDirectory() as scratch:
        plain, shared, out = (Path(scratch) / name for name in ("plain", "shared", "out"))
        subprocess.run(["objcopy", "--decompress-debug-sections", str(path), str(plain)], check=True)
        # dwz refuses a file whose location lists are already gone, and leaves one it cannot shrink as it was.
        deduplicated = subprocess.run(["dwz", "-q", "-L", "none", "-l", "none", "-o", str(shared), str(plain)],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        removed = [arg for name in NOT_FOR_SYMBOLS for arg in ("--remove-section", name)]
        source = shared if deduplicated and shared.is_file() else plain
        subprocess.run(["objcopy", *removed, str(source), str(out)], check=True)
        return out.read_bytes()


def container(data):
    """The symbol set cymbal reads: its zstd container around a zip holding the debug file as `dwarf`."""
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_STORED) as archive:
        archive.writestr("dwarf", data)
    packed = subprocess.run(["zstd", "-q", "-19", "-T0", "-c"], input=buffer.getvalue(), stdout=subprocess.PIPE,
                            check=True).stdout
    return MAGIC + struct.pack("<II", CONTAINER_VERSION, ELF_DEBUG_INFO) + bytes([ZSTD]) + packed


class Api:
    def __init__(self, host, project, key):
        self.base = f"{host.rstrip('/')}/api/projects/{project}/error_tracking/symbol_sets/"
        self.key = key

    def has(self, chunk_id):
        request = urllib.request.Request(f"{self.base}?{urllib.parse.urlencode({'ref': chunk_id, 'status': 'valid'})}",
                                         headers={"Authorization": f"Bearer {self.key}"})
        with urllib.request.urlopen(request, timeout=120) as response:
            return json.loads(response.read()).get("count", 0) > 0

    def post(self, path, body):
        request = urllib.request.Request(self.base + path, data=json.dumps(body).encode(), method="POST",
                                         headers={"Authorization": f"Bearer {self.key}",
                                                  "Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=120) as response:
            return json.loads(response.read() or b"{}")

    def upload(self, chunk_id, data):
        """False when PostHog already has this chunk id."""
        content_hash = hashlib.sha512(data).hexdigest()
        attempt, run = str(uuid.uuid4()), str(uuid.uuid4())
        sets = [{"chunk_id": chunk_id, "release_id": None, "content_hash": content_hash, "content_length": len(data)}]
        started = self.post("bulk_start_upload", {"symbol_sets": sets, "upload_attempt_id": attempt,
                                                  "upload_run_id": run, "force": False, "skip_on_conflict": True})
        target = started.get("id_map", {}).get(chunk_id)
        if not target:
            return False
        if target.get("presigned_put_url"):
            put = urllib.request.Request(target["presigned_put_url"], data=data, method="PUT")
            urllib.request.urlopen(put, timeout=600).close()
        else:
            presigned = target["presigned_url"]
            boundary = uuid.uuid4().hex
            parts = [f'--{boundary}\r\nContent-Disposition: form-data; name="{k}"\r\n\r\n{v}\r\n'.encode()
                     for k, v in presigned["fields"].items()]
            parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="file"\r\n'
                         "Content-Type: application/octet-stream\r\n\r\n".encode() + data + b"\r\n")
            body = b"".join(parts) + f"--{boundary}--\r\n".encode()
            post = urllib.request.Request(presigned["url"], data=body, method="POST",
                                          headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
            urllib.request.urlopen(post, timeout=600).close()
        self.post("bulk_finish_upload", {"content_hashes": {target["symbol_set_id"]: content_hash},
                                         "upload_attempt_id": attempt, "upload_run_id": run})
        return True


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("folder", type=Path)
    parser.add_argument("--dry-run", action="store_true", help="pack and list the symbol sets, upload nothing")
    args = parser.parse_args(argv)
    sets = symbol_sets(args.folder)
    if not sets:
        print(f"::warning::no symbols in {args.folder}")
        return 1
    api = None
    if not args.dry_run:
        key, project = os.environ.get("POSTHOG_CLI_API_KEY"), os.environ.get("POSTHOG_CLI_PROJECT_ID")
        if not key or not project:
            print("::warning::POSTHOG_CLI_API_KEY and POSTHOG_CLI_PROJECT_ID are needed")
            return 1
        api = Api(os.environ.get("POSTHOG_CLI_HOST") or DEFAULT_HOST, project, key)
    for tool in ("zstd", "objcopy", "dwz"):
        if not shutil.which(tool):
            print(f"::warning::{tool} is needed")
            return 1
    failed = 0
    for chunk_id, path in sets:
        name = path.relative_to(args.folder)
        try:
            if api is not None and api.has(chunk_id):
                print(f"{chunk_id} {name}: already there")
                continue
            data = container(debug_data(path))
            if len(data) > MAX_UPLOAD:
                print(f"::warning::{name} packs to {len(data)} bytes, over PostHog's {MAX_UPLOAD}; skipped")
                failed += 1
            elif api is None:
                print(f"{chunk_id} {name}: {len(data)} bytes")
            else:
                print(f"{chunk_id} {name}: {'uploaded' if api.upload(chunk_id, data) else 'already there'}")
        except (OSError, subprocess.CalledProcessError) as error:
            print(f"::warning::{name}: {error}")
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
