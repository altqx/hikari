#!/usr/bin/env python3
"""Checks that tools/icons/draw_icons.py reproduces src/ui/icons byte for byte.

  python3 -B tests/tools/draw_icons_tests.py
"""
import difflib
import os
import subprocess
import sys
import tempfile
import unittest

TREE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(TREE, "tools", "icons", "draw_icons.py")
ICONS = os.path.join(TREE, "src", "ui", "icons")


def lines(directory, name):
    with open(os.path.join(directory, name), "rb") as file:
        return file.read().decode("utf-8").splitlines(keepends=True)


class DrawIconsTest(unittest.TestCase):
    def test_script_reproduces_the_checked_in_icons(self):
        with tempfile.TemporaryDirectory() as out:
            run = subprocess.run([sys.executable, "-I", SCRIPT, out], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertEqual(sorted(os.listdir(out)), sorted(os.listdir(ICONS)))
            for name in sorted(os.listdir(out)):
                with self.subTest(name=name):
                    diff = "".join(difflib.unified_diff(lines(ICONS, name), lines(out, name), "src/ui/icons/" + name,
                                                        "draw_icons.py output"))
                    if diff:
                        self.fail("change tools/icons/draw_icons.py and run it:\n" + diff)


if __name__ == "__main__":
    unittest.main()
