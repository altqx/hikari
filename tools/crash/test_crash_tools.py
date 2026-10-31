#!/usr/bin/env python3
"""python3 -m unittest discover -s tools/crash -p 'test_*.py'"""
import http.server
import io
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
import urllib.parse
import zipfile
from contextlib import redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import posthog_symbols  # noqa: E402
import qt_symbols  # noqa: E402
import symbolize  # noqa: E402

BUILD_ID = bytes.fromhex("f9db511235ff1a7690ff786534e30872ca208bf5")
GUID = bytes.fromhex("e451d023db93b7408ab27072163144b9")


def elf_with_build_id(build_id):
    """A 64-bit ELF holding only a null section and a .note.gnu.build-id."""
    note = struct.pack("<III", 4, len(build_id), 3) + b"GNU\0" + build_id
    note += b"\0" * (-len(note) % 4)
    notes_at = 64
    sections_at = notes_at + len(note)
    header = b"\x7fELF" + bytes([2, 1, 1]) + b"\0" * 9
    header += struct.pack("<HHIQQQIHHHHHH", 1, 62, 1, 0, 0, sections_at, 0, 64, 0, 0, 64, 2, 0)
    null = b"\0" * 64
    section = struct.pack("<IIQQQQIIQQ", 0, 7, 2, 0, notes_at, len(note), 0, 0, 4, 0)
    return header + note + null + section


def pe_with_codeview(guid, age, pdb=b"Hikari.pdb"):
    """A PE32+ image whose debug directory points at an RSDS record."""
    pe = 0x40
    optional = pe + 24
    optional_size = 112 + 16 * 8
    table = optional + optional_size
    raw = 0x200
    debug_rva = 0x1000
    codeview_rva = debug_rva + 28
    image = bytearray(raw + 0x200)
    image[0:2] = b"MZ"
    struct.pack_into("<I", image, 0x3C, pe)
    image[pe:pe + 4] = b"PE\0\0"
    struct.pack_into("<HH", image, pe + 4, 0x8664, 1)
    struct.pack_into("<H", image, pe + 20, optional_size)
    struct.pack_into("<H", image, optional, 0x20B)
    struct.pack_into("<I", image, optional + 108, 16)
    struct.pack_into("<II", image, optional + 112 + 6 * 8, debug_rva, 28)
    image[table:table + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", image, table + 8, 0x200, 0x1000, 0x200, raw)
    struct.pack_into("<IIHHIIII", image, raw, 0, 0, 0, 0, 2, 24 + len(pdb) + 1, codeview_rva, raw + 28)
    image[raw + 28:raw + 32] = b"RSDS"
    image[raw + 32:raw + 48] = guid
    struct.pack_into("<I", image, raw + 48, age)
    image[raw + 52:raw + 52 + len(pdb)] = pdb
    return bytes(image)


def pdb_with_guid(guid):
    """An MSF 7 PDB whose info stream (stream 1) holds the GUID, after a one-block stream 0."""
    block = 512
    directory = struct.pack("<I2I2I", 2, 10, 28, 5, 4)
    blocks = [b""] * 6
    blocks[0] = qt_symbols.PDB_MAGIC + struct.pack("<6I", block, 1, len(blocks), len(directory), 0, 2)
    blocks[2] = struct.pack("<I", 3)
    blocks[3] = directory
    blocks[4] = struct.pack("<3I", 20000404, 0, 1) + guid
    return b"".join(b.ljust(block, b"\0") for b in blocks)


class Loopback(http.server.BaseHTTPRequestHandler):
    """PostHog's symbol set list, holding only the ref in `known`."""
    known = ""
    asked = []

    def do_GET(self):
        query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        Loopback.asked.append(query)
        body = json.dumps({"count": int(query.get("ref") == [self.known])}).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def loopback_posthog(known):
    Loopback.known, Loopback.asked = known, []
    server = http.server.HTTPServer(("127.0.0.1", 0), Loopback)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


class DebugIds(unittest.TestCase):
    def test_elf_build_id_reads_as_a_little_endian_guid(self):
        self.assertEqual(posthog_symbols.elf_debug_id(elf_with_build_id(BUILD_ID)), "1251dbf9-ff35-761a-90ff-786534e30872")

    def test_short_build_id_is_padded(self):
        self.assertEqual(posthog_symbols.elf_debug_id(elf_with_build_id(bytes.fromhex("0102030405060708"))),
                         "04030201-0605-0807-0000-000000000000")

    def test_pe_codeview_gives_guid_and_age(self):
        self.assertEqual(posthog_symbols.pe_debug_id(pe_with_codeview(GUID, 1)), "23d051e4-93db-40b7-8ab2-7072163144b9-1")
        self.assertEqual(posthog_symbols.pe_debug_id(pe_with_codeview(GUID, 0x1a)), "23d051e4-93db-40b7-8ab2-7072163144b9-1a")

    def test_other_files_have_none(self):
        self.assertIsNone(posthog_symbols.elf_debug_id(b"#!/bin/sh\n" + b"\0" * 64))
        self.assertIsNone(posthog_symbols.pe_debug_id(b"MZ" + b"\0" * 0x40))


class Folders(unittest.TestCase):
    def test_each_debug_id_once_and_build_id_links_skipped(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / ".build-id/f9").mkdir(parents=True)
            (root / "hikarisub.debug").write_bytes(elf_with_build_id(BUILD_ID))
            (root / ".build-id/f9/db5112.debug").write_bytes(elf_with_build_id(BUILD_ID))
            (root / "HikariSub.exe").write_bytes(pe_with_codeview(GUID, 1))
            (root / "HikariSub.pdb").write_bytes(b"Microsoft C/C++ MSF 7.00\r\n")
            (root / "Lonely.dll").write_bytes(pe_with_codeview(bytes(range(16)), 1))
            (root / "notes.txt").write_text("not symbols")
            found = posthog_symbols.symbol_sets(root)
        self.assertEqual([(ident, path.name) for ident, path in found],
                         [("1251dbf9-ff35-761a-90ff-786534e30872", "hikarisub.debug"),
                          ("23d051e4-93db-40b7-8ab2-7072163144b9-1", "HikariSub.pdb")])

    @unittest.skipUnless(shutil.which("zstd"), "needs zstd")
    def test_container_is_what_cymbal_reads(self):
        packed = posthog_symbols.container(b"debug bytes")
        head = len(posthog_symbols.MAGIC)
        self.assertEqual(packed[:head], b"posthog_error_tracking")
        self.assertEqual(struct.unpack_from("<II", packed, head), (2, 6))
        self.assertEqual(packed[head + 8], 1)
        unpacked = subprocess.run(["zstd", "-q", "-d", "-c"], input=packed[head + 9:], stdout=subprocess.PIPE,
                                  check=True).stdout
        with zipfile.ZipFile(io.BytesIO(unpacked)) as archive:
            self.assertEqual(archive.namelist(), ["dwarf"])
            self.assertEqual(archive.read("dwarf"), b"debug bytes")


class Upload(unittest.TestCase):
    def test_has_asks_for_a_valid_set_by_ref(self):
        server = loopback_posthog("1251dbf9-ff35-761a-90ff-786534e30872")
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        api = posthog_symbols.Api(f"http://127.0.0.1:{server.server_port}", "1", "key")
        self.assertTrue(api.has("1251dbf9-ff35-761a-90ff-786534e30872"))
        self.assertFalse(api.has("23d051e4-93db-40b7-8ab2-7072163144b9-1"))
        self.assertEqual(Loopback.asked[0]["status"], ["valid"])

    @unittest.skipUnless(all(map(shutil.which, ("zstd", "objcopy", "dwz"))), "needs zstd, objcopy and dwz")
    def test_a_set_posthog_has_is_not_packed(self):
        server = loopback_posthog("1251dbf9-ff35-761a-90ff-786534e30872")
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        with tempfile.TemporaryDirectory() as scratch:
            (Path(scratch) / "hikarisub.debug").write_bytes(elf_with_build_id(BUILD_ID))
            env = {"POSTHOG_CLI_API_KEY": "key", "POSTHOG_CLI_PROJECT_ID": "1",
                   "POSTHOG_CLI_HOST": f"http://127.0.0.1:{server.server_port}"}
            out = io.StringIO()
            with mock.patch.dict(os.environ, env), mock.patch.object(posthog_symbols, "debug_data") as packed, \
                    redirect_stdout(out):
                self.assertEqual(posthog_symbols.main([scratch]), 0)
        packed.assert_not_called()
        self.assertIn("hikarisub.debug: already there", out.getvalue())

    @unittest.skipUnless(all(map(shutil.which, ("g++", "objcopy", "dwz"))), "needs g++, objcopy and dwz")
    def test_dwz_shrinks_the_uploaded_copy_and_keeps_the_build_id(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for name in ("a", "b"):
                (root / f"{name}.cpp").write_text("#include <map>\n#include <string>\n#include <vector>\n"
                                                  f"std::map<std::string, std::vector<int>> {name}_table;\n"
                                                  f"int {name}() {{ return int({name}_table.size()); }}\n")
            (root / "main.cpp").write_text("int a(); int b(); int main() { return a() + b(); }\n")
            subprocess.run(["g++", "-g", "-O2", "-Wl,--build-id", "a.cpp", "b.cpp", "main.cpp", "-o", "program"],
                           cwd=root, check=True)
            subprocess.run(["objcopy", "--only-keep-debug", "--compress-debug-sections=zlib", "program",
                            "program.debug"], cwd=root, check=True)
            subprocess.run(["objcopy", "--decompress-debug-sections", "--remove-section", ".debug_loclists",
                            "program.debug", "plain.debug"], cwd=root, check=True)
            data = posthog_symbols.debug_data(root / "program.debug")
            (root / "uploaded.debug").write_bytes(data)
            sections = subprocess.run(["readelf", "-S", "-W", "uploaded.debug"], cwd=root, capture_output=True,
                                      text=True, check=True).stdout
            original = posthog_symbols.elf_debug_id((root / "program").read_bytes())
            plain_size = (root / "plain.debug").stat().st_size
        self.assertEqual(posthog_symbols.elf_debug_id(data), original)
        self.assertNotIn(".debug_loclists", sections)
        self.assertLess(len(data), plain_size)


class QtSymbols(unittest.TestCase):
    def test_debug_file_names_are_qts(self):
        self.assertEqual(qt_symbols.elf_debug_name(Path("lib/libQt6Core.so.6.12.0")), "Qt6Core.debug")
        self.assertEqual(qt_symbols.elf_debug_name(Path("plugins/platforms/libqxcb.so")), "qxcb.debug")
        self.assertIsNone(qt_symbols.elf_debug_name(Path("hikarisub")))

    def test_pdb_guid_is_the_info_streams(self):
        self.assertEqual(qt_symbols.pdb_guid(pdb_with_guid(GUID)), GUID)
        self.assertIsNone(qt_symbols.pdb_guid(b"not a pdb"))

    def test_only_the_debug_file_with_the_shipped_build_id_is_placed(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "package/lib").mkdir(parents=True)
            (root / "package/lib/libQt6Core.so.6.12.0").write_bytes(elf_with_build_id(BUILD_ID))
            (root / "package/lib/libQt6Core.so.6").symlink_to("libQt6Core.so.6.12.0")
            modules = qt_symbols.shipped(root / "package", "linux")
            (root / "Qt6Core.debug").write_bytes(elf_with_build_id(bytes(20)))
            stale = qt_symbols.place(root / "Qt6Core.debug", modules["qt6core.debug"], root / "package",
                                     root / "out", "linux")
            (root / "Qt6Core.debug").write_bytes(elf_with_build_id(BUILD_ID))
            matched = qt_symbols.place(root / "Qt6Core.debug", modules["qt6core.debug"], root / "package",
                                       root / "out", "linux")
            placed = sorted(p.relative_to(root / "out").as_posix() for p in (root / "out").rglob("*") if p.is_file())
        self.assertEqual((stale, matched), (0, 1))
        self.assertEqual(placed, ["lib/libQt6Core.so.6.12.0.debug"])

    def test_a_pdb_goes_beside_the_dll_with_its_guid(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            (root / "package").mkdir()
            (root / "package/Qt6Core.dll").write_bytes(pe_with_codeview(GUID, 2, b"C:\\Qt\\bin\\Qt6Core.pdb"))
            modules = qt_symbols.shipped(root / "package", "windows")
            (root / "Qt6Core.pdb").write_bytes(pdb_with_guid(bytes(16)))
            stale = qt_symbols.place(root / "Qt6Core.pdb", modules["qt6core.pdb"], root / "package", root / "out",
                                     "windows")
            (root / "Qt6Core.pdb").write_bytes(pdb_with_guid(GUID))
            matched = qt_symbols.place(root / "Qt6Core.pdb", modules["qt6core.pdb"], root / "package", root / "out",
                                       "windows")
            found = posthog_symbols.symbol_sets(root / "out")
        self.assertEqual((stale, matched), (0, 1))
        self.assertEqual([(ident, path.name) for ident, path in found],
                         [("23d051e4-93db-40b7-8ab2-7072163144b9-2", "Qt6Core.pdb")])

    def test_the_lock_is_for_the_qt_the_build_uses(self):
        lock = json.loads(qt_symbols.LOCK.read_text())
        for platform in lock["platforms"]:
            qt = json.loads((qt_symbols.QT_LOCKS / f"qt-{platform}.lock.json").read_text())["qt_version"]
            self.assertEqual(lock["qt_version"], qt, f"regenerate {qt_symbols.LOCK.name} for Qt {qt}")


class Symbolize(unittest.TestCase):
    def test_reads_frames_with_and_without_debug_ids(self):
        record = ("hikari-crash 1\nversion 0.1.0\ntype SIGSEGV\nmechanism signal\n"
                  "frame hikarisub 0x1316e0 0x400000 1251dbf9-ff35-761a-90ff-786534e30872\n"
                  "frame HikariSub.exe 0x14420 0x7ff6a0000000 23d051e4-93db-40b7-8ab2-7072163144b9-1\n"
                  "frame other 0x4d2a0\n")
        frames, version = symbolize.parse(record)
        self.assertEqual(frames, [("hikarisub", 0x1316E0), ("HikariSub.exe", 0x14420), ("other", 0x4D2A0)])
        self.assertEqual(version, "0.1.0")


if __name__ == "__main__":
    unittest.main()
