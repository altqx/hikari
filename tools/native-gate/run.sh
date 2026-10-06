#!/bin/bash
# run.sh [SESSION...] [-- STEP...]
# Builds the gate image if needed, starts a fresh container (never the
# host's display: every compositor runs headless inside it), starts each
# SESSION (sway, kwin, mutter, x11; default all four) and runs gate.py there.
# Evidence lands in out/native-gate-evidence/<session>/ of this worktree.
# GATE_CONTAINER names the container (default d1gate), so gates of several
# worktrees can run side by side.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
wt=$(cd "$here/../.." && pwd)
tree=${HIKARI_TREE:-/home/altq/Work/hikari-qt}
ctr=${GATE_CONTAINER:-d1gate}
evidence=${GATE_EVIDENCE:-$wt/out/native-gate-evidence}
sessions=()
steps=()
while [ $# -gt 0 ]; do
    if [ "$1" = "--" ]; then shift; steps=("$@"); break; fi
    sessions+=("$1"); shift
done
[ ${#sessions[@]} -eq 0 ] && sessions=(sway kwin mutter x11)

docker image inspect hikari-d1-gate >/dev/null 2>&1 || docker build -t hikari-d1-gate "$here"
docker rm -f "$ctr" >/dev/null 2>&1 || true
render=()
# A render node lets KWin composite with OpenGL (its ScreenShot2 needs it);
# Mesa still renders in software (llvmpipe) here.
if [ -e /dev/dri/renderD128 ]; then
    render=(--device /dev/dri/renderD128 --group-add "$(stat -c %g /dev/dri/renderD128)")
fi
# The build tree read-only (unless it is this worktree), and the Qt SDK it
# links when out/sdk is a link to another checkout's.
mounts=(-v "$wt:$wt")
[ "$tree" != "$wt" ] && mounts+=(-v "$tree:$tree:ro")
sdk=$(readlink -f "$tree/out/sdk")
case "$sdk" in "$tree"/*|"$wt"/*) ;; *) mounts+=(-v "$sdk:$sdk:ro") ;; esac
docker run -d --init --name "$ctr" --cpus=3 --shm-size=1g "${render[@]}" \
    "${mounts[@]}" \
    -e HIKARI_TREE="$tree" -e GATE_DIR="$here" -e EVIDENCE="$evidence" \
    hikari-d1-gate sleep infinity >/dev/null
docker exec "$ctr" "$here/build-tools.sh"
mkdir -p "$evidence"

for s in "${sessions[@]}"; do
    case $s in
    # sway-activate: sway with focus_on_window_activation focus (gate.py's SwayActivate).
    sway|sway-activate) docker exec "$ctr" "$here/sessions/sway-start.sh" ;;
    x11) docker exec "$ctr" "$here/sessions/x11-start.sh" ;;
    kwin) docker exec "$ctr" env KWIN_OUTPUTS=2 "$here/sessions/kwin-start.sh"
          docker exec -d "$ctr" "$here/sessions/enter.sh" kwin sh -c "exec python3 $here/eidaemon.py kwin > /tmp/ei-kwin.log 2>&1" ;;
    mutter) docker exec "$ctr" env MUTTER_MON2=1600x1000 "$here/sessions/mutter-start.sh"
            docker exec -d "$ctr" "$here/sessions/enter.sh" mutter sh -c "exec python3 $here/eidaemon.py mutter > /tmp/ei-mutter.log 2>&1" ;;
    esac
    sleep 3
    session=$s; [ "$s" = sway-activate ] && session=sway
    docker exec "$ctr" "$here/sessions/enter.sh" "$session" python3 "$here/gate.py" "$s" "${steps[@]}" || true
    if [ "$s" = sway ] && [ ${#steps[@]} -eq 0 ]; then
        # Cross-window focus again with focus_on_window_activation focus
        # (sway's default, urgent, refuses activation): a fresh session, as
        # step outputs removed HEADLESS-2. Evidence in sway-activate/.
        docker exec "$ctr" "$here/sessions/sway-start.sh"
        sleep 3
        docker exec "$ctr" "$here/sessions/enter.sh" sway python3 "$here/gate.py" sway-activate f6 fullscreen tests outputs || true
    fi
done
docker exec "$ctr" sh -c 'pacman -Q sway wlroots0.20 kwin mutter xorg-server-xvfb openbox orca at-spi2-core libei mesa' \
    > "$evidence/versions.txt"
echo "evidence: $evidence"
