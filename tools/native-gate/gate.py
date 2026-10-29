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

from gatelib import TEST_RUNS, default_order, drag_points, drag_steps, find_frame, unexpected_skips

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
    # E6's "{\\", Orca's modifier and KP_Enter, the Hangul key
    "bracketleft": (26, "bracketleft", "bracketleft"), "backslash": (43, "backslash", "backslash"),
    "insert": (110, "Insert", "Insert"), "kp_enter": (96, "KP_Enter", "KP_Enter"),
    "hangul": (122, "Hangul", "Hangul"),
    # WS1: Ctrl+PgUp / Ctrl+PgDn (GLOBAL_PREVIOUS_TAB / GLOBAL_NEXT_TAB)
    "pageup": (104, "Prior", "Prior"), "pagedown": (109, "Next", "Next"),
    # WS3: Manage layouts…' F2 (to the name) and Delete
    "f2": (60, "F2", "F2"), "delete": (111, "Delete", "Delete"),
    # SH1: F10 to the menus
    "f10": (68, "F10", "F10"),
}
LETTERS = "qwertyuiop asdfghjkl zxcvbnm"
CODES = {"q": 16, "w": 17, "e": 18, "r": 19, "t": 20, "y": 21, "u": 22, "i": 23, "o": 24, "p": 25,
         "a": 30, "s": 31, "d": 32, "f": 33, "g": 34, "h": 35, "j": 36, "k": 37, "l": 38,
         "z": 44, "x": 45, "c": 46, "v": 47, "b": 48, "n": 49, "m": 50, " ": 57, "1": 2, "2": 3, "3": 4}
for ch, code in CODES.items():
    KEYS.setdefault(ch, (code, "space" if ch == " " else ch, "space" if ch == " " else ch))


def caption_is(caption, title):
    """A window's caption names TITLE; the main window is titled
    "<document> - HikariSub" once a Document is open."""
    return caption == title or (title == "HikariSub" and caption.endswith(" - HikariSub"))


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
        if not keys:
            # A modifier alone (Alt tapped, SH1): wtype's -M/-m change the
            # modifier state only, with no key event; -k presses its key.
            mods, keys = [], mods
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
            if m and caption_is(m.group(1), title):
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
            if m and caption_is(m.group(1), title) and fr:
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
        # side decoration (30 px) above its 1280x800 client area, or, with
        # its own title bar (SH1: GNOME's default), frameless at (160, 100);
        # floating panel windows are placed by mutter and cannot be located.
        if title == "HikariSub":
            own = bool(where("button", "Window menu", "HikariSub"))
            return (160, 100 if own else 115, 1280, 800, "Meta-0")
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


class Hyprland(Sway):
    """Hyprland (D3), nested in KWin (sessions/hyprland-start.sh; session
    directory "hypr"): wtype and the virtual pointer as on sway, window
    geometry from hyprctl."""
    name = "hyprland"

    def hyprctl(self, *args):
        return sh(["hyprctl", *args])

    def extent(self):
        mons = json.loads(self.hyprctl("monitors", "-j") or "[]")
        if not mons:
            return 1600, 1000
        return (max(m["x"] + int(m["width"] / m["scale"]) for m in mons),
                max(m["y"] + int(m["height"] / m["scale"]) for m in mons))

    def windows(self):
        mons = {m["id"]: m["name"] for m in json.loads(self.hyprctl("monitors", "-j") or "[]")}
        active = json.loads(self.hyprctl("activewindow", "-j") or "{}").get("address")
        out = []
        for c in json.loads(self.hyprctl("clients", "-j") or "[]"):
            (x, y), (w, h) = c["at"], c["size"]
            title = "'" + c["title"] + "'"  # as swaymsg's, which client_rect reads, apostrophes and all
            out.append(f'{"floating_con" if c["floating"] else "con":12} {c["class"]} {title} {x},{y} {w}x{h} '
                       f'client={x},{y} {w}x{h} output={mons.get(c["monitor"])} fullscreen={c["fullscreen"]} '
                       f'{"FOCUSED" if c["address"] == active else ""}')
        return "\n".join(out)

    def urgent(self, title):
        return False

    def focus(self, title_rx):
        self.hyprctl("dispatch", f'hl.dsp.focus({{ window = "title:{title_rx}" }})')


BACKENDS = {"sway": Sway, "sway-activate": SwayActivate, "kwin": KWin, "mutter": Mutter, "x11": X11,
            "hyprland": Hyprland}
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


def park_pointer():
    """The pointer to the bottom-left corner (3, 997: every session's screen
    is 1000 pixels high) before a menu is walked by keys, away from the
    menus (it may rest on the main window's status bar, where a pointer
    selects nothing): a submenu that opens under the resting pointer highlights the item
    there (KWin put View > Panels' submenu under it and Search, its last
    item, took the highlight and opened, so the Downs went into Search)."""
    try:
        if B.name == "x11":
            sh(["xdotool", "mousemove", "3", "997"])
        else:
            B.pointer("move 3 997", "wait 150")
    except Exception as e:  # noqa: BLE001 - parking is a courtesy, never a failure
        log(f"park_pointer: {e!r}")


def open_view():
    """Alt+W opens the View menu ("Vie&w": Alt+V stays with legacy's &Video).
    Retried until the View menu (its "Move panel…" item) shows."""
    park_pointer()
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
    highlight is on the item NAME. Down skips disabled items (items enabled
    by what is open, Manage layouts… with nothing saved), so the Downs are
    counted among the enabled items AT-SPI lists (`atspi_tool.py
    menu`; AT-SPI does not reliably follow the highlight)."""
    if opener is None:
        if not open_view():
            return False
    else:
        park_pointer()
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
    elif B.name == "hyprland":
        B.focus("HikariSub$")
    else:
        r = B.client_rect("HikariSub")
        # Qt's decoration above the client, or (SH1) the window's own title
        # bar, right of its title
        y = r[1] + 16 if r[1] == 100 else r[1] - 15
        B.pointer(f"move {r[0] + 1000 if r[1] == 100 else r[0] + 400} {y}", "wait 150", "down", "wait 50", "up",
                  "wait 400")
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
# docs/docking.md "Chrome": one 35-pixel header per group (a lone panel's
# title bar, or tabs), its "⋯" button named "<panel> options" opening the
# panel's menu (Move panel…, Undock or Dock, Close); floating panels are
# borderless with a drawn 8-pixel shadow, resized from it through the window
# system; on Wayland the header's title docks (the engine's drag-and-drop)
# and its free part moves the window through the compositor.
SHADOW = 8
HEADER = 35
ACCENT = "#9CDBC9"  # the dark theme's default (green) accent: only the drop highlight uses it
WAYLAND = sys.argv[1] in ("sway", "sway-activate", "kwin", "mutter", "hyprland")


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
    at = find_frame(raw, iw, ih, fw, fh, scale, x_from)
    if at is None:
        return None
    return (at[0] - SHADOW * scale, at[1] - SHADOW * scale, fr["w"] * scale, fr["h"] * scale, "screenshot")


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
    for x, y in drag_points(sx, sy, ex, ey, steps):
        cmds += [f"move {x} {y}", "wait 40"]
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


def system_frame_shadow():
    """The shadow a floating panel draws in this session: none inside a
    frame the window system draws (Docking.windowShadow), which Hyprland's
    border always is; SHADOW elsewhere."""
    return 0 if B.name == "hyprland" else SHADOW


def header_point(r, part="title"):
    """A point on a floating panel's header: its title (the engine's drag)
    or its free part right of the title (the window system's move), keeping
    clear of the "⋯" button."""
    shadow = system_frame_shadow()
    top = r[1] + shadow + 1
    if part == "title":
        return r[0] + shadow + 1 + 22, top + HEADER // 2
    return r[0] + r[2] - shadow - 1 - 12 - 20 - 2 - 24, top + HEADER // 2


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
    """D3's floating window: the same header, the drawn shadow, moved by the
    header's free part through the window system (startSystemMove, on every
    window system since 2026-10-12) and, on X11, by its title (the engine's
    drag; on Wayland pointer-drag-dock drags the title), and resized from the
    shadow through the window system."""
    fresh(env={"QT_LOGGING_RULES": "hikari.decorations.debug=true"})
    how = float_panel("Audio", "floating")
    st = wait_for(lambda s: "Audio" in frames(s))
    r = origin("Audio")
    if r is None:
        for item in ("floating-header", "floating-shadow", "floating-system-move", "floating-move", "floating-resize"):
            verdict(item, "not-observable", f"the floating Audio window (floated by {how}) cannot be located on "
                                            f"{B.name}")
        return
    # X11 here runs no compositing manager (openbox alone): no transparency,
    # so no shadow (Docking.floatingShadow 0); a picom run below checks it.
    # Inside Hyprland's border the panel draws none either: the border sits
    # tight round its 1-pixel frame.
    shadow = 0 if B.name == "x11" else system_frame_shadow()
    floating_frame(r, shadow, "float-0-window")
    floating_decoration("Audio", r)
    # Move, somewhere no drop is offered: away from the main window where
    # there is room.
    main = origin("HikariSub")
    moves = [("floating-system-move", "free", "the header's free part (the window system's move, startSystemMove)")]
    if not WAYLAND:
        moves.append(("floating-move", "title", "the header's title (the engine's drag)"))
    for item, part, how_move in moves:
        r = origin("Audio") or r
        px, py = header_point(r, part=part)
        dx, dy = -160, 120
        if main and not WAYLAND:
            dx = (main[0] - 40 - r[2]) - r[0] if main[0] - 40 - r[2] > 0 else (main[0] + main[2] + 40) - r[0]
            dy = 60 if part == "title" else 100
        # steps of at most 8 pixels: the window lags the pointer by the
        # step that crosses the drag distance
        drag(px, py, px + dx, py + dy, steps=drag_steps(dx, dy))
        st = wait_for(lambda s: "Audio" in frames(s), timeout=3)
        r2 = origin("Audio")
        ev1, st = B.snap(f"float-1-moved-{part}")
        still = "Audio" in frames(st)
        # A system move starts once the pointer has moved the drag distance
        # (about 10 pixels), which the window does not follow
        moved = r2 is not None and abs((r2[0] - r[0]) - dx) <= 16 and abs((r2[1] - r[1]) - dy) <= 16
        verdict(item, "observed" if still and moved else "failed",
                f"dragged {how_move} from {px},{py} by {dx},{dy}: window {r[:2]} -> {r2 and r2[:2]}, still floating "
                f"{still}", ev1)
        if not still:
            break
    r2 = origin("Audio")
    if r2 and "Audio" in frames(atspi()):
        fx, fy = header_point(r2, part="free")
        click(fx, fy, double=True, wait=1500)
        st = wait_for(lambda s: "Audio" not in frames(s))
        ev, st = B.snap("float-2-free-part-dblclick")
        docked = "Audio" not in frames(st)
        verdict("free-part-dblclick-docks", "observed" if docked else "failed",
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
    elif B.name == "hyprland":
        # Hyprland answers xdg-decoration with server-side but draws only its
        # border (general:border_size) round the whole surface, no title
        # bar: the header must still show the panel's name.
        hdr = where("text", title, title)
        box = (r[0] + hdr[0]["x"] + 8, r[1] + hdr[0]["y"] + 6, 70, HEADER - 12) if r and hdr else None
        ink = title_ink("float-0-window.png", box) if box else None
        try:
            bs = json.loads(B.hyprctl("-j", "getoption", "general:border_size")).get("int")
        except ValueError:
            bs = "?"
        log_ = decoration_log()
        named = bool(ink) and ink >= 20
        verdict("floating-borderless", "observed" if named else "failed",
                f"Hyprland's xdg-decoration answer: {log_}; Hyprland draws no title bar, only its {bs}-pixel border "
                f"round the window's whole surface, with no shadow drawn inside it; the header's name "
                f"(title ink {ink} in {box}, 20 or more when drawn) "
                + ("is shown" if named else "is MISSING: the header leaves the name to a window-system title bar "
                   "that Hyprland never draws"), ["float-0-window.png"])
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
    where_ = ("X11 without a compositing manager" if B.name == "x11"
              else f"inside {B.name}'s border (no shadow drawn in a window-system frame)")
    verdict("floating-no-compositor-frame" if B.name == "x11" else "floating-shadow" + suffix,
            "observed" if edge and not black and len(set(edge)) <= 2 else "failed",
            f"{where_}: the window's outermost pixels {sorted(set(edge))} "
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


def arrangement(st):
    """Every shown panel's place for the persistence check: the window it is
    in ("main" or its own floating window), its rectangle inside that window
    and, for a floating panel, its window's size and (where the window system
    reports it) its position; the main window's size."""
    fr = frames(st)
    out = {}
    for p in st["panels"]:
        if p["id"] not in PANELS or p["id"] in out:
            continue
        floating = p["frame"] != "HikariSub"
        entry = {"window": "floating" if floating else "main", "rect": (p["x"], p["y"], p["w"], p["h"])}
        if floating:
            f = fr.get(p["frame"], {})
            # None when unreadable (no frame or no size): a reading before exit
            # without it is not compared.
            entry["size"] = (f["w"], f["h"]) if f.get("w") is not None and f.get("h") is not None else None
            r = B.client_rect(p["frame"]) if B.name == "x11" else None
            entry["position"] = r[:2] if r else None
        out[p["id"]] = entry
    main = fr.get("HikariSub", {})
    out["(main window)"] = {"window": "main", "size": (main["w"], main["h"])
                            if main.get("w") is not None and main.get("h") is not None else None}
    return out


def settled_arrangement(pred, timeout=10.0, step=0.5):
    """arrangement() once PRED holds and two readings STEP seconds apart
    agree (the compositor and the layout have stopped moving), or the last
    reading at TIMEOUT."""
    end = time.time() + timeout
    st = wait_for(pred, timeout=timeout)
    current = arrangement(st)
    while time.time() < end:
        time.sleep(step)
        st = atspi()
        reading = arrangement(st)
        if pred(st) and not arrangement_differences(current, reading, tolerance=0):
            return reading
        current = reading
    return current


def arrangement_differences(before, after, tolerance=2):
    """What moved between two arrangement() readings: a panel shown or gone,
    another window, or a rectangle, size or position more than TOLERANCE
    pixels away."""
    def near(a, b):
        return a is not None and b is not None and len(a) == len(b) and all(
            x is not None and y is not None and abs(x - y) <= tolerance for x, y in zip(a, b))
    diffs = []
    for pid in sorted(set(before) | set(after)):
        a, b = before.get(pid), after.get(pid)
        if a is None or b is None:
            diffs.append(f"{pid}: {'shown' if a else 'not shown'} before, {'shown' if b else 'not shown'} after")
            continue
        if a["window"] != b["window"]:
            diffs.append(f"{pid}: {a['window']} before, {b['window']} after")
            continue
        for key in ("rect", "size", "position"):
            if key in a and a[key] is not None and not near(a[key], b.get(key)):
                diffs.append(f"{pid}: {key} {a[key]} before, {b.get(key)} after")
    return diffs


def step_persistence():
    """Layout persistence across a restart, as the Windows gate checks it and
    more: every panel's window (docked in the main window or floating) and
    its geometry. Audio is floated first, so both kinds are in the layout."""
    fresh()
    float_panel("Audio", "persist")
    before = settled_arrangement(lambda s: "Audio" in frames(s))
    ev0, _ = B.snap("persist-0-before-exit", extra="# arrangement\n" + json.dumps(before, indent=1))
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
    after = settled_arrangement(
        lambda s: "Audio" in frames(s) and {p["id"] for p in s["panels"]} >= set(before) - {"(main window)"})
    ev1, _ = B.snap("persist-1-after-restart", extra="# layout files\n" + cfg + "\n# arrangement\n"
                    + json.dumps(after, indent=1))
    diffs = arrangement_differences(before, after)
    if diffs:  # read again once the restored windows had time to settle
        time.sleep(4)
        after = arrangement(atspi())
        diffs = arrangement_differences(before, after)
        ev2, _ = B.snap("persist-2-settled", extra="# arrangement\n" + json.dumps(after, indent=1))
        ev1 += ev2
    floating = sorted(p for p, e in before.items() if e["window"] == "floating")
    docked = sorted(p for p, e in before.items() if e["window"] == "main" and p != "(main window)")
    ok = gone and "Audio" in floating and docked and not diffs
    where = ("floating windows' positions compared (X11)" if B.name == "x11" else
             "floating windows' positions not compared: on Wayland the compositor places them, a client cannot")
    verdict("layout-persistence", "observed" if ok else "failed",
            f"main window closed through the compositor (process ended: {gone}); layout files: {cfg.strip()!r}; "
            f"before exit floating {floating}, docked {docked}; after restart every panel in the same window "
            f"with the same rectangle and window size (within 2 px): {not diffs}"
            + (f"; differences: {diffs}" if diffs else "") + f"; {where}", ev0 + ev1)


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
    runs = TEST_RUNS
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
    # gatelib.KNOWN_TEST_SKIPS: platform limits that other gate items observe
    unexpected = unexpected_skips(skips)
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
    """The header from the keyboard (docs/docking.md, Keyboard and
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
    # its button (docs/docking.md). F6 focuses the panel itself, whose
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


# ---------------------------------------------------------------- WS3 (#217)
CORE = ("Video", "Audio", "Line editor", "Grid")
# View > Layout's presets (WorkspaceLayoutController::presets), and the items
# WS3 retired: legacy's View arrangements and the editor switch.
PRESETS = ("Editing", "Timing", "Translation", "Typesetting")
RETIRED_VIEW_ITEMS = ("All", "Video and subs", "Audio and subs", "Only video", "Only subtitles")
RETIRED_EDITOR_ITEM = "Enable / Disable editor"


def core_panels(st):
    """The core panels shown docked in the main window, with their places."""
    return {p["id"]: (p["x"], p["y"], p["w"], p["h"]) for p in st["panels"]
            if p["id"] in CORE and p["frame"] == "HikariSub"}


def video_episode():
    shutil.copy(os.path.join(FIXTURES, "cfr.mkv"), os.path.join(HOME, "ep1.mkv"))
    subs = os.path.join(HOME, "ep1.ass")
    with open(subs, "w") as f:
        f.write("[Script Info]\r\nScriptType: v4.00+\r\nPlayResX: 320\r\nPlayResY: 240\r\nVideo File: ep1.mkv\r\n\r\n"
                "[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n"
                "Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,first\r\n"
                "Dialogue: 0,0:00:05.00,0:00:09.00,Default,,0,0,0,,second\r\n")
    return subs


def showing_menus():
    """The items of each popup menu showing (`atspi_tool.py menu`)."""
    try:
        return json.loads(sh(["python3", f"{GATE}/atspi_tool.py", "menu"]) or "[]")
    except ValueError:
        return []


def layout_menu():
    """The View > Layout submenu's items, when it shows."""
    return next((m for m in showing_menus() if any(i["name"] in PRESETS for i in m)), [])


def open_layout_menu():
    """View > Layout opened from the keyboard: its items, or [] when it does
    not show. Retried with the main window given the focus through the
    compositor (on mutter, after a restart, the keys stopped reaching the
    View menu the first time)."""
    for attempt in range(3):
        if menu_to("Layout"):
            B.keys("right", 0.6)
            items = layout_menu()
            if items:
                return items
        log(f"open_layout_menu: View > Layout did not show (attempt {attempt + 1})")
        B.keys("escape", "escape", "escape", 0.4)
        focus_main_compositor()
    return []


def layout_to(name):
    """View > Layout > NAME from the keyboard: Right opens the submenu with
    its first item highlighted; the Downs are counted among its enabled
    items (Down skips Manage layouts… while nothing is saved)."""
    items = open_layout_menu()
    enabled = [i["name"] for i in items if i["enabled"]]
    if name not in enabled:
        log(f"layout_to: {name!r} is not an enabled item of {items}")
        return False
    if enabled.index(name):
        B.keys(*(["down"] * enabled.index(name)), delay=0.3)
    return True


def layout_items():
    """View > Layout's items, {name: enabled}, then the menus closed."""
    items = {i["name"]: i["enabled"] for i in open_layout_menu()}
    B.keys("escape", "escape", "escape", 0.4)
    return items


def menu_names(opener):
    """Every item name of the menus a key opens, then the menus closed."""
    if opener == "view":
        open_view()
    else:
        park_pointer()
        B.keys(opener, 0.6)
    names = [i["name"] for m in showing_menus() for i in m]
    B.keys("escape", "escape", 0.4)
    return names


def switcher_label():
    """The layout switcher's accessible name ("Layout: NAME"), in the title bar since SH1."""
    try:
        bars = json.loads(sh(["python3", f"{GATE}/atspi_tool.py", "statusbar"]) or "[]")
    except ValueError:
        bars = []
    return next((c["name"] for bar in bars for c in bar["children"] if (c["name"] or "").startswith("Layout: ")),
                None)


def step_layouts():
    """WS3 (#217): the View arrangements and the editor switch are gone
    (menus and Ctrl+E); from the keyboard, View > Layout > Save layout as…
    keeps the tab's arrangement under a name, a preset replaces it and the
    saved layout brings it back, the status bar's switcher naming the tab's
    layout; the saved layout is there after a restart; Manage layouts…
    renames it (F2) and deletes it (Delete)."""
    subs = episode()
    fresh(subs)
    dismiss_notices()
    focus_main()
    ev, st = B.snap("layouts-0-editing")
    evidence = list(ev)
    base = core_panels(st)
    view = menu_names("view")
    subtitles = menu_names("alt+s")
    first = layout_items()
    switch0 = switcher_label()
    focus_main()
    B.keys("ctrl+e", 1.5)  # legacy's editor switch: bound to nothing now
    after_e = core_panels(atspi())
    gone = not set(RETIRED_VIEW_ITEMS) & set(view) and RETIRED_EDITOR_ITEM not in subtitles
    listed = all(first.get(p) for p in PRESETS) and first.get("Save layout as…") and first.get("Manage layouts…") is False
    verdict("layouts-menus", "observed" if gone and listed and "Layout" in view and switch0 == "Layout: Editing"
            else "failed",
            f"View: {view}; Subtitles has the editor switch: {RETIRED_EDITOR_ITEM in subtitles}; View > Layout "
            f"(enabled): {first}; switcher: {switch0!r}", evidence)
    verdict("layouts-editor-key-retired", "observed" if base and set(after_e) == set(base) else "failed",
            f"Ctrl+E: panels before {sorted(base)}, after {sorted(after_e)}", evidence)

    # Audio hidden, saved as "no audio"; Timing; the saved layout back. The
    # names are lowercase: a typed capital lost its Shift on KWin, mutter and X11.
    panel_menu("Audio", "Hide")
    st = wait_for(lambda s: "Audio" not in core_panels(s), timeout=5)
    saved = set(core_panels(st))
    reached = layout_to("Save layout as…")
    B.keys("return", 1.2)
    B.type_text("no audio")
    time.sleep(0.5)
    B.keys("return", 1.5)
    switch1 = switcher_label()
    ev, st = B.snap("layouts-1-saved")
    evidence += ev
    reached &= layout_to("Timing")
    B.keys("return", 1.5)
    st = wait_for(lambda s: set(core_panels(s)) != saved, timeout=5)
    timing, switch2 = set(core_panels(st)), switcher_label()
    ev, st = B.snap("layouts-2-timing")
    evidence += ev
    reached &= layout_to("no audio")
    B.keys("return", 1.5)
    st = wait_for(lambda s: set(core_panels(s)) == saved, timeout=5)
    back, switch3 = set(core_panels(st)), switcher_label()
    ev, st = B.snap("layouts-3-saved-applied")
    evidence += ev
    rows = [f"saved {sorted(saved)} as 'no audio' (switcher {switch1!r})",
            f"Timing: {sorted(timing)} ({switch2!r})", f"no audio again: {sorted(back)} ({switch3!r})"]
    verdict("layouts-save-apply", "observed" if reached and "Audio" not in saved and timing != saved
            and back == saved and switch1 == switch3 == "Layout: no audio" and switch2 == "Layout: Timing"
            else "failed", "from the keyboard: " + "; ".join(rows), evidence)

    # A restart: the saved layout is still listed (layouts.json).
    close_window("HikariSub")
    time.sleep(2.5)
    if app("alive").strip() != "no":
        time.sleep(6)
        app("kill")
    cfg = sh(f"find {os.environ['XDG_CONFIG_HOME']}/HikariSub -name 'layouts.json*' -printf '%p %s\\n'")
    app("start", subs)  # as the step started (on mutter, the start without a file left the menus without the keys)
    time.sleep(3)
    dismiss_notices()
    focus_main()
    restarted = layout_items()
    ev, _ = B.snap("layouts-4-restarted", extra="# layouts files\n" + cfg)
    evidence += ev
    verdict("layouts-persist", "observed" if restarted.get("no audio") and "layouts.json" in cfg else "failed",
            f"after a restart View > Layout: {restarted}; files: {cfg.strip()!r}", evidence)

    # Manage layouts…: F2 renames, Delete deletes; the dialog closes on the last.
    reached = layout_to("Manage layouts…")
    B.keys("return", 1.2)
    ev, _ = B.snap("layouts-5-manage")
    evidence += ev
    B.keys("f2", 0.4, "ctrl+a", 0.3)
    B.type_text("quiet")
    time.sleep(0.5)
    B.keys("return", 1.0, "escape", 0.8)
    renamed, switch4 = layout_items(), switcher_label()
    reached &= layout_to("Manage layouts…")
    B.keys("return", 1.2, "delete", 1.2)
    deleted, switch5 = layout_items(), switcher_label()
    ev, _ = B.snap("layouts-6-deleted")
    evidence += ev
    open(os.path.join(EVID, "layouts.txt"), "w").write(
        f"View: {view}\nSubtitles: {subtitles}\nfirst: {first}\n" + "\n".join(rows)
        + f"\nrestarted: {restarted}\nrenamed: {renamed} ({switch4!r})\ndeleted: {deleted} ({switch5!r})\n")
    evidence.append("layouts.txt")
    verdict("layouts-rename-delete", "observed" if reached and renamed.get("quiet") and "no audio" not in renamed
            and switch4 == "Layout: quiet" and "quiet" not in deleted and deleted.get("Manage layouts…") is False
            and switch5 == "Layout: Editing" else "failed",
            f"renamed: {renamed} ({switch4!r}); deleted: {deleted} ({switch5!r})", evidence)


# ---------------------------------------------------------------- WS1 (#215)
def step_tabs():
    """WS1 (ADR 0020): each Document tab keeps its own panel arrangement.
    Audio floats in the first tab; Ctrl+T opens a tab that copies it (the
    same windows, nothing restored); there Audio docks again and the Line
    editor floats. Ctrl+PgUp and Ctrl+PgDn through the compositor's keyboard
    switch back and forth: each time the tab's panels come back in the same
    windows with the same rectangles and window sizes (within 2 px; on X11
    the floating windows' positions too, on Wayland the compositor places
    them), and the panels a tab does not float are docked again."""
    fresh(episode())
    dismiss_notices()
    float_panel("Audio", "tabs")
    first = settled_arrangement(lambda s: "Audio" in frames(s))
    ev0, _ = B.snap("tabs-0-first-tab", extra="# arrangement\n" + json.dumps(first, indent=1))
    focus_main()
    B.keys("ctrl+t", 1.5)
    copy = settled_arrangement(lambda s: "Audio" in frames(s))
    ev1, st = B.snap("tabs-1-new-tab", extra="# arrangement\n" + json.dumps(copy, indent=1))
    copied = arrangement_differences(first, copy)
    panel_menu("Audio", "Dock")
    float_panel("Line editor", "tabs")
    second = settled_arrangement(lambda s: "Line editor" in frames(s) and "Audio" not in frames(s))
    ev2, _ = B.snap("tabs-2-second-tab-own", extra="# arrangement\n" + json.dumps(second, indent=1))
    focus_main()
    B.keys("ctrl+pageup", 1.5)
    back = settled_arrangement(lambda s: "Audio" in frames(s) and "Line editor" not in frames(s))
    ev3, _ = B.snap("tabs-3-first-tab-again", extra="# arrangement\n" + json.dumps(back, indent=1))
    focus_main()
    B.keys("ctrl+pagedown", 1.5)
    again = settled_arrangement(lambda s: "Line editor" in frames(s) and "Audio" not in frames(s))
    ev4, _ = B.snap("tabs-4-second-tab-again", extra="# arrangement\n" + json.dumps(again, indent=1))
    to_first = arrangement_differences(first, back)
    to_second = arrangement_differences(second, again)
    distinct = bool(arrangement_differences(first, second))
    where = ("floating windows' positions compared (X11)" if B.name == "x11" else
             "floating windows' positions not compared: on Wayland the compositor places them")
    verdict("tab-new-copies-arrangement", "observed" if not copied else "failed",
            "Ctrl+T: the new tab shows the first tab's arrangement, Audio floating"
            + (f"; differences: {copied}" if copied else ""), ev0 + ev1)
    verdict("tab-switch-floating", "observed" if distinct and not to_first and not to_second else "failed",
            f"tabs differ ({distinct}); Ctrl+PgUp back to the first tab: same windows and rectangles {not to_first}"
            + (f" {to_first}" if to_first else "") + f"; Ctrl+PgDn to the second: {not to_second}"
            + (f" {to_second}" if to_second else "") + f"; {where}", ev0 + ev2 + ev3 + ev4)


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

# ---------------------------------------------------------------- screen reader, IME, portal and icon steps
# Added for the wave-5 Linux sweep (2026-10-14): what a screen reader says of
# E6's tag list and P10's status bar, R2's reference tray through AT-SPI and
# Orca, K1's icon-only controls through AT-SPI, E6's tag list under a real
# input method (ibus), Y7's screen dropper (a real X11 desktop; Wayland
# through the screenshot portal's PickColor), K2's follow-system through the
# settings portal, and D3's free-part move against the window system's edge
# tiling.
def orca_start(tag):
    path = os.path.join(EVID, f"{tag}-orca-debug.out")
    if os.path.exists(path):
        os.unlink(path)
    p = subprocess.Popen(["orca", "--replace", "--debug-file", path], stdout=subprocess.DEVNULL,
                         stderr=open(os.path.join(EVID, f"{tag}-orca-stderr.txt"), "w"))
    time.sleep(8)
    return p, path


def orca_stop(p, path, tag):
    time.sleep(1.5)
    p.terminate()
    try:
        p.wait(10)
    except subprocess.TimeoutExpired:
        p.kill()
    text = open(path, errors="replace").read() if os.path.exists(path) else ""
    speech = [l.strip() for l in text.splitlines() if "SPEECH OUTPUT" in l]
    open(os.path.join(EVID, f"{tag}-orca-speech.txt"), "w").write("\n".join(speech) + "\n")
    return speech, text


def orca_key(key, times=1):
    """Orca's modifier (Insert, the desktop layout's) held while KEY is
    pressed, TIMES times in a row (Orca counts repeats within its
    double-click time)."""
    for _ in range(times):
        if B.name == "x11":
            sh(["xdotool", "keydown", "Insert", "key", KEYS[key][1], "keyup", "Insert"])
        elif isinstance(B, Sway):
            sh(["wtype", "-P", "Insert", "-k", KEYS[key][2], "-p", "Insert"])
        else:
            code = KEYS[key][0]
            B.ei("keydown 110", f"keydown {code}", f"keyup {code}", "keyup 110")
        time.sleep(0.12)


def state_of(line):
    m = re.search(r"<([^>]*)>", line or "")
    return m.group(1).split(",") if m else []


def show_tags():
    """E6: the hidden-tag view refuses ASS syntax (no tag list there); the
    Line editor's "Hide tags" switch, checked while tags are hidden, is
    pressed off through AT-SPI when it is on."""
    for role in ("toggle button", "button", "push button", "check box"):
        hits = [l for l in atspi_find(role, "Hide tags") if "showing" in state_of(l)]
        if hits:
            if "checked" in state_of(hits[0]):
                atspi_do(role, "Hide tags", "Press") if "Press" in hits[0] else atspi_do(role, "Hide tags")
                time.sleep(0.8)
            return role, "checked" in state_of((atspi_find(role, "Hide tags") or [""])[0])
    return None, None


def line_text():
    return atspi()["texts"].get("Line text")


def in_line_text(st=None):
    """The Line text holds the keyboard focus (Qt marks its panel focused
    too, which atspi()'s focus may name first)."""
    return any("focused" in state_of(l) for l in atspi_find("text", "Line text"))


def tag_list_shown():
    return any("showing" in state_of(l) for l in atspi_find("list", "Tag list"))


def open_tag_list():
    """Focus the Line text at its start and type "{\\" through the compositor."""
    atspi_do("text", "Line text", "SetFocus")
    time.sleep(0.6)
    B.keys("home", 0.3)
    if isinstance(B, Sway):
        # wtype's keymap has one level per key: Shift+[ stays "["
        sh(["wtype", "{\\"])
    else:
        B.combo("shift", "bracketleft")
        time.sleep(0.3)
        B.combo("backslash")
    time.sleep(1.5)


def step_tag_list_speech():
    """E6 (#188): the tag list's announcements (Accessible.announce from the
    Line text, which keeps the focus) as AT-SPI announcement events and in
    Orca's speech: the list opened by "{\\", narrowed by "b", Down, Return."""
    path = episode()
    fresh(path)
    dismiss_notices()
    ann_path = os.path.join(EVID, "tag-announcements.txt")
    ann = subprocess.Popen(["python3", f"{GATE}/atspi_tool.py", "announcements", "120"],
                           stdout=open(ann_path, "w"), stderr=subprocess.STDOUT)
    time.sleep(1)
    orca, olog = orca_start("tag")
    focus_main_compositor()
    role, still_hidden = show_tags()
    open_tag_list()
    shown0 = tag_list_shown()
    foc = [in_line_text()]
    ev0, st0 = B.snap("tag-0-list-open")
    B.combo("b")
    time.sleep(1.2)
    foc.append(in_line_text())
    ev1, st1 = B.snap("tag-1-narrowed-b")
    B.keys("down", 1.2)
    foc.append(in_line_text())
    ev2, st2 = B.snap("tag-2-down")
    B.keys("return", 1.2)
    st3 = atspi()
    ev3, _ = B.snap("tag-3-chosen")
    speech, _ = orca_stop(orca, olog, "tag")
    ann.terminate()
    ann.wait(5)
    said = [l.strip() for l in open(ann_path, errors="replace") if "announcement" in l]
    text = st3["texts"].get("Line text")
    kept = all(foc)
    verdict("tag-list-completes", "observed" if shown0 and text == "{\\blurfirst" and kept else "failed",
            f"'Hide tags' ({role}) off, still hidden {still_hidden}; '{{\\' in the Line text: Tag list shown {shown0}; "
            f"'b', Down, Return -> Line text {text!r} (expected '{{\\blurfirst'); focus stayed in the Line text: "
            f"{kept} {foc}", ev0 + ev1 + ev2 + ev3)
    narrowed = [l for l in said if " of 3" in l]
    verdict("tag-list-announced-atspi", "observed" if said and narrowed and any("blur" in l for l in narrowed)
            else "failed", f"{len(said)} object:announcement events: {said[:6]}", ["tag-announcements.txt"])
    spoke = [l for l in speech if " of 3" in l or " of 20" in l or "Edge blur" in l]
    verdict("tag-list-orca", "observed" if any(" of 3" in l for l in spoke) else "failed",
            f"Orca spoke {len(speech)} lines; tag list lines: {spoke[:6]} (tag-orca-speech.txt)",
            ["tag-orca-speech.txt", "tag-orca-debug.out"])


def step_status_bar_speech():
    """P10 (#200): Orca's "present status bar" (Orca+KP_Enter twice, the
    desktop layout) reads the status bar's fields, which take no focus."""
    subs = video_episode()
    fresh(subs)
    dismiss_notices()
    sh(["python3", f"{GATE}/atspi_tool.py", "press-showing", "Load associated"])
    time.sleep(4)
    fields = sh(["python3", f"{GATE}/atspi_tool.py", "statusbar"])
    open(os.path.join(EVID, "statusbar-atspi.json"), "w").write(fields)
    try:
        bars = json.loads(fields or "[]")
    except ValueError:
        bars = []
    texts = [c["name"] for b in bars for c in b["children"] if c["showing"] and c["name"]]
    orca, olog = orca_start("statusbar")
    focus_main_compositor()
    time.sleep(1)
    orca_key("kp_enter", times=2)
    time.sleep(3)
    route = "Orca+KP_Enter twice through the compositor's keyboard"
    speech, debug = orca_stop(orca, olog, "statusbar")
    heard = [t for t in texts if any(t in l for l in speech)]
    handled = "present_status_bar" in debug
    if not handled:
        # The keys did not reach Orca (no keyboard monitor in this
        # compositor): its D-Bus remote control runs the same command.
        orca, olog = orca_start("statusbar-dbus")
        focus_main_compositor()
        time.sleep(1)
        out = sh(["gdbus", "call", "--session", "--dest", "org.gnome.Orca1.Service", "--object-path",
                  "/org/gnome/Orca1/Service/WhereAmIPresenter", "--method",
                  "org.gnome.Orca1.WhereAmIPresenter.PresentStatusBar", "true"])
        time.sleep(3)
        speech, debug = orca_stop(orca, olog, "statusbar-dbus")
        heard = [t for t in texts if any(t in l for l in speech)]
        route = f"Orca's D-Bus command WhereAmIPresenter.PresentStatusBar ({out.strip()}); the keys did not reach Orca"
    ev, _ = B.snap("statusbar-0")
    verdict("status-bar-orca", "observed" if texts and len(heard) >= min(2, len(texts)) else "failed",
            f"status bar fields over AT-SPI {texts}; by {route}: Orca spoke {heard} "
            f"({[l for l in speech if any(t in l for t in texts)][:4]})",
            ev + ["statusbar-atspi.json", "statusbar-orca-speech.txt"])


def reference_session():
    """Two tabs restored from LastSession.txt (session.restore 2): ref.ass,
    then edit.ass, which is the editing target; Ctrl+Q shows ref.ass as the
    linked reference (R2, reference_tests' fixtures)."""
    head = "[Script Info]\nScriptType: v4.00+\n\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
    files = {"edit.ass": ["0:00:01.00,0:00:02.00,e1", "0:00:02.00,0:00:03.00,e2", "0:00:08.00,0:00:09.00,e3"],
             "ref.ass": ["0:00:01.00,0:00:02.00,r0", "0:00:05.00,0:00:06.00,r1", "0:00:01.50,0:00:02.50,r2",
                         "0:00:02.50,0:00:03.50,r3", "0:00:10.00,0:00:11.00,r4"]}
    for name, lines in files.items():
        with open(os.path.join(HOME, name), "w") as f:
            f.write(head + "".join(f"Dialogue: 0,{l.rsplit(',', 1)[0]},Default,,0,0,0,,{l.rsplit(',', 1)[1]}\n"
                                   for l in lines))
    app("fresh")
    conf = os.path.join(os.environ["XDG_CONFIG_HOME"], "HikariSub", "HikariSub")
    os.makedirs(conf, exist_ok=True)
    tabs = ""
    for i, name in enumerate(("ref.ass", "edit.ass")):
        tabs += (f"Tab: {i}\r\nVideo: \r\nPosition: 0\r\nFFMS2: 1\r\nSubtitles: {os.path.join(HOME, name)}\r\n"
                 "Active: 0\r\nScroll: 0\r\nEditor: 1\r\n")
    with open(os.path.join(conf, "LastSession.txt"), "w", encoding="utf-8-sig", newline="") as f:
        f.write("[HikariSub v0.0.1]\r\n[Close session]\r\n" + tabs)
    with open(os.path.join(conf, "hikari.ini"), "w") as f:
        f.write("[registry]\nschema=1\n\n[profile]\nsession.restore=2\n")
    app("start")
    time.sleep(3)
    dismiss_notices()


def rclick(x, y, wait=1200):
    if B.name == "x11":
        sh(["xdotool", "mousemove", str(x), str(y), "sleep", "0.25", "click", "3"])
        time.sleep(wait / 1000)
    else:
        B.pointer(f"move {x} {y}", "wait 250", "rdown", "wait 60", "rup", f"wait {wait}")


def step_reference_speech():
    """R2 (#194): the reference tray's names through AT-SPI (the linked
    toggle, the match status, Previous/Next match, the protected grid, the
    reference tab's description, Show nearest line, the occurrence menu)
    and what Orca says while the keyboard moves through it."""
    reference_session()
    st = atspi()
    title = [f["name"] for f in st["frames"]]
    orca, olog = orca_start("reference")
    focus_main_compositor()
    in_grid = f6_to("Grid")
    B.keys("ctrl+q", 2.0)
    dump0 = sh(["python3", f"{GATE}/atspi_tool.py", "dump"])
    open(os.path.join(EVID, "reference-0-tree.txt"), "w").write(dump0)
    ev0, _ = B.snap("reference-0-linked")
    names = {}
    for role, name in (("check box", "Follow the editing line"), ("toggle button", "Follow the editing line"),
                       ("button", "Previous match"), ("button", "Next match"), ("label", "Match 1 of 2"),
                       ("table", "Reference lines (protected, read-only)"), ("page tab", "ref.ass")):
        hits = atspi_find(role, name)
        if hits:
            names.setdefault(name, hits[0])
    tab_desc = "Protected reference" in names.get("ref.ass", "")
    # Next match through AT-SPI's action (the buttons take no keyboard
    # focus), then the keyboard: F6 into the tray, Down in its grid.
    atspi_do("button", "Next match")
    time.sleep(1.0)
    status2 = bool(atspi_find("label", "Match 2 of 2"))
    in_ref = f6_to("Reference")
    B.keys("down", 0.8, "up", 0.8)
    # The occurrence menu: a right click on the reference grid's first row
    # (legacy's preview menu came from the right button only).
    tbl = where("table", "Reference lines (protected, read-only)")
    main = origin("HikariSub")
    if tbl and main:
        rclick(main[0] + tbl[0]["x"] + 60, main[1] + tbl[0]["y"] + 34)
    menus = sh(["python3", f"{GATE}/atspi_tool.py", "menu"])
    ev1, _ = B.snap("reference-1-occurrence-menu", extra="# menus\n" + menus)
    B.keys("escape", 0.6)
    # Show nearest line: the editing Line e3 overlaps nothing.
    f6_to("Grid")
    B.keys("end", 1.2)
    nearest = [l for l in atspi_find("button", "Show nearest line") if "showing" in state_of(l)]
    nomatch = bool(atspi_find("label", "No matching line"))
    ev2, _ = B.snap("reference-2-no-match")
    speech, _ = orca_stop(orca, olog, "reference")
    expected = ["Follow the editing line", "Previous match", "Next match", "Match 1 of 2",
                "Reference lines (protected, read-only)"]
    verdict("reference-names-atspi", "observed" if all(n in names for n in expected) and tab_desc and status2
            and nearest and nomatch and menus.strip() not in ("", "[]") else "failed",
            f"windows {title}; editing Grid focused {in_grid}; after Ctrl+Q: {json.dumps(names)}; tab ref.ass "
            f"described 'Protected reference' {tab_desc}; Next match -> 'Match 2 of 2' {status2}; End in the Grid "
            f"(e3): 'No matching line' {nomatch}, 'Show nearest line' {nearest[:1]}; occurrence menu {menus.strip()}",
            ev0 + ev1 + ev2 + ["reference-0-tree.txt"])
    spoken = [l for l in speech if any(k in l for k in ("Reference lines", "protected", "Match", "Reference", "r0",
                                                         "r2", "r3"))]
    verdict("reference-orca", "observed" if in_ref and any("Reference lines" in l or "protected" in l for l in speech)
            else "failed", f"F6 into the Reference panel {in_ref}; Orca spoke {len(speech)} lines, the tray's: "
                           f"{spoken[:8]}", ["reference-orca-speech.txt", "reference-orca-debug.out"])


def controls_scan(tag):
    out = sh(["python3", f"{GATE}/atspi_tool.py", "controls"], timeout=180)
    open(os.path.join(EVID, f"icons-{tag}.json"), "w").write(out)
    ev, _ = B.snap(f"icons-{tag}")
    try:
        return json.loads(out), ev
    except ValueError:
        return {"controls": [], "unnamed": []}, ev


def step_icon_names():
    """K1 (#204): every control a pointer user knows by its icon has a name
    for assistive technology, read over AT-SPI: the main window with a
    video and subtitles open, a floating panel's header, the fullscreen
    video's panel and the Style manager."""
    subs = video_episode()
    fresh(subs)
    dismiss_notices()
    sh(["python3", f"{GATE}/atspi_tool.py", "press-showing", "Load associated"])
    time.sleep(4)
    scans, ev = {}, []
    scans["main"], e = controls_scan("main")
    ev += e
    float_panel("Audio", "icons")
    wait_for(lambda s: "Audio" in frames(s))
    scans["floating-audio"], e = controls_scan("floating-audio")
    ev += e
    focus_main_compositor()
    panel_menu("Audio", "Dock")
    focus_main_compositor()
    if fs_focus_video():
        B.combo("f")
        time.sleep(2.5)
        scans["video-fullscreen"], e = controls_scan("video-fullscreen")
        ev += e
        B.combo("escape")
        time.sleep(1.5)
    focus_main_compositor()
    B.keys("alt+s", 0.8)
    try:
        items = (json.loads(sh(["python3", f"{GATE}/atspi_tool.py", "menu"]) or "[]") or [[]])[-1]
    except ValueError:
        items = []
    enabled = [i["name"] for i in items if i["enabled"]]
    sm = next((n for n in enabled if n.startswith("Style m")), None)
    if sm:
        B.keys(*(["down"] * (enabled.index(sm) + 1)), "return", 2.5)
        scans["style-manager"], e = controls_scan("style-manager")
        ev += e
        close_window("Style manager")
    else:
        log(f"icons: no Style manager item in the Subtitles menu {enabled}")
        B.keys("escape")
    unnamed = {k: v["unnamed"] for k, v in scans.items() if v["unnamed"]}
    counts = {k: len(v["controls"]) for k, v in scans.items()}
    buttons = sum(1 for v in scans.values() for c in v["controls"] if "button" in c["role"])
    verdict("icon-controls-named", "observed" if not unnamed and len(scans) >= 3 and buttons else "failed",
            f"controls read per surface {counts} ({buttons} buttons); unnamed: "
            f"{json.dumps({k: [(c['role'], c['path'][-120:], c['box']) for c in v] for k, v in unnamed.items()})[:3000]}",
            ev + [f"icons-{k}.json" for k in scans])


def ibus_up():
    """ibus-daemon on this session's bus with the Hangul engine starting in
    Hangul mode; its address for the app (no machine id in the container)."""
    sh(["gsettings", "set", "org.freedesktop.ibus.engine.hangul", "initial-input-mode", "'hangul'"])
    sh("ibus exit >/dev/null 2>&1; true")
    subprocess.Popen(["ibus-daemon", "--replace", "--panel", "disable", "--verbose"],
                     stdout=open(os.path.join(EVID, "ime-ibus-daemon.log"), "w"), stderr=subprocess.STDOUT,
                     start_new_session=True)
    addr = ""
    for _ in range(40):
        addr = sh(["ibus", "address"]).strip()
        if addr and "(null)" not in addr:
            break
        time.sleep(0.25)
    sh(["ibus", "engine", "xkb:us::eng"])
    return addr


def step_ime():
    """E6 (#188) with a real input method: ibus (Qt's ibus input context) and
    ibus-hangul. With the tag list open over the Line text, the Hangul
    engine composes 한 from g, k, s (its preedit under the list) and Space
    commits it into the Line; the focus never leaves the Line text."""
    if not shutil.which("ibus-daemon"):
        verdict("ime-composition", "not-observable", "no ibus in this image")
        return
    # ibus-hangul's 2-set keyboard reads the key's code through a US
    # keymap, and wtype (sway) sends codes of its own keymap: there the
    # romaja keyboard, which reads the key's symbol, composes 한 from h, a, n.
    jamo = ("h", "a", "n") if isinstance(B, Sway) else ("g", "k", "s")
    sh(["gsettings", "set", "org.freedesktop.ibus.engine.hangul", "hangul-keyboard",
        "'ro'" if isinstance(B, Sway) else "'2'"])
    addr = ibus_up()
    path = episode()
    fresh(path, env={"QT_IM_MODULE": "ibus", "IBUS_ADDRESS": addr})
    dismiss_notices()
    focus_main_compositor()
    role, _ = show_tags()
    open_tag_list()
    B.combo("b")
    time.sleep(1.2)
    shown0 = tag_list_shown()
    foc = [in_line_text()]
    ev0, st0 = B.snap("ime-0-list-open")
    engine0 = sh(["ibus", "engine"]).strip()
    sh(["ibus", "engine", "hangul"])
    time.sleep(1.0)
    B.combo(jamo[0])
    time.sleep(0.4)
    B.combo(jamo[1])
    time.sleep(0.8)
    ev1, st1 = B.snap("ime-1-composing")
    shown1 = tag_list_shown()
    foc.append(in_line_text())
    text1 = st1["texts"].get("Line text")
    B.combo(jamo[2])
    time.sleep(0.8)
    ev2, st2 = B.snap("ime-2-composing-more")
    shown2 = tag_list_shown()
    foc.append(in_line_text())
    B.combo("space")
    time.sleep(1.2)
    st3 = atspi()
    foc.append(in_line_text())
    ev3, _ = B.snap("ime-3-committed")
    engine1 = sh(["ibus", "engine"]).strip()
    sh(["ibus", "engine", "xkb:us::eng"])
    text3 = st3["texts"].get("Line text")
    kept = all(foc)
    log_lines = [l for l in open(os.path.join(os.environ["XDG_RUNTIME_DIR"], "app.log"), errors="replace")
                 if "ibus" in l.lower() or "inputcontext" in l.lower()]
    ok = shown0 and text3 is not None and "한" in text3 and kept and text3.startswith("{\\b")
    verdict("ime-composition", "observed" if ok else "failed",
            f"ibus at {addr!r}, engine {engine0} then {engine1}; '{{\\b' typed: Tag list shown {shown0}; "
            f"{'romaja' if jamo[0] == 'h' else '2-set'} keyboard, {jamo[0]}, {jamo[1]} under the list: Line text "
            f"{text1!r} (the preedit is not committed text), list shown {shown1}; {jamo[2]}: list shown "
            f"{shown2}; Space commits: Line text {text3!r} (expected '{{\\b한 first'); focus in the Line text "
            f"throughout {kept}; app log {log_lines[-3:]}", ev0 + ev1 + ev2 + ev3 + ["ime-ibus-daemon.log"])


def portal_up(backend):
    """xdg-desktop-portal with BACKEND for every interface (portals.conf),
    restarted on this session's bus."""
    cdir = os.path.join(os.environ["XDG_CONFIG_HOME"], "xdg-desktop-portal")
    os.makedirs(cdir, exist_ok=True)
    open(os.path.join(cdir, "portals.conf"), "w").write(f"[preferred]\ndefault={backend}\n")
    for name in ("xdg-desktop-portal", f"xdg-desktop-portal-{backend}"):
        for p in sh(["pgrep", "-x", name[:15]]).split():
            try:
                env = open(f"/proc/{p}/environ", "rb").read().decode(errors="replace")
            except OSError:
                continue
            if f"XDG_RUNTIME_DIR={os.environ['XDG_RUNTIME_DIR']}\0" in env:
                sh(["kill", p])
    time.sleep(1)
    logf = open(os.path.join(EVID, f"portal-{backend}.log"), "a")
    # xdg-desktop-portal-kde offers Screenshot (PickColor) only in a Plasma
    # session (XDG_CURRENT_DESKTOP=KDE).
    env = dict(os.environ, XDG_CURRENT_DESKTOP="KDE") if backend == "kde" else None
    import glob
    if env and not glob.glob("/usr/lib/qt6/plugins/platforms/libqwayland*.so"):
        # without qt6-wayland the backend's Qt cannot open the compositor;
        # PickColor itself is KWin's (org.kde.KWin /ColorPicker)
        env["QT_QPA_PLATFORM"] = "offscreen"
        log("portal_up: no Qt Wayland plugin in the image; the KDE backend runs on Qt's offscreen platform")
    impl = subprocess.Popen([f"/usr/lib/xdg-desktop-portal-{backend}"], stdout=logf, stderr=subprocess.STDOUT,
                            start_new_session=True, env=env)
    # The backend's bus name first: otherwise the front end activates it
    # through D-Bus, without the environment above.
    names = {"kde": "org.freedesktop.impl.portal.desktop.kde", "gtk": "org.freedesktop.impl.portal.desktop.gtk"}
    for _ in range(40):
        if names.get(backend, "") in sh(["busctl", "--user", "list", "--acquired"]) or \
                names.get(backend, "") in sh(["gdbus", "call", "--session", "--dest", "org.freedesktop.DBus",
                                               "--object-path", "/org/freedesktop/DBus", "--method",
                                               "org.freedesktop.DBus.ListNames"]):
            break
        time.sleep(0.25)
    else:
        log(f"portal_up: {names.get(backend)} never appeared on the bus (backend exit {impl.poll()})")
    front = subprocess.Popen(["/usr/lib/xdg-desktop-portal", "--replace", "--verbose"], stdout=logf,
                             stderr=subprocess.STDOUT, start_new_session=True)
    time.sleep(3)
    return impl, front


def scheme_luma(png, r):
    """Mean brightness (0-255) of the main window's status bar row and its
    menu bar, where the theme's window colour shows."""
    iw, ih, raw = raw_pixels(os.path.join(EVID, png))
    x, y, w, h = r[:4]
    vals = []
    for py in (y + h - 6, y + h - 12):
        for px in range(x + 200, x + w - 200, 7):
            if 0 <= px < iw and 0 <= py < ih:
                i = (py * iw + px) * 3
                vals.append(sum(raw[i:i + 3]) / 3)
    return round(sum(vals) / len(vals)) if vals else None


def step_follow_system():
    """K2 (#206): "Follow system theme" (on by default) through the XDG
    settings portal (xdg-desktop-portal-gtk reading
    org.gnome.desktop.interface color-scheme): prefer-light, then
    prefer-dark while the app runs, then prefer-light again; the window
    colour follows each change without a restart."""
    portal_up("gtk")
    sh(["gsettings", "set", "org.gnome.desktop.interface", "color-scheme", "'prefer-light'"])
    read = sh(["gdbus", "call", "--session", "--dest", "org.freedesktop.portal.Desktop", "--object-path",
               "/org/freedesktop/portal/desktop", "--method", "org.freedesktop.portal.Settings.ReadOne",
               "org.freedesktop.appearance", "color-scheme"]).strip()
    results = {}
    variants = {"default": {}, "gnome": {"XDG_CURRENT_DESKTOP": "GNOME", "QT_QPA_PLATFORMTHEME": "gnome"},
                "xdgdesktopportal": {"QT_QPA_PLATFORMTHEME": "xdgdesktopportal"}}
    for tag, env in variants.items():
        path = episode()
        fresh(path, env=env)
        dismiss_notices()
        time.sleep(2)
        main = origin("HikariSub")
        seq = []
        ev = []
        for i, scheme in enumerate(("prefer-light", "prefer-dark", "prefer-light")):
            sh(["gsettings", "set", "org.gnome.desktop.interface", "color-scheme", f"'{scheme}'"])
            time.sleep(3)
            e, _ = B.snap(f"scheme-{tag}-{i}-{scheme}")
            ev += e
            seq.append((scheme, scheme_luma(e[0], main) if main else None))
        app("kill")
        results[tag] = (seq, ev)
    def follows(seq):
        l = [v for _, v in seq]
        return None not in l and l[0] > 128 and l[1] < 100 and l[2] > 128
    ok = {k: follows(v[0]) for k, v in results.items()}
    verdict("follow-system-portal", "observed" if any(ok.values()) else "failed",
            f"portal Settings.ReadOne(org.freedesktop.appearance, color-scheme) at prefer-light: {read}; window "
            f"brightness per live change (this session's own platform theme): {results['default'][0]} follows "
            f"{ok['default']}; GNOME's (XDG_CURRENT_DESKTOP=GNOME, Qt's built-in gnome theme, as the packages ship "
            f"without qgtk3): {results['gnome'][0]} follows {ok['gnome']}; QT_QPA_PLATFORMTHEME=xdgdesktopportal: "
            f"{results['xdgdesktopportal'][0]} follows {ok['xdgdesktopportal']}",
            [e for v in results.values() for e in v[1]] + ["portal-gtk.log"])


PATCH_RGB = (0x24, 0x68, 0xac)


def colour_patch():
    """A window of one colour (GTK 3), placed by the window system."""
    code = ("import gi\ngi.require_version('Gtk','3.0')\nfrom gi.repository import Gtk, Gdk\n"
            "w=Gtk.Window(title='patch')\nw.set_default_size(160,120)\n"
            "w.override_background_color(Gtk.StateFlags.NORMAL, Gdk.RGBA(%f,%f,%f,1))\n"
            "w.connect('destroy', Gtk.main_quit)\nw.show_all()\nGtk.main()\n"
            % tuple(c / 255 for c in PATCH_RGB))
    path = os.path.join(HOME, "patch.py")
    open(path, "w").write(code)
    env = dict(os.environ, GDK_BACKEND="x11" if B.name == "x11" else "wayland", NO_AT_BRIDGE="1")
    return subprocess.Popen(["python3", path], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def find_patch(png):
    """The patch's centre in a screenshot (its colour's bounding box)."""
    out = sh(["magick", os.path.join(EVID, png), "-alpha", "off", "-fuzz", "2%", "-fill", "black", "+opaque",
              "#%02x%02x%02x" % PATCH_RGB, "-fill", "white", "-opaque", "#%02x%02x%02x" % PATCH_RGB,
              "-format", "%@", "info:"]).strip()
    m = re.match(r"(\d+)x(\d+)\+(\d+)\+(\d+)", out)
    if not m or int(m.group(1)) < 20:
        return None
    w, h, x, y = map(int, m.groups())
    return x + w // 2, y + h // 2


def patch_centre():
    """The patch window's centre on the screen, from the window system."""
    if B.name == "x11":
        wid = sh("xdotool search --onlyvisible --name '^patch$'").split()
        if wid:
            g = sh(["xdotool", "getwindowgeometry", "--shell", wid[0]])
            v = dict(l.split("=", 1) for l in g.split() if "=" in l)
            return int(v["X"]) + int(v["WIDTH"]) // 2, int(v["Y"]) + int(v["HEIGHT"]) // 2
    elif B.name == "kwin":
        for line in B.windows().splitlines():
            m = re.match(r'\S+ "patch" (-?[\d.]+),(-?[\d.]+) (\d+)x(\d+)', line)
            if m:
                x, y, w, h = float(m.group(1)), float(m.group(2)), int(m.group(3)), int(m.group(4))
                return int(x + w / 2), int(y + h / 2)
    return None


def colour_button():
    """The Line editor's fourth colour button (changeColour3), right-clicked
    for the simple picker: by its accessible name."""
    for name in ("Border color",):
        hits = where("button", name, "HikariSub")
        if hits:
            return name, hits[0]
    return None, None


def step_screen_dropper():
    """Y7 (#190): the colour picker's screen dropper. X11 (a desktop with a
    window manager, real pointer input through XTEST): the simple picker
    tracks the pointer and a right press over another window picks that
    window's pixel. Wayland: its "Pick a colour from the screen" button asks
    the screenshot portal's PickColor (KWin: xdg-desktop-portal-kde, whose
    PickColor hands the pick to KWin); a click on the patch answers it."""
    portal = None
    if B.name == "kwin":
        portal = portal_up("kde")
    elif WAYLAND:
        verdict("dropper-portal", "not-observable",
                f"{B.name}: no portal backend with PickColor runs here (GNOME's needs gnome-shell, wlroots' has none)")
        return
    patch = colour_patch()
    time.sleep(2)
    # Away from the main window and from where the picker opens: the left
    # of X11's wide screen, KWin's second output.
    if B.name == "x11":
        sh("xdotool search --sync --onlyvisible --name '^patch$' windowmove 200 640")
    elif B.name == "kwin":
        B.script('for (const w of workspace.windowList()) if (w.caption == "patch") '
                 'w.frameGeometry = {x: 1800, y: 200, width: 160, height: 120};')
    time.sleep(1)
    path = episode()
    fresh(path)
    dismiss_notices()
    focus_main_compositor()
    show_tags()
    atspi_do("text", "Line text", "SetFocus")
    time.sleep(0.5)
    B.keys("home")
    main = origin("HikariSub")
    ev, _ = B.snap("dropper-0-patch")
    centre = patch_centre()
    name, btn = colour_button()
    if not main or not btn or not centre:
        verdict("dropper-portal" if WAYLAND else "dropper-x11", "failed",
                f"main window {main}, colour button {name} {btn}, patch on screen at {centre}", ev)
        patch.terminate()
        return
    bx, by = main[0] + btn["x"] + btn["w"] // 2, main[1] + btn["y"] + btn["h"] // 2
    if B.name == "x11":
        sh(["xdotool", "mousemove", str(bx), str(by), "click", "3"])
    else:
        rclick(bx, by, wait=900)
    time.sleep(1.5)
    ev1, st = B.snap("dropper-1-simple-picker")
    picker = [f for f in st["frames"] if f["name"] == "Color picker"]
    before = line_text()
    route = None
    if B.name == "x11":
        route = "grab"
        sh(["xdotool", "mousemove", str(centre[0]), str(centre[1])])
        time.sleep(0.8)
        ev2, _ = B.snap("dropper-2-tracking")
        sh(["xdotool", "mousedown", "3", "sleep", "0.1", "mouseup", "3"])
    else:
        route = "portal"
        hits = where("button", "Pick a colour from the screen")
        fr = frames().get(hits[0]["frame"]) if hits else None
        r = origin(hits[0]["frame"]) if hits else None
        if hits and r:
            click(r[0] + hits[0]["x"] + hits[0]["w"] // 2, r[1] + hits[0]["y"] + hits[0]["h"] // 2, wait=2000)
        elif hits:
            atspi_do("button", "Pick a colour from the screen")
            time.sleep(2)
        ev2, _ = B.snap("dropper-2-portal-picking", extra=f"# portal button {hits} frame {fr}")
        click(centre[0], centre[1], wait=2000)
    time.sleep(1.5)
    after = line_text()
    ev3, _ = B.snap("dropper-3-picked")
    patch.terminate()
    want = "&H%02X%02X%02X&" % (PATCH_RGB[2], PATCH_RGB[1], PATCH_RGB[0])
    item = "dropper-x11" if route == "grab" else "dropper-portal"
    verdict(item, "observed" if after and want in after else "failed",
            f"route {route}; right click on '{name}' at {bx},{by}: simple picker windows {picker}; patch "
            f"#{PATCH_RGB[0]:02x}{PATCH_RGB[1]:02x}{PATCH_RGB[2]:02x} at {centre}; Line text {before!r} -> {after!r} "
            f"(expected {want} in a \\3c tag)",
            ev + ev1 + ev2 + ev3 + ([f"portal-kde.log"] if portal else []))


def step_edge_tiling():
    """D3 (#207): the floating panel's free header part hands the move to
    the window system (startSystemMove); dragged to the screen's left edge,
    the window system's edge tiling, where it has any, applies to the
    borderless tool window. Recorded as observed either way, with what
    happened (the card asks for a note, not a behaviour)."""
    fresh()
    float_panel("Audio", "edgetile")
    wait_for(lambda s: "Audio" in frames(s))
    r = origin("Audio")
    main = origin("HikariSub")
    if not r:
        verdict("edge-tiling-note", "not-observable", f"the floating Audio window cannot be located on {B.name}")
        return
    ev0, st0 = B.snap("edge-0-floating")
    fr0 = frames(st0).get("Audio", {})
    px, py = header_point(r, part="free")
    # The left edge of the first output (x 0 in every session's layout).
    drag(px, py, 1, 400, steps=40)
    time.sleep(1.5)
    st1 = atspi()
    r1 = origin("Audio")
    ev1, st1 = B.snap("edge-1-at-left-edge")
    fr1 = frames(st1).get("Audio", {})
    floating = "Audio" in frames(st1)
    tiled = bool(fr1) and fr1.get("h", 0) >= fr0.get("h", 0) + 150
    verdict("edge-tiling-note", "observed" if floating else "failed",
            f"{B.name}: free part dragged from {px},{py} to the left edge (1,400): window {r[:4]} -> "
            f"{r1 and r1[:4]}, size {fr0.get('w')}x{fr0.get('h')} -> {fr1.get('w')}x{fr1.get('h')}; still a "
            f"floating panel {floating}; the window system "
            + ("TILED it (size grew to the edge tile)" if tiled else "did not tile it (no edge tiling applied)"),
            ev0 + ev1)


# ---------------------------------------------------------------- SH1 title bar
# SH1 (#220): the main window's own title bar (MainTitleBar.qml): the
# application icon, the menus, the layout switcher, the title and the window
# buttons on a frameless window, by default on KDE Plasma and GNOME (the
# gate's KWin session names no desktop: the app is started with KDE's
# names) and the system's on sway and on X11 under openbox; "custom" in the
# profile forces it anywhere. Each item drives the window through the
# compositor's own input.
TITLE_BAR = 32
# Each theme's panel role (the title bar's surface).
THEME_PANEL = {"light": "#F9FAFB", "dark": "#20262D", "highContrastWhite": "#FFFFFF", "highContrastBlack": "#080808"}


def title_bar_start(choice="auto", theme="dark"):
    """A fresh profile with the title bar's setting and the theme, then the app."""
    app("fresh")
    conf = os.path.join(os.environ["XDG_CONFIG_HOME"], "HikariSub", "HikariSub")
    os.makedirs(conf, exist_ok=True)
    with open(os.path.join(conf, "hikari.ini"), "w") as f:
        f.write(f"[registry]\nschema=1\n\n[profile]\nappearance.theme={theme}\nappearance.followSystem=false\n"
                f"appearance.titleBar={choice}\n")
    # The desktop each compositor stands for: Plasma's KWin, GNOME's mutter
    # (gnome-shell would set these; the container runs mutter alone).
    env = {"kwin": {"XDG_CURRENT_DESKTOP": "KDE", "XDG_SESSION_DESKTOP": "KDE"},
           "mutter": {"XDG_CURRENT_DESKTOP": "GNOME", "XDG_SESSION_DESKTOP": "gnome"}}.get(B.name)
    app("start", env=env)
    time.sleep(2)
    dismiss_notices()
    if B.name == "sway":
        # sway tiles the main window: floating, it can be moved, maximised and resized.
        B.msg('[title="HikariSub$"] floating enable, resize set 1280 800, move position center')
        time.sleep(1.0)


def title_bar_controls():
    """The title bar's controls that show, by name, in window coordinates."""
    out = {}
    for role, name in (("button", "Window menu"), ("button", "Minimise"), ("button", "Maximise"),
                       ("button", "Restore"), ("button", "Close"), ("button", "More menus"),
                       ("menu bar", "Application menu")):
        hits = where(role, name, "HikariSub")
        if hits:
            out[name] = hits[0]
    dump = sh(["python3", f"{GATE}/atspi_tool.py", "dump"])
    out["switcher"] = next((re.search(r"'(Layout: [^']*)'", l).group(1) for l in dump.splitlines()
                            if "'Layout: " in l and "showing" in l), None)
    return out


def main_size():
    fr = frames().get("HikariSub")
    return (fr["w"], fr["h"]) if fr else None


def main_at(x, y):
    """Window coordinates to the screen's (where the compositor says where the window is)."""
    r = origin("HikariSub")
    return (r[0] + x, r[1] + y) if r else None


def free_part(ctl):
    """A point of the title bar's free part: just left of Minimise."""
    m = ctl.get("Minimise")
    return (m["x"] - 16, TITLE_BAR // 2) if m else (900, TITLE_BAR // 2)


def system_frame(title="HikariSub"):
    """Whether the window system frames the window TITLE (the main window by
    default; a title bar or border of its own), and what says so."""
    if B.name == "kwin":
        r = B.client_rect(title)
        line = next((l for l in B.windows().splitlines() if f'"{title}"' in l or (title == "HikariSub" and "HikariSub" in l)), "")
        m = re.match(r'\S+ "(.*)" (-?[\d.]+),(-?[\d.]+) (\d+)x(\d+)', line)
        fr = frames().get(title)
        if m and fr:
            return int(m.group(5)) - fr["h"] > 0, f"KWin frame {m.group(4)}x{m.group(5)}, client {fr['w']}x{fr['h']}"
        return None, f"no KWin window ({r})"
    if B.name in ("sway", "sway-activate"):
        tree = B.windows()
        line = next((l for l in tree.splitlines() if title in l), "")
        border = json.loads(B.msg("-t", "get_tree") or "{}")

        def find(n):
            if (n.get("name") or "").endswith(title):
                return n
            for c in n.get("nodes", []) + n.get("floating_nodes", []):
                hit = find(c)
                if hit:
                    return hit
            return None
        node = find(border) or {}
        return node.get("border") not in ("csd", "none"), f"sway border {node.get('border')!r}: {line}"
    if B.name == "x11":
        w = sh(f"xdotool search --onlyvisible --name '{'HikariSub$' if title == 'HikariSub' else '^' + title + '$'}'").split()
        ext = sh(["xprop", "-id", w[0], "_NET_FRAME_EXTENTS"]).strip() if w else ""
        nums = [int(n) for n in re.findall(r"\d+", ext.split("=")[-1])] if "=" in ext else []
        return any(nums), f"_NET_FRAME_EXTENTS {ext or 'unset'}"
    return None, "mutter publishes no frame geometry (see the screenshot)"


def panel_colour_at(png, x, y):
    out = sh(["magick", os.path.join(EVID, png), "-format", f"%[hex:p{{{x},{y}}}]", "info:"]).strip()
    return "#" + out[:6].upper() if out else ""


def step_title_bar():
    """SH1 (#220) on the session's window system: the default by desktop,
    then HikariSub's own title bar forced: no frame of the window system's,
    moved by its free part, maximised by a double click and by Maximise,
    restored, resized from the edge band, the window menu on a right click,
    Alt and F10 to the menus, every control named, Minimise and Close; the
    system's title bar forced; the four themes; mixed DPI."""
    # The desktop's default.
    title_bar_start("auto")
    ctl = title_bar_controls()
    ev0, _ = B.snap("titlebar-0-default")
    custom_default = B.name in ("kwin", "mutter")
    custom_now = "Close" in ctl and "Window menu" in ctl
    framed, why = system_frame()
    verdict("titlebar-default", "observed" if custom_now == custom_default and "Application menu" in ctl
            and ctl.get("switcher") else "failed",
            f"{B.name}: expected {'HikariSub' if custom_default else 'the system'}'s title bar by default; window "
            f"buttons shown {custom_now}; menus {('Application menu' in ctl)}; switcher {ctl.get('switcher')!r}; "
            f"window system frame {framed} ({why})", ev0)

    # HikariSub's own, forced.
    title_bar_start("custom")
    ctl = title_bar_controls()
    ev1, _ = B.snap("titlebar-1-custom")
    framed, why = system_frame()
    names = {n: bool(ctl.get(n)) for n in ("Window menu", "Application menu", "Minimise", "Maximise", "Close")}
    verdict("titlebar-names", "observed" if all(names.values()) and ctl.get("switcher") else "failed",
            f"AT-SPI: {names}, switcher {ctl.get('switcher')!r}", ev1)
    verdict("titlebar-frameless", "observed" if framed is False else ("not-observable" if framed is None else "failed"),
            f"no frame of the window system's round HikariSub's title bar: {why}", ev1)

    # Moving by the free part (Wayland: the compositor's move; X11: the window manager's).
    size0 = main_size()
    fx, fy = free_part(ctl)
    r0 = origin("HikariSub")
    if r0 and B.name != "mutter":
        sx, sy = r0[0] + fx, r0[1] + fy
        drag(sx, sy, sx - 150, sy + 60, steps=drag_steps(150, 60))
        time.sleep(1.0)
        r1 = origin("HikariSub")
        ev2, _ = B.snap("titlebar-2-moved")
        moved = r1 is not None and abs((r1[0] - r0[0]) + 150) <= 12 and abs((r1[1] - r0[1]) - 60) <= 12
        verdict("titlebar-move", "observed" if moved else "failed",
                f"free part ({fx},{fy}) dragged by (-150,+60): window {r0[:4]} -> {r1 and r1[:4]}", ev2)
    else:
        verdict("titlebar-move", "not-observable", "mutter publishes no window position")

    # A double click on the free part maximises, again restores; then Maximise and Restore.
    r = origin("HikariSub")

    def at(x, y):
        return (r[0] + x, r[1] + y) if r else (x, y)
    click(*at(fx, fy), double=True, wait=1500)
    ctl_max = title_bar_controls()
    size_max = main_size()
    ev3, _ = B.snap("titlebar-3-maximised")
    # Maximised the window starts at the output's corner: the free part is at its own place there.
    rm = origin("HikariSub")
    mx, my = ((rm[0] if rm else 0) + (ctl_max.get("Minimise", {}).get("x", fx + 16) - 16), (rm[1] if rm else 0) + fy)
    if B.name == "mutter":
        mx, my = ctl_max.get("Minimise", {}).get("x", 1600 - 3 * 46) - 16, fy
    click(mx, my, double=True, wait=1500)
    ctl_back = title_bar_controls()
    size_back = main_size()
    # sway has no maximised state: it answers xdg_toplevel.set_maximized with none.
    no_max = B.name in ("sway", "sway-activate")
    verdict("titlebar-double-click", "not-observable" if no_max else "observed" if "Restore" in ctl_max and size_max
            and size0 and size_max[0] > size0[0] and "Maximise" in ctl_back and size_back == size0 else "failed",
            f"double click on the free part: Restore shown {'Restore' in ctl_max}, size {size0} -> {size_max}; "
            f"again: Maximise shown {'Maximise' in ctl_back}, size {size_back}"
            + ("; sway has no maximised state (set_maximized is answered with none)" if no_max else ""), ev3)
    r = origin("HikariSub")
    mb = ctl_back.get("Maximise") or {}
    click(*(at(mb.get("x", 0) + 23, 16) if B.name != "mutter" else (160 + mb.get("x", 0) + 23, 100 + 16)), wait=1500)
    ctl_max2 = title_bar_controls()
    size_max2 = main_size()
    ev4, _ = B.snap("titlebar-4-maximise-button")
    rb = ctl_max2.get("Restore") or {}
    rm = origin("HikariSub")
    click((rm[0] if rm and B.name != "mutter" else 0) + rb.get("x", 0) + 23,
          (rm[1] if rm and B.name != "mutter" else 0) + 16, wait=1500)
    size_back2 = main_size()
    verdict("titlebar-maximise-button", "not-observable" if no_max else "observed" if "Restore" in ctl_max2
            and size_max2 and size0 and size_max2[0] > size0[0] and size_back2 == size0 else "failed",
            f"Maximise: Restore shown {'Restore' in ctl_max2}, size {size0} -> {size_max2}; Restore: {size_back2}"
            + ("; sway has no maximised state" if no_max else ""), ev4)

    # Resizing from the band inside the bottom-right corner, from the window's
    # first place (the move left it against the output's bottom edge).
    title_bar_start("custom")
    size0 = main_size()
    r = origin("HikariSub")
    if B.name == "mutter":
        r = (160, 100, size0[0], size0[1], "Meta-0") if size0 else None
    if r and size0:
        cx, cy = r[0] + size0[0] - 2, r[1] + size0[1] - 2
        drag(cx, cy, cx + 120, cy + 80, steps=drag_steps(120, 80))
        time.sleep(1.0)
        size1 = main_size()
        ev5, _ = B.snap("titlebar-5-resized")
        grown = size1 is not None and size1[0] >= size0[0] + 100 and size1[1] >= size0[1] + 60
        verdict("titlebar-resize", "observed" if grown else "failed",
                f"bottom-right band dragged by (+120,+80): size {size0} -> {size1}", ev5)
    else:
        verdict("titlebar-resize", "failed", f"the main window cannot be located ({r}, {size0})")

    # The window menu on a right click on the free part.
    r = origin("HikariSub")
    if B.name == "mutter":
        r = (160, 100, 0, 0, "Meta-0")
    B.snap("titlebar-6-before-menu")
    rx, ry = (r[0] + fx, r[1] + fy) if r else (fx, fy)
    rclick(rx, ry, wait=1500)
    ev6, _ = B.snap("titlebar-6-window-menu", extra=f"# right click at {rx},{ry}\n" + B.windows())
    before = os.path.join(EVID, "titlebar-6-before-menu.png")
    after = os.path.join(EVID, "titlebar-6-window-menu.png")
    box = f"300x300+{max(rx - 20, 0)}+{max(ry, 0)}"
    try:
        changed = int(float(subprocess.run(["magick", "compare", "-metric", "AE", "-fuzz", "5%", f"{before}[{box}]",
                                            f"{after}[{box}]", "null:"], capture_output=True, text=True)
                            .stderr.split()[0]))
    except (ValueError, IndexError):
        changed = 0
    B.keys("escape", 0.8)
    # The window menu on X11 is the window manager's, asked for with
    # _GTK_SHOW_WINDOW_MENU, which openbox does not take (not in _NET_SUPPORTED).
    gtk_menu = B.name != "x11" or "_GTK_SHOW_WINDOW_MENU" in sh("xprop -root _NET_SUPPORTED")
    if B.name == "x11" and not gtk_menu:
        verdict("titlebar-window-menu", "not-observable",
                f"openbox does not take _GTK_SHOW_WINDOW_MENU (not in _NET_SUPPORTED), so it has no window menu to "
                f"show; the request is made ({changed} pixels changed)", ev6)
    elif B.name in ("kwin", "x11"):
        verdict("titlebar-window-menu", "observed" if changed > 2000 else "failed",
                f"right click on the free part at {rx},{ry}: {changed} pixels changed under it (the window "
                f"system's window menu; see the screenshot)", ev6)
    else:
        verdict("titlebar-window-menu", "not-observable",
                f"{B.name} has no window menu of its own (sway none; mutter's is gnome-shell's): the request is "
                f"made ({changed} pixels changed)", ev6)

    # Alt alone and F10 put the keyboard on the menus.
    focus_main_compositor()
    park_pointer()
    B.keys("alt", 0.6, "down", 1.0)
    file_items = [i["name"] for m in json.loads(sh(["python3", f"{GATE}/atspi_tool.py", "menu"]) or "[]") for i in m]
    ev7, _ = B.snap("titlebar-7-alt-down")
    B.keys("escape", 0.5, "escape", 0.5)
    B.combo("f10")
    time.sleep(0.6)
    B.keys("right", 0.5, "down", 1.0)
    edit_items = [i["name"] for m in json.loads(sh(["python3", f"{GATE}/atspi_tool.py", "menu"]) or "[]") for i in m]
    ev8, _ = B.snap("titlebar-8-f10-right-down")
    B.keys("escape", 0.5, "escape", 0.5)
    file_ok = any(n.startswith("Open") for n in file_items)
    edit_ok = any(n.startswith("Undo") for n in edit_items)
    verdict("titlebar-keyboard", "observed" if file_ok and edit_ok else "failed",
            f"Alt, Down: the File menu ({file_items[:4]}); F10, Right, Down: the Edit menu ({edit_items[:4]})",
            ev7 + ev8)

    # Close, then (a new start) Minimise: mutter alone has nothing that
    # brings a minimised window back.
    ctl = title_bar_controls()
    r = origin("HikariSub")
    if B.name == "mutter":
        r = (160, 100, 0, 0, "Meta-0")
    cl = ctl.get("Close") or {}
    if cl and r:
        click(r[0] + cl["x"] + 23, r[1] + 16, wait=2500)
        alive = app("alive").strip()
        verdict("titlebar-close", "observed" if alive == "no" else "failed", f"Close: the app still running {alive}")
    else:
        verdict("titlebar-close", "failed", f"Close not found ({sorted(ctl)})")
    if B.name in ("sway", "sway-activate"):
        verdict("titlebar-minimise", "not-observable", "sway has no minimised state (xdg_toplevel.set_minimized is ignored)")
    else:
        title_bar_start("custom")
        ctl = title_bar_controls()
        r = origin("HikariSub") if B.name != "mutter" else (160, 100, 0, 0, "Meta-0")
        mn = ctl.get("Minimise") or {}
        fx, fy = free_part(ctl)
        click(r[0] + mn.get("x", 0) + 23, r[1] + 16, wait=1500)
        ev9, _ = B.snap("titlebar-9-minimised")
        # AT-SPI keeps a minimised window's frame "showing": the window system says.
        if B.name == "kwin":
            line = next((l for l in B.windows().splitlines() if "HikariSub" in l), "")
            gone, why = "minimized=true" in line, f"KWin: {line}"
        elif B.name == "x11":
            state = sh("for w in $(xdotool search --name 'HikariSub$'); do xprop -id $w _NET_WM_STATE; done")
            gone, why = "_NET_WM_STATE_HIDDEN" in state, f"_NET_WM_STATE: {state.strip()}"
        else:
            got = panel_colour_at("titlebar-9-minimised.png", r[0] + fx, r[1] + 4)
            gone, why = got != THEME_PANEL["dark"], f"the title bar's free part on screen is {got} (the bar's {THEME_PANEL['dark']})"
        verdict("titlebar-minimise", "observed" if gone else "failed", f"Minimise: minimised {gone}; {why}", ev9)

    # The system's title bar, forced: the menus and the switcher in the first row, no window buttons.
    title_bar_start("system")
    ctl = title_bar_controls()
    framed, why = system_frame()
    ev10, _ = B.snap("titlebar-10-system")
    verdict("titlebar-system", "observed" if "Application menu" in ctl and ctl.get("switcher")
            and not any(n in ctl for n in ("Close", "Minimise", "Window menu")) and framed is not False else "failed",
            f"menus {'Application menu' in ctl}, switcher {ctl.get('switcher')!r}, window buttons "
            f"{[n for n in ('Close', 'Minimise', 'Window menu') if n in ctl]}; window system frame {framed} ({why})",
            ev10)

    # The four themes.
    shots, wrong = [], []
    for theme, panel in THEME_PANEL.items():
        title_bar_start("custom", theme)
        ev, _ = B.snap(f"titlebar-11-theme-{theme}")
        shots += ev
        r = origin("HikariSub") if B.name != "mutter" else (160, 100, 0, 0, "Meta-0")
        ctl = title_bar_controls()
        fx, fy = free_part(ctl)
        got = panel_colour_at(f"titlebar-11-theme-{theme}.png", r[0] + fx, r[1] + 4) if r else ""
        if got != panel:
            wrong.append(f"{theme}: {got} at the free part, expected {panel}")
    verdict("titlebar-themes", "observed" if not wrong else "failed",
            "the title bar in each theme's panel colour" + (f": {wrong}" if wrong else ""), shots)

    # Mixed DPI: the window on a scale-2 output.
    title_bar_start("custom")
    if B.name in ("sway", "sway-activate"):
        B.msg('[title="HikariSub$"] move to output HEADLESS-2, move position center')
        time.sleep(1.5)
        path = os.path.join(EVID, "titlebar-12-scale2.png")
        sh(["grim", "-o", "HEADLESS-2", path])
        r = B.client_rect("HikariSub")
        ctl = title_bar_controls()
        on2 = r is not None and r[4] == "HEADLESS-2"
        verdict("titlebar-mixed-dpi", "observed" if on2 and "Close" in ctl else "failed",
                f"main window on HEADLESS-2 (scale 2; HEADLESS-1 at 1): {r}; controls {sorted(ctl)} "
                f"(titlebar-12-scale2.png: native 2x pixels)", ["titlebar-12-scale2.png"])
        B.msg('[title="HikariSub$"] move to output HEADLESS-1')
    elif B.name == "kwin":
        sh("kscreen-doctor output.Virtual-1.scale.2 output.Virtual-0.scale.1")
        time.sleep(1)
        B.script('const o = workspace.screens.find(s => s.name === "Virtual-1"); '
                 'for (const w of workspace.windowList()) if (/HikariSub$/.test(w.caption)) workspace.sendClientToScreen(w, o);')
        time.sleep(1.5)
        ev, _ = B.snap("titlebar-12-scale2", extra="# kscreen-doctor\n" + sh("kscreen-doctor -o"))
        on2 = "output=Virtual-1" in "".join(l for l in B.windows().splitlines() if "HikariSub" in l)
        ctl = title_bar_controls()
        verdict("titlebar-mixed-dpi", "observed" if on2 and "Close" in ctl else "failed",
                f"main window on Virtual-1 (scale 2; Virtual-0 at 1): {on2}; controls {sorted(ctl)}", ev)
        sh("kscreen-doctor output.Virtual-1.scale.1")
    elif B.name == "mutter":
        sh("gdctl set --logical-monitor --primary --monitor Meta-0 --scale 1 "
           "--logical-monitor --monitor Meta-1 --scale 2 --right-of Meta-0")
        time.sleep(1.5)
        focus_main_compositor()
        B.keys("super+shift+right", 1.5)
        ev, _ = B.snap("titlebar-12-scale2", extra="# gdctl show\n" + sh("gdctl show"))
        ctl = title_bar_controls()
        verdict("titlebar-mixed-dpi", "observed" if "Close" in ctl else "failed",
                f"main window moved by Super+Shift+Right to Meta-1 (scale 2; Meta-0 at 1): controls {sorted(ctl)} "
                f"(the screenshot holds Meta-1's physical pixels right of Meta-0's 1600)", ev)
        sh("gdctl set --logical-monitor --primary --monitor Meta-0 --scale 1")
    else:
        verdict("titlebar-mixed-dpi", "not-observable",
                "X11 has one Xft DPI for the whole screen: Qt's xcb backend has no per-monitor scale to mix")
    app("kill")


def app_pid():
    with open(os.path.join(os.environ["XDG_RUNTIME_DIR"], "app.pid")) as f:
        return int(f.read().strip())


def step_gtk_bridge():
    """A11Y1 (#298): HikariSub on the session's desktop with GTK's ATK bridge
    left on (env.sh's NO_AT_BRIDGE=0). On GNOME Qt loads its GTK3 platform
    theme, which starts GTK, and GTK's bridge registers the process a second
    time, ahead of Qt's and without HikariSub's windows; it holds GTK's own
    dialogs (the theme's file chooser). The bridge stays: turning it off
    leaves that chooser silent to screen readers. What must hold: the
    lookup by name finds Qt's application, with the main window and every
    core panel, and Orca reads both the theme's file dialog and HikariSub."""
    env = {"kwin": {"XDG_CURRENT_DESKTOP": "KDE", "XDG_SESSION_DESKTOP": "KDE"},
           "mutter": {"XDG_CURRENT_DESKTOP": "GNOME", "XDG_SESSION_DESKTOP": "gnome"}}.get(B.name)
    desktop = (env or {}).get("XDG_CURRENT_DESKTOP", "unset")
    app("fresh")
    app("start", env=env)
    time.sleep(2)
    dismiss_notices()
    pid = app_pid()
    with open(f"/proc/{pid}/maps") as f:
        maps = f.read()
    theme = next((t for t in ("libqgtk3", "libqxdgdesktopportal") if t in maps), "built in")
    gtk = "libgtk-3.so" in maps
    registered = json.loads(sh(["python3", f"{GATE}/atspi_tool.py", "apps", str(pid)]) or "[]")
    with open(os.path.join(EVID, "gtkbridge-apps.json"), "w") as f:
        json.dump({"pid": pid, "env": env, "theme": theme, "gtk": gtk, "apps": registered}, f, indent=1)
    listed = "; ".join(f"{a.get('name')!r} ({a.get('toolkit')} {a.get('toolkitVersion')}, {a.get('bus')}): "
                       f"{len(a.get('windows', []))} windows" for a in registered)
    with open(os.path.join(EVID, "gtkbridge-tree.txt"), "w") as f:
        f.write(sh(["python3", f"{GATE}/atspi_tool.py", "dump", "4"]))
    st = atspi()
    panels = sorted(core_panels(st))
    main = "HikariSub" in frames(st)
    verdict("gtkbridge-lookup", "observed" if main and set(panels) == set(CORE) else "failed",
            f"{B.name} (XDG_CURRENT_DESKTOP={desktop}): platform theme {theme}, GTK started {gtk}; the "
            f"process's applications on the bus, in order: {listed}; the lookup by name finds the main window "
            f"{main} and the core panels {panels}", ["gtkbridge-apps.json", "gtkbridge-tree.txt"])

    # Orca: the theme's file dialog (GTK's chooser under the GTK3 theme,
    # Qt Quick's otherwise), then back to HikariSub.
    orca, path = orca_start("gtkbridge")
    focus_main_compositor()
    B.combo("ctrl", "o")
    time.sleep(3)
    ev, _ = B.snap("gtkbridge-open-dialog")
    B.keys("tab", 0.8, "tab", 0.8)
    B.combo("escape")
    time.sleep(1.5)
    speech, _ = orca_stop(orca, path, "gtkbridge")
    said = [l.split("SPEECH OUTPUT: ", 1)[-1].split(" {")[0].strip("'") for l in speech]
    dialog = any(w in said for w in ("Cancel", "file chooser."))
    back = "HikariSub" in (said[said.index("Cancel") + 1:] if "Cancel" in said else [])
    verdict("gtkbridge-orca", "observed" if dialog and back else "failed",
            f"Orca with File > Open under theme {theme}: the dialog read {dialog}, HikariSub read after it "
            f"{back}; spoken: {said}", ev + ["gtkbridge-orca-speech.txt"])
    app("kill")


# DG1 (#222): the dialog frame on the session's window system.
ABOUT = "About HikariSub"


def help_item(name, title, by_click=False):
    """Help > NAME from the keyboard; the dialog TITLE's frame once it shows,
    or None. BY_CLICK: the main window takes the focus from a click on its
    empty middle (as a user leaves a dialog), else from the compositor."""
    rm = origin("HikariSub") if by_click else None
    if rm:
        click(rm[0] + rm[2] // 2, rm[1] + rm[3] - 60)
        time.sleep(0.6)
        log("active after the click:", [f["id"] for f in atspi()["frames"] if f.get("active")])
    else:
        focus_main_compositor()
    park_pointer()
    B.keys("alt+h", 0.6)
    try:
        menus = json.loads(sh(["python3", f"{GATE}/atspi_tool.py", "menu"]) or "[]")
    except ValueError:
        menus = []
    enabled = [i["name"].replace("&", "") for i in (menus[-1] if menus else []) if i["enabled"]]
    at = next((i for i, n in enumerate(enabled) if n.startswith(name)), None)
    if at is None:
        log(f"help_item: no {name!r} in {enabled}")
        B.keys("escape", 0.3)
        return None
    B.keys(*(["down"] * (at + 1)), "return", delay=0.3)
    st = wait_for(lambda s: title in frames(s), timeout=6)
    return frames(st).get(title)


def step_dialog_frame():
    """DG1 (#222) on the session's window system: About is a window of its
    own on HikariSub's frame (no frame of the window system's; Close, no
    Minimise or Maximise on a fixed-size dialog); on X11 centred on the main
    window 20 lower, moved by its title bar and opened there again (on
    Wayland the compositor places it and only the size is kept); About and
    Credits open at once; Esc closes; the window system's frame with "Use
    system title bar"; the four themes."""
    title_bar_start("custom")
    about = help_item("About", ABOUT)
    ev0, _ = B.snap("dialogs-0-about")
    if not about:
        verdict("dialog-frame-window", "failed", "About did not open as a window of its own", ev0)
        app("kill")
        return
    own = {n: bool(where("button", n, ABOUT)) for n in ("Close", "Maximise", "Minimise")}
    framed, why = system_frame(ABOUT)
    verdict("dialog-frame-window", "observed" if own["Close"] and not own["Maximise"] and not own["Minimise"]
            and framed is not True else "failed",
            f"{B.name}: About {about['w']}x{about['h']} with its own buttons {own}; window system frame {framed} ({why})",
            ev0)

    rm, ra = origin("HikariSub"), origin(ABOUT)
    if B.name == "x11" and rm and ra:
        dx = rm[0] + rm[2] // 2 - (ra[0] + ra[2] // 2)
        dy = rm[1] + rm[3] // 2 + 20 - (ra[1] + ra[3] // 2)
        verdict("dialog-frame-centred", "observed" if abs(dx) <= 10 and abs(dy) <= 10 else "failed",
                f"About {ra[:4]} on the main window {rm[:4]}: its centre off the main window's + (0, 20) by ({dx}, {dy})",
                ev0)
    else:
        verdict("dialog-frame-centred", "not-observable",
                f"{B.name}: a Wayland client cannot place its windows; the compositor put About at {ra and ra[:4]} "
                f"(main window {rm and rm[:4]})", ev0)

    close = where("button", "Close", ABOUT)
    if B.name != "mutter" and ra and close:
        sx, sy = ra[0] + close[0]["x"] - 40, ra[1] + 16
        drag(sx, sy, sx - 80, sy + 50, steps=drag_steps(80, 50))
        time.sleep(1.0)
        moved = origin(ABOUT)
        B.keys("escape", 1.0)
        gone = ABOUT not in frames()
        help_item("About", ABOUT)
        time.sleep(0.8)
        again = origin(ABOUT)
        ev1, _ = B.snap("dialogs-1-moved-reopened")
        dragged = moved is not None and abs(moved[0] - ra[0] + 80) <= 12 and abs(moved[1] - ra[1] - 50) <= 12
        kept = again is not None and moved is not None and again[:2] == moved[:2]
        verdict("dialog-frame-move", "observed" if dragged and gone else "failed",
                f"title bar dragged by (-80,+50): {ra[:4]} -> {moved and moved[:4]}; Esc closed it: {gone}", ev1)
        verdict("dialog-frame-remembered", ("observed" if kept else "failed") if B.name == "x11" else "not-observable",
                f"opened again at {again and again[:4]} (left at {moved and moved[:4]})"
                + ("" if B.name == "x11" else "; on Wayland only the size is kept, the compositor places it"), ev1)
    else:
        verdict("dialog-frame-move", "not-observable", f"{B.name}: no window position published or no Close")

    # Back to the main window with a click (mutter: no window position; its
    # focus helper clicks the main window's title bar), then Help > Credits.
    help_item("Credits", "Credits", by_click=B.name != "mutter")
    st = atspi()
    both = ABOUT in frames(st) and "Credits" in frames(st)
    ev2, _ = B.snap("dialogs-2-about-and-credits")
    verdict("dialog-frame-modeless", "observed" if both else "failed",
            f"About open, a click on the main window, Help > Credits: both open {both} "
            f"(windows {sorted(frames(st))}, active {[f['id'] for f in st['frames'] if f.get('active')]})", ev2)

    title_bar_start("system")
    help_item("About", ABOUT)
    ev3, _ = B.snap("dialogs-3-system-title-bar")
    framed, why = system_frame(ABOUT)
    drawn = [n for n in ("Close", "Maximise") if where("button", n, ABOUT)]
    verdict("dialog-frame-system", "observed" if not drawn and framed is not False else "failed",
            f"About with the system's title bar: HikariSub's drawn buttons {drawn}; window system frame {framed} ({why})",
            ev3)

    shots = []
    for theme in ("light", "dark", "highContrastWhite", "highContrastBlack"):
        title_bar_start("custom", theme)
        help_item("About", ABOUT)
        ev, _ = B.snap(f"dialogs-4-theme-{theme}")
        shots += ev
    verdict("dialog-frame-themes", "observed", "About on HikariSub's frame in the four themes (screenshots)", shots)
    app("kill")


SWEEP_STEPS = {"tagspeech": step_tag_list_speech, "statusspeech": step_status_bar_speech,
               "refspeech": step_reference_speech, "icons": step_icon_names, "ime": step_ime,
               "scheme": step_follow_system, "dropper": step_screen_dropper, "edgetile": step_edge_tiling,
               "titlebar": step_title_bar, "gtkbridge": step_gtk_bridge, "dialogframe": step_dialog_frame}


STEPS = {"default": step_default, "kbd": step_keyboard_float_dock, "f6": step_f6_floating, "move": step_move_panel,
         "pointer": step_pointer, "video": step_video, "persist": step_persistence, "fullscreen": step_fullscreen,
         "menutext": step_menu_from_text, "tests": step_test_executables, "orca": step_orca,
         "a11y": step_a11y, "floating": step_floating, "header": step_header_keys,
         "layouts": step_layouts, "tabs": step_tabs, "videofs": step_video_fullscreen,
         # last: it removes an output, after which sway's virtual pointer
         # still maps absolute positions over the old layout
         "outputs": step_outputs}

if __name__ == "__main__":
    # Every step; the sweep's before "videofs" (on sway it leaves the main
    # window on the scale-2 output, half the size, where the main window
    # overflows it) and "outputs" (it removes an output).
    everything = default_order(STEPS, SWEEP_STEPS)
    wanted = sys.argv[2:] or everything
    for name in wanted:
        log(f"--- {B.name}: {name}")
        try:
            (STEPS.get(name) or SWEEP_STEPS[name])()
        except Exception as e:  # keep going: one broken step must not hide the others
            import traceback
            traceback.print_exc()
            verdict(name, "error", f"harness error: {e!r}")
    app("kill")
