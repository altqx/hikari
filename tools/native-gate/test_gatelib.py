#!/usr/bin/env python3
"""Tests of gatelib, the native gate's session-free parts.

  python3 -m unittest discover -s tools/native-gate -p 'test_*.py'

Each case is a harness gap the wave-5 Linux sweep found (2026-10-14)."""
import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import gatelib  # noqa: E402

BACKGROUND = (0x30, 0x30, 0x30)
FRAME = (0x5a, 0x5a, 0x5a)
INSIDE = (0x20, 0x20, 0x24)
MARK = (0xd0, 0x40, 0x40)


class Image:
    def __init__(self, w, h, colour=BACKGROUND):
        self.w, self.h = w, h
        self.px = bytearray(bytes(colour) * (w * h))

    def fill(self, x, y, w, h, colour):
        for yy in range(max(0, y), min(self.h, y + h)):
            for xx in range(max(0, x), min(self.w, x + w)):
                i = (yy * self.w + xx) * 3
                self.px[i:i + 3] = bytes(colour)

    def frame(self, x, y, w, h, line=1, colour=FRAME):
        """A box of W x H with a border LINE pixels thick, a darker inside
        and its top corners rounded off (3 LINE pixels each side)."""
        self.fill(x, y, w, h, colour)
        self.fill(x + line, y + line, w - 2 * line, h - 2 * line, INSIDE)
        for corner in (x, x + w - 3 * line):
            self.fill(corner, y, 3 * line, line, BACKGROUND)

    def find(self, fw, fh, scale=1, x_from=0):
        return gatelib.find_frame(bytes(self.px), self.w, self.h, fw, fh, scale, x_from)


class FindFrame(unittest.TestCase):
    """mutter's locate_floating (no window geometry on mutter: the frame is
    found in a screenshot)."""

    def test_a_stretch_of_flat_background_is_not_a_frame(self):
        # mutter 50 and 51 reported 813,216 for a window at 481,302: a run of
        # background between two marks as wide as the frame, with background
        # down its left side, matched first
        fw, fh = 300, 200
        img = Image(1400, 700)
        img.fill(799, 0, 1, 700, MARK)
        img.fill(799 + fw - 4, 0, 1, 700, MARK)
        img.frame(481, 302, fw, fh)
        self.assertEqual(img.find(fw, fh), (481, 302))

    def test_a_box_of_another_height_is_not_the_frame(self):
        # the Line editor's text box: the same top run, no bottom edge there
        fw, fh = 300, 200
        img = Image(1000, 700)
        img.frame(100, 40, fw, fh + 60)
        img.frame(500, 300, fw, fh)
        self.assertEqual(img.find(fw, fh), (500, 300))

    def test_the_frame_at_scale_2_is_two_pixels_thick(self):
        # mixed-dpi on mutter: the frame's 1-pixel line is two device pixels
        fw, fh = 2 * 150, 2 * 100
        img = Image(1200, 600)
        img.frame(640, 120, fw, fh, line=2)
        self.assertEqual(img.find(fw, fh, scale=2, x_from=600), (640, 120))

    def test_no_frame_no_answer(self):
        img = Image(600, 400)
        img.fill(0, 0, 600, 1, MARK)
        self.assertIsNone(img.find(300, 200))


class Drag(unittest.TestCase):
    """openbox's floating-system-move lagged one 44-pixel step (dx -1276
    for -1320): a window-system move drags in steps of 8 pixels or less."""

    def assert_short_steps(self, dx, dy):
        steps = gatelib.drag_steps(dx, dy)
        points = [(0, 0)] + gatelib.drag_points(0, 0, dx, dy, steps)
        longest = max(max(abs(b[0] - a[0]), abs(b[1] - a[1])) for a, b in zip(points, points[1:]))
        self.assertLessEqual(longest, gatelib.MAX_DRAG_STEP, (dx, dy, steps))
        self.assertEqual(points[-1], (dx, dy))

    def test_the_x11_system_move(self):
        self.assert_short_steps(-1320, 0)

    def test_distances_that_do_not_divide_by_eight(self):
        for d in (1, 7, 9, 241, 1321, 1327, -1333):
            self.assert_short_steps(d, d // 3)
            self.assert_short_steps(d // 5, d)

    def test_short_drags_keep_thirty_steps(self):
        self.assertEqual(gatelib.drag_steps(40, -12), 30)


def qskips(text):
    """The reasons of every QSKIP in C++ source TEXT (string literals only)."""
    return re.findall(r'QSKIP\((?:qPrintable\(QStringLiteral\()?"([^"]*)"', text)


def function_body(text, name):
    m = re.search(r"\n    void " + re.escape(name) + r"\(\)\n    \{\n(.*?)\n    \}\n", text, re.S)
    return m.group(1) if m else None


class TestSkips(unittest.TestCase):
    """test-executables failed on skips the gate did not know: every skip the
    runs it makes can report is a known platform limit."""

    def source(self, exe):
        path = os.path.join(TREE, "tests", "ui", exe.removeprefix("hikari_ui_") + ".cpp")
        with open(path, encoding="utf-8") as f:
            return f.read()

    def test_every_skip_of_the_runs_is_known(self):
        for exe, functions in gatelib.TEST_RUNS:
            text = self.source(exe)
            if functions:
                bodies = []
                for fn in functions:
                    body = function_body(text, fn)
                    self.assertIsNotNone(body, f"{exe}: no test function {fn}")
                    bodies.append(body)
                text = "\n".join(bodies)
            reasons = qskips(text)
            unknown = gatelib.unexpected_skips([f"SKIP   : {r}" for r in reasons])
            unknown = [s for s in unknown if not any(f in s for f in gatelib.FAILING_TEST_SKIPS)]
            self.assertEqual(unknown, [], exe)

    def test_every_known_reason_is_still_a_skip(self):
        texts = "\n".join(self.source(exe) for exe, _ in gatelib.TEST_RUNS)
        reasons = qskips(texts)
        for known in gatelib.KNOWN_TEST_SKIPS:
            self.assertTrue(any(known in r for r in reasons), known)

    def test_a_skip_without_a_window_manager_fails(self):
        skip = ("SKIP   : Shell::f6AndShortcutsReachAFloatingPanel() The platform moved activation away from the "
                "floating panel's window (no window manager)")
        self.assertEqual(gatelib.unexpected_skips([skip]), [skip])

    def test_an_unknown_skip_fails(self):
        skips = ["SKIP   : Shell::a() On Wayland the compositor places windows (docs/docking.md)",
                 "SKIP   : Shell::b() HIKARI_SURFACE_SHOT_DIR is not set"]
        self.assertEqual(gatelib.unexpected_skips(skips), skips[1:])


class Order(unittest.TestCase):
    """On sway, sweep steps after videofs found the main window on the
    scale-2 output."""

    def test_the_sweep_runs_before_videofs_and_outputs(self):
        steps = {"default": 0, "tests": 0, "videofs": 0, "outputs": 0}
        sweep = {"tagspeech": 0, "edgetile": 0}
        self.assertEqual(gatelib.default_order(steps, sweep),
                         ["default", "tests", "tagspeech", "edgetile", "videofs", "outputs"])


class Session(unittest.TestCase):
    """The image and session scripts."""

    def read(self, name):
        with open(os.path.join(HERE, name), encoding="utf-8") as f:
            return f.read()

    def test_the_gate_scripts_parse(self):
        # gate.py picks its session at import, so it is parsed, not imported.
        import ast
        for name in ("gate.py", "atspi_tool.py", "gatelib.py"):
            ast.parse(self.read(name), filename=name)

    def test_the_lookup_finds_qt_s_application(self):
        # A11Y1 (#298): on GNOME GTK's ATK bridge registered the process a
        # second time, without windows, ahead of Qt's; the lookup took it
        self.assertEqual(gatelib.hikarisub_application(["gtk", "Qt"]), 1)
        self.assertEqual(gatelib.hikarisub_application(["Qt"]), 0)
        self.assertEqual(gatelib.hikarisub_application(["gtk"]), 0)
        self.assertIsNone(gatelib.hikarisub_application([]))
        self.assertIn("hikarisub_application(", self.read("atspi_tool.py"))

    def test_the_gnome_session_keeps_gtk_s_bridge(self):
        # A11Y1 (#298): NO_AT_BRIDGE=1 would leave the GTK3 theme's file
        # chooser silent to screen readers; the gate no longer sets it for
        # HikariSub (the colour patch, a GTK program of the gate's, still may)
        gate = self.read("gate.py")
        for fn in ("title_bar_start", "step_gtk_bridge"):
            body = re.search(r"\ndef " + fn + r"\(.*?(?=\ndef |\n[A-Z_]+ = )", gate, re.S).group(0)
            self.assertNotIn("NO_AT_BRIDGE\": \"1", body, fn)

    def test_kwin_runs_its_qt_quick_windows_in_software(self):
        # KWin aborted in QSGRenderLoop::handleContextCreationFailure when the
        # PickColor prompt opened
        self.assertRegex(self.read("sessions/kwin-start.sh"), r"QT_QUICK_BACKEND=software kwin_wayland ")

    def test_the_image_has_a_machine_id(self):
        # libdbus aborted the app under ibus without /etc/machine-id
        self.assertIn("dbus-uuidgen --ensure=/etc/machine-id", self.read("Dockerfile"))

    def test_keyboard_menu_walks_park_the_pointer(self):
        # a submenu opened under the resting pointer highlights the item there
        gate = self.read("gate.py")
        for fn in ("open_view", "menu_to"):
            body = re.search(r"\ndef " + fn + r"\(.*?(?=\ndef )", gate, re.S).group(0)
            self.assertIn("park_pointer()", body, fn)


if __name__ == "__main__":
    unittest.main()
