#!/usr/bin/env python3
"""D1 native gate scenarios against HikariSub in one running compositor session.

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

    def pointer(self, *cmds):
        with open(os.path.join(os.environ["XDG_RUNTIME_DIR"], "vpointer.in"), "w") as f:
            f.write("\n".join(cmds) + "\n")
        for c in cmds:
            if c.startswith("wait"):
                time.sleep(int(c.split()[1]) / 1000)
        time.sleep(0.2)

    def shot(self, path):
        sh(["grim", path])

    def windows(self):
        return sh(["python3", f"{GATE}/swaytree.py"])

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


def panel_menu(panel, item):
    """View > Panels > PANEL > ITEM (Show, Hide, Float, Dock) from the keyboard."""
    open_view()
    seq = ["down", "right", 0.3] + ["down"] * PANELS.index(panel) + ["right", 0.3]
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
    open_view()
    B.keys("down", "down", "return", 1.2, "home", *(["down"] * PANELS.index(name)), "tab", "home",
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
    for name in ["Video", "Audio", "Line editor", "Grid", "Editing:", "No document open", "Reference", "Timing",
                 "Search"]:
        if f"panel:'{name}" in path or f"panel:\"{name}" in path:
            return "Grid" if name in ("Grid", "Editing:", "No document open") else name
    return None


def dismiss_notices():
    """Opening a Document may raise the spelling notice (no dictionaries in
    the private profile); press its OK so keys reach the shell."""
    sh(["python3", f"{GATE}/atspi_tool.py", "press-showing", "OK"])
    time.sleep(0.5)


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
        open_view()
        B.keys("down", "right", 0.3, "down", "down", "right", 0.3, "down", "down", "return")
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
    verdict("f6-from-floating-panel", "observed" if any(x and x != "Line editor" for x in reached_a) else "failed",
            f"F6 x4 right after Float reached {reached_a} (active {[w['active'] for w in walk_a]})", ev1)
    verdict("f6-into-floating-panel", "observed" if "Line editor" in reached_b and float_active_b else "failed",
            f"main window focused by the compositor (focus {start_b['focus']}); F6 x5 reached {reached_b} (active {[w['active'] for w in walk_b]}); "
            f"Shift+F6 x3 {[panel_of(w['path']) for w in back]}", ev2 + ["f6-floating.json"])
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
    verdict("shortcut-from-floating-panel", ("observed" if hist else "failed") if in_float else "not-observable",
            f"focus in the floating Line editor: {in_float}; Ctrl+Shift+H opened History: {hist}", ev3)
    B.keys("escape")


def combo_texts():
    """The displayed text of each showing combo box (its text child)."""
    code = ("import sys; sys.argv=['x','dump']\n"
            f"exec(open('{GATE}/atspi_tool.py').read().split('def main')[0])\n"
            "for o in all_objects(app()):\n"
            "    if o.get_role_name()=='combo box' and o.get_state_set().contains(Atspi.StateType.SHOWING):\n"
            "        print(o.get_name() + '=' + repr(text_of(o.get_child_at_index(0))))\n")
    return sh(["python3", "-c", code]).strip().replace("\n", "; ")


def step_move_panel():
    fresh()
    open_view()
    B.keys("down", "down", "return", 1.2)
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
    ok = a and g and a["frame"] == g["frame"] == "HikariSub" and a["x"] < g["x"] and abs(a["y"] - g["y"]) < 3
    verdict("keyboard-move-panel", "observed" if ok else "failed",
            f"Move panel ({combos_set}) -> Audio {a and (a['x'], a['y'], a['w'], a['h'])}, "
            f"Grid {g and (g['x'], g['y'], g['w'], g['h'])}; focus {st['focusPath']}", ev0 + ev1)
    close_window("Move panel")


def step_pointer():
    """Float button, double-click on title bars and drag-to-dock with real compositor input."""
    fresh()
    rect = B.client_rect("HikariSub")
    if rect is None:
        verdict("pointer-float-button", "not-observable", f"{B.name}: no window geometry to aim real pointer input")
        return
    x0, y0 = rect[0], rect[1]
    st = atspi()
    audio = panel(st, "Audio")
    # The KDDW title bar sits above the panel body: float button 30 px from the right edge.
    tx, ty = x0 + audio["x"] + audio["w"] - 22, y0 + audio["y"] - 16
    B.pointer(f"move {tx} {ty}", "wait 200", "down", "wait 60", "up", "wait 1200")
    st = wait_for(lambda s: "Audio" in frames(s))
    ev, st = B.snap("ptr-1-float-button")
    verdict("pointer-float-button", "observed" if "Audio" in frames(st) else "failed",
            f"click on Audio's float button at {tx},{ty}: own window={'Audio' in frames(st)}", ev)
    # Double-click the docked Video title bar: floats; double-click its floating title bar: docks.
    video = panel(st, "Video")
    vx, vy = x0 + video["x"] + 120, y0 + video["y"] - 16
    B.pointer(f"move {vx} {vy}", "wait 300", "down", "wait 40", "up", "wait 60", "down", "wait 40", "up", "wait 1500")
    st = wait_for(lambda s: "Video" in frames(s))
    ev1, st = B.snap("ptr-2-dblclick-float")
    dfloat = "Video" in frames(st)
    redock = False
    ev2 = []
    if dfloat:
        r = B.client_rect("Video")
        if r:
            B.pointer(f"move {r[0] + 120} {r[1] + 14}", "wait 300", "down", "wait 40", "up", "wait 60", "down", "wait 40",
                      "up", "wait 1500")
            st = wait_for(lambda s: "Video" not in frames(s))
            ev2, st = B.snap("ptr-3-dblclick-redock")
            redock = "Video" not in frames(st)
    if dfloat and not ev2:
        verdict("pointer-dblclick-float-redock", "not-observable",
                f"double-click docked Video title floated it ({dfloat}); the floating window cannot be located on "
                f"{B.name} to double-click its title", ev1)
    else:
        verdict("pointer-dblclick-float-redock", "observed" if dfloat and redock else "failed",
                f"double-click docked Video title: floated={dfloat}; double-click its floating title: redocked={redock}",
                ev1 + ev2)
    # Drag the floating Audio panel by its own (KDDW) title bar onto the Grid's centre.
    r = B.client_rect("Audio")
    st = atspi()
    grid = panel(st, "Grid")
    if not r or not grid:
        verdict("pointer-drag-dock", "not-observable", "could not locate the floating Audio window or the Grid")
        return
    sx, sy = r[0] + 150, r[1] + 14
    gx, gy = x0 + grid["x"] + grid["w"] // 2, y0 + grid["y"] + grid["h"] // 2 - 15
    cmds = [f"move {sx} {sy}", "wait 250", "down", "wait 250"]
    for i in range(1, 31):
        cmds += [f"move {sx + (gx - sx) * i // 30} {sy + (gy - sy) * i // 30}", "wait 40"]
    cmds += [f"move {gx + 3} {gy + 3}", "wait 300", f"move {gx} {gy}", "wait 700"]
    B.pointer(*cmds)
    ev3, _ = B.snap("ptr-4-drag-over-grid")
    B.pointer("up", "wait 1500", f"move {gx + 5} {gy + 5}", "wait 300")
    st = atspi()
    ev4, st = B.snap("ptr-5-dropped")
    docked = "Audio" not in frames(st) and panel_frame(st, "Audio") == "HikariSub"
    verdict("pointer-drag-dock", "observed" if docked else "failed",
            f"drag floating Audio by its title bar from {sx},{sy} to the Grid centre {gx},{gy} and release: "
            f"docked={docked}, windows {list(frames(st))}", ev3 + ev4)


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
        open_view()
        B.keys("down", "right", 0.3, "right", 0.3, "down", "down", "return")
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
        verdict("fullscreen-coexistence", "not-observable",
                "mutter without gnome-shell has no fullscreen request path for another client here (no Shell.Eval, "
                "no default toggle-fullscreen binding), and HikariSub has no fullscreen command")
        return
    time.sleep(1.5)
    ev0, st = B.snap("fullscreen-0-main-fullscreen")
    focus_main()
    walk = f6_walk(5)
    ev1, st = B.snap("fullscreen-1-after-f6")
    reached = [panel_of(w["path"]) for w in walk]
    float_active = any("Line editor" in w["active"] for w in walk)
    open(os.path.join(EVID, "fullscreen-f6.json"), "w").write(json.dumps(walk, indent=1))
    verdict("fullscreen-coexistence", "observed" if "Line editor" in reached and float_active else "failed",
            f"main window fullscreen by the compositor with Line editor floating; F6 reached {reached}, "
            f"floating window active at some step: {float_active} (see screenshots: is the floating panel visible "
            f"above the fullscreen window?)", ev0 + ev1 + ["fullscreen-f6.json"])


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
                                       "floatingPanelsOffEveryScreenComeBack"]),
            ("hikari_ui_docking_qualification_tests", []),
            ("hikari_ui_workspace_layout_tests", [])]
    out = []
    ok = True
    for exe, fns in runs:
        r = subprocess.run([os.path.join(ui, exe), *fns], capture_output=True, text=True, timeout=300)
        text = r.stdout + r.stderr
        out.append(f"### {exe} {' '.join(fns)} (exit {r.returncode})\n{text}")
        ok &= r.returncode == 0
        totals = re.findall(r"Totals: .*", text)
        skips = re.findall(r"SKIP.*", text)
        log(exe, totals, skips)
    open(os.path.join(EVID, "test-executables.txt"), "w").write("\n".join(out))
    summary = "; ".join(re.findall(r"Totals: [^,]+, [^,]+, [^,]+", "\n".join(out)))
    skips = re.findall(r"SKIP\s*:.*", "\n".join(out))
    # Expected on Wayland: the compositor places windows, so the shell does not move them.
    unexpected = [k for k in skips if "the compositor places windows" not in k]
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
    open_view()
    B.keys("down", 0.6, "right", 0.6, "down", 0.6, "down", 0.6, "right", 0.6, "down", 0.6, "down", 0.6,
           "return", 2.0)
    f6_walk(3)
    time.sleep(2)
    orca.terminate()
    try:
        orca.wait(10)
    except subprocess.TimeoutExpired:
        orca.kill()
    text = open(log_path, errors="replace").read() if os.path.exists(log_path) else ""
    speech = [l.strip() for l in text.splitlines() if "SPEECH OUTPUT" in l]
    open(os.path.join(EVID, "orca-speech.txt"), "w").write("\n".join(speech) + "\n")
    spoke_panels = [p for p in ("Video", "Audio", "Line editor", "Grid", "Panels", "Float")
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
        B.keys("super+shift+right" if "super" in KEYS else "f6", 1.5)
        ev0, st = B.snap("outputs-0-on-scale2", extra="# gdctl show\n" + sh("gdctl show"))
        verdict("mixed-dpi", "not-observable",
                "Meta-1 set to scale 2 beside Meta-0 at 1 (see outputs-0-*.txt), but mutter without gnome-shell "
                "gives this harness no way to place or locate a client window on a chosen monitor", ev0)
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
        verdict("monitor-removal", "not-observable",
                f"Meta-1 removed from the layout; the floating Line editor is still a window ({present}) but mutter "
                f"without gnome-shell gives no window geometry to tell where it went", ev1 + ev2)
    else:
        verdict("monitor-removal", "observed" if back else "failed",
                f"after removing the output holding the floating Line editor: window present={present}, "
                f"geometry/output {rect}", ev1)
    verdict("monitor-removal-show-focus", "observed" if focused_in == title else "failed",
            f"then View > Panels > Line editor > Show gives the focus to {focused_in!r} "
            f"(active windows {[f['id'] for f in st['frames'] if f['active']]})", ev2)


def atspi_find(role, name):
    return [l for l in sh(["python3", f"{GATE}/atspi_tool.py", "find", role, name]).splitlines() if l.strip()]


def step_a11y():
    """What a screen reader finds of the docking controls and the Grid:
    named title-bar buttons, named tabs with their own Float and Close, the
    Grid's table inside the panel named Grid; the buttons pressed through
    AT-SPI float and dock a panel."""
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
    names = ["Float Audio", "Close Audio", "Float Video", "Close Video", "Float Line editor", "Close Line editor",
             "Float Grid", "Close Grid"]
    found = {n: bool(atspi_find("button", n)) for n in names}
    press = [l for n in ("Float Audio",) for l in atspi_find("button", n)]
    out = sh(["python3", f"{GATE}/atspi_tool.py", "do", "button", "Float Audio"])
    st = wait_for(lambda s: "Audio" in frames(s))
    floated = "Audio" in frames(st)
    ev0, st = B.snap("a11y-0-float-audio-by-atspi")
    dock_btn = atspi_find("button", "Dock Audio")
    sh(["python3", f"{GATE}/atspi_tool.py", "do", "button", "Dock Audio"])
    st = wait_for(lambda s: "Audio" not in frames(s))
    docked = "Audio" not in frames(st)
    verdict("title-bar-buttons-accessible",
            "observed" if all(found.values()) and press and "Press" in press[0] and floated and dock_btn and docked
            else "failed",
            f"named buttons {found}; {press[:1]}; pressed 'Float Audio' -> own window={floated} ({out.strip()!r}); "
            f"then 'Dock Audio' {dock_btn[:1]} -> docked={docked}", ["a11y-tree.txt"] + ev0)
    # Timing opens as a tab beside the Line editor.
    focus_main()
    panel_menu("Timing", "Show")
    time.sleep(1)
    tabs = {n: atspi_find("page tab", n) for n in ("Line editor", "Timing")}
    tab_buttons = {n: bool(atspi_find("button", n)) for n in ("Float Timing", "Close Timing", "Float Line editor",
                                                                "Close Line editor")}
    ev1, st = B.snap("a11y-1-tabs")
    selected = [n for n, hits in tabs.items() if hits and ("selected" in hits[0] or "checked" in hits[0])]
    sh(["python3", f"{GATE}/atspi_tool.py", "do", "button", "Float Timing"])
    st = wait_for(lambda s: "Timing" in frames(s))
    tab_floated = "Timing" in frames(st)
    ev2, st = B.snap("a11y-2-float-timing-tab")
    open(os.path.join(EVID, "a11y-tabs.txt"), "w").write(
        json.dumps({"tabs": tabs, "buttons": tab_buttons, "selected": selected}, indent=1))
    verdict("tabs-accessible",
            "observed" if all(tabs.values()) and all(tab_buttons.values()) and tab_floated else "failed",
            f"page tabs { {n: bool(h) for n, h in tabs.items()} }: {[h[:1] for h in tabs.values()]}; "
            f"tab buttons {tab_buttons}; pressed 'Float Timing' -> own window={tab_floated}",
            ev1 + ev2 + ["a11y-tabs.txt"])


STEPS = {"default": step_default, "kbd": step_keyboard_float_dock, "f6": step_f6_floating, "move": step_move_panel,
         "pointer": step_pointer, "video": step_video, "persist": step_persistence, "fullscreen": step_fullscreen,
         "menutext": step_menu_from_text, "tests": step_test_executables, "orca": step_orca,
         "outputs": step_outputs, "a11y": step_a11y}

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
