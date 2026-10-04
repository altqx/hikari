#!/bin/bash
# stop.sh SESSION: end a session's processes (compositor, its session bus,
# injectors, the app) so the session can start again on the same sockets.
s=$1
rt=/tmp/gate-runtime-$s
case $s in
sway) pat='sway -c|/tmp/vpointer|wtype -s' ;;
kwin) pat='kwin_wayland|eidaemon.py kwin' ;;
mutter) pat='mutter --headless|eidaemon.py mutter|^pipewire|^wireplumber' ;;
x11) pat='Xvfb :7|openbox|picom' ;;
esac
python3 - "$rt" "$pat" <<'EOF'
import os, re, signal, sys
rt, pat = sys.argv[1], re.compile(sys.argv[2])
me = {os.getpid(), os.getppid()}
for pid in os.listdir("/proc"):
    if not pid.isdigit() or int(pid) in me:
        continue
    try:
        cmd = open(f"/proc/{pid}/cmdline", "rb").read().replace(b"\0", b" ").decode(errors="replace").strip()
        env = open(f"/proc/{pid}/environ", "rb").read().decode(errors="replace")
    except OSError:
        continue
    if not cmd:
        continue
    if pat.search(cmd) or f"unix:path={rt}/bus" in cmd or f"XDG_RUNTIME_DIR={rt}\0" in env:
        try:
            os.kill(int(pid), signal.SIGTERM)
        except OSError:
            pass
EOF
sleep 1.5
rm -f "$rt"/wayland-* "$rt"/bus "$rt"/sway-ipc.* "$rt"/pipewire-0* /tmp/.X11-unix/X7 /tmp/.X7-lock 2>/dev/null
true
