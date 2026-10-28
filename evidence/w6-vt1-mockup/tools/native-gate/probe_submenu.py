# Probe: does the third-level submenu (View > Panels > Audio) open from the keyboard?
import os
import sys
import time

sys.argv = ["gate.py", sys.argv[1]]
exec(open(os.environ["GATE_DIR"] + "/gate.py").read().split("\nif __name__ ==")[0])
fresh()
B.keys("alt+v", 0.4, "down", "right", 0.5, "down", "right", 1.0)
ev, st = B.snap("probe-submenu-third-level")
dump = sh(["python3", f"{GATE}/atspi_tool.py", "dump"])
print("\n".join(l for l in dump.splitlines() if "[popup menu]" in l or ("[menu item]" in l and "showing" in l)))
B.keys("down", "down", "return", 1.5)
print("frames after Float:", list(frames()), flush=True)
