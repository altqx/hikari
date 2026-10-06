#!/usr/bin/env python3
"""D1 native gate on Windows: HikariSub on the Winix VM desktop.

  WINIX_OWNER=<lease owner> gate_windows.py [--dpi] [--config winix.yaml] [--vm dev] [STEP...]

The Windows half of tools/native-gate/gate.py, with the same step names and
the same evidence layout under out/native-gate-evidence/windows/: one PNG and
one TXT (the app's windows and its UI Automation view as JSON) per
observation, steps.log, and results.json with a verdict per gate item:
observed, failed or not-observable (with the reason).

Everything goes through `winix ui` (winix 0.2.0):
  keys      `ui keys` (SendInput with scan codes, the extended flag on navigation keys)
  text      `ui text` (Unicode input)
  pointer   `ui pointer` / `ui calibrate` (the VM's USB HID tablet, host-side QMP)
  view      one `ui batch`: `windows --pid`, `state` (focus, its ancestors, the
            cursor) and `tree --pid --window N` for every window of the app
  focus     `ui focus --pid --window hwnd:N`
  app       `ui launch --exe --arg --env --cwd --no-console`, `ui kill`
  monitors  `ui monitors`, `ui screenshot [--monitor N]`
Guest tasks (gate.ps1, uia.ps1, display.ps1, nvda.ps1) remain for what winix
does not do: the profile (Qt ignores the APPDATA variables `--fresh-profile`
sets), pressing controls through UIA patterns only, GetDpiForWindow, the
borderless fullscreen and display changes, NVDA, the test executables. Task
parameters go through `run TASK --env K=V`. The VM definition is never changed.
"""
import argparse
import base64
import json
import re
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
EVID = ROOT / "out" / "native-gate-evidence" / "windows"
PANELS = ["Video", "Audio", "Line editor", "Grid", "Reference", "Timing", "Search"]
MAIN = "HikariSub"
# Windows maps the absolute HID tablet to the primary monitor only (step
# pointer measures it), so pointer coordinates map over the primary.
POINTER_MAP = "primary"

W = None        # Winix
LAYOUT = {}     # gate-win-prepare's GATE_LAYOUT
APP = {"pid": None, "log": None}
# The profile the first fresh() set aside: put back when the run ends.
PROFILE = {"original": None}
results = {}
LOG = None


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
    """`winix --json`; the lease owner comes from WINIX_OWNER."""

    def __init__(self, config, vm):
        self.config = Path(config).resolve()
        self.vm = vm

    def lines(self, *args, timeout=900):
        cmd = ["winix", "--json", "--vm", self.vm, "--config", str(self.config), *args]
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, cwd=ROOT)
        out = []
        for line in p.stdout.splitlines():
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError:
                pass
        if not out:
            out.append({"error": "unparsed", "message": (p.stdout[-500:] + p.stderr[-500:]).strip()})
        return out

    def call(self, *args, timeout=900):
        return self.lines(*args, timeout=timeout)[-1]

    def ui(self, action, **args):
        return self.call("ui", action, "--args", json.dumps(args))

    def batch(self, steps):
        return self.call("ui", "batch", "--args", json.dumps(steps), "--continue-on-error")

    def screenshot(self, path, monitor=None):
        path.parent.mkdir(parents=True, exist_ok=True)
        return self.call("ui", "screenshot", "--output", str(path), *(["--monitor", str(monitor)] if monitor is not None else []))

    def task(self, name, env=None, timeout=3600):
        """Runs a winix.yaml task to its end; returns (job record, output lines)."""
        args = ["run", name, "--wait"]
        for k, v in (env or {}).items():
            args += ["--env", f"{k}={v}"]
        job = next((j for j in reversed(self.lines(*args, timeout=timeout)) if j.get("type") == "job" or j.get("state")), {})
        if not job.get("id"):
            raise RuntimeError(f"task {name} did not start: {job}")
        # `job logs --all` pages the log: one JSON record per 64 KiB.
        text = "".join(r.get("text", "") for r in self.lines("job", "logs", job["id"], "--all") if isinstance(r, dict))
        lines = [l[6:] if l.startswith(("[out] ", "[err] ")) else l for l in text.splitlines()]
        return job, lines

    def pull(self, guest, host):
        return self.call("pull", guest, str(host))


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


# ---------------------------------------------------------------- input
def keys(*seq, delay=0.35):
    """Chords ("alt+w", "down") and pauses (seconds): each run of chords is
    one `ui keys` call, DELAY seconds between chords."""
    run = []

    def flush():
        if run:
            r = W.ui("keys", keys=" ".join(run), delayMs=int(delay * 1000))
            if isinstance(r, dict) and r.get("error"):
                log("keys", run, "->", r)
            time.sleep(delay)
            run.clear()
    for item in seq:
        if isinstance(item, (int, float)):
            flush()
            time.sleep(item)
        else:
            run.append(item.upper())
    flush()


def combo(*names):
    keys("+".join(names), delay=0)


def type_text(text):
    W.ui("text", text=text)


def pointer(path, **kw):
    """The tablet along PATH ([(x, y), ...]): pressed at the first point and
    released at the last (button "none": moves only); hold=True keeps it down."""
    r = W.ui("pointer", path=";".join(f"{x},{y}" for x, y in path), map=POINTER_MAP, **kw)
    if isinstance(r, dict) and r.get("error"):
        log("pointer", path, kw, "->", r)
    return r


def park_pointer():
    """The tablet cursor to the primary monitor's bottom-right corner (the
    taskbar's clock): a menu that opens under the cursor takes its current item
    from the hover, which would turn the keyboard's Down and Right into other
    moves (Right then goes to the next menu of the bar)."""
    m = next((x for x in monitors() if x["primary"]), None)
    if m:
        W.ui("pointer", path=f"{m['x'] + m['w'] - 30},{m['y'] + m['h'] - 20}", button="none", map="primary")


def release():
    return W.ui("pointer", release=True)


def double_click(x, y):
    """Two tablet clicks, one `ui pointer` call each (winix has no double-click
    at a point): a double-click only when the second press comes within the
    double-click time (500 ms) of the first; a call takes about 0.3 s, more
    when the VM is busy. Returns the seconds between the two calls' ends."""
    pointer([(x, y)])
    t1 = time.monotonic()
    pointer([(x, y)])
    return round(time.monotonic() - t1, 2)


def double_click_until(x, y, done, title=None):
    """A tablet double-click at X,Y, again (up to three times) while DONE(view)
    is false; then, as a last resort, `ui doubleclick` on the UIA title bar
    TITLE ((automationId, name, window): SendInput mouse, not the tablet).
    Returns (view, how, gaps)."""
    gaps = []
    for _ in range(3):
        gaps.append(double_click(x, y))
        time.sleep(1.5)
        st = wait_for(done, timeout=4)
        if done(st):
            return st, "tablet", gaps
        log("tablet double-click at", x, y, "had no effect (gap", gaps[-1], "s); again")
    if title:
        aid, name, window = title
        r = W.ui("doubleclick", pid=APP["pid"], automationId=aid, name=name, window=window)
        log("ui doubleclick", name, "->", r)
        time.sleep(1.5)
        st = wait_for(done, timeout=4)
        if done(st):
            return st, "ui doubleclick (SendInput)", gaps
    return uia(), None, gaps


def monitors():
    m = W.ui("monitors") or {}
    return [{"device": x["deviceName"], "primary": x["primary"], "x": x["bounds"]["x"], "y": x["bounds"]["y"],
             "w": x["bounds"]["width"], "h": x["bounds"]["height"], "dpi": x["dpi"]} for x in m.get("monitors") or []]


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


def guest_uia(do=None, settle_ms=None):
    """uia.ps1: controls pressed through UIA patterns, and GetDpiForWindow."""
    env = {}
    if do:
        env["GATE_UIA_DO"] = json.dumps(do)
    if settle_ms is not None:
        env["GATE_UIA_SETTLE_MS"] = settle_ms
    try:
        _, lines = W.task("gate-win-uia", env=env, timeout=600)
        raw = line_value(lines, "UIA_JSON_B64 ")
        return json.loads(base64.b64decode(raw).decode("utf-8")) if raw else {"error": lines[-10:]}
    except Exception as e:  # keep the step going; the result says why it is empty
        return {"error": repr(e)}


def flatten(node, parent, depth, win, els):
    """A `ui tree` node into the element list; owned windows (walked on their
    own) are skipped where they appear under their owner."""
    if node.get("separateWindow"):
        return
    e = node["element"]
    b = e.get("bounds") or {}
    t = e.get("controlType") or ""
    n = e.get("name") or ""
    i = len(els)
    els.append({"i": i, "p": parent, "d": depth, "win": win, "hw": e.get("hwnd") or 0, "t": t, "n": n,
                "id": e.get("automationId") or "", "x": b.get("x", 0), "y": b.get("y", 0), "w": b.get("width", 0),
                "h": b.get("height", 0), "en": bool(e.get("enabled")), "val": e.get("value"),
                # winix reports no IsOffscreen: an element without area counts as off screen.
                "off": not b.get("width") or not b.get("height")})
    for c in node.get("children") or []:
        flatten(c, i, depth + 1, win, els)


def uia(do=None, settle_ms=None, dpi=False):
    """The app's UI Automation view, shaped like atspi_tool.py json (frames,
    panels, focus, focusPath, texts, labels) plus the raw elements: one
    `ui batch` (windows, state, a tree per window). DO presses controls first
    (uia.ps1); DPI adds GetDpiForWindow per window (uia.ps1)."""
    pressed = guest_uia(do, settle_ms) if do or dpi else {}
    st = {"frames": [], "windows": [], "elements": [], "panels": [], "focus": None, "focusPath": None,
          "texts": {}, "labels": [], "popupMenuItems": [], "videoLabels": [], "combos": {},
          "actions": pressed.get("actions") or [], "windowInfo": pressed.get("windowDpi") or [],
          "cursor": None, "foreground": None}
    pid = APP["pid"]
    if not pid:
        st["error"] = "no app running"
        return st
    steps = [{"action": "windows", "pid": pid}, {"action": "state"}]
    steps += [{"action": "tree", "pid": pid, "window": i, "depth": 12, "limit": 1000} for i in range(6)]
    for attempt in range(3):
        r = W.batch(steps)
        res = r.get("steps") or []
        # A window changing under the walk (a menu opening) can fail a step: once more.
        if len(res) > 2 and res[1].get("ok") and res[2].get("ok"):
            break
        log("ui batch incomplete, again:", json.dumps([t.get("error") for t in res[:3]])[:300])
        time.sleep(0.5)
    if len(res) < 2 or not res[0].get("ok"):
        st["error"] = json.dumps(r)[:500]
        return st
    windows = res[0]["result"]
    state = res[1].get("result") or {}
    st["windows"] = windows
    st["cursor"] = state.get("cursor")
    st["foreground"] = state.get("foreground")
    fg = (state.get("foreground") or {}).get("hwnd")
    dpis = {w["hwnd"]: w["dpi"] for w in pressed.get("windowDpi") or []}
    els = []
    # Out-of-range window indexes fail by design; anything else is kept.
    st["errors"] = [t for t in res[1:] if not t.get("ok") and "out of range" not in (t.get("error") or "")]
    for t in res[2:]:
        if t.get("ok"):
            node = t["result"]
            flatten(node, -1, 0, node["element"].get("name") or "", els)
    st["elements"] = els
    # Every titled window: the main window and the windows it owns (floating
    # panels, the placement window, dialogs). Nameless windows are popups.
    st["frames"] = [{"name": w["title"], "type": "Window", "x": w["bounds"]["x"], "y": w["bounds"]["y"],
                     "w": w["bounds"]["width"], "h": w["bounds"]["height"], "hwnd": w["hwnd"], "dpi": dpis.get(w["hwnd"]),
                     "active": w["hwnd"] == fg, "id": frame_id(w["title"])}
                    for w in windows if w["title"] and w.get("visible", True) and not w.get("minimized")]
    for e in els:
        if e["off"]:
            continue
        if e["t"] == "Pane" and e["n"]:
            st["panels"].append({"name": e["n"], "id": panel_id(e["n"]), "frame": frame_id(e["win"]),
                                 "x": e["x"], "y": e["y"], "w": e["w"], "h": e["h"]})
        if e["t"] == "Edit" and e["n"] in ("Line text", "Start", "End"):
            st["texts"][e["n"]] = e["val"]
        if e["t"] == "Text" and e["n"] and (e["n"].startswith(("Editing", "No editing")) or e["n"] == "Video times"):
            st["labels"].append(e["n"])
        if e["t"] == "MenuItem" and not any(a["t"] in ("MenuBar", "TitleBar") for a in ancestors(els, e)):
            st["popupMenuItems"].append(e["n"])
        if e["t"] == "Text" and any(a["t"] == "Pane" and panel_id(a["n"]) == "Video" for a in ancestors(els, e)):
            st["videoLabels"].append((e["n"], e["val"]))
        if e["t"] == "ComboBox":
            kids = [c for c in els if c["p"] == e["i"]]
            # Qt Quick's ComboBox reports an empty Value; its text is the child Edit's.
            st["combos"][e["n"]] = e["val"] or next((c["val"] or c["n"] for c in kids if c["t"] in ("Text", "Edit")), None)
    focused = state.get("focused")
    if focused and focused.get("pid") == pid:
        chain = [a for a in reversed(state.get("ancestors") or []) if a.get("pid") == pid] + [focused]
        st["focusPath"] = " > ".join(f"{a['controlType']}:'{a.get('name') or ''}'" for a in chain)
        st["focus"] = f"[{focused['controlType']}] {focused.get('name') or ''!r} in {(state.get('foreground') or {}).get('title')!r}"
    else:
        st["focus"] = f"not in the app: {json.dumps(focused)[:200]}"
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


def snap(name, extra=None, monitor=None):
    png = EVID / f"{name}.png"
    W.screenshot(png, monitor)
    st = uia()
    with open(EVID / f"{name}.txt", "w") as f:
        f.write("# windows of the app (winix ui windows --pid)\n" + json.dumps(st["windows"], indent=1, ensure_ascii=False)
                + "\n# UI Automation (winix ui batch: state, tree per window)\n" + json.dumps(st, indent=1, ensure_ascii=False) + "\n")
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
    log("layout", json.dumps({k: v for k, v in LAYOUT.items() if k != "path"}))
    if not LAYOUT.get("app"):
        raise SystemExit("no hikarisub.exe under out/build/windows-x64-release in the VM (run the build task)")


def main_window():
    if not APP["pid"]:
        return None
    listed = W.ui("windows", pid=APP["pid"])
    for w in listed if isinstance(listed, list) else []:
        if frame_id(w.get("title") or "") == MAIN:
            return w
    return None


def app_start(doc=None):
    """HikariSub from the VM's build with the Qt SDK it links on PATH; its
    output goes to the winix launch log."""
    r = W.ui("launch", exe=LAYOUT["app"], arguments=[doc] if doc else [], cwd=LAYOUT["docs"], noConsole=True,
             env={"PATH": LAYOUT["qt_bin"] + ";" + LAYOUT["path"], "QT_FORCE_STDERR_LOGGING": "1"})
    APP["pid"], APP["log"] = r.get("pid"), r.get("log")
    log("launched", r)
    deadline = time.time() + 90
    while time.time() < deadline:
        main = main_window()
        if main:
            time.sleep(2)
            focus_main_compositor()
            return main
        time.sleep(2)
    hidden = W.ui("windows", pid=APP["pid"], includeHidden=True)
    log("app_start: no main window after 90 s; its windows, hidden ones included:",
        json.dumps([(w.get("title"), w.get("visible")) for w in hidden] if isinstance(hidden, list) else hidden)[:600])
    return None


def app_kill():
    if APP["pid"]:
        r = W.ui("kill", pid=APP["pid"])
        if r.get("error") and "pid" not in r:
            log("kill", APP["pid"], "->", r)
        APP["pid"] = None


def app_status():
    _, lines = W.task("gate-win-status")
    s = sections(lines)
    applog = []
    if APP["log"]:
        local = EVID / ".winix" / "app.log"
        local.parent.mkdir(parents=True, exist_ok=True)
        W.pull(APP["log"], local)
        if local.exists():
            applog = local.read_text(errors="replace").splitlines()[-40:]
    return {"alive": "ALIVE yes" in lines, "layout_files": [l for l in s.get("layout files", []) if l.strip()],
            "app_log": applog}


def fresh(doc=None):
    app_kill()
    job, lines = W.task("gate-win-fresh")
    for l in lines:
        if l.startswith("set aside"):
            log(l)
            if not PROFILE["original"]:
                PROFILE["original"] = l.split(" -> ", 1)[1].rsplit("\\", 1)[0]
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


def set_focus(name):
    """The keyboard focus onto the app's element NAME (`ui setfocus`, UIA SetFocus)."""
    r = W.ui("setfocus", pid=APP["pid"], name=name)
    if r.get("error"):
        log("setfocus", name, "->", r)
    time.sleep(0.5)
    return r


def focus_main_compositor():
    """The main window in the foreground (`ui focus`)."""
    main = main_window()
    if not main:
        return False
    W.ui("focus", pid=APP["pid"], window=f"hwnd:{main['hwnd']}")
    time.sleep(0.8)
    return True


def focus_main():
    """F6 until a panel of the main window has the focus (the shell activates
    the window of the panel it moves to); else `ui focus`."""
    for _ in range(6):
        st = uia()
        main = frames(st).get(MAIN)
        if main and main.get("active") and st["focusPath"] and panel_of(st["focusPath"]) \
                and not st["focusPath"].startswith("Window:'Line editor'") \
                and "> Window:'Line editor'" not in st["focusPath"]:
            return True
        combo("f6")
        time.sleep(0.8)
    focus_main_compositor()
    return False


def open_view():
    """Alt+W opens the View menu ("Vie&w": Alt+V stays with legacy's &Video).
    Retried until the View menu (its "Move panel…" item) shows."""
    park_pointer()
    for _ in range(3):
        keys("alt+w", 0.6)
        st = uia()
        if any("Move panel" in (n or "") for n in st["popupMenuItems"]):
            return True
        keys("escape", 0.3)
    log("open_view: the View menu did not open")
    return False


# The key that opens a submenu: Right, as on Linux. Should Right not open
# View > Panels, float_panel() records that once and switches to Return.
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
    W.ui("close", pid=APP["pid"], window=title)
    time.sleep(0.5)


def float_panel(name, tag):
    """View > Panels > NAME > Float, or View > Move panel… with Place: Float."""
    panel_menu(name, "Float")
    st = wait_for(lambda s: name in frames(s), timeout=4)
    if name in frames(st):
        if not SUBMENU["checked"]:
            SUBMENU["checked"] = True
            verdict("submenu-right-arrow", "observed",
                    f"Right on View > Panels opens its submenu and Right on {name} opens the panel's: "
                    f"View > Panels > {name} > Float with Right and Return floated it", [])
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
                    "reach the menu (Down moves the focus); `ui keys` sends Right with its scan code and the "
                    "extended-key flag. The rest of the run opens submenus with Return", ev)
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
    set_focus("Line text")
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
            f"main window brought to the foreground with `ui focus` (focus {start_b['focus']}); F6 x5 reached {reached_b} "
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


def window_combos(st, frame):
    """The combo boxes of one window (the Line editor's Style, Actor and
    Effect, empty, are in the main window)."""
    els = st.get("elements") or []
    return {e["n"]: e["val"] or next((c["val"] or c["n"] for c in els if c["p"] == e["i"] and c["t"] in ("Text", "Edit")), None)
            for e in els if e["t"] == "ComboBox" and not e["off"] and frame_id(e["win"]) == frame}


def step_move_panel():
    fresh()
    open_view()
    keys("down", "down", "return", 1.2)
    st = wait_for(lambda s: "Move panel" in frames(s))
    combos_open = window_combos(st, "Move panel")
    ev0, st = snap("move-panel-0-open", extra="# combo boxes\n" + json.dumps(combos_open, ensure_ascii=False))
    if "Move panel" not in frames(st):
        verdict("keyboard-move-panel", "failed", "View > Move panel… did not open the placement window", ev0)
        return
    verdict("move-panel-defaults", "observed" if combos_open and all(v for v in combos_open.values()) else "failed",
            f"placement window on open (Grid focused): {combos_open}", ev0)
    # Panel: Grid -> Audio (Up x2); Place: Left of (Down); Next to: Grid (Up x6, Down x3); Move.
    keys("up", "up", "tab", "down", "tab", *(["up"] * 6), *(["down"] * 3), 0.3)
    combos_set = window_combos(uia(), "Move panel")
    keys("tab", "space", 1.5)
    ev1, st = snap("move-panel-1-left-of-grid", extra="# combo boxes before Move\n" + json.dumps(combos_set, ensure_ascii=False))
    pos = {p["id"]: p for p in st["panels"]}
    a, g = pos.get("Audio"), pos.get("Grid")
    # D3: side by side their headers share a row (Audio's a title bar, the
    # Grid's a tab bar, whose content starts 8 lower), as in the Linux gate.
    ah = next((t for t in find(st, "TitleBar", "Audio") if frame_id(t["win"]) == MAIN), None)
    gh = next((t for t in find(st, "TabItem", "Grid") if frame_id(t["win"]) == MAIN), None)
    ok = a and g and a["frame"] == g["frame"] == MAIN and a["x"] < g["x"] and ah and gh and abs(ah["y"] - gh["y"]) < 3
    verdict("keyboard-move-panel", "observed" if ok else "failed",
            f"Move panel ({combos_set}) -> Audio {a and (a['x'], a['y'], a['w'], a['h'])} header "
            f"{ah and (ah['x'], ah['y'], ah['w'], ah['h'])}, Grid {g and (g['x'], g['y'], g['w'], g['h'])} header "
            f"{gh and (gh['x'], gh['y'], gh['w'], gh['h'])}; focus {st['focusPath']}", ev0 + ev1)
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


def title_ref(st, pid, frame_name):
    """(automationId, name, window hwnd) of PID's UIA title bar in FRAME_NAME, for `ui doubleclick`."""
    tb = next((t for t in st.get("elements") or [] if t["t"] == "TitleBar" and not t["off"]
               and panel_id(t["n"]) == pid and frame_id(t["win"]) == frame_name), None)
    f = frames(st).get(frame_name)
    return (tb["id"], tb["n"], f"hwnd:{f['hwnd']}") if tb and f else None


def calibrate_pointer():
    """Where the guest cursor lands (`ui calibrate`) on two targets per
    monitor, with the tablet mapped over the whole desktop (virtual) and over
    the primary monitor: the mapping that hits the targets is used."""
    global POINTER_MAP
    mons = monitors()
    points = []
    for m in mons:
        for fx, fy in ((0.25, 0.25), (0.75, 0.75)):
            x, y = m["x"] + int(m["w"] * fx), m["y"] + int(m["h"] * fy)
            for mapping in ("virtual", "primary"):
                r = W.ui("calibrate", point=f"{x},{y}", map=mapping)
                off = r.get("offset") or {}
                points.append({"monitor": m["device"], "map": mapping, "target": [x, y], "cursor": r.get("actual"),
                               "off": abs(off.get("x", 9999)) + abs(off.get("y", 9999))})
    hits = {mp: [p for p in points if p["map"] == mp and p["off"] <= 4] for mp in ("virtual", "primary")}
    POINTER_MAP = max(hits, key=lambda mp: len(hits[mp]))
    reachable = {p["monitor"] for p in hits[POINTER_MAP]}
    unreachable = [m["device"] for m in mons if m["device"] not in reachable]
    (EVID / "pointer-calibration.json").write_text(json.dumps(
        {"monitors": mons, "points": points, "map": POINTER_MAP, "unreachable": unreachable}, indent=1))
    return mons, points, reachable, unreachable


def step_pointer():
    """With real pointer input (the VM's HID tablet): the header's ⋯ menu, double-click on headers (a title
    bar, a tab), drag-to-dock with the drop highlight, and a floating window's move and resize."""
    fresh()
    # The main window opens 1280x800, larger than the 1024x768 primary, and
    # the tablet reaches the primary only: maximized, its title bars are there.
    focus_main_compositor()
    keys("win+up", 1.0)
    mons, points, reachable, unreachable = calibrate_pointer()
    used = [p for p in points if p["map"] == POINTER_MAP and p["monitor"] in reachable]
    off = [p["off"] for p in used]
    log("tablet calibration:", POINTER_MAP, off, "unreachable", unreachable)
    verdict("pointer-calibration", "observed" if off and max(off) <= 4 else "failed",
            f"`ui calibrate` with the tablet mapped over the {POINTER_MAP} area: the guest cursor landed {off} px "
            f"(|dx|+|dy|) from the targets on {sorted(reachable)}; not reachable with either mapping: {unreachable} "
            "(Windows maps the absolute HID pointer to the primary monitor) (pointer-calibration.json)",
            ["pointer-calibration.json"])
    st = uia()
    btn = next((b for b in find(st, "Button", "Audio options") if frame_id(b["win"]) == MAIN), None)
    if not btn:
        verdict("pointer-menu-undock", "not-observable", "no 'Audio options' button (the header's ⋯) found")
        return
    tx, ty = btn["x"] + btn["w"] // 2, btn["y"] + btn["h"] // 2
    pointer([(tx, ty)])
    st = wait_for(lambda s: "Undock" in s["popupMenuItems"], timeout=4)
    ev0, st = snap("ptr-1-menu-open")
    items = menu_items(st)
    undock = next((e for e in find(st, "MenuItem", "Undock")), None)
    if undock:
        pointer([(undock["x"] + undock["w"] // 2, undock["y"] + undock["h"] // 2)])
        time.sleep(1.2)
    st = wait_for(lambda s: "Audio" in frames(s))
    ev, st = snap("ptr-1-undocked")
    verdict("pointer-menu-undock", "observed" if undock and "Audio" in frames(st) else "failed",
            f"tablet click on the Audio header's ⋯ ('Audio options' at {tx},{ty}) opened {items}; a click on "
            f"Undock: own window={'Audio' in frames(st)}", ev0 + ev + ["pointer-calibration.json"])
    # Double-click the docked Video title bar: floats; double-click its floating title bar: docks.
    pt = title_point(st, "Video", MAIN)
    dfloat, redock, ev1, ev2, how1, how2, gaps = False, False, [], [], None, None, []
    if pt:
        vx, vy, _ = pt
        st, how1, g1 = double_click_until(vx, vy, lambda s: "Video" in frames(s), title_ref(st, "Video", MAIN))
        gaps.append(g1)
        ev1, st = snap("ptr-2-dblclick-float")
        dfloat = "Video" in frames(st)
    if dfloat:
        pt2 = title_point(st, "Video", "Video")
        if pt2:
            fx, fy, where = pt2
            log("floating Video title at", fx, fy, where)
            st, how2, g2 = double_click_until(fx, fy, lambda s: "Video" not in frames(s), title_ref(st, "Video", "Video"))
            gaps.append(g2)
            ev2, st = snap("ptr-3-dblclick-redock")
            redock = "Video" not in frames(st)
    verdict("pointer-dblclick-float-redock", "observed" if dfloat and redock else "failed",
            f"double-click on the docked Video title at {pt}: floated={dfloat} (by {how1}); double-click its "
            f"floating title: redocked={redock} (by {how2}); tablet attempts, seconds between the two clicks' "
            f"calls: {gaps}", ev1 + ev2)
    # Drag the floating Audio panel by its title bar onto the Grid's centre.
    st = uia()
    src = title_point(st, "Audio", "Audio", dx=150)
    grid = panel(st, "Grid")
    if not src or not grid or grid["frame"] != MAIN:
        verdict("pointer-drag-dock", "not-observable", f"could not locate the floating Audio window ({src}) or the Grid")
        return
    sx, sy, where = src
    gx, gy = grid["x"] + grid["w"] // 2, grid["y"] + grid["h"] // 2 - 15
    try:
        # Pressed on the title, along to the Grid's centre in 30 steps, a small
        # wiggle there, and held while the drop indicators are captured.
        pointer([(sx, sy)], hold=True)
        time.sleep(0.25)
        pointer([(sx, sy), (gx + 3, gy + 3), (gx, gy)], button="none", steps=30, delayMs=40)
        time.sleep(0.7)
        ev3, _ = snap("ptr-4-drag-over-grid")  # the drop indicators, button still held
    finally:
        release()
        time.sleep(1.5)
        pointer([(gx + 5, gy + 5)], button="none")
    ev4, st = snap("ptr-5-dropped")
    docked = "Audio" not in frames(st) and panel_frame(st, "Audio") == MAIN
    verdict("pointer-drag-dock", "observed" if docked else "failed",
            f"tablet drag of floating Audio by its title ({where}) from {sx},{sy} to the Grid centre {gx},{gy} and "
            f"release: docked={docked}, windows {list(frames(st))} (drop highlight: ptr-4-drag-over-grid.png)", ev3 + ev4)
    pointer_tab_dblclick()
    pointer_floating_move_resize()


def menu_items(st):
    """The open menu's items with their enabled state ("Undock", "Close (disabled)", ...)."""
    out = []
    for e in st.get("elements") or []:
        if e["t"] == "MenuItem" and not e["off"] and e["n"] in st["popupMenuItems"]:
            out.append(e["n"] + ("" if e["en"] else " (disabled)"))
    return out


def pointer_tab_dblclick():
    """D3: a double-click on a tab (the Grid's one-tab bar) floats the panel and one on its floating tab docks it."""
    st = uia()
    tab = next((t for t in find(st, "TabItem", "Grid") if frame_id(t["win"]) == MAIN), None)
    if not tab:
        verdict("pointer-dblclick-tab", "not-observable", "no 'Grid' tab in the main window")
        return
    x, y = tab["x"] + 18, tab["y"] + tab["h"] // 2
    st, how1, g1 = double_click_until(x, y, lambda s: "Grid" in frames(s))
    ev1, st = snap("ptr-6-dblclick-tab-float")
    floated = "Grid" in frames(st)
    how2, g2, docked, ev2 = None, [], False, []
    if floated:
        tab = next((t for t in find(st, "TabItem", "Grid") if frame_id(t["win"]) == "Grid"), None)
        if tab:
            st, how2, g2 = double_click_until(tab["x"] + 18, tab["y"] + tab["h"] // 2, lambda s: "Grid" not in frames(s))
        ev2, st = snap("ptr-7-dblclick-tab-dock")
        docked = "Grid" not in frames(st) and panel_frame(st, "Grid") == MAIN
    verdict("pointer-dblclick-tab", "observed" if floated and docked else "failed",
            f"double-click on the Grid's tab at {x},{y}: floated={floated} (by {how1}); on its floating tab: "
            f"docked={docked} (by {how2}); tablet gaps {g1} {g2}", ev1 + ev2)


def shadow_check(png, f, mons):
    """The drawn shadow on screen: around the floating window's frame (its
    window rectangle less 8 px), the outermost ring is the background
    showing through (the window is transparent there), not black, and the
    ring next to the frame is a little darker than the background."""
    from PIL import Image
    ox = min(m["x"] for m in mons)
    oy = min(m["y"] for m in mons)
    img = Image.open(EVID / png).convert("RGB")
    x0, y0, x1, y1 = f["x"] - ox, f["y"] - oy, f["x"] - ox + f["w"] - 1, f["y"] - oy + f["h"] - 1

    def mean(points):
        px = [img.getpixel(p) for p in points if 0 <= p[0] < img.width and 0 <= p[1] < img.height]
        return round(sum(sum(c) / 3 for c in px) / len(px), 1) if px else None
    ys = range(y0 + 20, y1 - 20, 7)
    outside = mean([(x0 - 3, y) for y in ys] + [(x1 + 3, y) for y in ys])
    outer = mean([(x0, y) for y in ys] + [(x1, y) for y in ys])
    inner = mean([(x0 + 7, y) for y in ys] + [(x1 - 7, y) for y in ys])
    frame = mean([(x0 + 8, y) for y in ys] + [(x1 - 8, y) for y in ys])
    crop = png.replace(".png", "-shadow.png")
    img.crop((max(0, x0 - 24), max(0, y0 - 24), min(img.width, x1 + 25), min(img.height, y0 + 120))).save(EVID / crop)
    ok = (outside is not None and outer is not None and abs(outer - outside) <= 12 and outer > 8
          and inner is not None and inner <= outside)
    return ok, {"background": outside, "outermost ring": outer, "ring by the frame": inner, "frame line": frame}, crop


def pointer_floating_move_resize():
    """D3: a floating panel is a borderless tool window (no caption or system
    frame) with a drawn shadow; dragging its header moves it (the engine's
    drag on Windows), dragging the shadow resizes it (startSystemResize)."""
    placed = fullscreen("left", width=480)
    log("main window on the left:", placed)
    try:
        time.sleep(1.0)
        mons = monitors()
        focus_main_compositor()
        float_panel("Audio", "move")
        st = wait_for(lambda s: "Audio" in frames(s), timeout=4)
        st = uia(dpi=True)
        fa = frames(st).get("Audio")
        if not fa:
            for item in ("floating-borderless", "floating-shadow", "floating-move", "floating-resize"):
                verdict(item, "not-observable", "Audio did not float")
            return
        styles = next((w for w in st["windowInfo"] if w["hwnd"] == fa["hwnd"]), {})
        style, ex = styles.get("style", 0), styles.get("exStyle", 0)
        caption, thick, tool = (style & 0x00C00000) == 0x00C00000, bool(style & 0x00040000), bool(ex & 0x80)
        system_bar = [e["path"] for e in st["elements"] if e["win"] == "Audio" and e["t"] == "TitleBar"
                      and panel_id(e["n"]) != "Audio"]
        ev0, st = snap("ptr-8-floating", extra="# window styles\n" + json.dumps(styles))
        verdict("floating-borderless", "observed" if not caption and not thick and tool and not system_bar else "failed",
                f"floating Audio window style {style:#x} (WS_CAPTION {caption}, WS_THICKFRAME {thick}), ex style "
                f"{ex:#x} (WS_EX_TOOLWINDOW {tool}); system title bars in UIA: {system_bar}", ev0)
        # Move: the header dragged over bare desktop, right of the main window.
        src = title_point(st, "Audio", "Audio", dx=60)
        main = frames(st).get(MAIN)
        prim = next(m for m in mons if m["primary"])
        left = (main["x"] + main["w"] if main else prim["x"] + 480) + 30
        sx, sy, where = src
        tx, ty = left + (sx - fa["x"]), prim["y"] + 40 + (sy - fa["y"])
        try:
            pointer([(sx, sy)], hold=True)
            time.sleep(0.25)
            pointer([(sx, sy), (tx + 3, ty + 3), (tx, ty)], button="none", steps=25, delayMs=40)
            time.sleep(0.5)
        finally:
            release()
            time.sleep(1.2)
        st = uia()
        fb = frames(st).get("Audio")
        want = (fa["x"] + tx - sx, fa["y"] + ty - sy)
        moved = bool(fb) and abs(fb["x"] - want[0]) <= 6 and abs(fb["y"] - want[1]) <= 6
        ev1, st = snap("ptr-9-floating-moved")
        if fb:
            ok, shades, crop = shadow_check(ev1[0], fb, mons)
            verdict("floating-shadow", "observed" if ok else "failed",
                    f"over the desktop, mean grey left and right of the frame {shades}: the background shows "
                    f"through the outer ring (not black) and the ring by the frame is darker ({crop})", ev1 + [crop])
        verdict("floating-move", "observed" if moved else "failed",
                f"tablet drag of the floating Audio header ({where}) from {sx},{sy} to {tx},{ty} over the desktop: "
                f"window {fa['x']},{fa['y']} -> {fb and (fb['x'], fb['y'])} (expected {want}), still floating "
                f"{bool(fb)}", ev1)
        if not fb:
            verdict("floating-resize", "not-observable", "the floating Audio window is gone after the move")
            return
        # The floating header's ⋯ by the tablet: its menu (Main.qml's, in the
        # main window) opens under the button, in the floating panel's place.
        btn = next((b for b in find(st, "Button", "Audio options") if frame_id(b["win"]) == "Audio"), None)
        dock_item, ev2 = None, []
        if btn:
            pointer([(btn["x"] + btn["w"] // 2, btn["y"] + btn["h"] // 2)])
            st = wait_for(lambda s: "Dock" in s["popupMenuItems"], timeout=4)
            ev2, st = snap("ptr-9-floating-menu")
            dock_item = next(iter(find(st, "MenuItem", "Dock")), None)
            keys("escape", 0.5)
        below = bool(btn and dock_item) and btn["y"] + btn["h"] - 4 <= dock_item["y"] <= btn["y"] + btn["h"] + 80 \
            and abs(dock_item["x"] - btn["x"]) <= 320
        verdict("floating-menu-position", "observed" if below else "failed",
                f"tablet click on the floating Audio's ⋯ {btn and (btn['x'], btn['y'], btn['w'], btn['h'])}: its "
                f"menu's Dock item at {dock_item and (dock_item['x'], dock_item['y'])} (expected just below the "
                f"button); menu {menu_items(st) if btn else None}", ev2)
        # Resize: the left edge (in the shadow) 90 px out, then the top edge 30 px up.
        shots, out = [], []
        top_x = max(fb["x"] + 30, min(fb["x"] + fb["w"] // 2, prim["x"] + prim["w"] - 20))
        for edge, (px, py), (dx, dy) in (("left", (fb["x"] + 4, fb["y"] + fb["h"] // 2), (-90, 0)),
                                         ("top", (top_x, fb["y"] + 4), (0, -30))):
            before = frames(uia()).get("Audio")
            try:
                pointer([(px, py)], hold=True)
                time.sleep(0.3)
                pointer([(px, py), (px + dx, py + dy)], button="none", steps=15, delayMs=40)
                time.sleep(0.4)
            finally:
                release()
                time.sleep(1.2)
            after = frames(uia()).get("Audio")
            out.append({"edge": edge, "pressed": [px, py], "by": [dx, dy],
                        "before": before and [before["x"], before["y"], before["w"], before["h"]],
                        "after": after and [after["x"], after["y"], after["w"], after["h"]]})
            ev, _ = snap(f"ptr-10-resized-{edge}")
            shots += ev
        grew = [abs((o["after"][2] - o["before"][2]) - abs(o["by"][0])) <= 6
                and abs((o["after"][3] - o["before"][3]) - abs(o["by"][1])) <= 6 if o["before"] and o["after"] else False
                for o in out]
        verdict("floating-resize", "observed" if all(grew) else "failed",
                f"tablet drags on the shadow edges of the floating Audio window: {out}", shots)
    finally:
        fullscreen("off")


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


def fullscreen(mode, width=None):
    env = {"GATE_FULLSCREEN": mode}
    if width:
        env["GATE_LEFT_WIDTH"] = width
    _, lines = W.task("gate-win-fullscreen", env=env)
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
    set_focus("Line text")
    st = uia()
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
    ok = len(exits) == 4 and all(code == "0" for _, code in exits)
    summary = "; ".join(re.findall(r"Totals: [^,]+, [^,]+, [^,]+", text))
    skips = re.findall(r"SKIP\s*:.*", text)
    # Skips declared as platform limits, each observed for real by another
    # item (as in the Linux gate): a synthetic drag between windows needs the
    # offscreen platform (pointer).
    known = ("the compositor places windows", "a synthetic drag between windows needs the offscreen platform")
    unexpected = [k for k in skips if not any(reason in k for reason in known)]
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
    header_how, ev, st = None, [], {"frames": []}
    try:
        focus_main_compositor()
        f6_walk(4)
        open_view()
        k = SUBMENU["key"]
        keys("down", 0.6, k, 0.6, "down", 0.6, "down", 0.6, k, 0.6, "down", 0.6, "down", 0.6, "return", 2.0)
        f6_walk(3)
        ev, st = snap("nvda-0-after-float")
        # D3: the Grid's tab, Right to its ⋯ button, Space opens the menu,
        # Down reaches its first item (as the Linux gate's Orca step does).
        focus_main_compositor()
        header_how, _ = focus_header_control("TabItem", "Grid", "Grid")
        keys("right", 0.8, "space", 1.2, "down", 0.8)
        ev += snap("nvda-1-header-menu")[0]
        keys("escape", 0.8)
        time.sleep(2)
    finally:
        job, lines = W.task("nvda-output", env={"GATE_NVDA_LOG": nvda_log})
    s = sections(lines)
    speech = [l.strip() for l in s.get("speech", []) if l.strip()]
    (EVID / "nvda-speech.txt").write_text("\n".join(speech) + "\n")
    (EVID / "nvda-output.txt").write_text("\n".join(lines) + "\n")
    W.pull(LAYOUT["nvda_dir"] + "\\nvda-last.log", EVID / "nvda.log")
    got_log = (EVID / "nvda.log").exists()
    synth = s.get("synth", [])
    silent = any("silence" in l for l in synth)
    spoke = [p for p in ("Video", "Audio", "Line editor", "Grid", "Panels", "Float", "Grid options", "Move panel",
                         "Undock") if any(p in l for l in speech)]
    evidence = ["nvda-speech.txt", "nvda-output.txt"] + (["nvda.log"] if got_log else []) + ev
    header_spoken = all(any(p in l for l in speech) for p in ("Grid options", "Move panel"))
    verdict("nvda-header-menu", "observed" if header_spoken else "failed",
            f"NVDA spoke the ⋯ button's name and the menu: {header_spoken} ('Grid options', 'Move panel')",
            ["nvda-speech.txt"])
    verdict("nvda", "observed" if speech and silent else "failed",
            f"{len(speech)} speech lines; panel/menu names spoken: {spoke}; synthesizer lines {synth[:3]} "
            f"(silent: {silent}); floating windows after the menu {list(frames(st))}; the Grid tab reached by "
            f"{header_how}, then Right and Space on its ⋯ (nvda-speech.txt; full log nvda.log)",
            evidence)
    verdict("nvda-float-from-menu", "observed" if "Line editor" in frames(st) else "failed",
            f"with NVDA running, View > Panels > Line editor > Float from the keyboard: windows {list(frames(st))}", ev)


def inside(f, m):
    """Whether window F's centre lies on monitor M."""
    cx, cy = f["x"] + f["w"] // 2, f["y"] + f["h"] // 2
    return m["x"] <= cx < m["x"] + m["w"] and m["y"] <= cy < m["y"] + m["h"]


def head_shot(name, monitor):
    """One monitor's pixels (`ui screenshot --monitor N`; the worker is per-monitor DPI aware)."""
    out = EVID / f"{name}.png"
    W.screenshot(out, monitor)
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
        how = float_panel("Audio", "outputs")
        st = wait_for(lambda s: "Audio" in frames(s), timeout=4)
        before = frames(st).get("Audio")
        # Windows offers 150 % only on a larger mode (about 1920x1200 and up).
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
            # Can the tablet reach the second monitor (`ui calibrate` over the whole desktop)?
            cal = W.ui("calibrate", point=f"{tx},{ty}", map="virtual")
            off = cal.get("offset") or {}
            if abs(off.get("x", 9999)) + abs(off.get("y", 9999)) <= 4:
                try:
                    W.ui("pointer", path=f"{sx},{sy}", map="virtual", hold=True)
                    time.sleep(0.25)
                    W.ui("pointer", path=f"{sx},{sy};{tx},{ty}", map="virtual", button="none", steps=30, delayMs=40)
                    time.sleep(0.5)
                finally:
                    release()
                    time.sleep(1.5)
                moved_by = f"tablet drag of its title bar from {sx},{sy} to {tx},{ty}"
            else:
                # Windows maps the tablet to the primary only, and a borderless
                # tool window (D3) takes no Win+Shift+Right. With the main
                # window on the monitor's left (bare desktop to drop on), the
                # header's title is dragged (the engine's drag) to the
                # primary's right edge: the window's centre goes onto the
                # second monitor.
                placed = fullscreen("left", width=480)
                time.sleep(1)
                st = uia()
                src = title_point(st, "Audio", "Audio", dx=20)
                ex, ey = prim["x"] + prim["w"] - 3, prim["y"] + 120
                if src:
                    sx, sy, _ = src
                    try:
                        pointer([(sx, sy)], hold=True)
                        time.sleep(0.25)
                        pointer([(sx, sy), (ex - 3, ey + 3), (ex, ey)], button="none", steps=25, delayMs=40)
                        time.sleep(0.5)
                    finally:
                        release()
                        time.sleep(1.5)
                moved_by = (f"a tablet drag of its title from {src and src[:2]} to the primary's right edge {ex},{ey}, "
                            f"the main window on the left ({placed}); the tablet does not reach the second monitor: "
                            f"`ui calibrate` {tx},{ty} landed at {cal.get('actual')}")
        st = wait_for(lambda s: "Audio" in frames(s) and inside(frames(s)["Audio"], sec), timeout=6)
        st = uia(dpi=True)
        fa = frames(st).get("Audio")
        ev0, st = snap("outputs-0-on-scale2", extra="# DISPLAY_RESULT\n" + json.dumps(d2))
        ev0 += head_shot("outputs-0-monitor2", 1) + head_shot("outputs-0-monitor1", 0)
        on2 = bool(fa) and inside(fa, sec)
        ratio = round(fa["w"] / before["w"], 2) if fa and before and before["w"] else None
        verdict("mixed-dpi", "observed" if on2 and sec.get("dpi") == 144 and fa.get("dpi") == 144 and prim.get("dpi") == 96
                else "failed",
                f"floating Audio (floated by {how}) moved by {moved_by} onto {sec['device']} at {sec['dpi']} dpi "
                f"(primary {prim['device']} at {prim['dpi']}): on it={on2}, window {fa and (fa['x'], fa['y'], fa['w'], fa['h'])}, "
                f"GetDpiForWindow {fa and fa.get('dpi')} (96 would mean bitmap-scaled by Windows), width x{ratio} of "
                f"its 100 % width {before and before['w']}; second monitor {sec['w']}x{sec['h']}, scale call {d2.get('scale2')} "
                "(outputs-0-monitor2.png: that monitor's pixels)", ev0)
        # Monitor removal with the panel on the second monitor.
        d3 = displays("detach")
        left = d3.get("monitors") or []
        st = wait_for(lambda s: "Audio" in frames(s) and len(left) == 1 and inside(frames(s)["Audio"], left[0]), timeout=8)
        ev1, st = snap("outputs-1-after-removal", extra="# DISPLAY_RESULT\n" + json.dumps(d3))
        st = uia(dpi=True)
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
        fullscreen("off")  # the main window back where it was, if step moved it
        r = displays("extend", scale2=100)
        ok = len(r.get("monitors") or []) == 2 and all(m["dpi"] == 96 and m["w"] == 1024 and m["h"] == 768
                                                      for m in r["monitors"])
        (EVID / "outputs-restored.txt").write_text(json.dumps(r, indent=1))
        verdict("outputs-restored", "observed" if ok else "failed",
                f"both monitors back at 1024x768 (vm.displays), 100 %, extended: {[(m['device'], m['x'], m['w'], m['h'], m['dpi']) for m in r.get('monitors') or []]}",
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
        ev1, st = snap("dpi-1-at-150", extra="# DPI_RESULT\n" + json.dumps(res))
        set_focus("Line text")
        keys("end")
        type_text(" hidpi")
        time.sleep(0.8)
        txt = uia()["texts"].get("Line text")
        both = MAIN in frames(st) and "Line editor" in frames(st)
        verdict("dpi-scale-change", "observed" if both and txt and txt.endswith(" hidpi") else "failed",
                f"scale 100 -> {res.get('after')} % live ({res}); floating Line editor (floated by {how}) and main "
                f"window still shown: {both}; typing there gives {txt!r} (dpi-1-at-150*.png)",
                ev0 + ev1)
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
                     + (" off" if e["off"] else "") + ("" if e["en"] else " disabled")
                     + (f" val={e['val']!r}" if e["val"] is not None else "") + (f" hwnd={e['hw']}" if e["hw"] else "")
                     + f"  <{e['win']}>" for e in els)
    (EVID / "a11y-tree.txt").write_text(tree + "\n")
    table = find(st, "Table", "Subtitle lines") or find(st, "DataGrid", "Subtitle lines")
    grid_panel = [p for p in st["panels"] if p["id"] == "Grid"]
    in_grid = bool(table) and panel_of(table[0]["path"]) == "Grid"
    verdict("grid-accessible", "observed" if in_grid and grid_panel else "failed",
            f"table 'Subtitle lines': {[t['path'] for t in table[:1]]}; Grid panel: {grid_panel[:1]}", ["a11y-tree.txt"])
    # D3: one header per group. A lone panel's is a title bar named after it,
    # tabs sit in a tab list ("Panels"), and the ⋯ button is "<panel> options".
    headers = {n: [e["path"] for e in find(st, "TitleBar", n)] for n in ("Video", "Audio", "Line editor")}
    grid_tab = [e["path"] for e in find(st, "TabItem", "Grid")]
    buttons = {n: bool(find(st, "Button", f"{n} options")) for n in ("Video", "Audio", "Line editor", "Grid")}
    old = [n for n in ("Float Audio", "Close Audio", "Float Grid", "Close Grid") if find(st, "Button", n)]
    # The header's title is its name only once: no Text repeats it inside the header.
    repeats = [el_path(els, e) for e in els if e["t"] == "Text" and e["n"] in ("Video", "Audio", "Line editor", "Grid")
               and any(a["t"] in ("TitleBar", "Tab", "TabItem") for a in ancestors(els, e))]
    (EVID / "a11y-headers.txt").write_text(json.dumps({"title bars": headers, "grid tab": grid_tab, "menu buttons": buttons,
                                                       "old float/close buttons": old, "repeated titles": repeats},
                                                      indent=1, ensure_ascii=False))
    verdict("headers-accessible",
            "observed" if all(headers.values()) and grid_tab and all(buttons.values()) and not old and not repeats
            else "failed",
            f"title bars { {n: bool(v) for n, v in headers.items()} }; Grid tab {grid_tab[:1]}; ⋯ buttons {buttons}; "
            f"float/close buttons left over: {old}; titles repeated as text: {repeats}", ["a11y-headers.txt", "a11y-tree.txt"])
    # The ⋯ button and the menu's items pressed through the Invoke pattern.
    st = uia(do=[{"action": "invoke", "name": "Audio options", "type": "Button", "after": 1000}])
    by_invoke = bool(st["actions"]) and st["actions"][0].get("done") == ["Invoke"]
    items = menu_items(st)
    ev0, _ = snap("a11y-0-audio-menu")
    st = uia(do=[{"action": "invoke", "name": "Undock", "type": "MenuItem"}])
    undock = st["actions"]
    st = wait_for(lambda s: "Audio" in frames(s))
    floated = "Audio" in frames(st)
    ev1, st = snap("a11y-1-undocked-by-uia")
    st = uia(do=[{"action": "invoke", "name": "Audio options", "type": "Button", "window": "Audio", "after": 1000}])
    items_floating = menu_items(st)
    uia(do=[{"action": "invoke", "name": "Dock", "type": "MenuItem"}])
    st = wait_for(lambda s: "Audio" not in frames(s))
    docked = "Audio" not in frames(st) and panel_frame(st, "Audio") == MAIN
    want = ["Move panel…", "Undock", "Close"]
    verdict("panel-menu-accessible",
            "observed" if by_invoke and items == want and floated and "Dock" in items_floating and docked else "failed",
            f"'Audio options' pressed through Invoke: {by_invoke}; its menu {items} (expected {want}); 'Undock' "
            f"invoked ({undock}) -> own window={floated}; the floating panel's menu {items_floating}; 'Dock' "
            f"invoked -> docked={docked}", ev0 + ev1)
    focus_main()
    panel_menu("Timing", "Show")
    time.sleep(1)
    st = uia()
    # Timing is titled Shift times (its tab, its "⋯" and its window).
    tabs = {n: find(st, "TabItem", n) for n in ("Line editor", "Shift times")}
    lists = [el_path(st["elements"], e) for e in st["elements"] if e["t"] == "Tab" and e["n"] == "Panels" and not e["off"]]
    tab_button = bool(find(st, "Button", "Shift times options"))
    ev2, st = snap("a11y-2-tabs")
    uia(do=[{"action": "invoke", "name": "Shift times options", "type": "Button", "after": 1000},
            {"action": "invoke", "name": "Undock", "type": "MenuItem"}])
    st = wait_for(lambda s: "Shift times" in frames(s))
    tab_floated = "Shift times" in frames(st)
    ev3, st = snap("a11y-3-undock-timing-tab")
    (EVID / "a11y-tabs.txt").write_text(json.dumps(
        {"tabs": {n: [h["path"] for h in hits] for n, hits in tabs.items()}, "tab lists": lists,
         "menu button": tab_button}, indent=1, ensure_ascii=False))
    verdict("tabs-accessible",
            "observed" if all(tabs.values()) and lists and tab_button and tab_floated else "failed",
            f"tab items { {n: bool(h) for n, h in tabs.items()} } in a tab list 'Panels' ({len(lists)}); the selected "
            f"tab's 'Shift times options': {tab_button}; its menu's Undock invoked -> own window={tab_floated}",
            ev2 + ev3 + ["a11y-tabs.txt"])


def focus_header_control(ctype, name, panel_name, tries=12):
    """The keyboard focus onto a header control the way a keyboard user gets
    there: F6 to the panel, then Shift+Tab (the header is the panel's first
    stop). Returns how it got there (or UI Automation's SetFocus as a last
    resort) and the focus path."""
    for _ in range(6):
        if panel_of(uia()["focusPath"]) == panel_name:
            break
        combo("f6")
        time.sleep(0.6)
    want = f"{ctype}:'{name}'"
    for i in range(tries):
        st = uia()
        if (st["focusPath"] or "").endswith(want):
            return f"F6, Shift+Tab x{i}", st["focusPath"]
        combo("shift", "tab")
        time.sleep(0.4)
    uia(do=[{"action": "focus", "name": name, "type": ctype}])
    st = uia()
    return ("UIA SetFocus" if (st["focusPath"] or "").endswith(want) else None), st["focusPath"]


def step_header():
    """D3 from the keyboard: the ⋯ menu from a tab (Right, then Space), from
    Shift+F10 on a tab and from a title bar's button (Return); Undock and Dock
    from the menu."""
    fresh(LAYOUT["episode"])
    dismiss_notices()
    park_pointer()
    focus_main_compositor()
    how, path = focus_header_control("TabItem", "Grid", "Grid")
    keys("right", 0.4)
    on_button = (uia()["focusPath"] or "").endswith("Button:'Grid options'")
    keys("space", 0.8)
    st = uia()
    items = menu_items(st)
    ev0, _ = snap("header-0-grid-menu-by-keys")
    keys("escape", 0.5)
    back = uia()["focusPath"]
    keys("left", 0.3, "shift+f10", 0.8)
    items_f10 = menu_items(uia())
    ev1, _ = snap("header-1-shift-f10")
    keys("escape", 0.5)
    want = ["Move panel…", "Undock", "Close"]
    verdict("header-menu-keyboard",
            "observed" if how and on_button and [i.split(" (")[0] for i in items] == want and items_f10 else "failed",
            f"the Grid tab focused by {how} ({path}); Right -> on 'Grid options'={on_button}; Space opened {items}; "
            f"Escape left the focus at {back}; Left, Shift+F10 opened {items_f10}", ev0 + ev1)
    # A lone panel: its title bar's ⋯ button, Return, Down to Undock, Return.
    how2, path2 = focus_header_control("Button", "Audio options", "Audio")
    keys("return", 0.8)
    items_audio = menu_items(uia())
    keys("down", "down", "return", 1.5)
    st = wait_for(lambda s: "Audio" in frames(s), timeout=4)
    floated = "Audio" in frames(st)
    ev2, st = snap("header-2-audio-undocked-by-keys")
    docked, how3 = False, None
    if floated:
        how3, _ = focus_header_control("Button", "Audio options", "Audio")
        keys("return", 0.8, "down", "down", "return", 1.5)
        st = wait_for(lambda s: "Audio" not in frames(s), timeout=4)
        docked = "Audio" not in frames(st) and panel_frame(st, "Audio") == MAIN
    ev3, st = snap("header-3-audio-docked-by-keys")
    verdict("header-menu-undock-dock-keyboard", "observed" if how2 and floated and docked else "failed",
            f"'Audio options' focused by {how2} ({path2}); Return opened {items_audio}; Down Down Return: own "
            f"window={floated}; in the floating window ('Audio options' by {how3}) Return Down Down Return: "
            f"docked={docked}; focus {st['focusPath']}", ev2 + ev3)


STEPS = {"default": step_default, "kbd": step_keyboard_float_dock, "f6": step_f6_floating, "move": step_move_panel,
         "pointer": step_pointer, "video": step_video, "persist": step_persistence, "fullscreen": step_fullscreen,
         "outputs": step_outputs, "nvda": step_nvda, "menutext": step_menu_from_text,
         "tests": step_test_executables, "a11y": step_a11y, "header": step_header, "dpi": step_dpi}


def main():
    global W, LOG
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("steps", nargs="*", help=f"steps (default: all but dpi): {', '.join(STEPS)}")
    ap.add_argument("--config", default=str(ROOT / "winix.yaml"))
    ap.add_argument("--vm", default="dev")
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
        release()  # never leave the left button held
        app_kill()
        if PROFILE["original"]:
            _, lines = W.task("gate-win-restore", env={"GATE_PROFILE_FROM": PROFILE["original"]})
            log("profile:", " | ".join(l for l in lines if l.startswith(("restored", "set aside"))))


if __name__ == "__main__":
    main()
