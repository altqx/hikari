# Probe: float the Line editor, then make the main window fullscreen through the window manager.
import os
import sys
import time

sys.argv = ["gate.py", sys.argv[1]]
exec(open(os.environ["GATE_DIR"] + "/gate.py").read().split("\nif __name__ ==")[0])
fresh(episode())
dismiss_notices()
print("floated by", float_panel("Line editor", "probe"), list(frames()), flush=True)
print(sh("xdotool search --name 'HikariSub$'"), flush=True)
print(sh("xdotool search --name 'HikariSub$' windowstate --add FULLSCREEN"), flush=True)
time.sleep(1.5)
print("after", list(frames()), B.windows(), flush=True)
B.snap("probe-fullscreen")
