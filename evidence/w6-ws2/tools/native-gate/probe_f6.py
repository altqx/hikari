# Probe: after floating the Line editor from the menu, where do F6 presses go?
import os
import sys
import time

sys.argv = ["gate.py", sys.argv[1]]
exec(open(os.environ["GATE_DIR"] + "/gate.py").read().split("\nif __name__ ==")[0])
fresh(episode())
dismiss_notices()
panel_menu("Line editor", "Float")
st = wait_for(lambda s: "Line editor" in frames(s))
print("after float", [(f["id"], f["active"]) for f in st["frames"]], st["focusPath"], flush=True)
for i in range(5):
    B.combo("f6")
    time.sleep(1)
    st = atspi()
    print("F6", [(f["id"], f["active"]) for f in st["frames"]], st["focusPath"], flush=True)
