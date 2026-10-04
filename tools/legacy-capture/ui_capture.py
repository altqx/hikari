#!/usr/bin/env python3
"""F1/F3: the legacy Find and replace dialog and the spell checker, driven.

Each case of plan.json's "ui" section runs a fresh copy of the legacy Linux
package on a private X server (as drive.py's other routes): a Config.txt
with the case's options (FIND_REPLACE_OPTIONS, FIND_REPLACE_STYLES, the
dictionary), the case's documents, Autoload emptied but for
automation/state-dump.lua, and the fixture dictionaries
(tests/fixtures/spelling) in the app's Dictionary folder. The steps are
semantic, so the rewrite can replay the same plan:

  select     rows (grid clicks: the first plain, the rest with Ctrl; the last
             one clicked is the active Line)
  open       "find" (Ctrl+F) or "replace" (Ctrl+H)
  find_text / replace_text   typed into the dialog's field
  find       Return in the search field (FindReplaceDialog::OnEnterConfirm)
  replace_next, replace_all, find_all_current, find_in_all_open
             the dialog's buttons (plan positions relative to the dialog)
  results_replace   types into the Search results' replace field and
             presses its Replace button (Replace checked)
  answer     "yes" (Return: the first button has the focus) or "no"
             (Escape) to the open message box; its title and a screenshot of
             it are kept
  dump       runs the state-dump macro (Automation > Hikari state dump, by
             menu hover and Return as the S3 route; first a click on the
             Effect field, plan "focus_at", so the frame has a focused
             window after a message box) and keeps its JSON line:
             the Lines, selected rows, active row and the Line editor's
             selection (aegisub.gui.get_selection: 1-based, end exclusive)
  spell_open Subtitles > Check spelling
  spell_walk dump, then the Spellchecker's Ignore, until "No spelling errors
             were found" (or the plan's limit); each dump is one misspelled
             word: the active row and the editor selection SetNextMisspell
             made (posStart, posEnd + 1)
  shot       a screenshot of the screen or of a named window

A dump step with "may_hang" records an app that stops answering (a legacy
hang the case expects) as its observation. A watchdog kills the app when
its resident memory passes "memory_limit_mb" (1024 by default) and
records it: the End-of-text hang grows without bound. The plan's and the case's "env"
are added to the app's environment (LANG: legacy's wxString::Lower follows
the C library's locale, so the plan runs under C.UTF-8 and one case under C).

After every step the visible windows are listed; a new message box is
recorded (title, screenshot) and stays open until an "answer" step. A step
that gets no dump back (the app busy or blocked) is recorded as such and
the case ends. What is recorded is what the old app did; it is not an
expectation for the rewrite.
"""
import argparse, hashlib, json, os, platform, re, shutil, subprocess, tarfile, tempfile, threading, time
from pathlib import Path

MAIN = "HikariSub v"
# Non-modal tool windows that stay open between steps.
TOOL_WINDOWS = {"Find", "Find and replace", "Find in subtitles", "Search results", "Spellchecker"}
# Message boxes and the log window are recorded and answered by "answer" steps.
# Config.txt: a header that does not match the build's program name makes
# legacy load its defaults first; SetRawOptions then needs more than ten
# entries, so a few defaults are written out with the case's options.
BASE_CONFIG = [
    "AUTOSAVE_MAX_FILES=3", "EDITOR_ON=true", "GRID_CHANGE_ACTIVE_ON_SELECTION=true", "GRID_FONT=Tahoma",
    "GRID_FONT_SIZE=10", "PROGRAM_FONT=Tahoma", "PROGRAM_FONT_SIZE=10", "PROGRAM_THEME=DarkSentro",
    "SHIFT_TIMES_ON=true", "TEXT_EDITOR_FONT_SIZE=10", "WINDOW_POSITION=0,0", "WINDOW_SIZE=1000,700",
]


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class X:
    def __init__(self, display):
        self.env = {**os.environ, "DISPLAY": display}

    def __call__(self, *cmd, timeout=30):
        try:
            return subprocess.run(list(cmd), capture_output=True, text=True, env=self.env, timeout=timeout).stdout
        except subprocess.TimeoutExpired:
            return ""

    def windows(self):
        out = []
        for w in self("xdotool", "search", "--onlyvisible", "--name", ".").split():
            name = self("xdotool", "getwindowname", w).strip()
            if name:
                out.append((w, name))
        return out

    def geometry(self, wid):
        o = self("xdotool", "getwindowgeometry", wid)
        p = re.search(r"Position: (-?\d+),(-?\d+)", o)
        g = re.search(r"Geometry: (\d+)x(\d+)", o)
        return (int(p[1]), int(p[2]), int(g[1]), int(g[2])) if p and g else None

    def click(self, x, y, *mods):
        if mods:
            self("xdotool", "mousemove", str(x), str(y))
            self("xdotool", "keydown", *mods)
            self("xdotool", "click", "1")
            self("xdotool", "keyup", *mods)
        else:
            self("xdotool", "mousemove", str(x), str(y), "click", "1")

    def key(self, chord):
        self("xdotool", "key", "--clearmodifiers", chord)

    def type(self, text):
        self("xdotool", "type", "--delay", "40", text, timeout=60)


class MemoryWatch:
    """Kills the app when its resident memory passes the limit.

    Legacy's End-of-text Find all never leaves the Line and appends results
    without end (about 0.8 GB a second): left alone it takes the whole
    machine, and on a GitHub runner the job is lost with its artifacts. The
    kill is recorded with the case; it is the observation of that hang."""

    def __init__(self, proc, limit_mb):
        self.proc, self.limit_kb = proc, limit_mb * 1024
        self.killed = None
        self.peak_kb = 0
        self.started = time.time()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._watch, daemon=True)
        self._thread.start()

    def _watch(self):
        status = Path(f"/proc/{self.proc.pid}/status")
        while not self._stop.is_set() and self.proc.poll() is None:
            try:
                rss = next((int(l.split()[1]) for l in status.read_text().splitlines() if l.startswith("VmRSS:")), 0)
            except (OSError, ValueError):
                rss = 0
            self.peak_kb = max(self.peak_kb, rss)
            if rss > self.limit_kb:
                self.proc.kill()
                self.killed = {"rss_mb": rss // 1024, "after_seconds": round(time.time() - self.started, 1)}
                return
            self._stop.wait(0.1)

    def stop(self):
        self._stop.set()
        self._thread.join(timeout=2)


class Case:
    def __init__(self, spec, case, package, x, workdir, plan_dir):
        self.spec, self.case, self.x, self.workdir = spec, case, x, workdir
        self.pos = spec["positions"]
        self.scratch = Path(tempfile.mkdtemp(prefix="hikari-ui-"))
        with tarfile.open(package) as t:
            t.extractall(self.scratch, filter="data")
        self.app = next(self.scratch.glob("*/hikarisub"))
        (self.scratch / "home").mkdir()
        autoload = self.app.parent / "Automation" / "automation" / "Autoload"
        for p in autoload.glob("*"):
            if p.is_file():
                p.unlink()
            else:
                shutil.rmtree(p)
        shutil.copyfile(plan_dir / spec["dump_script"], autoload / "state-dump.lua")
        dictionary = self.app.parent / "Dictionary"
        dictionary.mkdir(exist_ok=True)
        for f in sorted((plan_dir / spec["dictionaries"]).glob("*")):
            if f.suffix in (".dic", ".aff"):
                shutil.copyfile(f, dictionary / f.name)
        config = self.app.parent / "Config"
        config.mkdir(exist_ok=True)
        opts = dict(spec.get("config", {}))
        opts.update(case.get("config", {}))
        lines = ["[HikariSub v0.0.1]"] + BASE_CONFIG + [f"{k}={v}" for k, v in opts.items()]
        (config / "Config.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        self.docs = []
        (self.scratch / "doc").mkdir()
        for name in case["inputs"]:
            src = plan_dir / "inputs" / name
            doc = self.scratch / "doc" / name
            shutil.copyfile(src, doc)
            # Legacy's Linux build compares the file's time (with its
            # milliseconds) with the load time in whole seconds, so a document
            # written in the second it is opened asks "Subtitles were
            # modified by another program. Reload?" (ModificationChecker.cpp).
            old = time.time() - 120
            os.utime(doc, (old, old))
            self.docs.append(doc)
        self.out = self.scratch / "capture.jsonl"
        self.out.touch()
        self.main = None
        self.shots = 0

    def shot(self, label, wid=None):
        self.shots += 1
        name = f"{self.case['id']}-{self.shots:02d}-{label}"
        path = self.workdir / "screens" / f"{name}.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        self.x("import", "-window", wid or "root", str(path))
        return str(path.relative_to(self.workdir))

    def window(self, title):
        return next((w for w, n in self.x.windows() if n == title), None)

    def popups(self):
        return [(w, n) for w, n in self.x.windows() if MAIN not in n and n not in TOOL_WINDOWS and n != "hikarisub"]

    def dialog(self):
        for title in ("Find", "Find and replace", "Find in subtitles"):
            w = self.window(title)
            if w:
                return w, title
        return None, None

    def at(self, wid, rel):
        g = self.x.geometry(wid)
        return g[0] + rel[0], g[1] + rel[1]

    def menu(self, label_at, item_at, sub_at=None):
        """Opens an app-drawn menu by its label and runs an item by hover and Return."""
        x = self.x
        x("xdotool", "windowactivate", "--sync", self.main)
        time.sleep(0.3)
        x.click(*label_at)
        time.sleep(1)
        # The menu bar follows the pointer's x even over an open menu, so the
        # item is approached from below its own label.
        x("xdotool", "mousemove", str(item_at[0]), str(item_at[1]))
        time.sleep(0.7)
        if sub_at:
            x("xdotool", "mousemove", str(sub_at[0]), str(item_at[1]))
            time.sleep(0.5)
            x("xdotool", "mousemove", str(sub_at[0] + 5), str(sub_at[1]))
            time.sleep(0.5)
        else:
            x("xdotool", "mousemove", str(item_at[0] + 5), str(item_at[1]))
            time.sleep(0.5)
        x("xdotool", "windowfocus", "--sync", self.main)
        time.sleep(0.3)
        x.key("Return")

    def dump(self, timeout=20):
        before = len(self.out.read_text(encoding="utf-8").splitlines())
        if self.proc.poll() is not None or self.popups():
            return None
        p = self.pos
        # After a message box closes, legacy's main frame no longer passes
        # keys to the menu bar (no wx window has the focus); a click on the
        # Line editor's Effect field gives one the focus without editing.
        self.x("xdotool", "windowactivate", "--sync", self.main)
        self.x.click(*p["focus_at"])
        time.sleep(0.4)
        self.menu(p["automation_menu_at"], p["dump_script_at"], p["dump_macro_at"])
        deadline = time.time() + timeout
        while time.time() < deadline:
            got = self.out.read_text(encoding="utf-8").splitlines()
            if len(got) > before:
                return json.loads(got[before])
            time.sleep(0.25)
        return None

    def run_step(self, step):
        x, p = self.x, self.pos
        do = step["do"]
        rec = {"do": do}
        rec.update({k: v for k, v in step.items() if k != "do"})
        if do == "select":
            x0, y0 = p["grid_row0_at"]
            h = p["grid_row_height"]
            x("xdotool", "windowactivate", "--sync", self.main)
            for i, r in enumerate(step["rows"]):
                x.click(x0, y0 + h * r, *(["ctrl"] if i else []))
                time.sleep(0.4)
        elif do == "open":
            x("xdotool", "windowactivate", "--sync", self.main)
            time.sleep(0.3)
            x.key("ctrl+f" if step["tab"] == "find" else "ctrl+h")
            time.sleep(1.5)
        elif do in ("find_text", "replace_text"):
            wid, title = self.dialog()
            x("xdotool", "windowactivate", "--sync", wid)
            time.sleep(0.4)
            if do == "replace_text":
                x.click(*self.at(wid, p["dialog"]["replace"]["replace_text"]))
                time.sleep(0.3)
            x.key("ctrl+a")
            x.key("BackSpace")
            if step["text"]:
                x.type(step["text"])
            time.sleep(0.3)
        elif do == "find":
            wid, title = self.dialog()
            x("xdotool", "windowactivate", "--sync", wid)
            time.sleep(0.4)
            x.key("Return")
        elif do in ("replace_next", "replace_all", "find_all_current", "find_in_all_open"):
            wid, title = self.dialog()
            tab = "replace" if title == "Find and replace" else "find"
            x("xdotool", "windowactivate", "--sync", wid)
            time.sleep(0.4)
            x.click(*self.at(wid, p["dialog"][tab][do]))
        elif do == "results_replace":
            wid = self.window("Search results")
            x("xdotool", "windowactivate", "--sync", wid)
            time.sleep(0.4)
            x.click(*self.at(wid, p["results"]["replace_text"]))
            time.sleep(0.3)
            x.key("ctrl+a")
            x.key("BackSpace")
            x.type(step["text"])
            time.sleep(0.3)
            x.click(*self.at(wid, p["results"]["replace"]))
        elif do == "answer":
            pops = self.popups()
            if not pops:
                rec["status"] = "no-message-box"
                return rec
            wid, title = pops[-1]
            rec["title"] = title
            rec["screenshot"] = self.shot("message", wid)
            x("xdotool", "windowactivate", "--sync", wid)
            time.sleep(0.3)
            x.key("Return" if step["answer"] in ("yes", "ok") else "Escape")
        elif do == "dump":
            d = self.dump(step.get("timeout", 20))
            rec["state"] = d
            if d is None and step.get("may_hang"):
                # The case expects the app to stop answering (a legacy hang):
                # that is the observation.
                rec["response"] = "none"
                rec["screenshot"] = self.shot("no-response")
                return rec | {"status": "done"}
            if d is None:
                rec["status"] = "no-dump"
                rec["screenshot"] = self.shot("no-dump")
                return rec
        elif do == "spell_open":
            self.menu(p["subtitles_menu_at"], p["check_spelling_at"])
            time.sleep(2)
        elif do == "spell_walk":
            rec["words"] = []
            for _ in range(step.get("limit", 80)):
                time.sleep(0.8)
                if any(n == "Warning" for _, n in self.x.windows()):
                    rec["end"] = "Warning"
                    break
                d = self.dump()
                if d is None:
                    rec["end"] = "no-dump"
                    break
                wid = self.window("Spellchecker")
                word = {"active": d["active"], "editor_selection": d["editor_selection"]}
                if step.get("screens"):
                    word["screenshot"] = self.shot("spell", wid)
                rec["words"].append(word)
                rec["last_state"] = d
                x("xdotool", "windowactivate", "--sync", wid)
                time.sleep(0.4)
                x.click(*self.at(wid, p["spellchecker"]["ignore"]))
            else:
                rec["end"] = "limit"
        elif do == "shot":
            wid = self.window(step["window"]) if step.get("window") else None
            if step.get("delay"):
                time.sleep(step["delay"])
            rec["screenshot"] = self.shot(step.get("label", "shot"), wid)
        else:
            rec["status"] = "unknown-step"
            return rec
        time.sleep(step.get("settle", 1.2))
        new = [n for _, n in self.popups()]
        if new:
            rec["message_boxes_open"] = new
            if do != "answer":
                rec["message_screenshot"] = self.shot("message", self.popups()[-1][0])
        rec.setdefault("status", "done")
        return rec

    def run(self):
        x = self.x
        rec = {"id": self.case["id"], "card": self.case.get("card"), "inputs": self.case["inputs"],
               "input_sha256": {n: sha256(d) for n, d in zip(self.case["inputs"], self.docs)},
               "config": {**self.spec.get("config", {}), **self.case.get("config", {})},
               "status": "timeout", "steps": []}
        started = time.time()
        env = {**x.env, **self.spec.get("env", {}), **self.case.get("env", {})}
        rec["env"] = {**self.spec.get("env", {}), **self.case.get("env", {})}
        proc = subprocess.Popen([str(self.app), *map(str, self.docs)], cwd=self.app.parent,
                                env={**env, "HOME": str(self.scratch / "home"), "HIKARI_CAPTURE_OUT": str(self.out)},
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.proc = proc
        watch = MemoryWatch(proc, self.case.get("memory_limit_mb", self.spec.get("memory_limit_mb", 1024)))
        try:
            found = x("xdotool", "search", "--sync", "--onlyvisible", "--name", "HikariSub v[0-9]", timeout=90).split()
            if not found:
                rec["status"] = "no-main-window"
                return rec
            self.main = found[0]
            time.sleep(5)
            rec["startup_popups"] = [n for _, n in self.popups()]
            for w, _ in self.popups():
                x("xdotool", "windowactivate", "--sync", w)
                x.key("Return")
                time.sleep(1)
            expects_hang = any(st.get("may_hang") for st in self.case["steps"])
            for step in self.case["steps"]:
                if proc.poll() is not None:
                    # The app is gone (the watchdog's kill): in a case that
                    # expects a hang that is the observation, otherwise a failure.
                    rec["steps"].append({"do": step["do"], "status": "done" if expects_hang else "app-ended",
                                         "response": "none"})
                    if not expects_hang:
                        break
                    continue
                t0 = time.time()
                s = self.run_step(step)
                s["seconds"] = round(time.time() - t0, 1)
                rec["steps"].append(s)
                if s["status"] not in ("done",):
                    break
            rec["status"] = "captured" if all(s["status"] == "done" for s in rec["steps"]) and \
                len(rec["steps"]) == len(self.case["steps"]) else "incomplete"
        finally:
            watch.stop()
            rec["peak_rss_mb"] = watch.peak_kb // 1024
            if watch.killed:
                rec["killed_over_memory_limit"] = watch.killed
            rec["final_screenshot"] = self.shot("final")
            proc.kill()
            try:
                log, _ = proc.communicate(timeout=10)
            except subprocess.TimeoutExpired:
                log = ""
            rec["app_log_tail"] = (log or "")[-1500:]
            rec["seconds"] = round(time.time() - started, 1)
            shutil.rmtree(self.scratch, ignore_errors=True)
        return rec


def run(spec, package, display, workdir, plan_dir, only=None):
    x = X(display)
    out = []
    # Each case's record is also appended to ui-observations.jsonl as it
    # ends, so a lost run still leaves what it captured.
    partial = workdir / "ui-observations.jsonl"
    for case in spec["cases"]:
        if only and case["id"] not in only:
            continue
        rec = Case(spec, case, package, x, workdir, plan_dir).run()
        free = subprocess.run(["free", "-m"], capture_output=True, text=True).stdout.split("\n")[1:2]
        print(json.dumps({"case": rec["id"], "status": rec["status"], "seconds": rec.get("seconds"),
                          "peak_rss_mb": rec.get("peak_rss_mb"), "killed": rec.get("killed_over_memory_limit"),
                          "free_mb": " ".join(free[0].split()[1:4]) if free else None}), flush=True)
        with partial.open("a", encoding="utf-8") as f:
            f.write(json.dumps(rec, ensure_ascii=False) + "\n")
        out.append(rec)
    return out


def main():
    ap = argparse.ArgumentParser(description="Run only plan.json's ui cases (local reproduction).")
    ap.add_argument("--plan", required=True, type=Path)
    ap.add_argument("--package", required=True, type=Path)
    ap.add_argument("--display", default=":99")
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--legacy-commit", default="20d647c4c769ab7f5d383cf3c1c33f03876a94e9")
    ap.add_argument("--only", nargs="*")
    a = ap.parse_args()
    plan = json.loads(a.plan.read_text())
    a.out.parent.mkdir(parents=True, exist_ok=True)
    spec = plan["ui"]
    result = {"schema": 1, "legacy_commit": a.legacy_commit, "package_sha256": sha256(a.package),
              "host": platform.platform(), "captured_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
              "cases": [{"id": spec["id"], "route": "ui",
                         "observations": run(spec, a.package, a.display, a.out.parent, a.plan.parent, a.only)}]}
    a.out.write_text(json.dumps(result, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
