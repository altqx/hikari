#!/usr/bin/env python3
"""kwin_shot.py OUT.png [OUTPUT]: the whole KWin workspace through org.kde.KWin.ScreenShot2
(allowed by KWIN_WAYLAND_NO_PERMISSION_CHECKS in the gate session)."""
import os
import subprocess
import sys
import tempfile

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

out = sys.argv[1]
bus = Gio.bus_get_sync(Gio.BusType.SESSION)
r, w = os.pipe()
fdlist = Gio.UnixFDList.new()
idx = fdlist.append(w)
# A separate process drains the pipe: the blocking D-Bus call holds the GIL.
tmp = tempfile.NamedTemporaryFile(delete=False)
reader = subprocess.Popen(["cat"], stdin=r, stdout=tmp)
os.close(r)
opts = {"native-resolution": GLib.Variant("b", True)}
if len(sys.argv) > 2:  # one output by name (CaptureWorkspace needs the OpenGL compositor)
    method, args = "CaptureScreen", GLib.Variant("(sa{sv}h)", (sys.argv[2], opts, idx))
else:
    method, args = "CaptureWorkspace", GLib.Variant("(a{sv}h)", (opts, idx))
res, _ = bus.call_with_unix_fd_list_sync(
    "org.kde.KWin", "/org/kde/KWin/ScreenShot2", "org.kde.KWin.ScreenShot2", method,
    args, GLib.VariantType("(a{sv})"), Gio.DBusCallFlags.NONE, 15000, fdlist, None)
os.close(w)
for fd in fdlist.steal_fds():  # the list holds its own copy of the write end
    os.close(fd)
reader.wait()
meta = res.unpack()[0]
width, height, stride, fmt = meta["width"], meta["height"], meta["stride"], meta["format"]
raw = open(tmp.name, "rb").read()
os.unlink(tmp.name)
# QImage::Format_ARGB32 / RGB32 / ARGB32_Premultiplied are BGRA in memory on little endian.
rows = b"".join(raw[y * stride:y * stride + width * 4] for y in range(height))
subprocess.run(["magick", "-size", f"{width}x{height}", "-depth", "8", "bgra:-", out], input=rows, check=True)
print(out, width, height, "format", fmt)
