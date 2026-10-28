#!/usr/bin/env python3
"""mutter_shot.py OUT.png [CONNECTOR]: one frame of a mutter monitor
(default: every monitor, side by side) through org.gnome.Mutter.ScreenCast
and PipeWire (gst-launch pipewiresrc)."""
import os
import subprocess
import sys

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

SC = "org.gnome.Mutter.ScreenCast"
bus = Gio.bus_get_sync(Gio.BusType.SESSION)


def call(path, iface, method, args, reply):
    return bus.call_sync(SC, path, iface, method, args, GLib.VariantType(reply), Gio.DBusCallFlags.NONE, 10000, None)


def connectors():
    res = bus.call_sync("org.gnome.Mutter.DisplayConfig", "/org/gnome/Mutter/DisplayConfig",
                        "org.gnome.Mutter.DisplayConfig", "GetCurrentState", None, None,
                        Gio.DBusCallFlags.NONE, 10000, None).unpack()
    # the monitors in the current layout (a monitor taken out of it, as step
    # outputs does, has no picture), left to right
    logical = sorted(res[2], key=lambda lm: (lm[0], lm[1]))
    return [m[0] for lm in logical for m in lm[5]]


def grab(connector, out):
    session = call("/org/gnome/Mutter/ScreenCast", SC, "CreateSession", GLib.Variant("(a{sv})", ({},)), "(o)").unpack()[0]
    stream = call(session, SC + ".Session", "RecordMonitor",
                  GLib.Variant("(sa{sv})", (connector, {"cursor-mode": GLib.Variant("u", 1)})), "(o)").unpack()[0]
    loop = GLib.MainLoop()
    node = []

    def on_signal(conn, sender, path, iface, signal, params):
        node.append(params.unpack()[0])
        loop.quit()

    bus.signal_subscribe(SC, SC + ".Stream", "PipeWireStreamAdded", stream, None, 0, on_signal)
    call(session, SC + ".Session", "Start", None, "()")
    GLib.timeout_add(5000, loop.quit)
    loop.run()
    if not node:
        raise SystemExit("no PipeWire stream from mutter")
    subprocess.run(["gst-launch-1.0", "-q", "pipewiresrc", f"path={node[0]}", "num-buffers=3", "!",
                    "videoconvert", "!", "pngenc", "snapshot=true", "!", "filesink", f"location={out}"],
                   check=True, timeout=20)
    call(session, SC + ".Session", "Stop", None, "()")


def main():
    out = sys.argv[1]
    names = sys.argv[2:] or connectors()
    if len(names) == 1:
        grab(names[0], out)
    else:
        parts = []
        for n in names:
            p = f"{out}.{n}.png"
            grab(n, p)
            parts.append(p)
        subprocess.run(["magick", *parts, "+append", out], check=True)
        for p in parts:
            os.unlink(p)
    print(out, names)


if __name__ == "__main__":
    main()
