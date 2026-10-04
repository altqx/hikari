#!/usr/bin/env python3
"""Windows in sway's tree: type, app_id, title, layout rect, output, focus."""
import json
import subprocess
import sys

tree = json.loads(subprocess.check_output(["swaymsg", "-t", "get_tree"]))


def walk(n, output=None):
    if n.get("type") == "output":
        output = n["name"]
    if n.get("type") in ("con", "floating_con") and n.get("pid"):
        r = n["rect"]; wr = n["window_rect"]
        print(f'{n["type"]:12} id={n["id"]} {n.get("app_id")} {n["name"]!r} {r["x"]},{r["y"]} {r["width"]}x{r["height"]} client={r["x"]+wr["x"]},{r["y"]+wr["y"]} {wr["width"]}x{wr["height"]}'
              f' output={output} fullscreen={n.get("fullscreen_mode")} {"FOCUSED" if n.get("focused") else ""}'
              f' visible={n.get("visible")}')
    for c in n.get("nodes", []) + n.get("floating_nodes", []):
        walk(c, output)


walk(tree)
if len(sys.argv) > 1:
    print(json.dumps(tree, indent=1))
