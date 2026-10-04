# Probe: Line editor floating, main window focused by the compositor, then F6.
import os
import sys
import time

sys.argv = ["gate.py", sys.argv[1]]
exec(open(os.environ["GATE_DIR"] + "/gate.py").read().split("\nif __name__ ==")[0])
fresh(episode())
dismiss_notices()
float_panel("Line editor", "probe")
focus_main_compositor()
st = atspi()
print("main focused", [(f["id"], f["active"]) for f in st["frames"]], st["focusPath"], flush=True)
for i in range(4):
    B.combo("f6")
    time.sleep(1)
    st = atspi()
    print("F6", [(f["id"], f["active"]) for f in st["frames"]], panel_of(st["focusPath"]), flush=True)
