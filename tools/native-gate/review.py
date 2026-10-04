#!/usr/bin/env python3
"""Adds the screenshot reviews to results.json (items gate.py cannot judge by
itself) and writes out/native-gate-evidence/summary.txt, one line per gate
item and session.

  review.py EVIDENCE_DIR
"""
import json
import os
import sys

EVID = sys.argv[1]

# (session, item): (status, detail, evidence) from looking at the screenshots.
REVIEWS = {
    ("sway", "fullscreen-visible"): ("observed",
        "main window fullscreen (sway): the floating Line editor stays visible above it", ["fullscreen-0-main-fullscreen.png"]),
    ("kwin", "fullscreen-visible"): ("observed",
        "main window fullscreen (KWin): the floating Line editor stays visible above it", ["fullscreen-0-main-fullscreen.png"]),
    ("x11", "fullscreen-visible"): ("observed",
        "main window fullscreen (openbox, _NET_WM_STATE_FULLSCREEN): the floating Line editor stays visible above it",
        ["fullscreen-0-main-fullscreen.png"]),
    ("sway", "mixed-dpi-rendering"): ("observed",
        "floating Line editor on HEADLESS-2 (scale 2) renders at 2x (crisp text and title-bar icons) while the main window "
        "stays on HEADLESS-1 at scale 1; typing into it failed only because the floating window had no focused item "
        "(see focus-after-float)", ["outputs-0-scale2-output.png", "outputs-0-on-scale2.png"]),
    ("kwin", "mixed-dpi-rendering"): ("observed",
        "floating Line editor on Virtual-1 (scale 2) renders at 2x next to the main window on Virtual-0 (scale 1); "
        "typing reached it", ["outputs-0-on-scale2.png"]),
    ("mutter", "mixed-dpi-rendering"): ("observed",
        "Meta-1 at scale 2 beside Meta-0 at 1 (gdctl); Super+Shift+Right moved the floating Line editor onto Meta-1, where it "
        "renders at 2x (right half of the screenshot)", ["outputs-0-on-scale2.png"]),
    ("mutter", "monitor-removal-reviewed"): ("observed",
        "after Meta-1 left the layout, mutter put the floating Line editor back on Meta-0, visible over the main window",
        ["outputs-1-after-removal.png.Meta-0.png"]),
}

ITEMS = ["default-arrangement", "keyboard-float", "keyboard-float-move-panel", "draft-kept-across-float", "keyboard-dock",
         "draft-kept-across-dock", "keyboard-move-panel", "move-panel-defaults", "focus-after-float",
         "f6-from-floating-panel", "f6-into-floating-panel", "shortcut-from-floating-panel", "pointer-float-button",
         "pointer-dblclick-float-redock", "pointer-drag-dock", "video-float-redock", "layout-persistence",
         "fullscreen-coexistence", "fullscreen-visible", "mixed-dpi", "mixed-dpi-rendering", "monitor-removal",
         "monitor-removal-reviewed", "monitor-removal-show-focus", "orca", "menu-mnemonic-from-text-field",
         "test-executables"]
SESSIONS = ["sway", "kwin", "mutter", "x11"]

for (session, item), (status, detail, ev) in REVIEWS.items():
    path = os.path.join(EVID, session, "results.json")
    res = json.load(open(path)) if os.path.exists(path) else {}
    res[item] = {"status": status, "detail": "screenshot review: " + detail, "evidence": ev}
    json.dump(res, open(path, "w"), indent=1)

lines = [f"{'item':34}" + "".join(f"{s:16}" for s in SESSIONS)]
results = {s: json.load(open(os.path.join(EVID, s, "results.json"))) for s in SESSIONS
           if os.path.exists(os.path.join(EVID, s, "results.json"))}
for item in ITEMS:
    lines.append(f"{item:34}" + "".join(f"{results.get(s, {}).get(item, {}).get('status', '-'):16}" for s in SESSIONS))
lines.append("")
for s in SESSIONS:
    for item in ITEMS:
        r = results.get(s, {}).get(item)
        if r:
            lines.append(f"[{s}] {item}: {r['status']} - {r['detail']}")
open(os.path.join(EVID, "summary.txt"), "w").write("\n".join(lines) + "\n")
print("\n".join(lines[:len(ITEMS) + 1]))
