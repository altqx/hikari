import os
import subprocess
import sys
import time
sys.argv = ["gate.py", "kwin"]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate as g  # noqa: E402

patch = g.colour_patch()
time.sleep(2)
g.B.script('for (const w of workspace.windowList()) if (w.caption == "patch") '
           'w.frameGeometry = {x: 200, y: 700, width: 160, height: 120};')
time.sleep(1)
c = g.patch_centre()
mon = subprocess.Popen(["gdbus", "monitor", "--session", "--dest", "org.freedesktop.portal.Desktop"],
                       stdout=open(os.path.join(g.EVID, "probe-pickcolor-monitor.txt"), "w"), stderr=subprocess.STDOUT)
time.sleep(1)
out = g.sh(["gdbus", "call", "--session", "--dest", "org.freedesktop.portal.Desktop", "--object-path",
            "/org/freedesktop/portal/desktop", "--method", "org.freedesktop.portal.Screenshot.PickColor", "", "{}"])
g.log("probe PickColor call:", out.strip(), "patch at", c)
time.sleep(2)
g.B.snap("probe-pickcolor-0")
g.click(c[0], c[1], wait=2000)
time.sleep(2)
g.B.snap("probe-pickcolor-1-after-click")
g.B.keys("return", 2.0)
g.B.snap("probe-pickcolor-2-after-return")
mon.terminate()
g.log("probe monitor:", open(os.path.join(g.EVID, "probe-pickcolor-monitor.txt")).read()[-800:])
patch.terminate()
