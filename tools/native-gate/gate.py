#!/usr/bin/env python3
"""D1/D3 native gate scenarios against HikariSub in one running compositor session.

  gate.py SESSION [STEP...]      SESSION: sway | kwin | mutter | x11

Run inside the container through sessions/enter.sh SESSION (run.sh does it).
Each step drives the real application with the compositor's own input path
(sway: wlr virtual pointer + virtual keyboard; KWin and mutter: libei; X11:
XTEST through xdotool), then records screenshots, the compositor's window
list and AT-SPI state under $EVIDENCE/SESSION/, and a verdict in
results.json: observed (the expectation held), failed (it did not) or
not-observable (the platform offers no way to exercise it here, with the
reason).
"""
import json
import os
import re
import shutil
import subprocess
import sys
import time

GATE = os.environ["GATE_DIR"]
EVID = os.path.join(os.environ["EVIDENCE"], sys.argv[1])
APP = os.environ["APP"]
FIXTURES = os.environ["FIXTURES"]
HOME = os.environ["HOME"]
os.makedirs(EVID, exist_ok=True)
RESULTS_FILE = os.path.join(EVID, "results.json")
results = json.load(open(RESULTS_FILE)) if os.path.exists(RESULTS_FILE) else {}
LOG = open(os.path.join(EVID, "steps.log"), "a")


def log(*a):
    line = " ".join(str(x) for x in a)
    print(line, flush=True)
    LOG.write(time.strftime("%H:%M:%S ") + line + "\n")
    LOG.flush()


def sh(cmd, check=False, timeout=60, **kw):
    r = subprocess.run(cmd, shell=isinstance(cmd, str), capture_output=True, text=True, timeout=timeout, **kw)
    if check and r.returncode:
        raise RuntimeError(f"{cmd}: {r.stderr}")
    return r.stdout


def verdict(item, status, detail, evidence=()):
    results[item] = {"status": status, "detail": detail, "evidence": list(evidence),
                     "at": time.strftime("%Y-%m-%dT%H:%M:%S")}
    json.dump(results, open(RESULTS_FILE, "w"), indent=1)
    log(f"== {item}: {status} - {detail}")


def focus_verdict(item, ok, detail, evidence=(), targets=(), urgent=None):
    """A cross-window focus item. On sway with its default
    focus_on_window_activation (urgent), a window that asks for activation
    is marked urgent instead of focused: when the item failed and one of
    TARGETS is marked so, the app asked and sway refused, which this session
    cannot observe further (step sway-activate observes it)."""
    if ok:
        verdict(item, "observed", detail, evidence)
        return
    if B.name == "sway":
        # urgent: as the step saw it at the time, or now
        urgent = urgent if urgent is not None else [t for t in targets if B.urgent(t)]
        if urgent:
            verdict(item, "not-observable",
                    f"sway's focus_on_window_activation urgent policy: {urgent} asked for activation and were marked "
                    f"urgent instead of focused (observed under gate.py sway-activate). {detail}", evidence)
            return
    verdict(item, "failed", detail, evidence)


# ---------------------------------------------------------------- AT-SPI
def atspi():
    out = sh(["python3", f"{GATE}/atspi_tool.py", "json"], timeout=60)
    try:
        return json.loads(out)
    except ValueError:
        return {"frames": [], "panels": [], "focus": None, "focusPath": None, "texts": {}, "labels": []}


def frames(st=None):
    st = st or atspi()
    return {f["id"]: f for f in st["frames"]}


def panel_frame(st, panel):
    for p in st["panels"]:
        if p["id"] == panel:
            return p["frame"]
    return None


def panel(st, pid):
    return next((p for p in st["panels"] if p["id"] == pid), None)


def wait_for(pred, timeout=8.0, step=0.4):
    end = time.time() + timeout
    while True:
        st = atspi()
        if pred(st):
            return st
        if time.time() > end:
            return st
        time.sleep(step)


def atspi_do(role, name, action=None):
    args = ["python3", f"{GATE}/atspi_tool.py", "do", role, name] + ([action] if action else [])
    return sh(args)


# ---------------------------------------------------------------- keys
# name -> (evdev code, xdotool keysym, wtype -k keysym)
KEYS = {
    "alt": (56, "alt", "Alt_L"), "ctrl": (29, "ctrl", "Control_L"), "shift": (42, "shift", "Shift_L"),
    "down": (108, "Down", "Down"), "up": (103, "Up", "Up"), "left": (105, "Left", "Left"),
    "right": (106, "Right", "Right"), "return": (28, "Return", "Return"), "escape": (1, "Escape", "Escape"),
    "tab": (15, "Tab", "Tab"), "space": (57, "space", "space"), "f6": (64, "F6", "F6"),
    "end": (107, "End", "End"), "home": (102, "Home", "Home"), "f11": (87, "F11", "F11"),
    "f4": (62, "F4", "F4"), "super": (125, "super", "Super_L"),
    "menu": (127, "Menu", "Menu"),
}
LETTERS = "qwertyuiop asdfghjkl zxcvbnm"
CODES = {"q": 16, "w": 17, "e": 18, "r": 19, "t": 20, "y": 21, "u": 22, "i": 23, "o": 24, "p": 25,
         "a": 30, "s": 31, "d": 32, "f": 33, "g": 34, "h": 35, "j": 36, "k": 37, "l": 38,
         "z": 44, "x": 45, "c": 46, "v": 47, "b": 48, "n": 49, "m": 50, " ": 57, "1": 2, "2": 3, "3": 4}
for ch, code in CODES.items():
    KEYS.setdefault(ch, (code, "space" if ch == " " else ch, "space" if ch == " " else ch))


class Backend:
    name = "?"
    shot_ext = ".png"

    def combo(self, *names):
        raise NotImplementedError

    def type_text(self, text):
        for ch in text:
            self.combo(ch)

    def keys(self, *seq, delay=0.35):
        for item in seq:
            if isinstance(item, (int, float)):
                time.sleep(item)
                continue
            self.combo(*item.split("+"))
            time.sleep(delay)

    def shot(self, name):
        raise NotImplementedError

    def windows(self):
        return ""

    def pointer(self, *cmds):
        raise NotImplementedError

    def snap(self, name, extra=None):
        path = os.path.join(EVID, name)
        self.shot(path + ".png")
        st = atspi()
        with open(path + ".txt", "w") as f:
            f.write("# compositor windows\n" + self.windows() + "\n# AT-SPI\n" + json.dumps(st, indent=1) + "\n")
            if extra:
                f.write(extra + "\n")
        return [name + ".png", name + ".txt"], st


class EiBackend(Backend):
    """KWin and mutter: libei keyboard and absolute pointer."""

    def ei(self, *cmds):
        with open(os.path.join(os.environ["XDG_RUNTIME_DIR"], "ei.in"), "w") as f:
            f.write("\n".join(cmds) + "\n")
        time.sleep(0.05 + 0.002 * len(cmds))

    def combo(self, *names):
        codes = [KEYS[n.lower()][0] for n in names]
        self.ei(*[f"keydown {c}" for c in codes], *[f"keyup {c}" for c in reversed(codes)])

    def pointer(self, *cmds):
        self.ei(*cmds)
        for c in cmds:
            if c.startswith("wait"):
                time.sleep(int(c.split()[1]) / 1000)


class Sway(EiBackend):
    name = "sway"

    def type_text(self, text):
        sh(["wtype", "-d", "20", text])

    def combo(self, *names):
        mods = [n for n in names if n in ("alt", "ctrl", "shift")]
        keys = [n for n in names if n not in mods]
        args = ["wtype"]
        for m in mods:
            args += ["-M", {"alt": "alt", "ctrl": "ctrl", "shift": "shift"}[m]]
        for k in keys:
            args += ["-k", KEYS[k.lower()][2]]
        for m in reversed(mods):
            args += ["-m", m]
        sh(args)

    def extent(self):
        """The layout's bounding box now: the virtual pointer's absolute
        positions are fractions of it (outputs come and go in step
        outputs; a restarted vpointer starts at its default)."""
        outs = [o["rect"] for o in json.loads(self.msg("-t", "get_outputs") or "[]") if o.get("active")]
        if not outs:
            return 2400, 1000
        return max(r["x"] + r["width"] for r in outs), max(r["y"] + r["height"] for r in outs)

    def pointer(self, *cmds):
        w, h = self.extent()
        with open(os.path.join(os.environ["XDG_RUNTIME_DIR"], "vpointer.in"), "w") as f:
            f.write(f"extent {w} {h}\n" + "\n".join(cmds) + "\n")
        for c in cmds:
            if c.startswith("wait"):
                time.sleep(int(c.split()[1]) / 1000)
        time.sleep(0.2)

    def shot(self, path):
        # In layout coordinates: grim otherwise captures the whole layout at
        # the highest output scale (2 with HEADLESS-2), and the pixel checks
        # aim by layout position. The scale-2 output's own capture is in
        # step outputs.
        sh(["grim", "-s", "1", path])

    def windows(self):
        return sh(["python3", f"{GATE}/swaytree.py"])

    def urgent(self, title):
        """Whether sway marked the window urgent: it asked for activation
        (xdg_activation_v1) and sway's focus_on_window_activation urgent
        policy refused the focus."""
        # the main window is titled "<document> - HikariSub" with a Document open
        rx = re.compile(r"'(.* - )?" + re.escape(title) + "'")
        return any(rx.search(l) and "URGENT" in l for l in self.windows().splitlines())

    def client_rect(self, title):
        for line in self.windows().splitlines():
            m = re.search(r"'(.*)' .* client=(-?\d+),(-?\d+) (\d+)x(\d+) output=(\S+)", line)
            if m and m.group(1) == title:
                return tuple(int(m.group(i)) for i in range(2, 6)) + (m.group(6),)
        return None

    def msg(self, *args):
        return sh(["swaymsg", *args])


class KWin(EiBackend):
    name = "kwin"

    def shot(self, path):
        sh(["python3", f"{GATE}/kwin_shot.py", path], timeout=30)

    def windows(self):
        return sh([f"{GATE}/kwin_windows.sh"])

    def script(self, js):
        path = f"/tmp/gate-{time.time_ns()}.js"
        open(path, "w").write(js)
        i = sh(["gdbus", "call", "--session", "--dest", "org.kde.KWin", "--object-path", "/Scripting",
                "--method", "org.kde.kwin.Scripting.loadScript", path, os.path.basename(path)])
        num = re.sub(r"\D", "", i)
        sh(["gdbus", "call", "--session", "--dest", "org.kde.KWin", "--object-path", f"/Scripting/Script{num}",
            "--method", "org.kde.kwin.Script.run"])
        time.sleep(0.5)
        sh(["gdbus", "call", "--session", "--dest", "org.kde.KWin", "--object-path", "/Scripting",
            "--method", "org.kde.kwin.Scripting.unloadScript", os.path.basename(path)])

    def client_rect(self, title):
        st = atspi()
        fr = frames(st).get(title)
        for line in self.windows().splitlines():
            m = re.match(r'\S+ "(.*)" (-?[\d.]+),(-?[\d.]+) (\d+)x(\d+) output=(\S+)', line)
            if m and m.group(1) == title and fr:
                x, y, w, h = float(m.group(2)), float(m.group(3)), int(m.group(4)), int(m.group(5))
                deco = h - fr["h"]
                return (int(x + (w - fr["w"]) / 2), int(y + deco - (w - fr["w"]) / 2), fr["w"], fr["h"], m.group(6))
        return None


class Mutter(EiBackend):
    name = "mutter"

    def shot(self, path):
        sh(["python3", f"{GATE}/mutter_shot.py", path], timeout=60)

    def windows(self):
        return sh(["gdctl", "show"]) if shutil.which("gdctl") else ""

    def client_rect(self, title):
        # mutter without gnome-shell publishes no window geometry. The main
        # window is placed centred on the 1600x1000 monitor with Qt's client
        # side decoration (30 px) above its 1280x800 client area; floating
        # panel windows are placed by mutter and cannot be located.
        if title == "HikariSub":
            return (160, 115, 1280, 800, "Meta-0")
        return None


class X11(Backend):
    name = "x11"

    def combo(self, *names):
        sh(["xdotool", "key", "+".join(KEYS[n.lower()][1] for n in names)])

    def pointer(self, *cmds):
        args = ["xdotool"]
        for c in cmds:
            p = c.split()
            if p[0] == "move":
                args += ["mousemove", p[1], p[2]]
            elif p[0] == "down":
                args += ["mousedown", "1"]
            elif p[0] == "up":
                args += ["mouseup", "1"]
            elif p[0] == "wait":
                args += ["sleep", str(int(p[1]) / 1000)]
        sh(args)

    def shot(self, path):
        sh(["import", "-window", "root", path])

    def windows(self):
        out = []
        for w in sh("xdotool search --onlyvisible --name '.'").split():
            name = sh(["xdotool", "getwindowname", w]).strip()
            if name:
                out.append(f"{w} {name!r} " + sh(["xdotool", "getwindowgeometry", w]).replace("\n", " "))
        return "\n".join(out)

    def client_rect(self, title):
        fr = frames().get(title)
        return (fr["x"], fr["y"], fr["w"], fr["h"], "screen0") if fr else None


class SwayActivate(Sway):
    """sway with focus_on_window_activation focus: sway's default policy
    ("urgent") marks a window that asks for activation (xdg_activation_v1,
    with a token from the focused surface and its input serial) urgent
    instead of focusing it, so cross-window F6 needs this setting there.
    Evidence goes to its own directory (sway-activate)."""

    def __init__(self):
        sh(["swaymsg", "focus_on_window_activation", "focus"])


BACKENDS = {"sway": Sway, "sway-activate": SwayActivate, "kwin": KWin, "mutter": Mutter, "x11": X11}
B = BACKENDS[sys.argv[1]]()


# ---------------------------------------------------------------- app control
def app(cmd, *args, env=None):
    e = dict(os.environ, **(env or {}))
    return sh([f"{GATE}/app.sh", cmd, *args], env=e, timeout=90)


def fresh(*args, env=None):
    app("fresh")
    app("start", *args, env=env)
    time.sleep(2)
    return atspi()


def episode():
    path = os.path.join(HOME, "episode.ass")
    with open(path, "w") as f:
        f.write("[Script Info]\nScriptType: v4.00+\n\n[V4+ Styles]\n"
                "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                "Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
                "MarginL, MarginR, MarginV, Encoding\n"
                "Style: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\n\n"
                "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,first\n"
                "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,second\n")
    return path


# View menu paths. Opening the View menu highlights nothing; Down picks the
# first item. Panels: Video, Audio, Line editor, Grid, Reference, Timing, Search.
PANELS = ["Video", "Audio", "Line editor", "Grid", "Reference", "Timing", "Search"]


def open_view():
    """Alt+W opens the View menu ("Vie&w": Alt+V stays with legacy's &Video).
    Retried until the View menu (its "Move panel…" item) shows."""
    for _ in range(3):
        B.keys("alt+w", 0.4)
        dump = sh(["python3", f"{GATE}/atspi_tool.py", "dump"])
        if any("'Move panel…'" in l and "showing" in l for l in dump.splitlines()):
            return True
        B.keys("escape", 0.3)
    log("open_view: the View menu did not open")
    return False


def menu_to(name, opener=None):
    """Open a menu (the View menu by default) and press Down until the
    highlight is on the item NAME. D2 put the five arrangements first in the
    View menu, enabled by what is open, and Down skips disabled items, so the
    Downs are counted among the enabled items AT-SPI lists (`atspi_tool.py
    menu`; AT-SPI does not reliably follow the highlight)."""
    if opener is None:
        if not open_view():
            return False
    else:
        B.keys(opener, 0.6)
    try:
        menus = json.loads(sh(["python3", f"{GATE}/atspi_tool.py", "menu"]) or "[]")
    except ValueError:
        menus = []
    items = menus[-1] if menus else []
    enabled = [i["name"] for i in items if i["enabled"]]
    if name not in enabled:
        log(f"menu_to: {name!r} is not an enabled item of {items}")
        return False
    B.keys(*(["down"] * (enabled.index(name) + 1)), delay=0.3)
    return True


def view_to(name):
    return menu_to(name)


def panel_menu(panel, item):
    """View > Panels > PANEL > ITEM (Show, Hide, Float, Dock) from the keyboard."""
    view_to("Panels")
    seq = ["right", 0.3] + ["down"] * PANELS.index(panel) + ["right", 0.3]
    seq += ["down"] * ["Show", "Hide", "Float", "Dock"].index(item) + ["return"]
    B.keys(*seq)
    time.sleep(1.2)


def float_panel(name, tag):
    """Float a panel from the keyboard: View > Panels > NAME > Float, or, when
    that submenu does not open, View > Move panel… with Place: Float."""
    panel_menu(name, "Float")
    st = wait_for(lambda s: name in frames(s), timeout=4)
    if name in frames(st):
        return "menu"
    log(f"{tag}: View > Panels > {name} > Float did not float it; using View > Move panel…")
    B.keys("escape", "escape", "escape", "escape")
    focus_main_compositor()
    view_to("Move panel…")
    B.keys("return", 1.2, "home", *(["down"] * PANELS.index(name)), "tab", "home",
           *(["down"] * 5), "tab", "space", 1.5)
    st = wait_for(lambda s: name in frames(s), timeout=4)
    if "Move panel" in frames(st):
        close_window("Move panel")
    return "move-panel" if name in frames(st) else None


def close_window(title):
    rx = "HikariSub$" if title == "HikariSub" else f"^{title}$"
    if B.name == "sway":
        B.msg(f'[title="{rx}"] kill')
    elif B.name == "x11":
        # Visible windows only: Qt keeps hidden windows titled HikariSub too.
        w = sh(f"xdotool search --onlyvisible --name '{rx}'").split()
        for wid in w:
            sh(["xdotool", "windowclose", wid]) if False else sh(["xdotool", "windowactivate", "--sync", wid, "key", "alt+F4"])
    elif B.name == "kwin":
        B.script(f'for (const w of workspace.windowList()) if (/{rx}/.test(w.caption)) w.closeWindow();')
    else:
        B.keys("escape")
    time.sleep(0.5)


def focus_main_compositor():
    """Give the main window the focus the way a user would with the
    compositor (its own focus command, or a click on the main window's title)."""
    if B.name == "sway":
        B.msg('[title="HikariSub$"] focus')
    elif B.name == "x11":
        sh("xdotool search --onlyvisible --name 'HikariSub$' windowactivate")
    elif B.name == "kwin":
        B.script('for (const w of workspace.windowList()) if (/HikariSub$/.test(w.caption)) workspace.activeWindow = w;')
    else:
        r = B.client_rect("HikariSub")
        B.pointer(f"move {r[0] + 400} {r[1] - 15}", "wait 150", "down", "wait 50", "up", "wait 400")
    time.sleep(0.8)


def focus_main():
    """Bring the keyboard focus back to the main window from the keyboard:
    F6 until a panel of the main window has it (the shell activates the
    window of the panel it moves to)."""
    for _ in range(6):
        st = atspi()
        main = frames(st).get("HikariSub")
        if main and main["active"] and st["focusPath"] and "frame:'Line editor'" not in st["focusPath"] \
                and not any(f["active"] for f in st["frames"] if f["id"] != "HikariSub"):
            return True
        B.combo("f6")
        time.sleep(0.8)
    focus_main_compositor()
    return False


def f6_walk(n=5, key="f6"):
    seq = []
    for _ in range(n):
        B.combo(*key.split("+"))
        time.sleep(0.8)
        st = atspi()
        active = [f["name"] for f in st["frames"] if f["active"]]
        seq.append({"focus": st["focus"], "path": st["focusPath"], "active": active})
    return seq


def panel_of(path):
    if not path:
        return None
    for name in ["Video", "Audio", "Line editor", "Grid", "Editing:", "No document open", "Reference", "Timing", "Shift times",
                 "Search"]:
        if f"panel:'{name}" in path or f"panel:\"{name}" in path:
            return "Grid" if name in ("Grid", "Editing:", "No document open") else name
    return None


def dismiss_notices():
    """Opening a Document may raise the spelling notice (no dictionaries in
    the private profile); press its OK so keys reach the shell."""
    sh(["python3", f"{GATE}/atspi_tool.py", "press-showing", "OK"])
    time.sleep(0.5)


# ---------------------------------------------------------------- D3 chrome
# docs/qt/docking.md "Chrome": one 35-pixel header per group (a lone panel's
# title bar, or tabs), its "⋯" button named "<panel> options" opening the
# panel's menu (Move panel…, Undock or Dock, Close); floating panels are
# borderless with a drawn 8-pixel shadow, resized from it through the window
# system; on Wayland the header's title docks (the engine's drag-and-drop)
# and its free part moves the window through the compositor.
SHADOW = 8
HEADER = 35
ACCENT = "#9CDBC9"  # the dark theme's default (green) accent: only the drop highlight uses it
WAYLAND = sys.argv[1] in ("sway", "sway-activate", "kwin", "mutter")


def where(role, name, frame=None):
    """Showing objects with that role and name, in window coordinates."""
    try:
        hits = json.loads(sh(["python3", f"{GATE}/atspi_tool.py", "where", role, name]) or "[]")
    except ValueError:
        return []
    return [h for h in hits if frame is None or h["frame"] == frame]


def raw_pixels(path):
    """(width, height, RGB bytes) of a screenshot."""
    w, h = (int(v) for v in sh(["magick", "identify", "-format", "%w %h", path]).split())
    raw = subprocess.run(["magick", path, "-alpha", "off", "-depth", "8", "rgb:-"], capture_output=True).stdout
    return w, h, raw


def locate_floating(title, scale=1, x_from=0, path=None):
    """mutter publishes no window geometry: find the floating panel's frame
    (its boundary, as big as AT-SPI says the window is less the shadow, in
    pixels of a monitor at SCALE) in a screenshot, from image column X_FROM
    on (mutter_shot puts the monitors side by side). Returns (x, y, w, h,
    "screenshot") of the whole window, shadow included, in image pixels, or
    None."""
    fr = frames().get(title)
    if not fr:
        return None
    if path is None:
        path = os.path.join(EVID, "locate.png")
        B.shot(path)
    iw, ih, raw = raw_pixels(path)
    fw, fh = (fr["w"] - 2 * SHADOW) * scale, (fr["h"] - 2 * SHADOW) * scale
    for y in range(ih - fh + 1):
        row = raw[y * iw * 3:(y + 1) * iw * 3]
        x = x_from
        while x < iw:
            c = row[3 * x:3 * x + 3]
            e = x + 1
            while e < iw and row[3 * e:3 * e + 3] == c:
                e += 1
            run = e - x
            if fw - 10 * scale <= run <= fw - 2:
                left = x - (fw - run) // 2
                # the left boundary: the same colour down the frame's side
                ok = 0 <= left < iw
                for k in range(1, 9):
                    yy = y + k * fh // 9
                    if not ok or yy >= ih:
                        ok = False
                        break
                    ok = raw[(yy * iw + left) * 3:(yy * iw + left) * 3 + 3] == c
                if ok:
                    return (left - SHADOW * scale, y - SHADOW * scale, fr["w"] * scale, fr["h"] * scale, "screenshot")
            x = e
    return None


def origin(frame):
    """A window's position on the screen and size (x, y, w, h, output)."""
    r = B.client_rect(frame)
    if r is None and B.name == "mutter" and frame != "HikariSub":
        r = locate_floating(frame)
    return r


def click(x, y, double=False, wait=900):
    cmds = [f"move {x} {y}", "wait 250", "down", "wait 50", "up"]
    if double:
        cmds += ["wait 60", "down", "wait 40", "up"]
    B.pointer(*cmds, f"wait {wait}")


def drag(sx, sy, ex, ey, steps=30, release=True):
    cmds = [f"move {sx} {sy}", "wait 250", "down", "wait 250"]
    for i in range(1, steps + 1):
        cmds += [f"move {sx + (ex - sx) * i // steps} {sy + (ey - sy) * i // steps}", "wait 40"]
    cmds += [f"move {ex + 3} {ey + 3}", "wait 300", f"move {ex} {ey}", "wait 700"]
    if release:
        cmds += ["up", "wait 1200"]
    B.pointer(*cmds)


def accent_pixels(png, box=None):
    """Pixels of the accent colour in a screenshot (in box: x, y, w, h) and
    their bounding box."""
    args = ["magick", os.path.join(EVID, png), "-alpha", "off"]
    if box:
        args += ["-crop", f"{box[2]}x{box[3]}+{box[0]}+{box[1]}", "+repage"]
    args += ["-fuzz", "4%", "-fill", "black", "+opaque", ACCENT, "-fill", "white", "-opaque", ACCENT,
             "-colorspace", "gray", "-format", "%[fx:round(mean*w*h)] %@", "info:"]
    out = sh(args).split()
    try:
        return int(out[0]), (out[1] if len(out) > 1 else "")
    except (ValueError, IndexError):
        return 0, ""


def shadow_probe(png, r):
    """The drawn shadow around a floating window r (x, y, w, h): where the
    window system composites, its outermost rings show what is behind
    (almost transparent); without compositing they come out opaque black.
    Each edge's outermost ring is black or not, where the background 3
    pixels outside it is not itself black."""
    iw, ih, raw = raw_pixels(os.path.join(EVID, png))

    def px(x, y):
        if not (0 <= x < iw and 0 <= y < ih):
            return None
        i = (y * iw + x) * 3
        return tuple(raw[i:i + 3])

    x, y, w, h = r[:4]
    samples = []
    for k in range(1, 8):
        fx, fy = x + w * k // 8, y + h * k // 8
        samples += [((fx, y + 1), (fx, y - 3)), ((fx, y + h - 2), (fx, y + h + 2)),
                    ((x + 1, fy), (x - 3, fy)), ((x + w - 2, fy), (x + w + 2, fy))]
    see_through, opaque, unknown = 0, 0, 0
    for inner, outer in samples:
        a, b = px(*inner), px(*outer)
        if a is None or b is None or max(b) < 16:
            unknown += 1
        elif max(a) < 16:
            opaque += 1  # black where the background is not: the margin is not see-through
        else:
            see_through += 1
    # the inner rings darken what is behind: the ring next to the frame
    inner_ring = [px(x + w // 2, y + SHADOW - 1), px(x + w // 2, y - 3)]
    return {"see-through": see_through, "black": opaque, "background black or off screen": unknown,
            "ring next to the frame vs outside (top middle)": inner_ring}


def header_point(r, part="title"):
    """A point on a floating panel's header: its title (the engine's drag)
    or, on Wayland, its free part right of the title (the compositor's
    move), keeping clear of the "⋯" button."""
    top = r[1] + SHADOW + 1
    if part == "title":
        return r[0] + SHADOW + 1 + 22, top + HEADER // 2
    return r[0] + r[2] - SHADOW - 1 - 12 - 20 - 2 - 24, top + HEADER // 2


# ---------------------------------------------------------------- steps
def step_default():
    st = fresh()
    ev, st = B.snap("default-arrangement")
    fr = frames(st)
    docked = {p["id"]: p["frame"] for p in st["panels"] if p["id"] in ("Video", "Audio", "Line editor", "Grid")}
    ok = len(fr) == 1 and "HikariSub" in fr and len(docked) == 4 and all(v == "HikariSub" for v in docked.values())
    pos = {p["id"]: (p["x"], p["y"]) for p in st["panels"]}
    shape = (ok and pos["Video"][0] < pos["Audio"][0] and pos["Audio"][1] < pos["Line editor"][1]
             and pos["Grid"][1] > pos["Video"][1])
    verdict("default-arrangement", "observed" if shape else "failed",
            f"one window; panels {docked}; Video left, Audio over Line editor, Grid below: {bool(shape)}", ev)


def step_keyboard_float_dock():
    path = episode()
    st = fresh(path)
    dismiss_notices()
    atspi_do("text", "Line text", "SetFocus")
    time.sleep(0.5)
    B.keys("end")
    B.type_text(" draft")
    time.sleep(0.8)
    # Menus open from the Grid: in the Line text field Alt+V types a "v"
    # (recorded separately by step "menu-from-text").
    B.keys("f6", 0.5)
    st = atspi()
    before = st["texts"].get("Line text")
    labels_before = st["labels"]
    ev0, _ = B.snap("kbd-0-draft")
    how = float_panel("Line editor", "kbd")
    st = wait_for(lambda s: "Line editor" in frames(s))
    ev1, st = B.snap("kbd-1-floated")
    floated = "Line editor" in frames(st) and panel_frame(st, "Line editor") == "Line editor"
    text_float = st["texts"].get("Line text")
    labels_float = st["labels"]
    verdict("keyboard-float", "observed" if how == "menu" and floated else "failed",
            f"View > Panels > Line editor > Float from the keyboard: own window={floated} (floated by {how})",
            ev0 + ev1)
    if how == "move-panel":
        verdict("keyboard-float-move-panel", "observed" if floated else "failed",
                "View > Move panel… Place: Float (fallback after the View > Panels path failed): own window "
                f"{floated}", ev1)
    if not floated:
        verdict("keyboard-dock", "not-observable", "the Line editor could not be floated from the keyboard")
        return
    verdict("draft-kept-across-float", "observed" if floated and text_float == before and before.endswith(" draft") else "failed",
            f"Line text before {before!r}, floating {text_float!r}; status {labels_before} -> {labels_float}", ev0 + ev1)
    # Dock: Float is disabled on a floating panel, so count positions both ways.
    focus_main()
    panel_menu("Line editor", "Dock")
    st = wait_for(lambda s: "Line editor" not in frames(s))
    if "Line editor" in frames(st):
        log("Dock at the 4th position did not dock; trying the 3rd (disabled items skipped)")
        B.keys("escape", "escape", "escape")
        focus_main()
        view_to("Panels")
        B.keys("right", 0.3, "down", "down", "right", 0.3, "down", "down", "return")
        st = wait_for(lambda s: "Line editor" not in frames(s))
    ev2, st = B.snap("kbd-2-docked")
    docked = "Line editor" not in frames(st) and panel_frame(st, "Line editor") == "HikariSub"
    verdict("keyboard-dock", "observed" if docked else "failed",
            f"View > Panels > Line editor > Dock from the keyboard: back in the main window={docked}", ev2)
    text_dock = st["texts"].get("Line text")
    verdict("draft-kept-across-dock", "observed" if docked and text_dock == before else "failed",
            f"Line text after docking {text_dock!r}; status {st['labels']}", ev2)


def step_f6_floating():
    path = episode()
    fresh(path)
    dismiss_notices()
    how = float_panel("Line editor", "f6")
    st = atspi()
    ev0, st = B.snap("f6-0-after-float")
    after_float = {"active": [f["id"] for f in st["frames"] if f["active"]], "focus": st["focusPath"]}
    # (a) F6 straight after Float, from wherever the shell left the focus.
    walk_a = f6_walk(4)
    ev1, _ = B.snap("f6-1-after-f6-from-float")
    urgent_a = [t for t in ("HikariSub",) if B.name == "sway" and B.urgent(t)]
    # (b) From the Grid of the main window (focused through the compositor and the Grid's own action).
    focus_main_compositor()
    st = atspi()
    start_b = {"active": [f["id"] for f in st["frames"] if f["active"]], "focus": st["focusPath"]}
    walk_b = f6_walk(5)
    back = f6_walk(3, "shift+f6")
    ev2, st = B.snap("f6-2-walk-from-main")
    open(os.path.join(EVID, "f6-floating.json"), "w").write(json.dumps(
        {"floated_by": how, "after_float": after_float, "f6_after_float": walk_a, "main_focused": start_b,
         "f6_from_main": walk_b,
         "shift_f6": back}, indent=1))
    reached_a = [panel_of(w["path"]) for w in walk_a]
    reached_b = [panel_of(w["path"]) for w in walk_b]
    float_active_b = any("Line editor" in w["active"] for w in walk_b)
    verdict("focus-after-float", "observed" if after_float["focus"] else "failed",
            f"floated by {how}; then active windows {after_float['active']}, focused object {after_float['focus']}",
            ev0)
    focus_verdict("f6-from-floating-panel", any(x and x != "Line editor" for x in reached_a),
                  f"F6 x4 right after Float reached {reached_a} (active {[w['active'] for w in walk_a]})", ev1,
                  targets=("HikariSub",), urgent=urgent_a)
    focus_verdict("f6-into-floating-panel", "Line editor" in reached_b and float_active_b,
            f"main window focused by the compositor (focus {start_b['focus']}); F6 x5 reached {reached_b} (active {[w['active'] for w in walk_b]}); "
            f"Shift+F6 x3 {[panel_of(w['path']) for w in back]}", ev2 + ["f6-floating.json"], targets=("Line editor",))
    # Ctrl+Shift+H from the floating panel opens History.
    for _ in range(6):
        if panel_of(atspi()["focusPath"]) == "Line editor":
            break
        B.combo("f6")
        time.sleep(0.8)
    in_float = panel_of(atspi()["focusPath"]) == "Line editor"
    B.combo("ctrl", "shift", "h")
    st = wait_for(lambda s: any(f["name"].startswith("History") for f in s["frames"]))
    ev3, st = B.snap("ctrl-shift-h-from-floating")
    hist = any(f["name"].startswith("History") for f in st["frames"])
    if in_float or B.name != "sway":
        verdict("shortcut-from-floating-panel", ("observed" if hist else "failed") if in_float else "failed",
                f"focus in the floating Line editor: {in_float}; Ctrl+Shift+H opened History: {hist}", ev3)
    else:
        focus_verdict("shortcut-from-floating-panel", False,
                      f"the focus never reached the floating Line editor by F6; Ctrl+Shift+H opened History: {hist}",
                      ev3, targets=("Line editor",))
    B.keys("escape")


def combo_texts(frame="Move panel"):
    """The displayed text of each showing combo box (its text child) of a
    window (the placement window: the Line editor's own combo boxes are empty
    without a Document)."""
    code = ("import sys; sys.argv=['x','dump']\n"
            f"exec(open('{GATE}/atspi_tool.py').read().split('def main')[0])\n"
            "a = app()\n"
            f"f = [a.get_child_at_index(i) for i in range(a.get_child_count()) if a.get_child_at_index(i).get_name() == {frame!r}]\n"
            "for o in (all_objects(f[0]) if f else []):\n"
            "    if o.get_role_name()=='combo box' and o.get_state_set().contains(Atspi.StateType.SHOWING):\n"
            "        print(o.get_name() + '=' + repr(text_of(o.get_child_at_index(0))))\n")
    return sh(["python3", "-c", code]).strip().replace("\n", "; ")


def step_move_panel():
    fresh()
    view_to("Move panel…")
    B.keys("return", 1.2)
    st = wait_for(lambda s: "Move panel" in frames(s))
    combos_open = combo_texts()
    ev0, st = B.snap("move-panel-0-open", extra="# combo boxes\n" + combos_open)
    if "Move panel" not in frames(st):
        verdict("keyboard-move-panel", "failed", "View > Move panel… did not open the placement window", ev0)
        return
    verdict("move-panel-defaults", "observed" if "''" not in combos_open else "failed",
            f"placement window on open (Grid focused): {combos_open}", ev0)
    # Panel: Grid -> Audio (Up x2); Place: Left of (Down); Next to: Grid (Up x6, Down x3); Move.
    B.keys("up", "up", "tab", "down", "tab", *(["up"] * 6), *(["down"] * 3), 0.3)
    combos_set = combo_texts()
    B.keys("tab", "space", 1.5)
    st = atspi()
    ev1, st = B.snap("move-panel-1-left-of-grid", extra="# combo boxes before Move\n" + combos_set)
    pos = {p["id"]: p for p in st["panels"]}
    a, g = pos.get("Audio"), pos.get("Grid")
    # D3: side by side their headers share a row (Audio's a title bar, the
    # Grid's a tab bar, whose content starts 8 lower).
    ah, gh = where("text", "Audio", "HikariSub"), where("page tab", "Grid", "HikariSub")
    ok = a and g and a["frame"] == g["frame"] == "HikariSub" and a["x"] < g["x"] and ah and gh \
        and abs(ah[0]["y"] - gh[0]["y"]) < 3
    verdict("keyboard-move-panel", "observed" if ok else "failed",
            f"Move panel ({combos_set}) -> Audio {a and (a['x'], a['y'], a['w'], a['h'])} header {ah[:1]}, "
            f"Grid {g and (g['x'], g['y'], g['w'], g['h'])} header {gh[:1]}; focus {st['focusPath']}", ev0 + ev1)
    close_window("Move panel")


def step_pointer():
    """D3 with real compositor input: the "⋯" button and its menu's Undock,
    double-click on headers (float, dock), and a floating panel dragged by
    its title onto the Grid, with the drop highlight, docking there."""
    fresh()
    main = origin("HikariSub")
    if main is None:
        for item in ("pointer-menu-undock", "pointer-dblclick-float-redock", "pointer-drag-dock",
                     "drop-highlight"):
            verdict(item, "not-observable", f"{B.name}: no window geometry to aim real pointer input")
        return
    x0, y0 = main[0], main[1]
    # 1. Audio's "⋯" button, then Undock in its menu, both by the pointer.
    btn = where("button", "Audio options", "HikariSub")
    ev = []
    menu = []
    if btn:
        b = btn[0]
        click(x0 + b["x"] + b["w"] // 2, y0 + b["y"] + b["h"] // 2)
        menu = [n for n in ("Move panel…", "Undock", "Dock", "Close") if where("menu item", n)]
        ev, _ = B.snap("ptr-1-menu")
        und = where("menu item", "Undock", "HikariSub")
        if und:
            u = und[0]
            click(x0 + u["x"] + 30, y0 + u["y"] + u["h"] // 2, wait=1200)
    st = wait_for(lambda s: "Audio" in frames(s))
    ev1, st = B.snap("ptr-2-undocked")
    floated = "Audio" in frames(st)
    verdict("pointer-menu-undock",
            "observed" if btn and menu == ["Move panel…", "Undock", "Close"] and floated else "failed",
            f"click on 'Audio options' {btn[:1]}: menu {menu}; click on Undock -> own window={floated}", ev + ev1)
    # 2. Double-click the docked Video header: floats; double-click its
    # floating header (the title): docks.
    video = panel(st, "Video")
    vx, vy = x0 + video["x"] + 30, y0 + video["y"] - HEADER // 2
    click(vx, vy, double=True, wait=1500)
    st = wait_for(lambda s: "Video" in frames(s))
    ev2, st = B.snap("ptr-3-dblclick-float")
    dfloat = "Video" in frames(st)
    redock, ev3, r = False, [], None
    if dfloat:
        r = origin("Video")
        if r:
            click(*header_point(r), double=True, wait=1500)
            st = wait_for(lambda s: "Video" not in frames(s))
            ev3, st = B.snap("ptr-4-dblclick-redock")
            redock = "Video" not in frames(st)
    if dfloat and r is None:
        verdict("pointer-dblclick-float-redock", "not-observable",
                f"double-click on the docked Video header floated it ({dfloat}); the floating window cannot be "
                f"located on {B.name} to double-click its header", ev2)
    else:
        verdict("pointer-dblclick-float-redock", "observed" if dfloat and redock else "failed",
                f"double-click on the docked Video header at {vx},{vy}: floated={dfloat}; double-click on its "
                f"floating header's title {r and header_point(r)}: docked={redock}", ev2 + ev3)
    # 3. Drag the floating Audio panel by its title onto the Grid's centre:
    # the accent highlight covers the Grid's group, and the release docks it.
    r = origin("Audio")
    st = atspi()
    grid = panel(st, "Grid")
    if not r or not grid:
        for item in ("pointer-drag-dock", "drop-highlight"):
            verdict(item, "not-observable", f"could not locate the floating Audio window ({r}) or the Grid")
        return
    sx, sy = header_point(r)
    gx, gy = x0 + grid["x"] + grid["w"] // 2, y0 + grid["y"] + grid["h"] // 2 - 15
    ev4, _ = B.snap("ptr-5-before-drag")
    base, _ = accent_pixels("ptr-5-before-drag.png")
    drag(sx, sy, gx, gy, release=False)
    ev5, _ = B.snap("ptr-6-drag-over-grid")
    box = (x0 + grid["x"] - 4, y0 + grid["y"] - HEADER - 12, grid["w"] + 8, grid["h"] + HEADER + 16)
    lit, bbox = accent_pixels("ptr-6-drag-over-grid.png", box)
    total, _ = accent_pixels("ptr-6-drag-over-grid.png")
    B.pointer("up", "wait 1500", f"move {gx + 5} {gy + 5}", "wait 300")
    st = atspi()
    ev6, st = B.snap("ptr-7-dropped")
    docked = "Audio" not in frames(st) and panel_frame(st, "Audio") == "HikariSub"
    verdict("drop-highlight", "observed" if lit > 400 and total - base > 400 else "failed",
            f"accent ({ACCENT}) pixels: {base} before the drag, {total} while over the Grid, {lit} of them over "
            f"the Grid's group {box} (bounding box {bbox} in it)", ev4 + ev5)
    verdict("pointer-drag-dock", "observed" if docked else "failed",
            f"drag floating Audio by its title from {sx},{sy} to the Grid centre {gx},{gy} and release: "
            f"docked={docked}, windows {list(frames(st))}", ev5 + ev6)


def step_floating():
    """D3's floating window: the same header, the drawn shadow, moved (X11:
    the engine's drag by the header; Wayland: the compositor's move from the
    header's free part) and resized from the shadow through the window
    system."""
    fresh(env={"QT_LOGGING_RULES": "hikari.decorations.debug=true"})
    how = float_panel("Audio", "floating")
    st = wait_for(lambda s: "Audio" in frames(s))
    r = origin("Audio")
    if r is None:
        for item in ("floating-header", "floating-shadow", "floating-move", "floating-resize"):
            verdict(item, "not-observable", f"the floating Audio window (floated by {how}) cannot be located on "
                                            f"{B.name}")
        return
    # X11 here runs no compositing manager (openbox alone): no transparency,
    # so no shadow (Docking.floatingShadow 0); a picom run below checks it.
    shadow = 0 if B.name == "x11" else SHADOW
    floating_frame(r, shadow, "float-0-window")
    floating_decoration("Audio", r)
    # Move.
    if WAYLAND:
        px, py = header_point(r, part="free")
        how_move = "the header's free part (the compositor's move, startSystemMove)"
    else:
        px, py = header_point(r)
        how_move = "the header's title (the engine's drag)"
    # somewhere no drop is offered: away from the main window where there is room
    main = origin("HikariSub")
    dx, dy = -160, 120
    if main and not WAYLAND:
        dx = (main[0] - 40 - r[2]) - r[0] if main[0] - 40 - r[2] > 0 else (main[0] + main[2] + 40) - r[0]
        dy = 60
    drag(px, py, px + dx, py + dy)
    st = wait_for(lambda s: "Audio" in frames(s), timeout=3)
    r2 = origin("Audio")
    ev1, st = B.snap("float-1-moved")
    still = "Audio" in frames(st)
    # Wayland: the compositor takes over once the pointer has moved the
    # drag distance (about 10 pixels), which the window does not follow
    moved = r2 is not None and abs((r2[0] - r[0]) - dx) <= 16 and abs((r2[1] - r[1]) - dy) <= 16
    verdict("floating-move", "observed" if still and moved else "failed",
            f"dragged {how_move} from {px},{py} by {dx},{dy}: window {r[:2]} -> {r2 and r2[:2]}, still floating "
            f"{still}", ev1)
    if WAYLAND and still and r2:
        fx, fy = header_point(r2, part="free")
        click(fx, fy, double=True, wait=1500)
        st = wait_for(lambda s: "Audio" not in frames(s))
        ev, st = B.snap("float-2-free-part-dblclick")
        docked = "Audio" not in frames(st)
        verdict("wayland-free-part-dblclick-docks", "observed" if docked else "failed",
                f"double-click on the header's free part at {fx},{fy}: docked={docked}", ev)
        if docked:
            float_panel("Audio", "floating")
            wait_for(lambda s: "Audio" in frames(s))
    # Resize from the bottom-right corner of the shadow.
    r = origin("Audio")
    st = atspi()
    fr = frames(st).get("Audio")
    if not r or not fr:
        verdict("floating-resize", "not-observable", f"the floating Audio window could not be located again ({r})")
        return
    w0, h0 = fr["w"], fr["h"]
    cx, cy = r[0] + r[2] - 2, r[1] + r[3] - 2
    drag(cx, cy, cx + 90, cy + 70)
    st = wait_for(lambda s: frames(s).get("Audio", {}).get("w", 0) > w0, timeout=3)
    fr = frames(st).get("Audio", {})
    ev2, st = B.snap("float-3-resized")
    grew = fr.get("w", 0) >= w0 + 60 and fr.get("h", 0) >= h0 + 40
    verdict("floating-resize", "observed" if grew else "failed",
            f"dragged the window's bottom-right corner {cx},{cy} ({'the shadow' if shadow else 'the band inside the frame'}) "
            f"by 90,70: {w0}x{h0} -> {fr.get('w')}x{fr.get('h')}", ev2)
    if B.name.startswith("sway"):
        floating_forced_frame()
    if B.name == "x11":
        # With a compositing manager the drawn shadow comes back (Qt follows
        # the _NET_WM_CM_S0 selection; a window floated now reads it).
        # An empty configuration: Arch's /etc/xdg/picom.conf draws picom's
        # own shadows, which would darken what this compares against.
        comp = subprocess.Popen(["picom", "--config", "/dev/null", "--backend", "xrender", "--no-fading-openclose"],
                                stdout=subprocess.DEVNULL, stderr=open(os.path.join(EVID, "picom.log"), "w"))
        time.sleep(2)
        try:
            focus_main_compositor()
            panel_menu("Audio", "Dock")
            wait_for(lambda s: "Audio" not in frames(s))
            focus_main_compositor()
            float_panel("Audio", "floating-composited")
            wait_for(lambda s: "Audio" in frames(s))
            time.sleep(1)
            # over the main window, so the shadow has something to show through
            main = origin("HikariSub")
            for wid in sh("xdotool search --onlyvisible --name '^Audio$'").split():
                sh(["xdotool", "windowmove", wid, str(main[0] + 300), str(main[1] + 200)])
            time.sleep(1)
            r = origin("Audio")
            if r is None:
                verdict("floating-shadow", "not-observable", "the floating Audio window could not be located with picom")
            else:
                floating_frame(r, SHADOW, "float-4-composited", suffix="-composited")
        finally:
            comp.terminate()
            comp.wait(10)
            time.sleep(1)


def floating_decoration(title, r):
    """D3: a floating panel is a borderless tool window; the window system
    adds no frame or title bar of its own."""
    fr = frames().get(title, {})
    if B.name.startswith("sway"):
        line = next((l for l in B.windows().splitlines() if f"'{title}'" in l), "")
        border = re.search(r"border=(\S+)", line)
        border = border.group(1) if border else "?"
        verdict("floating-borderless", "observed" if border in ("none", "csd") else "failed",
                f"sway's container border for the floating {title}: {border!r} ({line.strip()}); the window asks "
                f"for client-side decorations through xdg-decoration ({decoration_log()})", ["float-0-window.png"])
    elif B.name == "kwin":
        line = next((l for l in B.windows().splitlines() if f'"{title}"' in l), "")
        m = re.search(r"(\d+)x(\d+) output", line)
        same = m and (int(m.group(1)), int(m.group(2))) == (fr.get("w"), fr.get("h"))
        verdict("floating-borderless", "observed" if same else "failed",
                f"KWin's frame geometry {m and m.group(0)} vs the window's own size {fr.get('w')}x{fr.get('h')} "
                f"({line.strip()})", ["float-0-window.png"])
    elif B.name == "x11":
        wid = sh(f"xdotool search --onlyvisible --name '^{title}$'").split()
        ext = sh(["xprop", "-id", wid[0], "_NET_FRAME_EXTENTS"]).strip() if wid else "no window"
        none = "= 0, 0, 0, 0" in ext or "not found" in ext
        verdict("floating-borderless", "observed" if none else "failed",
                f"openbox's frame extents for the floating {title}: {ext}", ["float-0-window.png"])
    else:
        verdict("floating-borderless", "observed" if r and r[4] == "screenshot" else "not-observable",
                f"mutter draws no frame for Wayland clients without xdg-decoration; the floating {title}'s own "
                f"1-pixel frame was found in the screenshot where the window's size puts it: {r}",
                ["float-0-window.png"])


def decoration_log():
    """What the app logged of the compositor's decoration answers
    (hikari.decorations, enabled by step floating)."""
    try:
        with open(os.path.join(os.environ["XDG_RUNTIME_DIR"], "app.log"), errors="replace") as f:
            lines = [l.strip() for l in f if "decorat" in l]
    except OSError:
        return "no app log"
    return "; ".join(lines[-3:]) or "nothing logged"


def title_ink(png, box):
    """Pixels in box (x, y, w, h) of a screenshot that stand out from the
    box's most common colour: the drawn title's letters."""
    iw, ih, raw = raw_pixels(os.path.join(EVID, png))
    x, y, w, h = box
    pixels = [tuple(raw[(py * iw + px) * 3:(py * iw + px) * 3 + 3])
              for py in range(max(0, y), min(ih, y + h)) for px in range(max(0, x), min(iw, x + w))]
    if not pixels:
        return 0
    ground = max(set(pixels), key=pixels.count)
    return sum(1 for p in pixels if sum(abs(a - b) for a, b in zip(p, ground)) > 120)


def floating_forced_frame():
    """sway: a border rule frames the floating panel anyway (server-side
    decorations, sent through xdg-decoration). sway's title bar names the
    window, so the header drops its own title and keeps the "⋯" button;
    border csd gives the title back."""
    def look(tag):
        ev, st = B.snap(f"float-4-{tag}")
        r = origin("Audio")
        hdr = where("text", "Audio", "Audio")
        btn = where("button", "Audio options", "Audio")
        if not r or not hdr:
            return ev, r, None, bool(btn), ""
        box = (r[0] + hdr[0]["x"] + 8, r[1] + hdr[0]["y"] + 6, 70, HEADER - 12)
        line = next((l for l in B.windows().splitlines() if "'Audio'" in l), "")
        border = re.search(r"border=(\S+)", line)
        return ev, r, title_ink(ev[0], box), bool(btn), border.group(1) if border else "?"

    ev0, r0, ink0, btn0, border0 = look("own-title")
    B.msg('[app_id="hikarisub" floating] border normal')
    time.sleep(1.5)
    ev1, r1, ink1, btn1, border1 = look("forced-frame")
    log1 = decoration_log()
    B.msg('[app_id="hikarisub" floating] border csd')
    time.sleep(1.5)
    ev2, r2, ink2, btn2, border2 = look("frame-gone")
    ok = (border0 == "csd" and ink0 and ink0 >= 20 and border1 == "normal" and ink1 is not None and ink1 <= 2
          and btn1 and "server-side" in log1 and border2 == "csd" and ink2 and ink2 >= 20 and btn2)
    verdict("floating-forced-frame", "observed" if ok else "failed",
            f"border {border0} -> 'border normal' -> {border1} -> 'border csd' -> {border2}; title ink in the header "
            f"{ink0} -> {ink1} -> {ink2}; '⋯' shown {btn0} -> {btn1} -> {btn2}; app log: {log1}", ev0 + ev1 + ev2)


def floating_frame(r, shadow, shot, suffix=""):
    """The floating Audio window's header inside its frame, and around the
    frame the drawn shadow (shadow > 0: the outermost rings show what is
    behind) or, without one, no margin at all (its edge is the frame's
    boundary, not black)."""
    ev0, st = B.snap(shot)
    hdr = where("text", "Audio", "Audio")
    btn = where("button", "Audio options", "Audio")
    fr = frames(st).get("Audio", {})
    ok = hdr and btn and hdr[0]["h"] == HEADER and hdr[0]["x"] == shadow + 1 and hdr[0]["y"] == shadow + 1
    verdict("floating-header" + suffix, "observed" if ok else "failed",
            f"floating Audio ({fr.get('w')}x{fr.get('h')} at {r[:2]}): header {hdr[:1]}, button {btn[:1]} "
            f"(expected the {HEADER}-pixel header inside a {shadow}-pixel shadow and the 1-pixel frame)", ev0)
    if shadow:
        probe = shadow_probe(shot + ".png", r)
        if probe["see-through"] + probe["black"] == 0:
            verdict("floating-shadow", "not-observable", f"nothing but black behind the shadow to compare with: {probe}",
                    ev0)
        else:
            verdict("floating-shadow", "observed" if probe["black"] == 0 and probe["see-through"] >= 4
                    else "failed", f"outermost shadow ring (black or showing what is behind) where the background 3 "
                                   f"pixels outside is not black, per edge sample: {probe}",
                    ev0)
        return
    iw, ih, raw = raw_pixels(os.path.join(EVID, shot + ".png"))
    x, y, w, h = r[:4]
    edge = []
    for k in range(1, 8):
        for px, py in ((x + w * k // 8, y), (x + w * k // 8, y + h - 1), (x, y + h * k // 8), (x + w - 1, y + h * k // 8)):
            if 0 <= px < iw and 0 <= py < ih:
                i = (py * iw + px) * 3
                edge.append(tuple(raw[i:i + 3]))
    black = [c for c in edge if max(c) < 16]
    verdict("floating-no-compositor-frame", "observed" if edge and not black and len(set(edge)) <= 2 else "failed",
            f"X11 without a compositing manager: the window's outermost pixels {sorted(set(edge))} "
            f"({len(black)} of {len(edge)} black); the frame fills the window", ev0)


def video_labels():
    """Every showing label of the Video panel (the status label carries the
    frame position)."""
    code = ("import sys; sys.argv=['x','dump']\n"
            f"exec(open('{GATE}/atspi_tool.py').read().split('def main')[0])\n"
            "for o in all_objects(app()):\n"
            "    if o.get_role_name()=='panel' and o.get_name()=='Video':\n"
            "        for c in all_objects(o):\n"
            "            if c.get_role_name()=='label' and c.get_state_set().contains(Atspi.StateType.SHOWING):\n"
            "                print(repr(c.get_name()), repr(text_of(c)))\n")
    return sh(["python3", "-c", code]).strip().replace("\n", " | ")


def image_diff(a, b):
    """Pixels that differ between two screenshots (ImageMagick AE metric)."""
    r = subprocess.run(["magick", "compare", "-metric", "AE", "-fuzz", "2%", os.path.join(EVID, a),
                        os.path.join(EVID, b), "null:"], capture_output=True, text=True)
    try:
        return int(float(r.stderr.split()[0]))
    except (ValueError, IndexError):
        return r.stderr.strip()


def step_video():
    shutil.copy(os.path.join(FIXTURES, "cfr.mkv"), os.path.join(HOME, "ep1.mkv"))
    subs = os.path.join(HOME, "ep1.ass")
    with open(subs, "w") as f:
        f.write("[Script Info]\r\nScriptType: v4.00+\r\nPlayResX: 320\r\nPlayResY: 240\r\nVideo File: ep1.mkv\r\n\r\n"
                "[V4+ Styles]\r\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
                "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, "
                "Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\r\n"
                "Style: Default,Arial,40,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1"
                "\r\n\r\n[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,overlay\r\n")
    fresh(subs)
    dismiss_notices()
    sh(["python3", f"{GATE}/atspi_tool.py", "press-showing", "Load associated"])
    time.sleep(5)
    focus_main()
    shots = []

    def state(tag):
        ev, st = B.snap(f"video-{tag}")
        shots.append(ev[0])
        return ev, video_labels()

    def step_frames():
        # The panel's own Next frame button, pressed through AT-SPI: F6 into
        # a floating panel is a separate (failing) item, not this one.
        for _ in range(3):
            sh(["python3", f"{GATE}/atspi_tool.py", "press-showing", "Next frame"])
            time.sleep(0.5)
        time.sleep(1.5)

    ev0, l0 = state("0-docked")
    step_frames()
    ev1, l1 = state("1-docked-stepped")
    how = float_panel("Video", "video")
    time.sleep(2)
    ev2, l2 = state("2-floating")
    step_frames()
    ev3, l3 = state("3-floating-stepped")
    focus_main()
    panel_menu("Video", "Dock")
    st = wait_for(lambda s: "Video" not in frames(s), timeout=4)
    if "Video" in frames(st):
        B.keys("escape", "escape", "escape", "escape")
        focus_main_compositor()
        view_to("Panels")
        B.keys("right", 0.3, "right", 0.3, "down", "down", "return")
        st = wait_for(lambda s: "Video" not in frames(s), timeout=4)
    time.sleep(2)
    ev4, l4 = state("4-redocked")
    step_frames()
    ev5, l5 = state("5-redocked-stepped")
    diffs = {"docked step": image_diff(shots[0], shots[1]), "floating step": image_diff(shots[2], shots[3]),
             "redocked step": image_diff(shots[4], shots[5])}
    applog = sh(f"grep -v GetApplicationBusAddress {os.environ['XDG_RUNTIME_DIR']}/app.log | tail -40")
    open(os.path.join(EVID, "video-labels.txt"), "w").write(
        "\n".join(f"{k}: {v}" for k, v in [("0 docked", l0), ("1 docked stepped", l1), ("2 floating", l2),
                                            ("3 floating stepped", l3), ("4 redocked", l4), ("5 redocked stepped", l5)])
        + f"\nfloated by: {how}\npixel differences: {diffs}\n\n# app log\n{applog}")
    opened = "No video open" not in l0
    changed = all(isinstance(v, int) and v > 50 for v in diffs.values())
    verdict("video-float-redock", "observed" if opened and changed and how else "failed",
            f"video opened: {opened}; floated by {how}; screen pixels changed by 3 frame steps: {diffs}; "
            f"labels docked {l1!r} floating {l3!r} redocked {l5!r}",
            ev0 + ev1 + ev2 + ev3 + ev4 + ev5 + ["video-labels.txt"])


def step_persistence():
    fresh()
    float_panel("Audio", "persist")
    ev0, st = B.snap("persist-0-before-exit")
    # Close the main window the way the compositor does (its close request
    # reaches onClosing, which saves the layout).
    if B.name == "mutter":
        focus_main_compositor()
        B.keys("alt+f4")
    else:
        close_window("HikariSub")
    time.sleep(2.5)
    gone = app("alive").strip() == "no"
    if not gone:
        log("File > Exit did not end the app; stopping it after the 5 s layout check")
        time.sleep(6)
        app("kill")
    cfg = sh(f"find {os.environ['XDG_CONFIG_HOME']}/HikariSub -name 'layout.json*' -printf '%p %s\\n'")
    app("start")
    time.sleep(2)
    st = wait_for(lambda s: "Audio" in frames(s))
    ev1, st = B.snap("persist-1-after-restart", extra="# layout files\n" + cfg)
    ok = "Audio" in frames(st)
    verdict("layout-persistence", "observed" if ok else "failed",
            f"main window closed through the compositor (process ended: {gone}); layout files: {cfg.strip()!r}; "
            f"after restart Audio floating: {ok}", ev0 + ev1)


def step_fullscreen():
    path = episode()
    fresh(path)
    dismiss_notices()
    float_panel("Line editor", "fullscreen")
    if B.name == "sway":
        B.msg('[title="HikariSub$"] fullscreen enable')
    elif B.name == "x11":
        sh("xdotool search --onlyvisible --name 'HikariSub$' windowstate --add FULLSCREEN")
    elif B.name == "kwin":
        B.script('for (const w of workspace.windowList()) if (/HikariSub$/.test(w.caption)) w.fullScreen = true;')
    else:
        # mutter's own toggle-fullscreen keybinding (unbound by default),
        # bound in this session's settings, on the focused main window
        sh(["gsettings", "set", "org.gnome.desktop.wm.keybindings", "toggle-fullscreen", "['<Super>f']"])
        time.sleep(0.5)
        focus_main_compositor()
        B.keys("super+f", 1.5)
        main = frames().get("HikariSub", {})
        if (main.get("w"), main.get("h")) != (1600, 1000):
            verdict("fullscreen-coexistence", "failed",
                    f"Super+F (mutter's toggle-fullscreen) did not make the main window fill Meta-0: {main}")
            return
    time.sleep(1.5)
    ev0, st = B.snap("fullscreen-0-main-fullscreen")
    focus_main()
    walk = f6_walk(5)
    ev1, st = B.snap("fullscreen-1-after-f6")
    reached = [panel_of(w["path"]) for w in walk]
    float_active = any("Line editor" in w["active"] for w in walk)
    open(os.path.join(EVID, "fullscreen-f6.json"), "w").write(json.dumps(walk, indent=1))
    focus_verdict("fullscreen-coexistence", "Line editor" in reached and float_active,
            f"main window fullscreen by the compositor with Line editor floating; F6 reached {reached}, "
            f"floating window active at some step: {float_active} (see screenshots: is the floating panel visible "
            f"above the fullscreen window?)", ev0 + ev1 + ["fullscreen-f6.json"], targets=("Line editor",))


def step_menu_from_text():
    """Alt+V (the View menu's mnemonic) with the focus in the Line text field."""
    path = episode()
    fresh(path)
    dismiss_notices()
    atspi_do("text", "Line text", "SetFocus")
    time.sleep(0.5)
    before = atspi()["texts"].get("Line text")
    B.keys("alt+v", 1.0)
    ev, st = B.snap("menu-from-text")
    after = st["texts"].get("Line text")
    menu_open = any("[popup menu]" in l and "showing" in l
                    for l in sh(["python3", f"{GATE}/atspi_tool.py", "dump"]).splitlines())
    B.keys("escape")
    verdict("menu-mnemonic-from-text-field", "observed" if menu_open and after == before else "failed",
            f"Alt+V in Line text: View menu open={menu_open}; text {before!r} -> {after!r}", ev)


def step_test_executables():
    ui = os.environ["UI_TESTS"]
    runs = [("hikari_ui_shell_tests", ["f6AndShortcutsReachAFloatingPanel", "panelsFloatDockHideAndKeepTheirState",
                                       "placementWindowMovesTabsAndResizes",
                                       "floatF6AndShowActivateTheFloatingPanelsWindow",
                                       "placementWindowShowsItsDefaultsAndKeyboardChanges",
                                       "fileDropAreaLeavesPanelDragsToTheDockingEngine",
                                       "dockingControlsAndTheGridAreAccessible",
                                       "floatingPanelsOffEveryScreenComeBack",
                                       # D3
                                       "panelMenuHoldsMoveUndockAndClose", "keyboardReachesThePanelHeaderAndItsMenu",
                                       "doubleClickOnAHeaderFloatsAndDocks", "bottomPanelsKeepATabAndTheTrayToolbar",
                                       "panelsKeepTheirMinimumSizes"]),
            ("hikari_ui_docking_qualification_tests", []),
            ("hikari_ui_workspace_layout_tests", [])]
    # The tests open their own windows: none of this session's HikariSub
    # over them (step fullscreen leaves it fullscreen).
    app("kill")
    out = []
    ok = True
    # sway: the virtual pointer's own hover events would mix with the
    # tests' synthetic ones (the header's cursor test); none while they run.
    if B.name.startswith("sway"):
        sh("pkill -x vpointer; sleep 0.5")
    for exe, fns in runs:
        r = subprocess.run([os.path.join(ui, exe), *fns], capture_output=True, text=True, timeout=300)
        text = r.stdout + r.stderr
        out.append(f"### {exe} {' '.join(fns)} (exit {r.returncode})\n{text}")
        ok &= r.returncode == 0
        totals = re.findall(r"Totals: .*", text)
        skips = re.findall(r"SKIP.*", text)
        log(exe, totals, skips)
    if B.name.startswith("sway"):
        rt = os.environ["XDG_RUNTIME_DIR"]
        subprocess.Popen(f"exec /tmp/vpointer < {rt}/vpointer.in > /tmp/vpointer.log 2>&1", shell=True,
                         start_new_session=True)
        time.sleep(0.5)
    open(os.path.join(EVID, "test-executables.txt"), "w").write("\n".join(out))
    summary = "; ".join(re.findall(r"Totals: [^,]+, [^,]+, [^,]+", "\n".join(out)))
    skips = re.findall(r"SKIP\s*:.*", "\n".join(out))
    # Skips the tests declare as platform limits, each observed for real by
    # another gate item: Wayland compositors place windows (outputs); a
    # synthetic drag between windows needs the offscreen platform (pointer);
    # a window manager's focus-stealing policy (the F6 items; on sway, the
    # sway-activate pass runs these tests again).
    known = ("the compositor places windows", "a synthetic drag between windows needs the offscreen platform",
             "refused the shell's activation request", "did not activate the main window")
    unexpected = [k for k in skips if not any(reason in k for reason in known)]
    verdict("test-executables", "observed" if ok and not unexpected else "failed",
            f"{summary}; skips: {skips}", ["test-executables.txt"])


def step_orca():
    """Orca against HikariSub: what it would speak while the keyboard moves
    focus through the panels, opens View > Panels and floats a panel."""
    path = episode()
    fresh(path)
    dismiss_notices()
    log_path = os.path.join(EVID, "orca-debug.out")
    if os.path.exists(log_path):
        os.unlink(log_path)
    orca = subprocess.Popen(["orca", "--replace", "--debug-file", log_path], stdout=subprocess.DEVNULL,
                            stderr=open(os.path.join(EVID, "orca-stderr.txt"), "w"))
    time.sleep(8)
    focus_main_compositor()
    f6_walk(4)
    view_to("Panels")
    B.keys("right", 0.6, "down", 0.6, "down", 0.6, "right", 0.6, "down", 0.6, "down", 0.6,
           "return", 2.0)
    f6_walk(3)
    # D3: the Grid's header from the keyboard, its "⋯" button and menu.
    B.keys("escape", "escape")
    focus_main_compositor()
    f6_to("Grid")
    shift_tab_to("page tab", "Grid")
    B.keys("right", 0.8, "space", 1.0, "down", 0.8, "escape", 0.8)
    time.sleep(2)
    orca.terminate()
    try:
        orca.wait(10)
    except subprocess.TimeoutExpired:
        orca.kill()
    text = open(log_path, errors="replace").read() if os.path.exists(log_path) else ""
    speech = [l.strip() for l in text.splitlines() if "SPEECH OUTPUT" in l]
    open(os.path.join(EVID, "orca-speech.txt"), "w").write("\n".join(speech) + "\n")
    spoke_panels = [p for p in ("Video", "Audio", "Line editor", "Grid", "Panels", "Float", "Grid options",
                                "Move panel", "page tab")
                    if any(p in l for l in speech)]
    verdict("orca", "observed" if speech else "failed",
            f"{len(speech)} speech lines; panel/menu names spoken: {spoke_panels} (orca-speech.txt; full log "
            f"orca-debug.out)", ["orca-speech.txt", "orca-debug.out", "orca-stderr.txt"])


def step_outputs():
    """Mixed DPI and monitor removal: a floating panel on a scale-2 output,
    then that output removed."""
    path = episode()
    fresh(path)
    dismiss_notices()
    how = float_panel("Line editor", "outputs")
    title = "Line editor"
    if B.name == "sway":
        B.msg(f'[title="^{title}$"] move to output HEADLESS-2')
        time.sleep(1.5)
        B.msg('[title="^Line editor$"] focus')
        ev0, st = B.snap("outputs-0-on-scale2")
        sh(["grim", "-o", "HEADLESS-2", os.path.join(EVID, "outputs-0-scale2-output.png")])
        where = B.client_rect(title)
        on2 = where is not None and where[4] == "HEADLESS-2"
        B.type_text(" hidpi")
        time.sleep(0.8)
        txt = atspi()["texts"].get("Line text")
        verdict("mixed-dpi", "observed" if on2 and txt and " hidpi" in txt else "failed",
                f"floating Line editor (floated by {how}) moved to HEADLESS-2 (scale 2, other output scale 1): "
                f"{where}; typing there gives {txt!r} (see outputs-0-scale2-output.png, native 2x pixels)",
                ev0 + ["outputs-0-scale2-output.png"])
        B.msg("output HEADLESS-2 unplug")
        time.sleep(2)
    elif B.name == "kwin":
        sh("kscreen-doctor output.Virtual-1.scale.2 output.Virtual-0.scale.1")
        time.sleep(1)
        B.script(f'const o = workspace.screens.find(s => s.name === "Virtual-1"); '
                 f'for (const w of workspace.windowList()) if (w.caption === "{title}") workspace.sendClientToScreen(w, o);')
        time.sleep(1.5)
        ev0, st = B.snap("outputs-0-on-scale2", extra="# kscreen-doctor\n" + sh("kscreen-doctor -o"))
        on2 = "output=Virtual-1" in "".join(l for l in B.windows().splitlines() if f'"{title}"' in l)
        B.script(f'for (const w of workspace.windowList()) if (w.caption === "{title}") workspace.activeWindow = w;')
        B.type_text(" hidpi")
        time.sleep(0.8)
        txt = atspi()["texts"].get("Line text")
        verdict("mixed-dpi", "observed" if on2 and txt and " hidpi" in txt else "failed",
                f"floating Line editor (floated by {how}) on Virtual-1 at scale 2 (Virtual-0 at 1): {on2}; "
                f"typing there gives {txt!r}", ev0)
        sh("kscreen-doctor output.Virtual-1.disable")
        time.sleep(2)
    elif B.name == "mutter":
        sh("gdctl set --logical-monitor --primary --monitor Meta-0 --scale 1 "
           "--logical-monitor --monitor Meta-1 --scale 2 --right-of Meta-0")
        time.sleep(1.5)
        # mutter's own keybinding (move-to-monitor-right) with the floating
        # panel focused; the screenshot puts Meta-1 (its physical pixels, 2x)
        # right of Meta-0's 1600
        B.keys("super+shift+right", 1.5)
        ev0, st = B.snap("outputs-0-on-scale2", extra="# gdctl show\n" + sh("gdctl show"))
        where2 = locate_floating(title, scale=2, x_from=1600, path=os.path.join(EVID, "outputs-0-on-scale2.png"))
        B.type_text(" hidpi")
        time.sleep(0.8)
        txt = atspi()["texts"].get("Line text")
        verdict("mixed-dpi", "observed" if where2 and txt and " hidpi" in txt else "failed",
                f"floating Line editor (floated by {how}) moved by Super+Shift+Right to Meta-1 (scale 2; Meta-0 at "
                f"1): its frame found at 2x on Meta-1 in the screenshot at {where2}; typing there gives {txt!r}", ev0)
        sh("gdctl set --logical-monitor --primary --monitor Meta-0 --scale 1")
        time.sleep(2)
    else:
        verdict("mixed-dpi", "not-observable",
                "X11 has one Xft DPI for the whole screen: Qt's xcb backend has no per-monitor scale to mix")
        sh("xrandr --setmonitor left 1600/400x1000/250+0+0 none")
        sh("xrandr --setmonitor right 1600/200x1000/125+1600+0 none")
        time.sleep(1)
        w = sh(f"xdotool search --name '^{title}$'").split()
        if w:
            sh(["xdotool", "windowmove", w[0], "2000", "300"])
        time.sleep(1)
        B.snap("outputs-0-x11-right-monitor", extra=sh("xrandr --listmonitors"))
        # The right monitor goes away: without its RandR monitor Xvfb's own
        # output monitor (the whole 3200 px framebuffer) would still cover
        # x 1600..3200, so the output shrinks to the left monitor's size.
        sh("xrandr --delmonitor right; xrandr --delmonitor left")
        sh("xrandr --newmode 1600x1000 0 1600 0 0 1600 1000 0 0 1000; xrandr --addmode screen 1600x1000")
        sh("xrandr --output screen --mode 1600x1000 --fb 1600x1000")
        time.sleep(2)
    ev1, st = B.snap("outputs-1-after-removal")
    fr = frames(st)
    present = title in fr
    rect = B.client_rect(title) if present else None
    # Reachable: View > Panels > Line editor > Show brings it up with the focus.
    focus_main_compositor()
    panel_menu(title, "Show")
    time.sleep(1)
    ev2, st = B.snap("outputs-2-show-after-removal")
    fr2 = frames(st)
    focused_in = panel_of(st["focusPath"])
    remaining = {"sway": ("HEADLESS-1",), "kwin": ("Virtual-0",)}.get(B.name)
    back = present and rect is not None and (remaining is None or rect[4] in remaining)
    if B.name == "x11" and rect is not None:
        back = present and rect[0] + 40 < 1600  # the remaining RandR monitor spans x 0..1600
    if B.name == "mutter":
        # the screenshot now holds Meta-0 alone
        found = locate_floating(title, path=os.path.join(EVID, "outputs-1-after-removal.png")) if present else None
        verdict("monitor-removal", "observed" if found else "failed",
                f"Meta-1 removed from the layout: the floating Line editor still a window ({present}), its frame "
                f"found on Meta-0 in the screenshot at {found}", ev1 + ev2)
    else:
        verdict("monitor-removal", "observed" if back else "failed",
                f"after removing the output holding the floating Line editor: window present={present}, "
                f"geometry/output {rect}", ev1)
    focus_verdict("monitor-removal-show-focus", focused_in == title,
                  f"then View > Panels > Line editor > Show gives the focus to {focused_in!r} "
                  f"(active windows {[f['id'] for f in st['frames'] if f['active']]})", ev2, targets=(title,))


def atspi_find(role, name):
    return [l for l in sh(["python3", f"{GATE}/atspi_tool.py", "find", role, name]).splitlines() if l.strip()]


def menu_items():
    return [n for n in ("Move panel…", "Undock", "Dock", "Close") if where("menu item", n)]


def step_a11y():
    """What a screen reader finds of the D3 headers and the Grid: each
    header named after its panel (a title bar, or a page tab list of page
    tabs checked when selected) with its "<panel> options" button; the
    menu's items pressed through AT-SPI float and dock a panel, a tab
    pressed selects it."""
    path = episode()
    fresh(path)
    dismiss_notices()
    tree = sh(["python3", f"{GATE}/atspi_tool.py", "dump"])
    open(os.path.join(EVID, "a11y-tree.txt"), "w").write(tree)
    table = atspi_find("table", "Subtitle lines")
    grid_panel = atspi_find("panel", "Grid")
    in_grid = bool(table) and "panel:'Grid'" in table[0]
    verdict("grid-accessible", "observed" if in_grid and grid_panel else "failed",
            f"table 'Subtitle lines': {table[:1]}; panel 'Grid': {grid_panel[:1]}", ["a11y-tree.txt"])
    # Headers: a lone panel's title bar (Qt's AT-SPI bridge gives
    # QAccessible::TitleBar the role "text"), the Grid's tab bar.
    headers = {}
    for name in ("Video", "Audio", "Line editor"):
        hits = [l for l in atspi_find("text", name) if "frame:'" in l]
        headers[name] = hits[:1]
    tabs_grid = atspi_find("page tab", "Grid")
    buttons = {n: atspi_find("button", f"{n} options")[:1] for n in ("Video", "Audio", "Line editor", "Grid")}
    named = all(headers.values()) and tabs_grid and all(buttons.values()) \
        and all(f"text:'{n}' > button:'{n} options'" in buttons[n][0] for n in ("Video", "Audio", "Line editor"))
    # The menu by AT-SPI: Undock floats, then Dock docks.
    atspi_do("button", "Audio options")
    time.sleep(1)
    m1 = menu_items()
    atspi_do("menu item", "Undock")
    st = wait_for(lambda s: "Audio" in frames(s))
    floated = "Audio" in frames(st)
    ev0, st = B.snap("a11y-0-undock-audio-by-atspi")
    atspi_do("button", "Audio options")
    time.sleep(1)
    m2 = menu_items()
    atspi_do("menu item", "Dock")
    st = wait_for(lambda s: "Audio" not in frames(s))
    docked = "Audio" not in frames(st)
    verdict("header-controls-accessible",
            "observed" if named and m1 == ["Move panel…", "Undock", "Close"] and floated
            and m2 == ["Move panel…", "Dock", "Close"] and docked else "failed",
            f"title bars {headers}; Grid's tab {tabs_grid[:1]}; buttons {buttons}; 'Audio options' pressed: menu "
            f"{m1}, Undock -> own window={floated}; again: menu {m2}, Dock -> docked={docked}",
            ["a11y-tree.txt"] + ev0)
    # Timing (titled Shift times) opens as a tab beside the Line editor.
    focus_main()
    panel_menu("Timing", "Show")
    time.sleep(1)
    tabs = {n: atspi_find("page tab", n) for n in ("Line editor", "Shift times")}
    checked0 = [n for n, hits in tabs.items() if hits and "checked" in hits[0].split("> @")[0]]
    opts0 = [n for n in ("Line editor", "Shift times") if where("button", f"{n} options")]
    ev1, st = B.snap("a11y-1-tabs")
    atspi_do("page tab", "Line editor", "Press")
    time.sleep(0.8)
    tabs1 = {n: atspi_find("page tab", n) for n in ("Line editor", "Shift times")}
    checked1 = [n for n, hits in tabs1.items() if hits and "checked" in hits[0].split("> @")[0]]
    opts1 = [n for n in ("Line editor", "Shift times") if where("button", f"{n} options")]
    atspi_do("page tab", "Shift times", "Press")
    time.sleep(0.8)
    atspi_do("button", "Shift times options")
    time.sleep(1)
    m3 = menu_items()
    atspi_do("menu item", "Undock")
    st = wait_for(lambda s: "Shift times" in frames(s))
    tab_floated = "Shift times" in frames(st)
    ev2, st = B.snap("a11y-2-undock-timing-tab")
    open(os.path.join(EVID, "a11y-tabs.txt"), "w").write(json.dumps(
        {"tabs": tabs, "checked": checked0, "options buttons": opts0, "after pressing Line editor": tabs1,
         "checked then": checked1, "options buttons then": opts1, "Timing menu": m3}, indent=1))
    verdict("tabs-accessible",
            "observed" if all(tabs.values()) and checked0 == ["Shift times"] and opts0 == ["Shift times"]
            and checked1 == ["Line editor"] and opts1 == ["Line editor"] and tab_floated else "failed",
            f"page tabs {[h[:1] for h in tabs.values()]}; checked {checked0}, options button on {opts0}; Line editor "
            f"tab pressed: checked {checked1}, options button on {opts1}; Shift times selected again, its menu {m3}, "
            f"Undock -> own window={tab_floated}", ev1 + ev2 + ["a11y-tabs.txt"])


def focused_is(role, name):
    st = atspi()
    return bool(st["focus"]) and st["focus"].startswith(f"[{role}] {name!r}"), st["focus"]


def f6_to(name, tries=7):
    """F6 until the keyboard focus is in panel NAME (F6 focuses the panel)."""
    for _ in range(tries):
        if panel_of(atspi()["focusPath"]) == name:
            return True
        B.combo("f6")
        time.sleep(0.8)
    return panel_of(atspi()["focusPath"]) == name


def shift_tab_to(role, name, tries=3):
    """Shift+Tab from inside a panel until its header control (role, name)
    has the focus; how many presses it took (0: never)."""
    for n in range(1, tries + 1):
        B.keys("shift+tab", 0.5)
        if focused_is(role, name)[0]:
            return n
    return 0


def step_header_keys():
    """The header from the keyboard (docs/qt/docking.md, Keyboard and
    names): Shift+Tab from inside the Grid reaches its selected tab, Right
    its "⋯" button, Space opens the menu, Escape closes it; the menu's
    Undock floats the Grid and, from the floating Grid, Dock docks it. A
    title bar's one Tab stop is its "⋯" button."""
    path = episode()
    fresh(path)
    dismiss_notices()
    focus_main_compositor()
    in_grid = f6_to("Grid")
    start = atspi()["focus"]
    n_tab = shift_tab_to("page tab", "Grid")
    B.keys("right", 0.5)
    on_btn, f_btn = focused_is("button", "Grid options")
    B.keys("space", 0.8)
    m1 = menu_items()
    ev0, _ = B.snap("keys-0-menu")
    B.keys("escape", 0.6)
    closed = not menu_items()
    back, f_back = focused_is("button", "Grid options")
    verdict("header-keyboard-menu", "observed" if in_grid and n_tab and on_btn and
            m1 == ["Move panel…", "Undock", "Close"] and closed and back else "failed",
            f"F6 to the Grid ({start}); Shift+Tab x{n_tab or 'never'} -> its tab; Right -> {f_btn}; Space -> menu "
            f"{m1}; Escape -> closed {closed}, focus {f_back}", ev0)
    # Undock from the menu, then Dock from the floating Grid's own menu.
    B.keys("space", 0.8, "down", "down", "return", 1.5)
    st = wait_for(lambda s: "Grid" in frames(s))
    floated = "Grid" in frames(st)
    ev1, st = B.snap("keys-1-undocked")
    docked, m2, f2, n2 = False, [], None, 0
    if floated:
        f6_to("Grid")
        n2 = shift_tab_to("page tab", "Grid")
        B.keys("right", 0.5)
        f2 = atspi()["focus"]
        B.keys("space", 0.8)
        m2 = menu_items()
        B.keys("down", "down", "return", 1.5)
        st = wait_for(lambda s: "Grid" not in frames(s))
        docked = "Grid" not in frames(st)
    ev2, st = B.snap("keys-2-docked")
    verdict("header-keyboard-undock-dock", "observed" if floated and docked and m2 == ["Move panel…", "Dock", "Close"]
            else "failed",
            f"Space, Down, Down, Return on 'Grid options' -> own window={floated}; in the floating Grid Shift+Tab "
            f"x{n2 or 'never'}, Right -> {f2}; its menu {m2}; Down, Down, Return -> docked={docked}", ev1 + ev2)
    # A title bar: Shift+Tab from the Video panel's first control reaches
    # its button (docs/qt/docking.md). F6 focuses the panel itself, whose
    # place in the Tab chain is not its content's (pre-D3 already: Tab from
    # there leaves the panel), so where Shift+Tab goes from there is noted.
    focus_main()
    in_video = f6_to("Video")
    B.keys("shift+tab", 0.5)
    from_f6 = atspi()["focus"]
    atspi_do("button", "Video options", "SetFocus")
    time.sleep(0.4)
    B.keys("tab", 0.5)
    first = atspi()["focusPath"]
    in_first = panel_of(first) == "Video"
    B.keys("shift+tab", 0.5)
    on_vbtn, f = focused_is("button", "Video options")
    B.keys("return", 0.8)
    m3 = menu_items()
    ev3, _ = B.snap("keys-3-title-bar-menu")
    B.keys("escape", 0.4)
    verdict("header-keyboard-title-bar", "observed" if in_first and on_vbtn and m3 == ["Move panel…", "Undock", "Close"]
            else "failed",
            f"Tab from 'Video options' into the panel's first control ({first and first.split(chr(62))[-1]}); "
            f"Shift+Tab -> {f}; Return -> menu {m3}. (F6 to Video {in_video}, then Shift+Tab from the panel itself "
            f"-> {from_f6})", ev3)


# ---------------------------------------------------------------- D2
CORE = ("Video", "Audio", "Line editor", "Grid")
# Legacy's View menu (HikariSubFrame.cpp:308-312) and the core panels each
# arrangement shows (OnMenuSelected, HikariSubFrame.cpp:849-892), in the
# order the gate applies them (All last: the round trip).
ARRANGEMENTS = [("Only subtitles", {"Line editor", "Grid"}), ("Only video", {"Video"}),
                ("Video and subs", {"Video", "Line editor", "Grid"}), ("Audio and subs", {"Audio", "Line editor", "Grid"}),
                ("All", set(CORE))]


def core_panels(st):
    """The core panels shown docked in the main window, with their places."""
    return {p["id"]: (p["x"], p["y"], p["w"], p["h"]) for p in st["panels"]
            if p["id"] in CORE and p["frame"] == "HikariSub"}


def same_places(a, b, slack=2):
    return set(a) == set(b) and all(all(abs(x - y) <= slack for x, y in zip(a[k], b[k])) for k in a)


def video_episode():
    shutil.copy(os.path.join(FIXTURES, "cfr.mkv"), os.path.join(HOME, "ep1.mkv"))
    subs = os.path.join(HOME, "ep1.ass")
    with open(subs, "w") as f:
        f.write("[Script Info]\r\nScriptType: v4.00+\r\nPlayResX: 320\r\nPlayResY: 240\r\nVideo File: ep1.mkv\r\n\r\n"
                "[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,first\r\n"
                "Dialogue: 0,0:00:05.00,0:00:09.00,Default,,0,0,0,,second\r\n")
    return subs


def type_draft():
    """A draft in the Line editor (" draft" typed at the end of the Line
    text), then F6 to the Grid, where the menus open from."""
    atspi_do("text", "Line text", "SetFocus")
    time.sleep(0.5)
    B.keys("end")
    B.type_text(" draft")
    time.sleep(0.8)
    B.keys("f6", 0.5)
    return atspi()["texts"].get("Line text")


def step_views():
    """D2: View > All, Video and subs, Audio and subs, Only video and Only
    subtitles from the keyboard over the docked Workspace: the panels each
    shows, the focus on a shown panel, the draft kept, and All back to the
    places the panels had."""
    subs = video_episode()
    fresh(subs)
    dismiss_notices()
    sh(["python3", f"{GATE}/atspi_tool.py", "press-showing", "Load associated"])
    time.sleep(5)
    focus_main()
    # Blank audio after the video (a video without audio closes the box).
    blank = menu_to("Open blank 2h30m audio", opener="alt+u")
    B.keys("return", 2.5)
    draft = type_draft()
    ev, st = B.snap("views-0-editing")
    evidence = list(ev)
    base = core_panels(st)
    rows, shown_ok, focus_ok, draft_ok = [], True, True, True
    for i, (label, want) in enumerate(ARRANGEMENTS, 1):
        reached = view_to(label)
        B.keys("return", 1.5)
        st = wait_for(lambda s: set(core_panels(s)) == want, timeout=5)
        ev, st = B.snap(f"views-{i}-" + label.lower().replace(" ", "-"))
        evidence += ev
        shown = set(core_panels(st))
        focus = panel_of(st["focusPath"])
        text = st["texts"].get("Line text")
        shown_ok &= reached and shown == want
        focus_ok &= focus in want
        if "Line editor" in want:
            draft_ok &= text == draft
        rows.append(f"{label}: reached={reached} shown={sorted(shown)} focus={focus} Line text={text!r}")
    back = same_places(core_panels(st), base)
    open(os.path.join(EVID, "views.txt"), "w").write(
        f"blank audio from the keyboard: {blank}\nediting: {base}\nafter All: {core_panels(st)}\n" + "\n".join(rows))
    evidence.append("views.txt")
    detail = "; ".join(rows)
    verdict("view-arrangements", "observed" if shown_ok and base and set(base) == set(CORE) else "failed",
            f"from the keyboard, the core panels each arrangement shows: {detail}", evidence)
    verdict("view-focus-on-shown-panel", "observed" if focus_ok else "failed",
            f"the focus after each arrangement is on a shown panel: {detail}", evidence)
    verdict("view-draft-kept", "observed" if draft_ok and draft and draft.endswith(" draft") else "failed",
            f"Line text with the draft {draft!r}: {detail}", evidence)
    verdict("view-all-round-trip", "observed" if back else "failed",
            f"View > All returns each panel to its place (2 px): before {base}, after {core_panels(st)}", evidence)


def step_editor():
    """D2: GLOBAL_EDITOR (Ctrl+E) with compositor key input: the player
    layout (only the Video panel, with the focus), and back to the
    arrangement it held, the focus on the Grid and the draft kept."""
    fresh(episode())
    dismiss_notices()
    draft = type_draft()
    ev0, st = B.snap("editor-0-on")
    base = core_panels(st)
    B.keys("ctrl+e", 1.5)
    st = wait_for(lambda s: set(core_panels(s)) == {"Video"}, timeout=5)
    ev1, st = B.snap("editor-1-off")
    off = set(core_panels(st))
    focus_off = panel_of(st["focusPath"])
    title_off = [f["name"] for f in st["frames"]]
    B.keys("ctrl+e", 1.5)
    st = wait_for(lambda s: set(core_panels(s)) == set(base), timeout=5)
    time.sleep(1)
    ev2, st = B.snap("editor-2-on-again")
    on = core_panels(st)
    focus_on = panel_of(st["focusPath"])
    text = st["texts"].get("Line text")
    evidence = ev0 + ev1 + ev2
    verdict("editor-player-layout", "observed" if off == {"Video"} and focus_off == "Video" else "failed",
            f"Ctrl+E: panels shown {sorted(off)}, focus on {focus_off}, windows {title_off}", evidence)
    verdict("editor-round-trip", "observed" if base and same_places(on, base) else "failed",
            f"Ctrl+E again: before {base}, after {on}", evidence)
    verdict("editor-focus-and-draft", "observed" if focus_on == "Grid" and draft and text == draft else "failed",
            f"focus on {focus_on}; Line text {draft!r} -> {text!r}", evidence)


# ---------------------------------------------------------------- V5 (#184)
def video_document():
    """cfr.mkv (barcode frames) as ep1.mkv beside ep1.ass naming it."""
    shutil.copy(os.path.join(FIXTURES, "cfr.mkv"), os.path.join(HOME, "ep1.mkv"))
    subs = os.path.join(HOME, "ep1.ass")
    with open(subs, "w") as f:
        f.write("[Script Info]\r\nScriptType: v4.00+\r\nPlayResX: 320\r\nPlayResY: 240\r\nVideo File: ep1.mkv\r\n\r\n"
                "[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,overlay\r\n")
    return subs


def fs_state():
    out = sh(["python3", f"{GATE}/atspi_tool.py", "fsjson"], timeout=60)
    try:
        st = json.loads(out)
    except ValueError:
        return {"frames": [], "focusPath": None}, None, None
    full = next((f for f in st["frames"] if f["fullscreen"]), None)
    main = next((f for f in st["frames"] if not f["fullscreen"] and f["name"].endswith("HikariSub")), None)
    return st, full, main


def fs_wait(pred, timeout=8.0):
    end = time.time() + timeout
    while True:
        st, full, main = fs_state()
        if pred(full, main) or time.time() > end:
            return st, full, main
        time.sleep(0.4)


# Each session's outputs (logical sizes and scale), the second at scale 2
# where the compositor has per-output scales; on X11 two RandR monitors.
FS_OUTPUTS = {"sway": {"HEADLESS-1": (1600, 1000, 1), "HEADLESS-2": (800, 500, 2)},
              "kwin": {"Virtual-0": (1600, 1000, 1), "Virtual-1": (800, 500, 2)},
              "mutter": {"Meta-0": (1600, 1000, 1), "Meta-1": (800, 500, 2)},
              "x11": {"left": (1600, 1000, 1), "right": (1600, 1000, 1)}}
FS_OUTPUTS["sway-activate"] = FS_OUTPUTS["sway"]


def fs_outputs_setup():
    if B.name == "kwin":
        sh("kscreen-doctor output.Virtual-0.scale.1 output.Virtual-1.scale.2")
    elif B.name == "mutter":
        sh("gdctl set --logical-monitor --primary --monitor Meta-0 --scale 1 "
           "--logical-monitor --monitor Meta-1 --scale 2 --right-of Meta-0")
    elif B.name == "x11":
        # Two RandR monitors on Xvfb's 3200x1000 screen (Qt's xcb screens).
        sh("xrandr --setmonitor left 1600/400x1000/250+0+0 none")
        sh("xrandr --setmonitor right 1600/400x1000/250+1600+0 none")
    time.sleep(1.5)
    return {"sway": lambda: sh(["swaymsg", "-t", "get_outputs"]), "kwin": lambda: sh("kscreen-doctor -o"),
            "mutter": lambda: sh("gdctl show"), "x11": lambda: sh("xrandr --listmonitors")}.get(
                B.name.split("-")[0], lambda: "")()


def fs_outputs_reset():
    if B.name == "x11":
        sh("xrandr --delmonitor right; xrandr --delmonitor left")
    elif B.name == "kwin":
        sh("kscreen-doctor output.Virtual-1.scale.1")
    elif B.name == "mutter":
        sh("gdctl set --logical-monitor --primary --monitor Meta-0 --scale 1 "
           "--logical-monitor --monitor Meta-1 --scale 1 --right-of Meta-0")


def fs_window_output(fullscreen=True):
    """The compositor's output for the fullscreen window (or the main window),
    where the compositor says (sway, KWin; X11 from the frame's position)."""
    if B.name.startswith("sway"):
        for line in B.windows().splitlines():
            if "HikariSub" in line and (("fullscreen=1" in line or "fullscreen=2" in line) == fullscreen):
                m = re.search(r"output=(\S+)", line)
                return m.group(1) if m else None
    if B.name == "kwin":
        for line in B.windows().splitlines():
            if "HikariSub" in line and (("fullscreen=true" in line) == fullscreen):
                m = re.search(r"output=(\S+)", line)
                return m.group(1) if m else None
    if B.name == "x11":
        _, full, main = fs_state()
        f = full if fullscreen else main
        return None if f is None else ("left" if f["x"] + f["w"] / 2 < 1600 else "right")
    return None


def fs_move_main(output):
    """The main window onto OUTPUT the way a user would with the compositor."""
    if B.name.startswith("sway"):
        B.msg(f'[title="HikariSub$"] move to output {output}')
        B.msg('[title="HikariSub$"] focus')
    elif B.name == "kwin":
        B.script(f'const o = workspace.screens.find(s => s.name === "{output}"); '
                 'for (const w of workspace.windowList()) if (/HikariSub$/.test(w.caption)) '
                 '{ workspace.sendClientToScreen(w, o); workspace.activeWindow = w; }')
    elif B.name == "x11":
        x = 100 if output == "left" else 1700
        sh(f"xdotool search --onlyvisible --name 'HikariSub$' windowmove {x} 100 windowactivate")
    time.sleep(1.5)


def fs_covers(full, name):
    """The fullscreen frame covers output NAME (its size on Wayland, where a
    client does not know its position; the rectangle on X11)."""
    if not full or name not in FS_OUTPUTS[B.name]:
        return False
    w, h, _ = FS_OUTPUTS[B.name][name]
    if (full["w"], full["h"]) != (w, h):
        return False
    if B.name == "x11":
        return (full["x"], full["y"]) == ((0 if name == "left" else 1600), 0)
    return True


def fs_focus_video():
    """The Video panel takes the keyboard (F6 through the panels)."""
    for _ in range(8):
        if panel_of(atspi()["focusPath"]) == "Video":
            return True
        B.combo("f6")
        time.sleep(0.8)
    return panel_of(atspi()["focusPath"]) == "Video"


def step_video_fullscreen():
    """V5 (#184): F in the Video panel shows the video fullscreen on the main
    window's output; Space and Right work there; Esc leaves with the main
    window's geometry and the keyboard focus as they were; the context menu's
    "Open in full screen on monitor 2" puts it on Qt's second monitor (primary
    first, as legacy's MonitorEnumProc1; the main window is put on the primary
    first so that this is another output), at that output's scale (2 on sway,
    KWin and mutter), and Esc brings the docked video back. Real compositor
    keys; the menu item is pressed through AT-SPI. Then the Spix workflow."""
    outputs_text = fs_outputs_setup()
    # The Spix workflow first: it prints Qt's screens (primary marked).
    ui = os.environ["UI_TESTS"]
    r = subprocess.run([os.path.join(ui, "hikari_ui_video_fullscreen_workflow"), "--monitors", "2"],
                       capture_output=True, text=True, timeout=300)
    text = r.stdout + r.stderr
    open(os.path.join(EVID, "videofs-workflow.txt"), "w").write(text)
    failed = [l for l in text.splitlines() if l.startswith("FAILED")]
    verdict("videofs-spix-workflow", "observed" if r.returncode == 0 else "failed",
            f"hikari_ui_video_fullscreen_workflow --monitors 2 exit {r.returncode}; failed checks: {failed}",
            ["videofs-workflow.txt"])
    screens = re.findall(r"^screen (\S+) .*?( primary)?$", text, re.M)
    order = [n for n, p in screens if p] + [n for n, p in screens if not p]
    log("Qt's monitors, primary first:", order)

    subs = video_document()
    fresh(subs)
    dismiss_notices()
    sh(["python3", f"{GATE}/atspi_tool.py", "press-showing", "Load associated"])
    time.sleep(5)
    focus_main()
    st0, full0, main0 = fs_state()
    main_out = fs_window_output(fullscreen=False)
    in_video = fs_focus_video()
    ev0, _ = B.snap("videofs-0-docked", extra="# outputs\n" + outputs_text + f"\n# Qt's monitors {order}\n# fsjson\n"
                    + json.dumps(st0, indent=1))
    B.combo("f")
    st1, full1, main1 = fs_wait(lambda f, m: f is not None and f["active"])
    time.sleep(1.0)
    st1, full1, main1 = fs_state()
    where1 = fs_window_output()
    ev1, _ = B.snap("videofs-1-fullscreen", extra="# fsjson\n" + json.dumps(st1, indent=1)
                    + f"\n# compositor output of the fullscreen window: {where1}; of the main window before: {main_out}")
    if B.name.startswith("sway") and where1:
        sh(["grim", "-o", where1, os.path.join(EVID, "videofs-1-output.png")])
        ev1.append("videofs-1-output.png")
    if main_out:
        on1 = fs_covers(full1, main_out) and where1 == main_out
    else:  # mutter: the main window's output is not published; the size tells which
        on1 = any(fs_covers(full1, n) for n in FS_OUTPUTS[B.name])
    # Transport keys in the fullscreen window.
    B.combo("space")
    stp, fp, _ = fs_wait(lambda f, m: f is not None and "Pause" in f["buttons"], timeout=5)
    playing = fp is not None and "Pause" in fp["buttons"]
    time.sleep(0.6)
    B.combo("space")
    sts, fs_, _ = fs_wait(lambda f, m: f is not None and "Play" in f["buttons"], timeout=5)
    paused = fs_ is not None and "Play" in fs_["buttons"]
    # The fullscreen seek bar's value is the frame shown.
    before = fs_["position"] if fs_ else None
    B.combo("right")
    str_, fr_, _ = fs_wait(lambda f, m: f is not None and f["position"] != before, timeout=5)
    after = fr_["position"] if fr_ else None
    stepped = before is not None and after is not None and after == before + 1
    B.combo("escape")
    st2, full2, main2 = fs_wait(lambda f, m: f is None and m is not None and m["active"])
    time.sleep(1.0)
    st2, full2, main2 = fs_state()
    ev2, _ = B.snap("videofs-2-left", extra="# fsjson\n" + json.dumps(st2, indent=1))
    keys = ("x", "y", "w", "h")
    same = main0 is not None and main2 is not None and all(main0[k] == main2[k] for k in keys)
    focus_back = panel_of(st2["focusPath"]) == "Video"
    verdict("videofs-enter-leave", "observed" if in_video and on1 and full1 and full1["active"] and full2 is None
            and main2 and main2["active"] and same and focus_back else "failed",
            f"F in the Video panel (focus there: {in_video}): fullscreen frame "
            f"{full1 and {k: full1[k] for k in keys + ('active',)}} on {where1 or 'an output of that size'}, the main "
            f"window's output {main_out}: {on1}; Esc: fullscreen gone {full2 is None}, main window active "
            f"{main2 and main2['active']}, geometry before {main0 and {k: main0[k] for k in keys}} after "
            f"{main2 and {k: main2[k] for k in keys}} same {same}; focus back in the Video panel: {focus_back} "
            f"({st2['focusPath']})", ev0 + ev1 + ev2)
    verdict("videofs-transport-keys", "observed" if playing and paused and stepped else "failed",
            f"in fullscreen Space played (Pause button shown: {playing}) and paused (Play shown again: {paused}); "
            f"Right stepped the seek bar's frame {before!r} -> {after!r}", ev1)

    # The second monitor through the context menu (menu key, then the item).
    primary = order[0] if order else None
    second = order[1] if len(order) > 1 else None
    moved = None
    if primary and main_out and main_out != primary:
        fs_move_main(primary)
        moved = fs_window_output(fullscreen=False)
        fs_focus_video()
    _, _, main3a = fs_state()
    B.combo("menu")
    time.sleep(1.0)
    item = sh(["python3", f"{GATE}/atspi_tool.py", "find", "menu item", "Open in full screen on monitor 2"])
    offered = "Open in full screen on monitor 2" in item
    # Down to the item, then Return (real keys: the activation request that
    # follows carries their input serial, which Wayland compositors want).
    how = None
    if offered:
        for _ in range(8):
            line = sh(["python3", f"{GATE}/atspi_tool.py", "find", "menu item", "Open in full screen on monitor 2"])
            log("menu item:", line.strip(), "| focus:", atspi()["focus"])
            if re.search(r"<[^>]*\b(focused|selected)\b", line):
                B.combo("return")
                how = "keys"
                break
            B.combo("down")
            time.sleep(0.4)
        else:
            sh(["python3", f"{GATE}/atspi_tool.py", "do", "menu item", "Open in full screen on monitor 2"])
            how = "AT-SPI action"
    else:
        B.keys("escape")
    st3, full3, main3 = fs_wait(lambda f, m: f is not None, timeout=8)
    time.sleep(1.5)
    st3, full3, main3 = fs_state()
    where3 = fs_window_output()
    ev3, _ = B.snap("videofs-3-monitor2", extra="# menu item\n" + item + "\n# fsjson\n" + json.dumps(st3, indent=1)
                    + f"\n# compositor output of the fullscreen window: {where3}; main window moved to {moved}")
    if B.name.startswith("sway") and where3:
        sh(["grim", "-o", where3, os.path.join(EVID, "videofs-3-output.png")])
        ev3.append("videofs-3-output.png")
    on2 = second is not None and fs_covers(full3, second) and (where3 is None or where3 == second)
    elsewhere = (moved or main_out) != second
    dock_hidden = main3 is not None and "Video" not in main3["panels"]
    B.combo("escape")
    st4, full4, main4 = fs_wait(lambda f, m: f is None and m is not None and "Video" in m["panels"])
    time.sleep(1.0)
    st4, full4, main4 = fs_state()
    ev4, _ = B.snap("videofs-4-left-monitor2", extra="# fsjson\n" + json.dumps(st4, indent=1))
    dock_back = main4 is not None and "Video" in main4["panels"]
    same4 = main3a is not None and main4 is not None and all(main3a[k] == main4[k] for k in keys)
    focus4 = panel_of(st4["focusPath"]) == "Video"
    scale = FS_OUTPUTS[B.name].get(second or "", (0, 0, 0))[2]
    verdict("videofs-monitor-choice", "observed" if offered and on2 and elsewhere and dock_hidden and full4 is None
            and dock_back and same4 and focus4 else "failed",
            f"Qt's monitors {order}; main window on {moved or main_out}; menu key in the Video panel offered 'Open in "
            f"full screen on monitor 2': {offered} (chosen by {how}); fullscreen frame {full3 and {k: full3[k] for k in keys + ('active',)}} "
            f"on {where3 or 'an output of that size'}, expected {second} (scale {scale}): {on2}, another output than "
            f"the main window's: {elsewhere}; docked Video hidden meanwhile: {dock_hidden}; Esc: fullscreen gone "
            f"{full4 is None}, docked Video back {dock_back}, main geometry unchanged {same4}, focus back in the Video "
            f"panel {focus4}", ev3 + ev4)
    app("kill")
    fs_outputs_reset()

STEPS = {"default": step_default, "kbd": step_keyboard_float_dock, "f6": step_f6_floating, "move": step_move_panel,
         "pointer": step_pointer, "video": step_video, "persist": step_persistence, "fullscreen": step_fullscreen,
         "menutext": step_menu_from_text, "tests": step_test_executables, "orca": step_orca,
         "a11y": step_a11y, "floating": step_floating, "header": step_header_keys,
         "views": step_views, "editor": step_editor, "videofs": step_video_fullscreen,
         # last: it removes an output, after which sway's virtual pointer
         # still maps absolute positions over the old layout
         "outputs": step_outputs}

if __name__ == "__main__":
    wanted = sys.argv[2:] or list(STEPS)
    for name in wanted:
        log(f"--- {B.name}: {name}")
        try:
            STEPS[name]()
        except Exception as e:  # keep going: one broken step must not hide the others
            import traceback
            traceback.print_exc()
            verdict(name, "error", f"harness error: {e!r}")
    app("kill")
