#!/usr/bin/env python3
"""python3 -m unittest discover -s tools/crash -p 'test_*.py'"""
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import posthog_symbols  # noqa: E402
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


def pe_with_codeview(guid, age):
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
    struct.pack_into("<IIHHIIII", image, raw, 0, 0, 0, 0, 2, 24 + 9, codeview_rva, raw + 28)
    image[raw + 28:raw + 32] = b"RSDS"
    image[raw + 32:raw + 48] = guid
    struct.pack_into("<I", image, raw + 48, age)
    image[raw + 52:raw + 61] = b"Hikari.pdb"[:9]
    return bytes(image)


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
