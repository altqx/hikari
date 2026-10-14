"""Y7 probe (X11): does the simple colour picker hold the pointer once the
window manager has mapped it? Opens the picker from the Line editor's
Border color button with a real right click, then moves the real pointer
over a colour patch and reads the picker's hex field (AT-SPI) and the X
server's pointer grab (xdotool cannot read it: a second client's
XGrabPointer answers AlreadyGrabbed while the app holds it)."""
import os
import sys
import time
sys.argv = ["gate.py", "x11"]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate as g  # noqa: E402

patch = g.colour_patch()
time.sleep(2)
g.sh("xdotool search --sync --onlyvisible --name '^patch$' windowmove 200 640")
g.fresh(g.episode(), env={"QT_LOGGING_RULES": "qt.qpa.xcb*=true;qt.qpa.input*=true"})
g.dismiss_notices()
g.show_tags()
main = g.origin("HikariSub")
name, btn = g.colour_button()
bx, by = main[0] + btn["x"] + btn["w"] // 2, main[1] + btn["y"] + btn["h"] // 2
g.sh(["xdotool", "mousemove", str(bx), str(by), "click", "3"])
time.sleep(2)
c = g.patch_centre()
grab_probe = """
import ctypes, ctypes.util
x = ctypes.CDLL(ctypes.util.find_library('X11'))
x.XOpenDisplay.restype = ctypes.c_void_p
d = x.XOpenDisplay(None)
root = x.XDefaultRootWindow(ctypes.c_void_p(d))
r = x.XGrabPointer(ctypes.c_void_p(d), ctypes.c_ulong(root), 0, 0, 1, 1, 0, 0, 0)
print({0: 'GrabSuccess (nobody holds the pointer)', 1: 'AlreadyGrabbed (the app holds the pointer)'}.get(r, r))
x.XUngrabPointer(ctypes.c_void_p(d), 0); x.XCloseDisplay(ctypes.c_void_p(d))
"""
for i, (px, py) in enumerate([(c[0], c[1]), (c[0] + 10, c[1] + 5)]):
    g.sh(["xdotool", "mousemove", str(px), str(py)])
    time.sleep(1)
    hexes = [l for l in g.sh(["python3", f"{g.GATE}/atspi_tool.py", "dump"]).splitlines() if "&H" in l]
    grab = g.sh(["python3", "-c", grab_probe]).strip()
    g.log(f"probe {i}: pointer at {px},{py}: picker fields {hexes[:3]}; pointer grab: {grab}")
g.B.snap("probe-dropper")
log = [l for l in open(os.path.join(os.environ["XDG_RUNTIME_DIR"], "app.log"), errors="replace")
       if "grab" in l.lower()]
g.log("app log, grab lines:", log[-10:])
patch.terminate()
g.app("kill")
