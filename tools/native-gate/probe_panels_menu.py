import json
import os
import sys
import time
sess = sys.argv[1] if len(sys.argv) > 1 else "kwin"
sys.argv = ["gate.py", sess]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate as g  # noqa: E402

g.fresh(g.episode())
g.dismiss_notices()
g.focus_main_compositor()
ok = g.view_to("Panels")
seq = [("right", "panels-submenu"), ("down", "down1"), ("right", "line-editor-submenu"), ("down", "d1"),
       ("down", "d2"), ("return", "float")]
g.log("probe: view_to Panels", ok, g.sh(["python3", f"{g.GATE}/atspi_tool.py", "menu"]).strip())
for i, (key, tag) in enumerate(seq):
    g.B.keys(key, 0.6)
    menus = g.sh(["python3", f"{g.GATE}/atspi_tool.py", "menu"]).strip()
    g.B.snap(f"probe-panels-{i}-{tag}", extra="# menus\n" + menus)
    g.log(f"probe: after {key}: {menus[:600]}")
time.sleep(1.5)
g.log("probe: frames", list(g.frames()))
g.app("kill")
