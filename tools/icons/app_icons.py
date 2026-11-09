#!/usr/bin/env python3
"""Renders every shipped app and file-type icon raster from the Dawn Peek SVG masters.

  python3 tools/icons/app_icons.py

Needs rsvg-convert and Pillow. Records the hashes of the script, the masters and every output in
tools/icons/app_icons.json, which tests/tools/app_icons_tests.py checks.
"""

import hashlib
import io
import json
import os
import re
import shutil
import struct
import subprocess
import tempfile

from PIL import Image

TREE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MASTERS = "packaging/icons/dawn-peek"
HICOLOR = "packaging/linux/icons/hicolor"
WINDOWS = "src/app/windows"
STAMP = "tools/icons/app_icons.json"

LINUX = {"apps/hikarisub": "app", "mimetypes/application-x-subrip": "srt", "mimetypes/text-x-ssa": "ass",
         "mimetypes/text-x-microdvd": "sub"}
LINUX_SIZES = (16, 32, 48, 64, 128, 256)
ICONS = {"HikariLargeIcon.ico": "app", "HikariSub_ass.ico": "ass", "HikariSub_srt.ico": "srt",
         "HikariSub_ssa.ico": "ssa", "HikariSub_sub.ico": "sub", "HikariSub_txt.ico": "txt"}
ICON_SIZES = (16, 24, 32, 48, 64, 128, 256)
SMALL_ICON = ("HikariSmallIcon.ico", "app", 64)
RENDER_SIZE = 2304


def master(name):
    return os.path.join(MASTERS, name + ".svg")


def squared(svg):
    box = re.search(r'viewBox="([^"]+)"', svg)
    x, y, w, h = (float(v) for v in box.group(1).split())
    side = max(w, h)
    square = "%g %g %g %g" % (x - (side - w) / 2, y - (side - h) / 2, side, side)
    return svg[:box.start(1)] + square + svg[box.end(1):]


def render(name):
    with open(os.path.join(TREE, master(name)), encoding="utf-8") as file:
        svg = squared(file.read())
    with tempfile.TemporaryDirectory() as tmp:
        src, png = os.path.join(tmp, "icon.svg"), os.path.join(tmp, "icon.png")
        with open(src, "w", encoding="utf-8") as file:
            file.write(svg)
        subprocess.run(["rsvg-convert", "-w", str(RENDER_SIZE), "-h", str(RENDER_SIZE), "-o", png, src], check=True)
        return Image.open(png).convert("RGBA")


def downsample(big, size):
    assert RENDER_SIZE % size == 0
    return big.resize((size, size), Image.Resampling.BOX)


def png_bytes(image):
    out = io.BytesIO()
    image.save(out, "PNG", optimize=True)
    return out.getvalue()


def bmp_frame(image):
    size = image.width
    rows = [image.crop((0, y, size, y + 1)).tobytes("raw", "BGRA") for y in reversed(range(size))]
    mask_stride = (size + 31) // 32 * 4
    mask = bytearray()
    for y in reversed(range(size)):
        bits = bytearray(mask_stride)
        for x in range(size):
            if image.getpixel((x, y))[3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    header = struct.pack("<IiiHHIIiiII", 40, size, 2 * size, 1, 32, 0, size * size * 4, 3780, 3780, 0, 0)
    return header + b"".join(rows) + bytes(mask)


def ico(frames):
    head = struct.pack("<HHH", 0, 1, len(frames))
    offset = 6 + 16 * len(frames)
    entries, data = b"", b""
    for size, blob in frames:
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(blob), offset + len(data))
        data += blob
    return head + entries + data


def outputs():
    renders = {name: render(name) for name in sorted(set(LINUX.values()) | set(ICONS.values()))}
    for path, name in LINUX.items():
        yield "%s/scalable/%s.svg" % (HICOLOR, path), None, master(name)
        for size in LINUX_SIZES:
            yield "%s/%dx%d/%s.png" % (HICOLOR, size, size, path), png_bytes(downsample(renders[name], size)), None
    for file, name in ICONS.items():
        frames = [(size, png_bytes(downsample(renders[name], size))) for size in ICON_SIZES]
        yield "%s/%s" % (WINDOWS, file), ico(frames), None
    file, name, size = SMALL_ICON
    yield "%s/%s" % (WINDOWS, file), ico([(size, bmp_frame(downsample(renders[name], size)))]), None


def sha256(path):
    with open(os.path.join(TREE, path), "rb") as file:
        return hashlib.sha256(file.read()).hexdigest()


def main():
    written = []
    for path, blob, copy_of in outputs():
        target = os.path.join(TREE, path)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        if copy_of:
            shutil.copyfile(os.path.join(TREE, copy_of), target)
        else:
            with open(target, "wb") as file:
                file.write(blob)
        written.append(path)
    masters = sorted(os.path.join(MASTERS, f) for f in os.listdir(os.path.join(TREE, MASTERS)) if f.endswith(".svg"))
    stamp = {"script": sha256("tools/icons/app_icons.py"),
             "masters": {path: sha256(path) for path in masters},
             "outputs": {path: sha256(path) for path in sorted(written)}}
    with open(os.path.join(TREE, STAMP), "w", encoding="utf-8") as file:
        json.dump(stamp, file, indent=1, sort_keys=True)
        file.write("\n")


if __name__ == "__main__":
    main()
