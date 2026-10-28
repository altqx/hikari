# Probe: close the main window (compositor close request) with a panel floating.
import os
import sys
import time

sys.argv = ["gate.py", sys.argv[1]]
exec(open(os.environ["GATE_DIR"] + "/gate.py").read().split("\nif __name__ ==")[0])
fresh()
if os.environ.get("NOFLOAT") != "1":
    float_panel("Audio", "close")
time.sleep(1)
close_window("HikariSub")
time.sleep(3)
print("process alive:", sh([f"{GATE}/app.sh", "alive"]).strip() == "yes", flush=True)
print("windows:", B.windows(), flush=True)
st = atspi()
print("AT-SPI frames:", [(f["name"], f["active"]) for f in st["frames"]], flush=True)
B.snap("close-main-with-floating-panel")
