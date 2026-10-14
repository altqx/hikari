#!/usr/bin/env python3
"""drive_windows.py against a stand-in for the winix CLI (ctest
legacy-capture.drive-windows): legacy starts in the exe's folder, and the
message boxes and dialogs it owns are seen through `ui tree`'s window list
(`ui windows` lists only unowned top-level windows). The replies have the
shape the Winix VM gave in the S4 sweep (`ui tree --pid` with an owned
"Info" box)."""
import base64
import json
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import drive_windows  # noqa: E402

PID = 18496
MAIN = "s4.ass - HikariSub v0.0.1-rc.1  HEAD 20d647c "
EXE = r"C:\Winix\work\out\legacy-win\s4\separators\HikariSub_x64\HikariSub.exe"


def window(title, owned, foreground, pid=PID, visible=True):
    return {"hwnd": sum(map(ord, title)) * 1000 + pid % 1000, "title": title,
            "className": "#32770" if owned else "HikariSub_main_window", "pid": pid,
            "owner": 30343494 if owned else None, "owned": owned, "visible": visible, "minimized": False,
            "foreground": foreground, "bounds": {"x": 250, "y": 257, "width": 510, "height": 204}}


def listed(name, pid=PID):
    """A `ui windows` row (unowned top-level windows only)."""
    return {"name": name, "controlType": "Window", "pid": pid, "bounds": {"x": 12, "y": 10, "width": 1000, "height": 700}}


class FakeWinix:
    """The desktop as `winix ui` reports it: `windows` lists the unowned
    main window, `tree` the process's windows with the owned ones."""

    def __init__(self):
        self.calls = []
        self.owned = []          # owned windows of the process, in z-order
        self.edit = None         # an edit's automation id in the foreground dialog
        self.records = []        # the S4 case's capture lines
        self.close_id = True     # the log window's Close button has its automation id
        self.on_hotkey = None    # what the run-last hotkey does

    def ui(self, action, **args):
        self.calls.append((action, args))
        if action == "launch":
            return {"pid": PID}
        if action == "windows":
            return [listed("Taskbar", pid=5196), listed(MAIN)]
        if action == "tree":
            ws = self.owned + [window(MAIN, owned=False, foreground=not any(w["foreground"] for w in self.owned))]
            children = [{"element": {"controlType": "Edit", "automationId": self.edit}, "children": []}] if self.edit else []
            return {"element": {"name": ws[0]["title"], "controlType": "Pane"}, "children": children, "windows": ws}
        if action == "focus":
            hwnd = int(args["window"].split(":")[1])
            for w in self.owned:
                w["foreground"] = w["hwnd"] == hwnd
            return {"ok": True}
        if action == "keys":
            front = next((w for w in self.owned if w["foreground"]), None)
            if args["keys"].startswith("CTRL+") and self.on_hotkey:
                self.on_hotkey()
            elif front and front["title"] != "Log window" and args["keys"] in ("RETURN", "ESCAPE"):
                # A box closes on Return (a picker on Escape); the log window
                # does not, and while the modal box is up nothing else does.
                self.close(front)
            return {"ok": True}
        if action == "click" and args.get("automationId") == drive_windows.LOG_CLOSE_ID and not self.close_id:
            return {"error": "not-found"}
        if action == "click" and "x" in args:
            # A click on the log window's Close button, bottom centre.
            log = next((w for w in self.owned if w["title"] == "Log window"), None)
            b = log["bounds"] if log else None
            if (b and not any(w["title"] == "Error" for w in self.owned)
                    and abs(args["x"] - (b["x"] + b["width"] // 2)) <= 30
                    and b["y"] + b["height"] - 32 <= args["y"] <= b["y"] + b["height"] - 10):
                self.close(log)
            return {"ok": True}
        if action == "click" and args.get("automationId") == drive_windows.LOG_CLOSE_ID:
            log = next((w for w in self.owned if f"hwnd:{w['hwnd']}" == args.get("window")), None)
            if log and log["title"] == "Log window" and not any(w["title"] == "Error" for w in self.owned):
                self.close(log)
            return {"ok": True} if log else {"error": "not-found"}
        return {"ok": True}

    def close(self, w):
        self.owned.remove(w)
        self.records.append({"script": "after", "event": w["title"]})
        if self.owned:
            self.owned[0]["foreground"] = True

    def screenshot(self, path):
        return {"ok": True}

    def task(self, name):
        if name == "legacy-win-s4-setup":
            layout = {"zip": "HikariSub.zip", "cases": {"separators": {"exe": EXE, "doc": EXE.rsplit("\\", 2)[0] + r"\s4.ass"}}}
            return {"id": "j1", "state": "succeeded"}, ["S4_LAYOUT " + json.dumps(layout)]
        if name == "legacy-win-s4-output":
            text = base64.b64encode("".join(json.dumps(r) + "\n" for r in self.records).encode()).decode()
            return {"id": "j2", "state": "succeeded"}, ["== processes", "== separators", text, "== end"]
        return {"id": "j3", "state": "succeeded"}, []


def legacy_with(fake):
    legacy = drive_windows.Legacy(fake, Path("."), Path("."))
    legacy.pid = PID
    return legacy


@mock.patch.object(drive_windows.time, "sleep", lambda s: None)
class DriveWindowsTests(unittest.TestCase):
    def test_launch_starts_legacy_in_the_exe_folder(self):
        fake = FakeWinix()
        legacy = drive_windows.Legacy(fake, Path("."), Path("."))
        self.assertTrue(legacy.launch(EXE, r"C:\doc.ass"))
        launch = [a for c, a in fake.calls if c == "launch"]
        self.assertEqual(launch[0]["cwd"], r"C:\Winix\work\out\legacy-win\s4\separators\HikariSub_x64")
        legacy.launch("C:/legacy/HikariSub_x64/HikariSub.exe", "C:/doc.ass")
        self.assertEqual([a for c, a in fake.calls if c == "launch"][1]["cwd"], "C:/legacy/HikariSub_x64")

    def test_popups_lists_owned_message_box_foreground_first(self):
        fake = FakeWinix()
        fake.owned = [window("Log window", owned=True, foreground=False), window("Info", owned=True, foreground=True),
                      window("hidden", owned=True, foreground=False, visible=False),
                      window("Other app", owned=True, foreground=False, pid=4242)]
        self.assertEqual(legacy_with(fake).popups(), ["Info", "Log window"])

    def test_popups_empty_without_owned_windows(self):
        self.assertEqual(legacy_with(FakeWinix()).popups(), [])

    def test_popups_falls_back_to_ui_windows_without_tree_window_list(self):
        fake = FakeWinix()
        fake.ui = lambda action, **args: [listed(MAIN), listed("Dialog")] if action == "windows" else {"error": "x"}
        self.assertEqual(legacy_with(fake).popups(), ["Dialog"])

    def test_typed_text_goes_to_an_owned_dialog_edit(self):
        fake = FakeWinix()
        fake.owned = [window("Shift times", owned=True, foreground=True)]
        fake.edit = "1001"
        sent = drive_windows.send_keys(legacy_with(fake), ["type:0:00:01.00"])
        self.assertEqual(sent[0]["reply"], {"ok": True})
        self.assertIn(("type", {"pid": PID, "automationId": "1001", "text": "0:00:01.00"}), fake.calls)

    def run_case(self, fake, steps=1):
        spec = {"hotkey": "ctrl-shift-f12",
                "cases": [{"id": "separators", "document_scripts": "s4/first.lua", "steps": [{"do": "run_last"}] * steps}]}
        obs = drive_windows.document_scripts(drive_windows.Legacy(fake, Path("."), Path(".")), spec)[0]
        self.assertEqual(obs["status"], "captured")
        return obs["steps"]

    def test_document_scripts_answers_legacy_message_box(self):
        """no-scripts on Windows: legacy's owned "Info" box is recorded and
        answered with Return, so the next press does not go to a modal box."""
        fake = FakeWinix()

        def hotkey():
            fake.records.append({"script": "last", "event": "load"})
            fake.owned = [window("Info", owned=True, foreground=True)]

        fake.on_hotkey = hotkey
        steps = self.run_case(fake, steps=2)
        self.assertEqual([[(w["title"], w["answered"]) for w in s["windows"]] for s in steps],
                         [[("Info", "RETURN")], [("Info", "RETURN")]])
        self.assertEqual([s["main_title"] for s in steps], [MAIN, MAIN])
        self.assertEqual([r["event"] for s in steps for r in s["records"]], ["load", "load"])
        self.assertEqual(fake.owned, [])

    def test_document_scripts_answers_the_box_under_the_log_window_first(self):
        """validation-error on Windows: the log window opens over the modal
        "Error" box. As on Linux the box is answered first (brought forward,
        Return), then the log window by its Close button."""
        fake = FakeWinix()

        def hotkey():
            fake.owned = [window("Log window", owned=True, foreground=True),
                          window("Error", owned=True, foreground=False)]

        fake.on_hotkey = hotkey
        steps = self.run_case(fake)
        self.assertEqual([(w["title"], w["answered"]) for w in steps[0]["windows"]],
                         [("Error", "RETURN"), ("Log window", "Close")])
        self.assertEqual([r["event"] for w in steps[0]["windows"] for r in w.get("records_after", [])],
                         ["Error", "Log window"])
        self.assertEqual(fake.owned, [])
        error = window("Error", owned=True, foreground=False)
        self.assertIn(("focus", {"pid": PID, "window": f"hwnd:{error['hwnd']}"}), fake.calls)
    def test_log_window_closes_by_its_bottom_centre_without_the_button_id(self):
        """The Close button not found by its automation id: the click goes to
        the log window's bottom centre, where legacy puts the button."""
        fake = FakeWinix()
        fake.close_id = False
        log = window("Log window", owned=True, foreground=True)
        fake.owned = [log]
        legacy = legacy_with(fake)
        legacy.answer(legacy.popup_windows()[0], "Close")
        b = log["bounds"]
        self.assertIn(("click", {"x": b["x"] + b["width"] // 2, "y": b["y"] + b["height"] - 22}), fake.calls)
        self.assertEqual(fake.owned, [])

    def test_fallback_to_ui_windows_is_logged(self):
        fake = FakeWinix()
        fake.ui = lambda action, **args: [listed(MAIN)] if action == "windows" else {"error": "x"}
        with mock.patch.object(drive_windows.sys, "stderr") as err:
            legacy_with(fake).popups()
        self.assertIn("no window list", "".join(c.args[0] for c in err.write.call_args_list))


if __name__ == "__main__":
    unittest.main(verbosity=2)
