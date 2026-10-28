#!/usr/bin/env python3
"""S4: the scripts a Document names and "Run the last loaded script", driven.

Each case of plan.json's "document_scripts" section runs a fresh copy of the
legacy Linux package on a private X server (as drive.py's other routes),
Autoload emptied, with a document whose Script Info names its scripts in
"Automation Scripts" (legacy Automation::AddFromSubs, Automation.cpp:1273-
1307). The scripts are automation/document-scripts/*.lua, each written out
with record.lua.in before its body: the top level records a load, the
validation function and the macro record what they see (the first Line's
text, the Line count, the Script Info keys), one JSON line each in
$HIKARI_CAPTURE_OUT. "{abs}" in a case's scripts value is a folder outside
the app (an absolute path); a relative path names the app's folder, which
is the app's working directory (legacy reads it with wxFileExists).

GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT (HikariSubFrame.cpp:916-936) is bound to
the plan's hotkey in the app's Config/Hotkeys.txt, so it runs without the
app-drawn menu (whose positions depend on the window manager). Like the
other routes it needs a window manager on the X server (openbox in CI and in
the hikari-legacy-env container): without one the app does not open its
document and gets no keys. Steps:

  run_last   a click on the Line editor's Effect field (plan "focus_at", as
             ui_capture.py's dump: after a message box closes legacy's
             frame has no focused window and gets no keys), then the hotkey;
             the records the scripts wrote, the windows that opened
             (message boxes, the log window, the script editor picker) and
             the main window's title are kept; each window is then answered
             (Return, Escape for a file picker, its Close button for the
             log window) and what opens after it is kept too
  touch      appends a comment to the named script in both folders and
             moves its time a minute on (LuaScript::CheckLastModified)

What is recorded is what the old app did; it is not an expectation for the
rewrite. A window's text is not read (only its title): the scripts' records
say which outcome a box belongs to.
"""
import argparse, json, os, platform, shutil, subprocess, tarfile, tempfile, time
from pathlib import Path

from ui_capture import BASE_CONFIG, MAIN, X, sha256

FILE_PICKERS = ("Select a script editor", "Choose script")
LOG_WINDOW = "Log window"


def script_text(plan_dir, spec, name):
    folder = plan_dir / spec["scripts_dir"]
    body = (folder / f"{name}.lua").read_text(encoding="utf-8")
    head = (folder / "record.lua.in").read_text(encoding="utf-8").replace("local tag = TAG", f'local tag = "{name}"')
    return head + "\n" + body


class Case:
    def __init__(self, spec, case, package, x, workdir, plan_dir):
        self.spec, self.case, self.x, self.workdir, self.plan_dir = spec, case, x, workdir, plan_dir
        self.scratch = Path(tempfile.mkdtemp(prefix="hikari-s4-"))
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
        # The scripts, in a folder outside the app ({abs}) and in the app's
        # folder (relative paths).
        self.folders = [self.scratch / "abs", self.app.parent / "s4"]
        for folder in self.folders:
            folder.mkdir()
            for name in spec["scripts"]:
                (folder / f"{name}.lua").write_text(script_text(plan_dir, spec, name), encoding="utf-8")
        config = self.app.parent / "Config"
        config.mkdir(exist_ok=True)
        (config / "Config.txt").write_text("\n".join(["[HikariSub v0.0.1]"] + BASE_CONFIG) + "\n", encoding="utf-8")
        # More than ten entries keep LoadHkeys from adding its defaults
        # (Hotkeys.cpp:309-314): script hotkeys for scripts that do not exist,
        # then the one command the cases use.
        hotkeys = ["[HikariSub 0.0.1.9999]"] + [f"Script /nonexistent/none.lua-{k}=Ctrl-Alt-F{k + 1}" for k in range(11)]
        hotkeys.append(f"GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT G={spec['hotkey']}")
        (config / "Hotkeys.txt").write_text("\r\n".join(hotkeys) + "\r\n", encoding="utf-8")
        scripts = case.get("document_scripts")
        info = "[Script Info]\r\nTitle: s4\r\nScriptType: v4.00+\r\n"
        if scripts is not None:
            info += "Automation Scripts: " + scripts.replace("{abs}", str(self.folders[0])) + "\r\n"
        self.doc = self.scratch / "doc" / "s4.ass"
        self.doc.parent.mkdir()
        self.doc.write_text(info + "\r\n[V4+ Styles]\r\n" + spec["styles"] + "\r\n\r\n[Events]\r\n" + spec["events"],
                            encoding="utf-8")
        # Dated back: legacy's Linux build would otherwise ask whether to
        # reload a file written in the second it is opened (ui_capture.py).
        old = time.time() - 120
        os.utime(self.doc, (old, old))
        self.out = self.scratch / "capture.jsonl"
        self.out.touch()
        self.main = None

    def lines(self):
        return self.out.read_text(encoding="utf-8").splitlines()

    def popups(self):
        return [(w, n) for w, n in self.x.windows() if MAIN not in n and n != "hikarisub"]

    def shot(self, label, wid=None):
        """A screenshot (ImageMagick's import, or FFmpeg's x11grab of the screen)."""
        path = self.workdir / "screens" / f"s4-{self.case['id']}-{label}.png"
        path.parent.mkdir(parents=True, exist_ok=True)
        if shutil.which("import"):
            self.x("import", "-window", wid or "root", str(path))
        elif shutil.which("ffmpeg"):
            self.x("ffmpeg", "-loglevel", "error", "-y", "-f", "x11grab", "-i", self.x.env["DISPLAY"],
                   "-frames:v", "1", str(path))
        return str(path.relative_to(self.workdir)) if path.exists() else None

    def settle(self, before, timeout):
        """Waits until the records and the windows stop changing."""
        deadline = time.time() + timeout
        last, quiet = None, 0
        while time.time() < deadline:
            now = (len(self.lines()), tuple(n for _, n in self.popups()))
            if now == last:
                quiet += 1
                if quiet >= 6 and (now[0] > before or now[1]):
                    break
            else:
                last, quiet = now, 0
            time.sleep(0.5)

    def run_last(self, step):
        x = self.x
        rec = {"do": "run_last"}
        before = len(self.lines())
        x("xdotool", "windowactivate", "--sync", self.main)
        x.click(*self.spec["focus_at"])
        time.sleep(0.5)
        x.key(self.spec["xdotool_key"])
        self.settle(before, step.get("timeout", 20))
        got = self.lines()
        rec["records"] = [json.loads(l) for l in got[before:]]
        rec["windows"] = []
        if self.popups():
            # The whole screen first: a box under another window (the log
            # window over the message box) is not readable from its own grab.
            time.sleep(0.5)
            rec["popups_screenshot"] = self.shot(f"step{self.steps_run}-popups")
        # Answer what opened, one window at a time, keeping what opens next.
        for _ in range(6):
            popups = self.popups()
            if not popups:
                break
            wid, title = popups[0]
            time.sleep(0.5)  # drawn before the screenshot
            entry = {"title": title, "screenshot": self.shot(f"step{self.steps_run}-{len(rec['windows'])}", wid)}
            x("xdotool", "windowactivate", "--sync", wid)
            time.sleep(0.3)
            if title == LOG_WINDOW:
                # Return does not close it: its Close button, bottom centre.
                gx, gy, gw, gh = x.geometry(wid)
                x.click(gx + gw // 2, gy + gh - 22)
                answer = "Close"
            else:
                answer = "Escape" if title in FILE_PICKERS else "Return"
                x.key(answer)
            entry["answered"] = answer
            rec["windows"].append(entry)
            time.sleep(1.5)
            more = self.lines()
            if len(more) > len(got):
                entry["records_after"] = [json.loads(l) for l in more[len(got):]]
                got = more
        rec["main_title"] = x("xdotool", "getwindowname", self.main).strip()
        rec["screenshot"] = self.shot(f"step{self.steps_run}")
        rec["status"] = "done" if self.proc.poll() is None else "app-ended"
        return rec

    def touch(self, step):
        for folder in self.folders:
            path = folder / f"{step['script']}.lua"
            with path.open("a", encoding="utf-8") as f:
                f.write("-- changed\n")
            later = time.time() + 60
            os.utime(path, (later, later))
        return {"do": "touch", "script": step["script"], "status": "done"}

    def run(self):
        x = self.x
        rec = {"id": self.case["id"], "document_scripts": self.case.get("document_scripts"),
               "status": "timeout", "steps": []}
        started = time.time()
        self.proc = proc = subprocess.Popen(
            [str(self.app), str(self.doc)], cwd=self.app.parent,
            env={**x.env, "HOME": str(self.scratch / "home"), "HIKARI_CAPTURE_OUT": str(self.out)},
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            found = x("xdotool", "search", "--sync", "--onlyvisible", "--name", "HikariSub v[0-9]", timeout=90).split()
            if not found:
                rec["status"] = "no-main-window"
                return rec
            self.main = found[0]
            time.sleep(5)
            rec["startup_popups"] = [n for _, n in self.popups()]
            rec["startup_title"] = x("xdotool", "getwindowname", self.main).strip()
            rec["startup_screenshot"] = self.shot("startup")
            rec["startup_records"] = [json.loads(l) for l in self.lines()]
            for w, _ in self.popups():
                x("xdotool", "windowactivate", "--sync", w)
                x.key("Return")
                time.sleep(1)
            for step in self.case["steps"]:
                self.steps_run = len(rec["steps"])
                s = getattr(self, step["do"])(step)
                rec["steps"].append(s)
                if s["status"] != "done":
                    break
            rec["status"] = "captured" if len(rec["steps"]) == len(self.case["steps"]) and \
                all(s["status"] == "done" for s in rec["steps"]) else "incomplete"
        finally:
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
    for case in spec["cases"]:
        if only and case["id"] not in only:
            continue
        rec = Case(spec, case, package, x, workdir, plan_dir).run()
        print(json.dumps({"case": rec["id"], "status": rec["status"], "seconds": rec.get("seconds")}), flush=True)
        out.append(rec)
    return out


def main():
    ap = argparse.ArgumentParser(description="Run only plan.json's document_scripts cases.")
    ap.add_argument("--plan", required=True, type=Path)
    ap.add_argument("--package", required=True, type=Path)
    ap.add_argument("--display", default=os.environ.get("DISPLAY", ":99"))
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--legacy-commit", default="20d647c4c769ab7f5d383cf3c1c33f03876a94e9")
    ap.add_argument("--only", nargs="*")
    a = ap.parse_args()
    plan = json.loads(a.plan.read_text())
    a.out.parent.mkdir(parents=True, exist_ok=True)
    spec = plan["document_scripts"]
    folder = a.plan.parent / spec["scripts_dir"]
    result = {"schema": 1, "legacy_commit": a.legacy_commit, "package_sha256": sha256(a.package),
              "scripts_sha256": {p.name: sha256(p) for p in sorted(folder.glob("*"))},
              "host": platform.platform(), "captured_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
              "cases": [{"id": spec["id"], "route": "document-scripts",
                         "observations": run(spec, a.package, a.display, a.out.parent, a.plan.parent, a.only)}]}
    a.out.write_text(json.dumps(result, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
