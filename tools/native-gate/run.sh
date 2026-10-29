#!/bin/bash
# run.sh [SESSION...] [-- STEP...]: SESSION sway, kwin, mutter, x11 (default all four) or hyprland.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
wt=$(cd "$here/../.." && pwd)
tree=${HIKARI_TREE:-/home/altq/Work/hikari-qt}
ctr=${GATE_CONTAINER:-d1gate}
image=${GATE_IMAGE:-hikari-d1-gate}
evidence=${GATE_EVIDENCE:-$wt/out/native-gate-evidence}
sessions=()
steps=()
while [ $# -gt 0 ]; do
    if [ "$1" = "--" ]; then shift; steps=("$@"); break; fi
    sessions+=("$1"); shift
done
[ ${#sessions[@]} -eq 0 ] && sessions=(sway kwin mutter x11)

docker image inspect "$image" >/dev/null 2>&1 || docker build -t "$image" "$here"
docker rm -f "$ctr" >/dev/null 2>&1 || true
render=()
# KWin's ScreenShot2 needs OpenGL compositing: a render node.
if [ -e /dev/dri/renderD128 ]; then
    render=(--device /dev/dri/renderD128 --group-add "$(stat -c %g /dev/dri/renderD128)")
fi
mounts=(-v "$wt:$wt")
[ "$tree" != "$wt" ] && mounts+=(-v "$tree:$tree:ro")
mkdir -p "$evidence"
case "$evidence" in "$tree"/*|"$wt"/*) ;; *) mounts+=(-v "$evidence:$evidence") ;; esac
sdk=$(readlink -f "$tree/out/sdk")
case "$sdk" in "$tree"/*|"$wt"/*) ;; *) mounts+=(-v "$sdk:$sdk:ro") ;; esac
# Hyprland's nested backend needs working GBM: the NVIDIA devices.
case " ${sessions[*]} " in *" hyprland "*)
    for d in /dev/nvidia0 /dev/nvidiactl /dev/nvidia-modeset /dev/nvidia-uvm; do
        [ -e "$d" ] && render+=(--device "$d")
    done ;;
esac
docker run -d --init --name "$ctr" --cpus=3 --shm-size=1g "${render[@]}" \
    "${mounts[@]}" \
    -e HIKARI_TREE="$tree" -e GATE_DIR="$here" -e EVIDENCE="$evidence" \
    "$image" sleep infinity >/dev/null
docker exec "$ctr" "$here/build-tools.sh"
mkdir -p "$evidence"

for s in "${sessions[@]}"; do
    case $s in
    sway|sway-activate) docker exec "$ctr" "$here/sessions/sway-start.sh" ;;
    x11) docker exec "$ctr" "$here/sessions/x11-start.sh" ;;
    kwin) docker exec "$ctr" env KWIN_OUTPUTS=2 "$here/sessions/kwin-start.sh"
          docker exec -d "$ctr" "$here/sessions/enter.sh" kwin sh -c "exec python3 $here/eidaemon.py kwin > /tmp/ei-kwin.log 2>&1" ;;
    mutter) docker exec "$ctr" env MUTTER_MON2=1600x1000 "$here/sessions/mutter-start.sh"
            docker exec -d "$ctr" "$here/sessions/enter.sh" mutter sh -c "exec python3 $here/eidaemon.py mutter > /tmp/ei-mutter.log 2>&1" ;;
    hyprland) docker exec "$ctr" "$here/sessions/hyprland-start.sh" ;;
    esac
    sleep 3
    session=$s; [ "$s" = sway-activate ] && session=sway; [ "$s" = hyprland ] && session=hypr
    docker exec "$ctr" "$here/sessions/enter.sh" "$session" python3 "$here/gate.py" "$s" "${steps[@]}" || true
    if [ "$s" = sway ] && [ ${#steps[@]} -eq 0 ]; then
        # sway's default focus_on_window_activation (urgent) refuses activation: again with focus.
        docker exec "$ctr" "$here/sessions/sway-start.sh"
        sleep 3
        docker exec "$ctr" "$here/sessions/enter.sh" sway python3 "$here/gate.py" sway-activate f6 fullscreen tests outputs || true
    fi
done
docker exec "$ctr" sh -c 'pacman -Q sway wlroots0.20 kwin mutter hyprland xorg-server-xvfb openbox orca at-spi2-core libei mesa ibus ibus-hangul xdg-desktop-portal xdg-desktop-portal-kde xdg-desktop-portal-gtk' \
    > "$evidence/versions.txt"
echo "evidence: $evidence"
