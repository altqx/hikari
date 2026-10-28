#!/usr/bin/env python3
"""Opens a libei connection to the session's compositor and keeps
eiinject running on it, fed from $XDG_RUNTIME_DIR/ei.in (a FIFO).

  eidaemon.py kwin     org.kde.KWin.EIS.RemoteDesktop.connectToEIS
  eidaemon.py mutter   org.gnome.Mutter.RemoteDesktop session (+ a ScreenCast
                       of every monitor so the absolute pointer has regions)
"""
import os
import subprocess
import sys

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402

bus = Gio.bus_get_sync(Gio.BusType.SESSION)


def call(dest, path, iface, method, args, reply, fds=False):
    if fds:
        res, fdlist = bus.call_with_unix_fd_list_sync(dest, path, iface, method, args, GLib.VariantType(reply),
                                                      Gio.DBusCallFlags.NONE, -1, None, None)
        return res, fdlist
    return bus.call_sync(dest, path, iface, method, args, GLib.VariantType(reply), Gio.DBusCallFlags.NONE, -1, None)


def kwin_fd():
    res, fdlist = call("org.kde.KWin", "/org/kde/KWin/EIS/RemoteDesktop", "org.kde.KWin.EIS.RemoteDesktop",
                       "connectToEIS", GLib.Variant("(i)", (0xff,)), "(hi)", fds=True)
    return fdlist.get(res.unpack()[0])


def mutter_fd():
    rd = "org.gnome.Mutter.RemoteDesktop"
    sc = "org.gnome.Mutter.ScreenCast"
    session = call(rd, "/org/gnome/Mutter/RemoteDesktop", rd, "CreateSession", None, "(o)").unpack()[0]
    session_id = bus.call_sync(rd, session, "org.freedesktop.DBus.Properties", "Get",
                               GLib.Variant("(ss)", (rd + ".Session", "SessionId")), GLib.VariantType("(v)"),
                               Gio.DBusCallFlags.NONE, -1, None).unpack()[0]
    print("remote desktop session", session, session_id, file=sys.stderr)
    try:
        scs = call(sc, "/org/gnome/Mutter/ScreenCast", sc, "CreateSession",
                   GLib.Variant("(a{sv})", ({"remote-desktop-session-id": GLib.Variant("s", session_id)},)),
                   "(o)").unpack()[0]
        disp = Gio.DBusProxy.new_sync(bus, 0, None, "org.gnome.Mutter.DisplayConfig", "/org/gnome/Mutter/DisplayConfig",
                                      "org.gnome.Mutter.DisplayConfig", None)
        state = disp.call_sync("GetCurrentState", None, 0, -1, None).unpack()
        for mon in state[1]:
            connector = mon[0][0]
            stream = call(sc, scs, sc + ".Session", "RecordMonitor",
                          GLib.Variant("(sa{sv})", (connector, {})), "(o)").unpack()[0]
            print("screencast stream", connector, stream, file=sys.stderr)
    except GLib.Error as e:
        print("screencast unavailable:", e.message, file=sys.stderr)
    call(rd, session, rd + ".Session", "Start", None, "()")
    res, fdlist = call(rd, session, rd + ".Session", "ConnectToEIS",
                       GLib.Variant("(a{sv})", ({},)), "(h)", fds=True)
    return fdlist.get(res.unpack()[0])


def main():
    which = sys.argv[1]
    fd = kwin_fd() if which == "kwin" else mutter_fd()
    fifo = os.path.join(os.environ["XDG_RUNTIME_DIR"], "ei.in")
    if not os.path.exists(fifo):
        os.mkfifo(fifo)
    # Open read-write so writers coming and going never end the stream.
    src = os.open(fifo, os.O_RDWR)
    proc = subprocess.Popen([os.environ.get("EIINJECT", "/tmp/eiinject"), str(fd)], stdin=src, pass_fds=(fd,))
    if which == "mutter":
        # The session lives as long as this connection; keep the loop alive.
        GLib.MainLoop().run()
    proc.wait()


if __name__ == "__main__":
    main()
