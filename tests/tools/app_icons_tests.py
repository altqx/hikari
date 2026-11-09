#!/usr/bin/env python3
"""Checks the app and file-type icon masters and that tools/icons/app_icons.py made the shipped rasters.

  python3 -B tests/tools/app_icons_tests.py
"""
import hashlib
import json
import os
import re
import unittest

TREE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
STAMP = os.path.join(TREE, "tools", "icons", "app_icons.json")
DESIGNS = ["packaging/icons/dawn-peek", "packaging/icons/light-note"]
RERUN = "run python3 tools/icons/app_icons.py and commit what it writes"

NUMBER = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?"
SLACK = 2.0
ARGS = {"M": 2, "L": 2, "H": 1, "V": 1, "C": 6, "S": 4, "Q": 4, "T": 2, "A": 7, "Z": 0}


def sha256(path):
    with open(os.path.join(TREE, path), "rb") as file:
        return hashlib.sha256(file.read()).hexdigest()


def bezier(points, steps=16):
    for k in range(1, steps + 1):
        t, pts = k / steps, points
        while len(pts) > 1:
            pts = [(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t) for a, b in zip(pts, pts[1:])]
        yield pts[0]


def path_points(d):
    tokens = re.findall(r"[MmLlHhVvCcSsQqTtAaZz]|" + NUMBER, d)
    x = y = sx = sy = 0.0
    points, last_control, last = [], None, None
    i, cmd = 0, None
    while i < len(tokens):
        if tokens[i].isalpha():
            cmd = tokens[i]
            i += 1
        upper, rel = cmd.upper(), cmd.islower()
        if upper == "Z":
            x, y = sx, sy
            last = upper
            continue
        if upper == "A":
            arc, j = [], i
            while len(arc) < 7:
                token = tokens[j]
                if len(arc) in (3, 4) and len(token) > 1 and token[0] in "01":
                    arc.append(float(token[0]))
                    tokens[j] = token[1:]
                    continue
                arc.append(float(token))
                j += 1
            i, args = j, arc
        else:
            args = [float(t) for t in tokens[i:i + ARGS[upper]]]
            i += ARGS[upper]
        start, control = (x, y), None
        if upper == "H":
            x = args[0] + (x if rel else 0)
        elif upper == "V":
            y = args[0] + (y if rel else 0)
        else:
            pairs = [(args[k], args[k + 1]) for k in range(5 if upper == "A" else 0, len(args), 2)]
            pts = [(px + x, py + y) if rel else (px, py) for px, py in pairs]
            if upper in "ST":
                smooth = last_control if last and last in ("CS" if upper == "S" else "QT") else None
                pts.insert(0, (2 * x - smooth[0], 2 * y - smooth[1]) if smooth else start)
            if upper in "CSQT":
                points += bezier([start] + pts)
                control = pts[-2]
            x, y = pts[-1]
        points.append((x, y))
        last_control, last = control, upper
        if upper == "M":
            sx, sy = x, y
            cmd = "l" if rel else "L"
    return points


def masters():
    for design in DESIGNS:
        for name in sorted(os.listdir(os.path.join(TREE, design))):
            if name.endswith(".svg"):
                yield design + "/" + name


class AppIconsTest(unittest.TestCase):
    def test_masters_are_plain_vector_art(self):
        for master in masters():
            with self.subTest(master=master):
                with open(os.path.join(TREE, master), encoding="utf-8") as file:
                    svg = file.read()
                for tag in ("<image", "<clipPath", "<mask", "<filter", "transform="):
                    self.assertNotIn(tag, svg)

    def test_view_boxes_are_tight_to_the_art(self):
        for master in masters():
            with self.subTest(master=master):
                with open(os.path.join(TREE, master), encoding="utf-8") as file:
                    svg = file.read()
                x0, y0, w, h = (float(v) for v in re.search(r'viewBox="([^"]+)"', svg).group(1).split())
                points = [p for d in re.findall(r'<path[^>]* d="([^"]+)"', svg) for p in path_points(d)]
                xs, ys = [p[0] for p in points], [p[1] for p in points]
                art = (min(xs), min(ys), max(xs), max(ys))
                for edge, inset in zip(("left", "top", "right", "bottom"),
                                       (art[0] - x0, art[1] - y0, x0 + w - art[2], y0 + h - art[3])):
                    self.assertTrue(-SLACK <= inset <= SLACK, "%s edge is %.2f inside the viewBox" % (edge, inset))

    def test_rasters_come_from_the_current_script_and_masters(self):
        with open(STAMP, encoding="utf-8") as file:
            stamp = json.load(file)
        self.assertEqual(stamp["script"], sha256("tools/icons/app_icons.py"), RERUN)
        dawn_peek = sorted(m for m in masters() if m.startswith(DESIGNS[0] + "/"))
        self.assertEqual(sorted(stamp["masters"]), dawn_peek, RERUN)
        for path, digest in sorted(stamp["masters"].items()) + sorted(stamp["outputs"].items()):
            with self.subTest(path=path):
                self.assertTrue(os.path.isfile(os.path.join(TREE, path)), RERUN)
                self.assertEqual(sha256(path), digest, RERUN)


if __name__ == "__main__":
    unittest.main()
