#!/usr/bin/env python3
"""D1 native gate on Windows: HikariSub on the Winix VM desktop.

  gate_windows.py [--dpi] [--config winix.yaml] [--vm dev] [STEP...]

The Windows half of tools/native-gate/gate.py, with the same step names and
the same evidence layout under out/native-gate-evidence/windows/: one PNG and
one TXT (top-level windows and the UI Automation view as JSON) per
observation, steps.log, and results.json with a verdict per gate item:
observed, failed or not-observable (with the reason).

Input paths:
  keys     `winix ui keys` (SendInput into the foreground window)
  pointer  the VM's QEMU USB HID tablet through QMP input-send-event
           (`virsh qemu-monitor-command`): absolute x/y, left button down/up,
           in small steps; screen pixels map to the tablet's 0..32767 from the
           guest screen size (the screenshot's dimensions)
  clicks   for focusing a window, `winix ui click x,y` on its title bar
UI Automation comes from the gate-win-uia task (uia.ps1), which walks every
top-level window of the app; `winix ui tree/find` reach only the first.
The app starts through launch.ps1 by `winix ui launch` (a task ends every
process it started). The VM definition is never changed.
"""
import argparse
import base64
import json
import re
import shutil
import struct
import subprocess
import sys
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
EVID = ROOT / "out" / "native-gate-evidence" / "windows"
PANELS = ["Video", "Audio", "Line editor", "Grid", "Reference", "Timing", "Search"]
MAIN = "HikariSub"

W = None        # Winix
TAB = None      # Tablet
LAYOUT = {}     # gate-win-prepare's GATE_LAYOUT
results = {}
LOG = None
SCREEN = {"size": None}


def log(*a):
    line = " ".join(str(x) for x in a)
    print(line, flush=True)
    LOG.write(time.strftime("%H:%M:%S ") + line + "\n")
    LOG.flush()


def verdict(item, status, detail, evidence=()):
    results[item] = {"status": status, "detail": detail, "evidence": list(evidence),
                     "at": time.strftime("%Y-%m-%dT%H:%M:%S")}
    (EVID / "results.json").write_text(json.dumps(results, indent=1, ensure_ascii=False))
    log(f"== {item}: {status} - {detail}")


# ---------------------------------------------------------------- Winix
class Winix:
    """`winix --json` (the drive_windows.py wrapper), plus task parameters
    passed through the task's environment in a derived config."""

    def __init__(self, config, vm):
        self.config = Path(config).resolve()
        self.vm = vm

    def call(self, *args, config=None, timeout=900):
        cmd = ["winix", "--json", "--vm", self.vm, "--config", str(config or self.config), *args]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=ROOT)
        lines = p.stdout.strip().splitlines()
        try:
            return json.loads(lines[-1]) if lines else None
        except json.JSONDecodeError:
            return {"error": "unparsed", "message": p.stdout[-500:] + p.stderr[-500:]}

    def ui(self, action, **args):
        return self.call("ui", action, "--args", json.dumps(args))

    def screenshot(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        return self.call("ui", "screenshot", "--output", str(path))

    def derived(self, name, env):
        import yaml
        cfg = yaml.safe_load(self.config.read_text())
        task = cfg["tasks"][name]
        task["environment"] = {**(task.get("environment") or {}), **{k: str(v) for k, v in env.items()}}
        path = EVID / ".winix" / "gate-env.yaml"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(yaml.safe_dump(cfg, sort_keys=False, allow_unicode=True))
        return path

    def task(self, name, env=None, timeout=3600):
        """Runs a winix.yaml task to its end; returns (job record, output lines)."""
        config = self.derived(name, env) if env else None
        job = None
        for _ in range(3):  # `run --json` occasionally answers without a job id
            job = self.call("run", name, "--wait", config=config, timeout=timeout)
            if job and job.get("id"):
                break
        if not job or not job.get("id"):
            raise RuntimeError(f"task {name} did not start: {job}")
        text, offset = "", 0
        while True:
            chunk = self.call("job", "logs", job["id"], "--offset", str(offset)) or {}
            text += chunk.get("text", "")
            offset = chunk.get("nextOffset", offset)
            if chunk.get("end", True):
                break
        lines = [l[6:] if l.startswith(("[out] ", "[err] ")) else l for l in text.splitlines()]
        return job, lines

    def artifact(self, job_id, basename, dest):
        """One file of a job's artifacts (the zip `job artifacts` writes) copied to DEST."""
        outdir = EVID / ".winix" / "artifacts"
        outdir.mkdir(parents=True, exist_ok=True)
        art = self.call("job", "artifacts", job_id, "--output", str(outdir)) or {}
        path = Path(art.get("path") or "")
        if path.is_file() and zipfile.is_zipfile(path):
            with zipfile.ZipFile(path) as z:
                for n in z.namelist():
                    if n.endswith("/" + basename) or n == basename:
                        dest.write_bytes(z.read(n))
                        return True
        elif path.is_dir():
            for f in path.rglob(basename):
                shutil.copy(f, dest)
                return True
        return False


def line_value(lines, prefix):
    return next((l[len(prefix):] for l in lines if l.startswith(prefix)), None)


def sections(lines):
    """"== name" sections of a task's output."""
    out, current = {}, None
    for line in lines:
        if line.startswith("== "):
            current = line[3:].strip()
            out[current] = []
        elif current:
            out[current].append(line)
    return out


# ---------------------------------------------------------------- pointer (QMP tablet)
class Tablet:
    """The VM's USB HID tablet driven through QMP input-send-event. Windows has
    one cursor for every pointing device, so the button events act at the
    tablet's position whichever QEMU pointer handler receives them."""

    def __init__(self, domain, uri):
        self.domain, self.uri = domain, uri
        self.sent = []
        # The screen area the tablet's 0..32767 spans (x, y, w, h): the whole
        # screenshot until calibrate() measures it (with two monitors Windows
        # may map it to the virtual desktop or to the primary only).
        self.rect = None

    def qmp(self, execute, arguments=None):
        payload = {"execute": execute}
        if arguments:
            payload["arguments"] = arguments
        r = subprocess.run(["virsh", "-c", self.uri, "qemu-monitor-command", self.domain, json.dumps(payload)],
                           capture_output=True, text=True, timeout=30)
        if r.returncode:
            raise RuntimeError(f"QMP {execute}: {r.stderr.strip()}")
        reply = json.loads(r.stdout)
        if "error" in reply:
            raise RuntimeError(f"QMP {execute}: {reply['error']}")
        return reply

    def units(self, x, y):
        x0, y0, w, h = self.rect or (0, 0, *(SCREEN["size"] or screen_size()))
        return (min(32767, max(0, int((x - x0 + 0.5) * 32768 / w))), min(32767, max(0, int((y - y0 + 0.5) * 32768 / h))))

    def rel(self, dx, dy):
        """Relative motion (QEMU routes it to the VM's relative pointer, the
        PS/2 mouse): the tablet cannot leave the area Windows maps it to."""
        self.send({"type": "rel", "data": {"axis": "x", "value": dx}}, {"type": "rel", "data": {"axis": "y", "value": dy}})

    def move_units(self, ux, uy):
        self.send({"type": "abs", "data": {"axis": "x", "value": ux}}, {"type": "abs", "data": {"axis": "y", "value": uy}})

    def calibrate(self):
        """Measures the area the tablet spans: two raw positions, the guest
        cursor (GetCursorPos, physical pixels) at each."""
        seen = []
        for u in (8192, 24576):
            self.move_units(u, u)
            time.sleep(0.4)
            c = uia().get("cursor") or {}
            seen.append((u, c.get("x"), c.get("y")))
        (u1, x1, y1), (u2, x2, y2) = seen
        if None in (x1, x2, y1, y2) or x1 == x2 or y1 == y2:
            return {"error": "no cursor positions", "seen": seen}
        w = (x2 - x1) * 32768 / (u2 - u1)
        h = (y2 - y1) * 32768 / (u2 - u1)
        self.rect = (round(x1 - u1 * w / 32768), round(y1 - u1 * h / 32768), round(w), round(h))
        return {"seen": seen, "rect": self.rect}

    def send(self, *events):
        self.qmp("input-send-event", {"events": list(events)})

    def move(self, x, y):
        ax, ay = self.units(x, y)
        self.send({"type": "abs", "data": {"axis": "x", "value": ax}}, {"type": "abs", "data": {"axis": "y", "value": ay}})

    def button(self, down):
        self.send({"type": "btn", "data": {"down": down, "button": "left"}})

    def pointer(self, *cmds):
        """The Linux gate's pointer script: "move X Y", "down", "up", "wait MS"."""
        for c in cmds:
            p = c.split()
            if p[0] == "move":
                self.move(int(p[1]), int(p[2]))
            elif p[0] == "rel":
                self.rel(int(p[1]), int(p[2]))
            elif p[0] in ("down", "up"):
                self.button(p[0] == "down")
            elif p[0] == "wait":
                time.sleep(int(p[1]) / 1000)
            self.sent.append(c)
        time.sleep(0.2)


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    if head[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", head[16:24])


def displays(mode=None, scale2=None, second=None):
    """The guest's monitors (display.ps1): unchanged, or extended / detached
    (SECOND: the second monitor's (w, h)), and the second monitor's scale."""
    env = {"GATE_DISPLAY": mode or "status"}
    if second:
        env["GATE_WIDTH2"], env["GATE_HEIGHT2"] = second
    if scale2:
        env["GATE_SCALE2"] = scale2
    _, lines = W.task("gate-win-display", env=env)
    raw = line_value(lines, "DISPLAY_RESULT ")
    return json.loads(raw) if raw else {"error": lines[-10:]}


def screen_size():
    """The guest screen in pixels, from a desktop screenshot's dimensions."""
    path = EVID / ".winix" / "screen.png"
    W.screenshot(path)
    size = png_size(path) if path.exists() else None
    if not size:
        raise RuntimeError("no screenshot to size the guest screen")
    SCREEN["size"] = size
    return size


# ---------------------------------------------------------------- keys
KEYMAP = {"alt": "ALT", "ctrl": "CTRL", "shift": "SHIFT", "down": "DOWN", "up": "UP", "left": "LEFT",
          "right": "RIGHT", "return": "RETURN", "escape": "ESCAPE", "tab": "TAB", "space": "SPACE", "f6": "F6",
          "end": "END", "home": "HOME", "f11": "F11", "f4": "F4"}


# Keys that a keyboard sends with the extended-key flag. `winix ui keys`
# sends them without it (keypad keys), which NVDA takes as review-cursor
# commands, so sequences and these keys go through gate-win-keys instead.
EXTENDED = {"down", "up", "left", "right", "home", "end", "delete", "win"}


def combo(*names):
    keys("+".join(names), delay=0)


def winix_keys(*seq, delay=0.35):
    for item in seq:
        if isinstance(item, (int, float)):
            time.sleep(item)
            continue
        r = W.ui("keys", keys="+".join(KEYMAP.get(n.lower(), n.upper()) for n in item.split("+")))
        if isinstance(r, dict) and r.get("error"):
            log("keys", item, "->", r)
        time.sleep(delay)


def keys(*seq, delay=0.35):
    """A key sequence (chords and pauses in seconds): with a navigation key in
    it, sent in one gate-win-keys task; otherwise through `winix ui keys`."""
    items = [x if isinstance(x, (int, float)) else x.lower() for x in seq]
    if not any(isinstance(x, str) and set(x.split("+")) & EXTENDED for x in items):
        winix_keys(*items, delay=delay)
        return
    job, lines = W.task("gate-win-keys", env={"GATE_KEYS": json.dumps(items), "GATE_KEYS_DELAY": delay})
    if job.get("state") != "succeeded":
        log("keys", items, "->", job.get("state"), lines[-5:])


def type_text(text):
    for ch in text:
        if ch == " ":
            combo("space")
        elif ch.isupper():
            combo("shift", ch)
        else:
            combo(ch)
        time.sleep(0.05)


# ---------------------------------------------------------------- UI Automation
def frame_id(name):
    """The main window is titled "<document> - HikariSub" once a Document is open."""
    return MAIN if name == MAIN or name.endswith(" - " + MAIN) else name


def panel_id(name):
    if name.startswith("Editing:") or name == "No document open":
        return "Grid"
    return name.split(":")[0]


def el_path(els, e):
    parts = []
    while e is not None:
        parts.append(f"{e['t']}:'{e['n']}'")
        e = els[e["p"]] if e["p"] >= 0 else None
    return " > ".join(reversed(parts))


def ancestors(els, e):
    while e["p"] >= 0:
        e = els[e["p"]]
        yield e


def uia(do=None, settle_ms=None):
    """The app's UI Automation view, shaped like atspi_tool.py json (frames,
    panels, focus, focusPath, texts, labels) plus the raw elements."""
    env = {}
    if do:
        env["GATE_UIA_DO"] = json.dumps(do)
    if settle_ms is not None:
        env["GATE_UIA_SETTLE_MS"] = settle_ms
    try:
        job, lines = W.task("gate-win-uia", env=env or None, timeout=600)
        raw = line_value(lines, "UIA_JSON_B64 ")
        data = json.loads(base64.b64decode(raw).decode("utf-8")) if raw else None
    except Exception as e:  # keep the step going; the view says why it is empty
        data, lines = None, [repr(e)]
    if not data:
        return {"frames": [], "windows": [], "elements": [], "panels": [], "focus": None, "focusPath": None,
                "texts": {}, "labels": [], "popupMenuItems": [], "videoLabels": [], "combos": {}, "actions": [],
                "error": "\n".join(lines[-20:])}
    els = data.get("elements") or []
    st = dict(data)
    # Every titled window: the main window and the windows it owns (floating
    # panels, the placement window, dialogs), which UI Automation nests under
    # it. Nameless windows are popups.
    fg = (data.get("foreground") or {}).get("hwnd")
    st["frames"] = [{"name": e["n"], "type": "Window", "x": e["x"], "y": e["y"], "w": e["w"], "h": e["h"],
                     "hwnd": e["hw"], "dpi": e.get("dpi"), "active": bool(e["hw"]) and e["hw"] == fg, "id": frame_id(e["n"])}
                    for e in els if e["t"] == "Window" and e["n"] and not e["off"]]
    st["panels"] = []
    st["texts"], st["labels"], st["videoLabels"], st["combos"] = {}, [], [], {}
    st["popupMenuItems"] = []
    for e in els:
        if e["off"]:
            continue
        if e["t"] == "Pane" and e["n"]:
            st["panels"].append({"name": e["n"], "id": panel_id(e["n"]), "frame": frame_id(e["win"]),
                                 "x": e["x"], "y": e["y"], "w": e["w"], "h": e["h"]})
        if e["t"] == "Edit" and e["n"] in ("Line text", "Start", "End"):
            st["texts"][e["n"]] = e["val"] if e["val"] is not None else e["txt"]
        if e["t"] == "Text" and e["n"] and (e["n"].startswith(("Editing", "No editing")) or e["n"] == "Video times"):
            st["labels"].append(e["txt"] or e["n"])
        if e["t"] == "MenuItem" and not any(a["t"] == "MenuBar" for a in ancestors(els, e)):
            st["popupMenuItems"].append(e["n"])
        if e["t"] == "Text" and any(a["t"] == "Pane" and panel_id(a["n"]) == "Video" for a in ancestors(els, e)):
            st["videoLabels"].append((e["n"], e["txt"]))
        if e["t"] == "ComboBox":
            kids = [c for c in els if c["p"] == e["i"]]
            # Qt Quick's ComboBox reports an empty Value; its text is the child Edit's.
            st["combos"][e["n"]] = e["val"] or next(
                (c["val"] or c["txt"] or c["n"] for c in kids if c["t"] in ("Text", "Edit")), None)
    f = data.get("focus") or {}
    if f.get("index", -1) >= 0 and f["index"] < len(els):
        fe = els[f["index"]]
        st["focus"] = f"[{fe['t']}] {fe['n']!r} in {fe['win']!r}"
        st["focusPath"] = el_path(els, fe)  # starts at the top-level window
    else:
        st["focus"] = f.get("path") or f.get("error")
        st["focusPath"] = f.get("path")
    return st


def frames(st):
    return {f["id"]: f for f in st["frames"]}


def active_frames(st):
    return [f["id"] for f in st["frames"] if f.get("active")]


def panel(st, pid):
    return next((p for p in st["panels"] if p["id"] == pid), None)


def panel_frame(st, pid):
    p = panel(st, pid)
    return p["frame"] if p else None


def find(st, ctype, name):
    els = st.get("elements") or []
    return [dict(e, path=el_path(els, e)) for e in els
            if e["t"] == ctype and e["n"] == name and not e["off"]]


def wait_for(pred, timeout=8.0, step=0.4):
    end = time.time() + timeout
    while True:
        st = uia()
        if pred(st) or time.time() > end:
            return st
        time.sleep(step)


def panel_of(path):
    """The innermost panel on a focus path."""
    found = None
    for part in (path or "").split(" > "):
        m = re.match(r"(\w+):'(.*)'$", part)
        if m and m.group(1) == "Pane" and panel_id(m.group(2)) in PANELS:
            found = panel_id(m.group(2))
    return found


def top_windows():
    listed = W.ui("windows")
    return listed if isinstance(listed, list) else []


def snap(name, extra=None):
    png = EVID / f"{name}.png"
    W.screenshot(png)
    if png.exists() and not SCREEN["size"]:
        SCREEN["size"] = png_size(png)
    st = uia()
    with open(EVID / f"{name}.txt", "w") as f:
        f.write("# top-level windows (winix ui windows)\n" + json.dumps(top_windows(), indent=1, ensure_ascii=False)
                + "\n# UI Automation (gate-win-uia)\n" + json.dumps(st, indent=1, ensure_ascii=False) + "\n")
        if extra:
            f.write(extra + "\n")
    return [f"{name}.png", f"{name}.txt"], st


# ---------------------------------------------------------------- app control
def prepare():
    job, lines = W.task("gate-win-prepare")
    raw = line_value(lines, "GATE_LAYOUT ")
    if job.get("state") != "succeeded" or not raw:
        raise SystemExit("gate-win-prepare failed:\n" + "\n".join(lines[-30:]))
    LAYOUT.update(json.loads(raw))
    log("layout", json.dumps(LAYOUT))
    if not LAYOUT.get("app"):
        raise SystemExit("no hikarisub.exe under out/build/windows-x64-release in the VM (run the build task)")


def main_window():
    for w in top_windows():
        if frame_id(w.get("name") or "") == MAIN:
            return w
    return None


def app_start(doc=None):
    args = ["-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", LAYOUT["launcher"]]
    W.ui("launch", exe="powershell.exe", arguments=args + ([doc] if doc else []), cwd=LAYOUT["root"])
    deadline = time.time() + 90
    while time.time() < deadline:
        main = main_window()
        if main:
            time.sleep(2)
            focus_main_compositor()
            return main
        time.sleep(2)
    log("app_start: no main window after 90 s")
    return None


def app_kill():
    W.task("gate-win-kill")


def app_status():
    _, lines = W.task("gate-win-status")
    s = sections(lines)
    return {"alive": "ALIVE yes" in lines, "layout_files": [l for l in s.get("layout files", []) if l.strip()],
            "app_log": s.get("app log", [])}


def fresh(doc=None):
    job, lines = W.task("gate-win-fresh")
    for l in lines:
        if l.startswith("set aside"):
            log(l)
    if job.get("state") != "succeeded":
        raise RuntimeError("gate-win-fresh failed (the profile was not set aside): " + " | ".join(lines[-6:]))
    app_start(doc)
    return uia()


def dismiss_notices():
    """Opening a Document may raise the spelling notice; press its OK."""
    st = uia(do=[{"action": "invoke", "name": "OK", "type": "Button", "all": True}], settle_ms=500)
    others = [f["id"] for f in st["frames"] if f["id"] != MAIN]
    if others:
        log("windows besides the main one after the notices:", others)


def focus_main_compositor():
    """Give the main window the focus the way a user would: a click on its
    title bar (in the borderless fullscreen state the same point is the
    menu bar's empty middle)."""
    main = main_window()
    if not main:
        return False
    b = main["bounds"]
    W.ui("click", x=b["x"] + b["width"] // 2, y=max(b["y"], 0) + 12)
    time.sleep(0.8)
    return True


def focus_main():
    """F6 until a panel of the main window has the focus (the shell activates
    the window of the panel it moves to); else the title bar click."""
    for _ in range(6):
        st = uia()
        main = frames(st).get(MAIN)
        if main and main.get("active") and st["focusPath"] and panel_of(st["focusPath"]) \
                and not st["focusPath"].startswith("Window:'Line editor'"):
            return True
        combo("f6")
        time.sleep(0.8)
    focus_main_compositor()
    return False


def open_view():
    """Alt+W opens the View menu ("Vie&w": Alt+V stays with legacy's &Video).
    Retried until the View menu (its "Move panel…" item) shows."""
    for _ in range(3):
        keys("alt+w", 0.6)
        st = uia()
        if any("Move panel" in (n or "") for n in st["popupMenuItems"]):
            return True
        keys("escape", 0.3)
    log("open_view: the View menu did not open")
    return False


# The key that opens a submenu: Right, as on Linux; on Windows Right does not
# open View > Panels (first run), so float_panel() records that once and
# switches to Return, which opens it.
SUBMENU = {"key": "right", "checked": False}


def panel_menu(name, item):
    """View > Panels > NAME > ITEM (Show, Hide, Float, Dock) from the keyboard."""
    open_view()
    k = SUBMENU["key"]
    seq = ["down", k, 0.3] + ["down"] * PANELS.index(name) + [k, 0.3]
    seq += ["down"] * ["Show", "Hide", "Float", "Dock"].index(item) + ["return"]
    keys(*seq)
    time.sleep(1.2)


def close_window(title):
    uia(do=[{"action": "closewindow", "window": title}], settle_ms=500)


def float_panel(name, tag):
    """View > Panels > NAME > Float, or View > Move panel… with Place: Float."""
    panel_menu(name, "Float")
    st = wait_for(lambda s: name in frames(s), timeout=4)
    if name in frames(st):
        return "menu" if SUBMENU["key"] == "right" else "menu-return"
    if not SUBMENU["checked"]:
        SUBMENU["checked"] = True
        ev, _ = snap(f"{tag}-submenu-right")
        keys("escape", "escape", "escape", "escape")
        focus_main_compositor()
        SUBMENU["key"] = "return"
        panel_menu(name, "Float")
        st = wait_for(lambda s: name in frames(s), timeout=4)
        if name in frames(st):
            verdict("submenu-right-arrow", "failed",
                    "Right on View > Panels (focused item) does not open its submenu, so View > Panels > "
                    f"{name} > Float with Right did nothing; Return opens the submenus and floats it. The keys "
                    "reach the menu (Down moves the focus); Right sent as a keyboard sends it (SendInput with the "
                    "scan code and the extended-key flag) and without the flag behaves the same. The rest of "
                    "the run opens submenus with Return", ev)
            return "menu-return"
        SUBMENU["key"] = "right"
    log(f"{tag}: View > Panels > {name} > Float did not float it; using View > Move panel…")
    keys("escape", "escape", "escape", "escape")
    focus_main_compositor()
    open_view()
    keys("down", "down", "return", 1.2, "home", *(["down"] * PANELS.index(name)), "tab", "home",
         *(["down"] * 5), "tab", "space", 1.5)
    st = wait_for(lambda s: name in frames(s), timeout=4)
    if "Move panel" in frames(st):
        close_window("Move panel")
    return "move-panel" if name in frames(st) else None


def f6_walk(n=5, key="f6"):
    seq = []
    for _ in range(n):
        combo(*key.split("+"))
        time.sleep(0.8)
        st = uia()
        seq.append({"focus": st["focus"], "path": st["focusPath"], "active": active_frames(st)})
    return seq


def image_diff(a, b):
    r = subprocess.run(["magick", "compare", "-metric", "AE", "-fuzz", "2%", str(EVID / a), str(EVID / b), "null:"],
                       capture_output=True, text=True)
    try:
        return int(float(r.stderr.split()[0]))
    except (ValueError, IndexError):
        return r.stderr.strip()


# ---------------------------------------------------------------- steps
def step_default():
    fresh()
    ev, st = snap("default-arrangement")
    fr = frames(st)
    docked = {p["id"]: p["frame"] for p in st["panels"] if p["id"] in ("Video", "Audio", "Line editor", "Grid")}
    ok = len(fr) == 1 and MAIN in fr and len(docked) == 4 and all(v == MAIN for v in docked.values())
    pos = {p["id"]: (p["x"], p["y"]) for p in st["panels"]}
    shape = (ok and pos["Video"][0] < pos["Audio"][0] and pos["Audio"][1] < pos["Line editor"][1]
             and pos["Grid"][1] > pos["Video"][1])
    verdict("default-arrangement", "observed" if shape else "failed",
            f"one window ({list(fr)}); panels {docked}; Video left, Audio over Line editor, Grid below: {bool(shape)}", ev)


def step_keyboard_float_dock():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    uia(do=[{"action": "focus", "name": "Line text", "type": "Edit"}], settle_ms=500)
    keys("end")
    type_text(" draft")
    time.sleep(0.8)
    keys("f6", 0.5)
    st = uia()
    before = st["texts"].get("Line text")
    labels_before = st["labels"]
    ev0, _ = snap("kbd-0-draft")
    how = float_panel("Line editor", "kbd")
    st = wait_for(lambda s: "Line editor" in frames(s))
    ev1, st = snap("kbd-1-floated")
    floated = "Line editor" in frames(st) and panel_frame(st, "Line editor") == "Line editor"
    text_float = st["texts"].get("Line text")
    verdict("keyboard-float", "observed" if how in ("menu", "menu-return") and floated else "failed",
            f"View > Panels > Line editor > Float from the keyboard: own window={floated} (floated by {how})", ev0 + ev1)
    if how == "move-panel":
        verdict("keyboard-float-move-panel", "observed" if floated else "failed",
                f"View > Move panel… Place: Float (fallback after the View > Panels path failed): own window {floated}", ev1)
    if not floated:
        verdict("keyboard-dock", "not-observable", "the Line editor could not be floated from the keyboard")
        return
    verdict("draft-kept-across-float",
            "observed" if text_float == before and (before or "").endswith(" draft") else "failed",
            f"Line text before {before!r}, floating {text_float!r}; status {labels_before} -> {st['labels']}", ev0 + ev1)
    focus_main()
    panel_menu("Line editor", "Dock")
    st = wait_for(lambda s: "Line editor" not in frames(s))
    if "Line editor" in frames(st):
        log("Dock at the 4th position did not dock; trying the 3rd (disabled items skipped)")
        keys("escape", "escape", "escape")
        focus_main()
        open_view()
        k = SUBMENU["key"]
        keys("down", k, 0.3, "down", "down", k, 0.3, "down", "down", "return")
        st = wait_for(lambda s: "Line editor" not in frames(s))
    ev2, st = snap("kbd-2-docked")
    docked = "Line editor" not in frames(st) and panel_frame(st, "Line editor") == MAIN
    verdict("keyboard-dock", "observed" if docked else "failed",
            f"View > Panels > Line editor > Dock from the keyboard: back in the main window={docked}", ev2)
    text_dock = st["texts"].get("Line text")
    verdict("draft-kept-across-dock", "observed" if docked and text_dock == before else "failed",
            f"Line text after docking {text_dock!r}; status {st['labels']}", ev2)


def step_f6_floating():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    how = float_panel("Line editor", "f6")
    ev0, st = snap("f6-0-after-float")
    after_float = {"active": active_frames(st), "focus": st["focusPath"]}
    walk_a = f6_walk(4)
    ev1, _ = snap("f6-1-after-f6-from-float")
    focus_main_compositor()
    st = uia()
    start_b = {"active": active_frames(st), "focus": st["focusPath"]}
    walk_b = f6_walk(5)
    back = f6_walk(3, "shift+f6")
    ev2, st = snap("f6-2-walk-from-main")
    (EVID / "f6-floating.json").write_text(json.dumps(
        {"floated_by": how, "after_float": after_float, "f6_after_float": walk_a, "main_focused": start_b,
         "f6_from_main": walk_b, "shift_f6": back}, indent=1, ensure_ascii=False))
    reached_a = [panel_of(w["path"]) for w in walk_a]
    reached_b = [panel_of(w["path"]) for w in walk_b]
    float_active_b = any("Line editor" in w["active"] for w in walk_b)
    verdict("focus-after-float", "observed" if after_float["focus"] else "failed",
            f"floated by {how}; then active windows {after_float['active']}, focused object {after_float['focus']}", ev0)
    verdict("f6-from-floating-panel", "observed" if any(x and x != "Line editor" for x in reached_a) else "failed",
            f"F6 x4 right after Float reached {reached_a} (active {[w['active'] for w in walk_a]})", ev1)
    verdict("f6-into-floating-panel", "observed" if "Line editor" in reached_b and float_active_b else "failed",
            f"main window focused by a title-bar click (focus {start_b['focus']}); F6 x5 reached {reached_b} "
            f"(active {[w['active'] for w in walk_b]}); Shift+F6 x3 {[panel_of(w['path']) for w in back]}",
            ev2 + ["f6-floating.json"])
    for _ in range(6):
        if panel_of(uia()["focusPath"]) == "Line editor":
            break
        combo("f6")
        time.sleep(0.8)
    in_float = panel_of(uia()["focusPath"]) == "Line editor"
    combo("ctrl", "shift", "h")
    st = wait_for(lambda s: any(f["name"].startswith("History") for f in s["frames"]))
    ev3, st = snap("ctrl-shift-h-from-floating")
    hist = any(f["name"].startswith("History") for f in st["frames"])
    verdict("shortcut-from-floating-panel", ("observed" if hist else "failed") if in_float else "not-observable",
            f"focus in the floating Line editor: {in_float}; Ctrl+Shift+H opened History: {hist}", ev3)
    keys("escape")


def step_move_panel():
    fresh()
    open_view()
    keys("down", "down", "return", 1.2)
    st = wait_for(lambda s: "Move panel" in frames(s))
    combos_open = st["combos"]
    ev0, st = snap("move-panel-0-open", extra="# combo boxes\n" + json.dumps(combos_open, ensure_ascii=False))
    if "Move panel" not in frames(st):
        verdict("keyboard-move-panel", "failed", "View > Move panel… did not open the placement window", ev0)
        return
    verdict("move-panel-defaults", "observed" if combos_open and all(v for v in combos_open.values()) else "failed",
            f"placement window on open (Grid focused): {combos_open}", ev0)
    # Panel: Grid -> Audio (Up x2); Place: Left of (Down); Next to: Grid (Up x6, Down x3); Move.
    keys("up", "up", "tab", "down", "tab", *(["up"] * 6), *(["down"] * 3), 0.3)
    combos_set = uia()["combos"]
    keys("tab", "space", 1.5)
    ev1, st = snap("move-panel-1-left-of-grid", extra="# combo boxes before Move\n" + json.dumps(combos_set, ensure_ascii=False))
    pos = {p["id"]: p for p in st["panels"]}
    a, g = pos.get("Audio"), pos.get("Grid")
    ok = a and g and a["frame"] == g["frame"] == MAIN and a["x"] < g["x"] and abs(a["y"] - g["y"]) < 3
    verdict("keyboard-move-panel", "observed" if ok else "failed",
            f"Move panel ({combos_set}) -> Audio {a and (a['x'], a['y'], a['w'], a['h'])}, "
            f"Grid {g and (g['x'], g['y'], g['w'], g['h'])}; focus {st['focusPath']}", ev0 + ev1)
    close_window("Move panel")


def title_point(st, pid, frame_name, dx=120):
    """A point on PID's title bar, DX px in from its left edge: the UIA
    TitleBar KDDockWidgets exposes (named after the panel), else the row of
    its Float/Dock button, else (neither found) the window's native caption."""
    tb = next((t for t in st.get("elements") or [] if t["t"] == "TitleBar" and not t["off"]
               and panel_id(t["n"]) == pid and frame_id(t["win"]) == frame_name), None)
    if tb:
        return tb["x"] + dx, tb["y"] + tb["h"] // 2, "UIA TitleBar"
    p = next((q for q in st["panels"] if q["id"] == pid and q["frame"] == frame_name), None)
    btn = next((b for n in (f"Float {pid}", f"Dock {pid}") for b in find(st, "Button", n)
                if frame_id(b["win"]) == frame_name), None)
    if btn and p:
        return p["x"] + dx, btn["y"] + btn["h"] // 2, "title-bar button row"
    f = frames(st).get(frame_name)
    if f:
        return f["x"] + dx, f["y"] + 12, "window caption"
    return None


def step_pointer():
    """Float button, double-click on title bars and drag-to-dock with real pointer input (QMP tablet)."""
    fresh()
    screen_size()
    # Calibration: the area the tablet spans, then where the cursor lands on
    # targets across every monitor (GetCursorPos in the guest).
    measured = TAB.calibrate()
    mons = displays().get("monitors") or [{"x": 0, "y": 0, "w": SCREEN["size"][0], "h": SCREEN["size"][1]}]
    cal, unreachable = [], []
    rect = measured.get("rect") or (0, 0, *SCREEN["size"])
    for m in mons:
        for fx, fy in ((0.25, 0.25), (0.75, 0.75)):
            x, y = m["x"] + int(m["w"] * fx), m["y"] + int(m["h"] * fy)
            if not (rect[0] <= x < rect[0] + rect[2] and rect[1] <= y < rect[1] + rect[3]):
                unreachable.append({"monitor": m.get("device"), "point": [x, y]})
                continue
            TAB.pointer(f"move {x} {y}", "wait 300")
            cal.append({"monitor": m.get("device"), "sent": [x, y], "units": TAB.units(x, y), "cursor": uia().get("cursor")})
    (EVID / "pointer-calibration.json").write_text(json.dumps(
        {"screenshot": SCREEN["size"], "monitors": mons, "tablet": measured, "points": cal,
         "unreachable_by_the_tablet": unreachable}, indent=1))
    off = [abs(c["cursor"]["x"] - c["sent"][0]) + abs(c["cursor"]["y"] - c["sent"][1]) for c in cal if c.get("cursor")]
    log("tablet calibration (px off):", off, measured)
    verdict("pointer-calibration", "observed" if off and len(off) == len(cal) and max(off) <= 4 else "failed",
            f"tablet spans {measured.get('rect')} over monitors {[(m.get('device'), m['x'], m['y'], m['w'], m['h']) for m in mons]}; "
            f"the guest cursor landed {off} px (|dx|+|dy|) from the targets; {len(unreachable)} target(s) outside "
            "the tablet's area (Windows maps the absolute HID pointer to the primary monitor) (pointer-calibration.json)",
            ["pointer-calibration.json"])
    st = uia()
    btn = next((b for b in find(st, "Button", "Float Audio") if frame_id(b["win"]) == MAIN), None)
    audio = panel(st, "Audio")
    if btn:
        tx, ty, aim = btn["x"] + btn["w"] // 2, btn["y"] + btn["h"] // 2, "UIA bounds of 'Float Audio'"
    elif audio:
        tx, ty, aim = audio["x"] + audio["w"] - 22, audio["y"] - 16, "30 px from the Audio panel's right edge"
    else:
        verdict("pointer-float-button", "not-observable", "neither the 'Float Audio' button nor the Audio panel found")
        return
    TAB.pointer(f"move {tx} {ty}", "wait 200", "down", "wait 60", "up", "wait 1200")
    st = wait_for(lambda s: "Audio" in frames(s))
    ev, st = snap("ptr-1-float-button")
    verdict("pointer-float-button", "observed" if "Audio" in frames(st) else "failed",
            f"tablet click on Audio's float button at {tx},{ty} ({aim}): own window={'Audio' in frames(st)}",
            ev + ["pointer-calibration.json"])
    # Double-click the docked Video title bar: floats; double-click its floating title bar: docks.
    pt = title_point(st, "Video", MAIN)
    dfloat, redock, ev1, ev2 = False, False, [], []
    if pt:
        vx, vy, _ = pt
        TAB.pointer(f"move {vx} {vy}", "wait 300", "down", "wait 40", "up", "wait 60", "down", "wait 40", "up", "wait 1500")
        st = wait_for(lambda s: "Video" in frames(s))
        ev1, st = snap("ptr-2-dblclick-float")
        dfloat = "Video" in frames(st)
    if dfloat:
        pt2 = title_point(st, "Video", "Video")
        if pt2:
            fx, fy, where = pt2
            log("floating Video title at", fx, fy, where)
            TAB.pointer(f"move {fx} {fy}", "wait 300", "down", "wait 40", "up", "wait 60", "down", "wait 40", "up",
                        "wait 1500")
            st = wait_for(lambda s: "Video" not in frames(s))
            ev2, st = snap("ptr-3-dblclick-redock")
            redock = "Video" not in frames(st)
    verdict("pointer-dblclick-float-redock", "observed" if dfloat and redock else "failed",
            f"double-click docked Video title at {pt}: floated={dfloat}; double-click its floating title: "
            f"redocked={redock}", ev1 + ev2)
    # Drag the floating Audio panel by its title bar onto the Grid's centre.
    st = uia()
    src = title_point(st, "Audio", "Audio", dx=150)
    grid = panel(st, "Grid")
    if not src or not grid or grid["frame"] != MAIN:
        verdict("pointer-drag-dock", "not-observable", f"could not locate the floating Audio window ({src}) or the Grid")
        return
    sx, sy, where = src
    gx, gy = grid["x"] + grid["w"] // 2, grid["y"] + grid["h"] // 2 - 15
    cmds = [f"move {sx} {sy}", "wait 250", "down", "wait 250"]
    for i in range(1, 31):
        cmds += [f"move {sx + (gx - sx) * i // 30} {sy + (gy - sy) * i // 30}", "wait 40"]
    cmds += [f"move {gx + 3} {gy + 3}", "wait 300", f"move {gx} {gy}", "wait 700"]
    try:
        TAB.pointer(*cmds)
        ev3, _ = snap("ptr-4-drag-over-grid")  # the drop indicators, button still held
    finally:
        TAB.pointer("up", "wait 1500", f"move {gx + 5} {gy + 5}", "wait 300")
    ev4, st = snap("ptr-5-dropped")
    docked = "Audio" not in frames(st) and panel_frame(st, "Audio") == MAIN
    verdict("pointer-drag-dock", "observed" if docked else "failed",
            f"tablet drag of floating Audio by its title ({where}) from {sx},{sy} to the Grid centre {gx},{gy} and "
            f"release: docked={docked}, windows {list(frames(st))} (drop indicators: ptr-4-drag-over-grid.png)", ev3 + ev4)


def step_video():
    if not LAYOUT.get("ep1_ass"):
        verdict("video-float-redock", "error", "no cfr.mkv media fixture in the VM's build tree (run the test task once)")
        return
    fresh(LAYOUT["ep1_ass"])
    dismiss_notices()
    uia(do=[{"action": "invoke", "name": "Load associated", "type": "Button", "all": True}])
    time.sleep(5)
    focus_main()
    shots = []

    def state(tag):
        ev, st = snap(f"video-{tag}")
        shots.append(ev[0])
        return ev, " | ".join(f"{n!r} {t!r}" for n, t in st["videoLabels"])

    def step_frames():
        # The panel's own Next frame button, pressed through UI Automation.
        uia(do=[{"action": "invoke", "name": "Next frame", "type": "Button", "all": True, "repeat": 3}], settle_ms=1500)

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
        keys("escape", "escape", "escape", "escape")
        focus_main_compositor()
        open_view()
        k = SUBMENU["key"]
        keys("down", k, 0.3, k, 0.3, "down", "down", "return")
        wait_for(lambda s: "Video" not in frames(s), timeout=4)
    time.sleep(2)
    ev4, l4 = state("4-redocked")
    step_frames()
    ev5, l5 = state("5-redocked-stepped")
    diffs = {"docked step": image_diff(shots[0], shots[1]), "floating step": image_diff(shots[2], shots[3]),
             "redocked step": image_diff(shots[4], shots[5])}
    applog = "\n".join(app_status()["app_log"])
    (EVID / "video-labels.txt").write_text(
        "\n".join(f"{k}: {v}" for k, v in [("0 docked", l0), ("1 docked stepped", l1), ("2 floating", l2),
                                            ("3 floating stepped", l3), ("4 redocked", l4), ("5 redocked stepped", l5)])
        + f"\nfloated by: {how}\npixel differences: {diffs}\n\n# app log\n{applog}\n")
    opened = "No video open" not in l0
    changed = all(isinstance(v, int) and v > 50 for v in diffs.values())
    verdict("video-float-redock", "observed" if opened and changed and how else "failed",
            f"video opened: {opened}; floated by {how}; screen pixels changed by 3 frame steps: {diffs}; "
            f"labels docked {l1!r} floating {l3!r} redocked {l5!r}",
            ev0 + ev1 + ev2 + ev3 + ev4 + ev5 + ["video-labels.txt"])


def step_persistence():
    fresh()
    float_panel("Audio", "persist")
    ev0, st = snap("persist-0-before-exit")
    # Close the main window the way a user does: its title bar, then Alt+F4
    # (WM_CLOSE reaches onClosing, which saves the layout).
    focus_main_compositor()
    keys("alt+f4")
    time.sleep(2.5)
    status = app_status()
    gone = not status["alive"]
    if not gone:
        log("Alt+F4 did not end the app; ending it after the 5 s layout check")
        time.sleep(6)
        app_kill()
        status = app_status()
    cfg = "\n".join(status["layout_files"])
    app_start()
    st = wait_for(lambda s: "Audio" in frames(s))
    ev1, st = snap("persist-1-after-restart", extra="# layout files\n" + cfg)
    ok = "Audio" in frames(st)
    verdict("layout-persistence", "observed" if ok else "failed",
            f"main window closed with Alt+F4 (process ended: {gone}); layout files: {cfg.strip()!r}; "
            f"after restart Audio floating: {ok}", ev0 + ev1)


def fullscreen(mode):
    _, lines = W.task("gate-win-fullscreen", env={"GATE_FULLSCREEN": mode})
    raw = line_value(lines, "FULLSCREEN_RESULT ")
    return json.loads(raw) if raw else {"error": lines[-10:]}


def step_fullscreen():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    float_panel("Line editor", "fullscreen")
    # Windows has no compositor fullscreen request for another application's
    # window and HikariSub has no fullscreen command: the main window is made
    # borderless over the whole monitor (taskbar included), as a borderless
    # fullscreen tool does. Windows treats such a window as fullscreen.
    fs = fullscreen("on")
    try:
        time.sleep(1.5)
        ev0, st0 = snap("fullscreen-0-main-fullscreen", extra="# FULLSCREEN_RESULT\n" + json.dumps(fs))
        focus_main()
        walk = f6_walk(5)
        ev1, st = snap("fullscreen-1-after-f6")
    finally:
        restored = fullscreen("off")
    reached = [panel_of(w["path"]) for w in walk]
    float_active = any("Line editor" in w["active"] for w in walk)
    (EVID / "fullscreen-f6.json").write_text(json.dumps({"fullscreen": fs, "walk": walk, "restored": restored},
                                                        indent=1, ensure_ascii=False))
    main_reached = [panel_of(w["path"]) for w in walk if w["active"] == [MAIN] and panel_of(w["path"])]
    verdict("fullscreen-coexistence", "observed" if "Line editor" in reached and float_active and main_reached
            and not fs.get("caption", True) else "failed",
            f"main window borderless over the monitor ({fs}) with Line editor floating (windows "
            f"{list(frames(st0))}); F6 reached {reached}, main-window panels {main_reached}, "
            f"floating window active at some step: {float_active} (see screenshots: is the floating panel visible "
            f"above the fullscreen window?)", ev0 + ev1 + ["fullscreen-f6.json"])


def step_menu_from_text():
    fresh(LAYOUT["episode"])
    dismiss_notices()
    st = uia(do=[{"action": "focus", "name": "Line text", "type": "Edit"}], settle_ms=500)
    before = st["texts"].get("Line text")
    keys("alt+v", 1.0)
    ev, st = snap("menu-from-text")
    after = st["texts"].get("Line text")
    menu_open = bool(st["popupMenuItems"])
    keys("escape")
    verdict("menu-mnemonic-from-text-field", "observed" if menu_open and after == before else "failed",
            f"Alt+V in Line text: a menu open={menu_open} ({st['popupMenuItems'][:6]}); text {before!r} -> {after!r}", ev)


def step_test_executables():
    app_kill()  # the tests' windows need the desktop to themselves
    job, lines = W.task("gate-win-tests", timeout=3000)
    text = "\n".join(lines)
    (EVID / "test-executables.txt").write_text(f"# winix job {job.get('id')} ({job.get('state')})\n{text}\n")
    exits = re.findall(r"^### (\S+) .*\(exit (.+)\)$", text, re.M)
    ok = len(exits) == 3 and all(code == "0" for _, code in exits)
    summary = "; ".join(re.findall(r"Totals: [^,]+, [^,]+, [^,]+", text))
    skips = re.findall(r"SKIP\s*:.*", text)
    unexpected = [k for k in skips if "the compositor places windows" not in k]
    log("tests", exits, summary, skips)
    verdict("test-executables", "observed" if ok and not unexpected else "failed",
            f"exits {exits}; {summary}; skips: {skips}", ["test-executables.txt"])


def step_nvda():
    """NVDA (portable, "No speech" synthesizer, io log level) while F6 moves
    through the panels and a panel is floated from the View menu."""
    if not LAYOUT.get("nvda_exe"):
        job, lines = W.task("nvda-setup", timeout=1500)
        raw = line_value(lines, "NVDA_LAYOUT ")
        (EVID / "nvda-setup.txt").write_text("\n".join(lines) + "\n")
        if not raw:
            verdict("nvda", "error", "nvda-setup did not create the portable copy (nvda-setup.txt)", ["nvda-setup.txt"])
            return
        LAYOUT["nvda_exe"] = json.loads(raw)["exe"]
    fresh(LAYOUT["episode"])
    dismiss_notices()
    nvda_log = LAYOUT["nvda_dir"] + "\\nvda-" + time.strftime("%Y%m%d-%H%M%S") + ".log"
    W.ui("launch", exe=LAYOUT["nvda_exe"], arguments=[
        "--minimal", "--replace", "--disable-addons", "--log-level=12", f"--log-file={nvda_log}",
        f"--config-path={LAYOUT['nvda_config']}"], cwd=LAYOUT["nvda_dir"])
    time.sleep(10)
    try:
        focus_main_compositor()
        f6_walk(4)
        open_view()
        k = SUBMENU["key"]
        keys("down", 0.6, k, 0.6, "down", 0.6, "down", 0.6, k, 0.6, "down", 0.6, "down", 0.6, "return", 2.0)
        f6_walk(3)
        ev, st = snap("nvda-0-after-float")
        time.sleep(2)
    finally:
        job, lines = W.task("nvda-output", env={"GATE_NVDA_LOG": nvda_log})
    s = sections(lines)
    speech = [l.strip() for l in s.get("speech", []) if l.strip()]
    (EVID / "nvda-speech.txt").write_text("\n".join(speech) + "\n")
    (EVID / "nvda-output.txt").write_text("\n".join(lines) + "\n")
    got_log = W.artifact(job["id"], "nvda-last.log", EVID / "nvda.log")
    synth = s.get("synth", [])
    silent = any("silence" in l for l in synth)
    spoke = [p for p in ("Video", "Audio", "Line editor", "Grid", "Panels", "Float") if any(p in l for l in speech)]
    evidence = ["nvda-speech.txt", "nvda-output.txt"] + (["nvda.log"] if got_log else []) + ev
    verdict("nvda", "observed" if speech else "failed",
            f"{len(speech)} speech lines; panel/menu names spoken: {spoke}; synthesizer lines {synth[:3]} "
            f"(silent: {silent}); floating windows after the menu {list(frames(st))} (nvda-speech.txt; full log nvda.log)",
            evidence)
    verdict("nvda-float-from-menu", "observed" if "Line editor" in frames(st) else "failed",
            f"with NVDA running, View > Panels > Line editor > Float from the keyboard: windows {list(frames(st))}", ev)


def inside(f, m):
    """Whether window F's centre lies on monitor M."""
    cx, cy = f["x"] + f["w"] // 2, f["y"] + f["h"] // 2
    return m["x"] <= cx < m["x"] + m["w"] and m["y"] <= cy < m["y"] + m["h"]


def framebuffer(name, head):
    """One QXL head's framebuffer from the host (`virsh screenshot --screen`, read-only)."""
    out = EVID / f"{name}.png"
    tmp = out.with_suffix(".ppm")
    r = subprocess.run(["virsh", "-c", TAB.uri, "screenshot", TAB.domain, str(tmp), "--screen", str(head)],
                       capture_output=True, text=True)
    if r.returncode == 0:
        subprocess.run(["magick", str(tmp), str(out)], capture_output=True)
    tmp.unlink(missing_ok=True)
    return [out.name] if out.exists() else []


def step_outputs():
    """Mixed DPI and monitor removal on the VM's two QXL monitors: a floating
    panel (Audio) dragged with the real pointer onto the second monitor at
    150 % (primary 100 %), then that monitor removed from the desktop."""
    d = displays("extend")
    mons = d.get("monitors") or []
    if len(mons) < 2:
        reason = f"only {len(mons)} monitor(s) on the guest desktop after extending ({d.get('steps')})"
        for item in ("mixed-dpi", "monitor-removal", "monitor-removal-show-focus"):
            verdict(item, "not-observable", reason)
        return
    fresh(LAYOUT["episode"])
    dismiss_notices()
    try:
        screen_size()
        measured = TAB.calibrate()
        log("tablet", measured)
        how = float_panel("Audio", "outputs")
        st = wait_for(lambda s: "Audio" in frames(s), timeout=4)
        before = frames(st).get("Audio")
        # 150 % needs a larger mode on the second monitor (1280x800 allows 125 %).
        d2 = displays("extend", scale2=150, second=(1920, 1200))
        mons = d2.get("monitors") or mons
        prim = next(m for m in mons if m["primary"])
        sec = next(m for m in mons if not m["primary"])
        st = uia()
        moved_by = None
        src = title_point(st, "Audio", "Audio", dx=100)
        if src and before:
            sx, sy, _ = src
            tx, ty = sec["x"] + sec["w"] // 3, sec["y"] + 120
            reach = TAB.rect and TAB.rect[0] <= tx < TAB.rect[0] + TAB.rect[2]
            if reach:
                cmds = [f"move {sx} {sy}", "wait 250", "down", "wait 250"]
                for i in range(1, 31):
                    cmds += [f"move {sx + (tx - sx) * i // 30} {sy + (ty - sy) * i // 30}", "wait 40"]
                try:
                    TAB.pointer(*cmds, "wait 500")
                finally:
                    TAB.pointer("up", "wait 1500")
                moved_by = f"tablet drag of its title bar from {sx},{sy} to {tx},{ty}"
            else:
                # Windows maps the tablet to the primary only. A drag carried on
                # with relative motion does reach the second monitor, but the
                # tablet's button-up report puts the cursor back at its last
                # absolute position (the primary's edge, where Windows snaps
                # the window). So the window moves the keyboard way: active
                # (a click on its title), then Win+Shift+Right.
                TAB.pointer(f"move {sx} {sy}", "wait 200", "down", "wait 60", "up", "wait 500")
                keys("win+shift+right", 1.5)
                moved_by = f"Win+Shift+Right after a click on its title (the tablet spans {TAB.rect} only)"
        st = wait_for(lambda s: "Audio" in frames(s) and inside(frames(s)["Audio"], sec), timeout=6)
        fa = frames(st).get("Audio")
        ev0, st = snap("outputs-0-on-scale2", extra="# DISPLAY_RESULT\n" + json.dumps(d2))
        ev0 += framebuffer("outputs-0-monitor2-framebuffer", 1) + framebuffer("outputs-0-monitor1-framebuffer", 0)
        on2 = bool(fa) and inside(fa, sec)
        ratio = round(fa["w"] / before["w"], 2) if fa and before and before["w"] else None
        verdict("mixed-dpi", "observed" if on2 and sec.get("dpi") == 144 and fa.get("dpi") == 144 and prim.get("dpi") == 96
                else "failed",
                f"floating Audio (floated by {how}) moved by {moved_by} onto {sec['device']} at {sec['dpi']} dpi "
                f"(primary {prim['device']} at {prim['dpi']}): on it={on2}, window {fa and (fa['x'], fa['y'], fa['w'], fa['h'])}, "
                f"GetDpiForWindow {fa and fa.get('dpi')} (96 would mean bitmap-scaled by Windows), width x{ratio} of "
                f"its 100 % width {before and before['w']}; second monitor {sec['w']}x{sec['h']}, scale call {d2.get('scale2')} "
                "(outputs-0-monitor2-framebuffer.png: the head's own pixels)", ev0)
        # Monitor removal with the panel on the second monitor.
        d3 = displays("detach")
        left = d3.get("monitors") or []
        st = wait_for(lambda s: "Audio" in frames(s) and len(left) == 1 and inside(frames(s)["Audio"], left[0]), timeout=8)
        ev1, st = snap("outputs-1-after-removal", extra="# DISPLAY_RESULT\n" + json.dumps(d3))
        fa = frames(st).get("Audio")
        back = len(left) == 1 and bool(fa) and inside(fa, left[0])
        verdict("monitor-removal", "observed" if back else "failed",
                f"second monitor removed ({d3.get('steps')}; monitors now {[(m['device'], m['x'], m['y'], m['w'], m['h']) for m in left]}): "
                f"floating Audio present={bool(fa)}, window {fa and (fa['x'], fa['y'], fa['w'], fa['h'])} dpi {fa and fa.get('dpi')}, "
                f"on the remaining monitor={back}", ev1)
        focus_main_compositor()
        panel_menu("Audio", "Show")
        time.sleep(1)
        ev2, st = snap("outputs-2-show-after-removal")
        focused_in = panel_of(st["focusPath"])
        verdict("monitor-removal-show-focus", "observed" if focused_in == "Audio" else "failed",
                f"then View > Panels > Audio > Show gives the focus to {focused_in!r} (active windows "
                f"{active_frames(st)}; focus {st['focusPath']})", ev2)
    finally:
        r = displays("extend", scale2=100)
        ok = len(r.get("monitors") or []) == 2 and all(m["dpi"] == 96 and m["w"] == 1280 and m["h"] == 800
                                                      for m in r["monitors"])
        (EVID / "outputs-restored.txt").write_text(json.dumps(r, indent=1))
        verdict("outputs-restored", "observed" if ok else "failed",
                f"both monitors back at 1280x800, 100 %, extended: {[(m['device'], m['x'], m['w'], m['h'], m['dpi']) for m in r.get('monitors') or []]}",
                ["outputs-restored.txt"])


def dpi_set(percent):
    _, lines = W.task("gate-win-dpi", env={"GATE_DPI_PERCENT": percent})
    raw = line_value(lines, "DPI_RESULT ")
    return json.loads(raw) if raw else {"error": lines[-10:]}


def step_dpi():
    """The one monitor's display scale changed to 150 % live (a guest setting)
    with a floating panel, then restored to 100 %."""
    fresh(LAYOUT["episode"])
    dismiss_notices()
    how = float_panel("Line editor", "dpi")
    ev0, st0 = snap("dpi-0-at-100")
    res = dpi_set(150)
    try:
        if res.get("after") != 150:
            verdict("dpi-scale-change", "not-observable",
                    f"the guest did not change its scale to 150 % without signing out: {res}", ev0)
            return
        time.sleep(3)
        # The worker that takes screenshots is not DPI aware: keep the host's view of the framebuffer too.
        host = EVID / "dpi-1-at-150-framebuffer.png"
        r = subprocess.run(["virsh", "-c", TAB.uri, "screenshot", TAB.domain, str(host.with_suffix(".ppm"))],
                           capture_output=True, text=True)
        if r.returncode == 0:
            subprocess.run(["magick", str(host.with_suffix(".ppm")), str(host)], capture_output=True)
            host.with_suffix(".ppm").unlink(missing_ok=True)
        ev1, st = snap("dpi-1-at-150", extra="# DPI_RESULT\n" + json.dumps(res))
        uia(do=[{"action": "focus", "name": "Line text", "type": "Edit"}], settle_ms=300)
        keys("end")
        type_text(" hidpi")
        time.sleep(0.8)
        txt = uia()["texts"].get("Line text")
        both = MAIN in frames(st) and "Line editor" in frames(st)
        verdict("dpi-scale-change", "observed" if both and txt and txt.endswith(" hidpi") else "failed",
                f"scale 100 -> {res.get('after')} % live ({res}); floating Line editor (floated by {how}) and main "
                f"window still shown: {both}; typing there gives {txt!r} (dpi-1-at-150*.png)",
                ev0 + ev1 + ([host.name] if host.exists() else []))
    finally:
        back = dpi_set(100)
        time.sleep(3)
        ev2, st = snap("dpi-2-restored", extra="# DPI_RESULT\n" + json.dumps(back))
        verdict("dpi-restored", "observed" if back.get("after") == 100 else "failed",
                f"scale restored: {back}", ev2)


def step_a11y():
    """What a screen reader finds of the docking controls and the Grid through
    UI Automation; the buttons pressed through UIA float and dock a panel."""
    fresh(LAYOUT["episode"])
    dismiss_notices()
    st = uia()
    els = st.get("elements") or []
    tree = "\n".join("  " * e["d"] + f"[{e['t']}] {e['n']!r} id={e['id']!r} @{e['x']},{e['y']} {e['w']}x{e['h']}"
                     + "".join(f" {k}" for k in ("fo", "off", "inv") if e[k]) + ("" if e["en"] else " disabled")
                     + (f" val={e['val']!r}" if e["val"] is not None else "") + (f" sel={e['sel']}" if e["sel"] else "")
                     + f"  <{e['win']}>" for e in els)
    (EVID / "a11y-tree.txt").write_text(tree + "\n")
    table = find(st, "Table", "Subtitle lines") or find(st, "DataGrid", "Subtitle lines")
    grid_panel = [p for p in st["panels"] if p["id"] == "Grid"]
    in_grid = bool(table) and panel_of(table[0]["path"]) == "Grid"
    verdict("grid-accessible", "observed" if in_grid and grid_panel else "failed",
            f"table 'Subtitle lines': {[t['path'] for t in table[:1]]}; Grid panel: {grid_panel[:1]}", ["a11y-tree.txt"])
    names = ["Float Audio", "Close Audio", "Float Video", "Close Video", "Float Line editor", "Close Line editor",
             "Float Grid", "Close Grid"]
    found = {n: bool(find(st, "Button", n)) for n in names}
    press = find(st, "Button", "Float Audio")
    st = uia(do=[{"action": "invoke", "name": "Float Audio", "type": "Button"}])
    out = st["actions"]
    st = wait_for(lambda s: "Audio" in frames(s))
    floated = "Audio" in frames(st)
    ev0, st = snap("a11y-0-float-audio-by-uia")
    dock_btn = find(st, "Button", "Dock Audio")
    uia(do=[{"action": "invoke", "name": "Dock Audio", "type": "Button"}])
    st = wait_for(lambda s: "Audio" not in frames(s))
    docked = "Audio" not in frames(st)
    verdict("title-bar-buttons-accessible",
            "observed" if all(found.values()) and press and press[0]["inv"] and floated and dock_btn and docked
            else "failed",
            f"named buttons {found}; Invoke pattern on 'Float Audio': {bool(press and press[0]['inv'])}; invoked -> "
            f"own window={floated} ({out}); then 'Dock Audio' {[d['path'] for d in dock_btn[:1]]} -> docked={docked}",
            ["a11y-tree.txt"] + ev0)
    focus_main()
    panel_menu("Timing", "Show")
    time.sleep(1)
    st = uia()
    tabs = {n: find(st, "TabItem", n) for n in ("Line editor", "Timing")}
    tab_buttons = {n: bool(find(st, "Button", n)) for n in ("Float Timing", "Close Timing", "Float Line editor",
                                                            "Close Line editor")}
    ev1, st = snap("a11y-1-tabs")
    selected = [n for n, hits in tabs.items() if hits and hits[0]["sel"]]
    uia(do=[{"action": "invoke", "name": "Float Timing", "type": "Button"}])
    st = wait_for(lambda s: "Timing" in frames(s))
    tab_floated = "Timing" in frames(st)
    ev2, st = snap("a11y-2-float-timing-tab")
    (EVID / "a11y-tabs.txt").write_text(json.dumps(
        {"tabs": {n: [h["path"] for h in hits] for n, hits in tabs.items()}, "buttons": tab_buttons,
         "selected": selected}, indent=1, ensure_ascii=False))
    verdict("tabs-accessible",
            "observed" if all(tabs.values()) and all(tab_buttons.values()) and tab_floated else "failed",
            f"tab items { {n: bool(h) for n, h in tabs.items()} } (selected {selected}); tab buttons {tab_buttons}; "
            f"invoked 'Float Timing' -> own window={tab_floated}", ev1 + ev2 + ["a11y-tabs.txt"])


STEPS = {"default": step_default, "kbd": step_keyboard_float_dock, "f6": step_f6_floating, "move": step_move_panel,
         "pointer": step_pointer, "video": step_video, "persist": step_persistence, "fullscreen": step_fullscreen,
         "outputs": step_outputs, "nvda": step_nvda, "menutext": step_menu_from_text,
         "tests": step_test_executables, "a11y": step_a11y, "dpi": step_dpi}


def main():
    global W, TAB, LOG
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("steps", nargs="*", help=f"steps (default: all but dpi): {', '.join(STEPS)}")
    ap.add_argument("--config", default=str(ROOT / "winix.yaml"))
    ap.add_argument("--vm", default="dev")
    ap.add_argument("--domain", default="winix-dev", help="libvirt domain of the VM (QMP pointer input)")
    ap.add_argument("--libvirt-uri", default="qemu:///session")
    ap.add_argument("--dpi", action="store_true", help="allow the dpi step (changes the guest's display scale "
                                                       "to 150 %% and back to 100 %%)")
    a = ap.parse_args()
    wanted = a.steps or [s for s in STEPS if s != "dpi" or a.dpi]
    unknown = [s for s in wanted if s not in STEPS]
    if unknown:
        raise SystemExit(f"unknown steps {unknown}; steps: {', '.join(STEPS)}")
    if "dpi" in wanted and not a.dpi:
        raise SystemExit("the dpi step changes the guest's display scale; pass --dpi to run it")
    EVID.mkdir(parents=True, exist_ok=True)
    results.update(json.loads((EVID / "results.json").read_text()) if (EVID / "results.json").exists() else {})
    LOG = open(EVID / "steps.log", "a")
    W = Winix(a.config, a.vm)
    TAB = Tablet(a.domain, a.libvirt_uri)
    ready = W.call("vm", "ready", a.vm) or {}
    if not ready.get("desktopReady") or ready.get("activeJob"):
        raise SystemExit(f"the VM desktop is not ready or busy: {json.dumps(ready)[:300]}")
    prepare()
    try:
        for name in wanted:
            log(f"--- windows: {name}")
            try:
                STEPS[name]()
            except Exception as e:  # keep going: one broken step must not hide the others
                import traceback
                traceback.print_exc()
                verdict(name, "error", f"harness error: {e!r}")
    finally:
        try:
            TAB.button(False)  # never leave the left button held
        except Exception:
            pass
        app_kill()


if __name__ == "__main__":
    main()
