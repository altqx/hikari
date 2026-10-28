#!/usr/bin/env bash
# The silent installer checks of SPEC 8.3 on the Winix VM, in order, against
# installers built with packaging/windows/build-installer.sh:
#
#   run-installer-tests.sh <dir with the setups> <legacy V> <qt V1> <qt V2> <qt V3>
#
# The legacy setup is a legacy payload (for example v0.0.1's zip) built with
# --codebase legacy; V1 < V2 < V3 are Qt test versions (0.1.0-test.N); V1's
# payload carries the extra files orphan-test.txt and OrphanDir/gone.txt,
# which V2 must remove. The VM must have no HikariSub install. Take the lease
# first (winix lease acquire --owner "$WINIX_OWNER"); every step prints
# "ok"/"FAIL" lines and the script stops at the first failing step.
#
# The interactive pages (scope page locked by an existing install, the
# Retry prompt, the UAC refusal message, Finish starting the app) are driven
# with `winix ui` by hand; see tests/packaging/windows/README.md.
set -euo pipefail
[ $# -eq 5 ] || { sed -n '2,15p' "$0" >&2; exit 2; }
setups=$1 legacy=$2 v1=$3 v2=$4 v3=$5
repo=$(cd "$(dirname "$0")/../../.." && pwd)
config="$repo/tests/packaging/windows/winix.yaml"
owner=${WINIX_OWNER:-inst-qt-windows}
w() { winix --owner "$owner" --config "$config" "$@"; }
guest='C:\Winix\work\inst-qt-windows\out\inst'
user='C:\Users\winix\AppData\Local\Programs\HikariSub'
machine='C:\Program Files\HikariSub'
legacy_orphans='ffms2.dll|D3DX9_43.dll|HikariSub.pdb|HikariSub_AVX.pdb|Locale|Themes|Csri|LICENSE.txt'

# A job without an exit code never ran its script (the worker failed to
# start it, for example "Access to the path is denied."); it is run once more.
step() {
    local task=$1; shift
    local args=() kv
    for kv in "$@"; do args+=(--env "$kv"); done
    echo "## $task $*"
    local id code attempt
    for attempt in 1 2; do
        id=$(w run "$task" --json "${args[@]}" | head -1 | python3 -c 'import json,sys; print(json.loads(sys.stdin.readline())["id"])')
        w job wait "$id" > /dev/null || true
        winix job logs "$id" --all | sed 's/^\[out\] //'
        code=$(winix job status "$id" | python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["exitCode"] if d["exitCode"] is not None else "none: " + str(d.get("error")))')
        [[ "$code" == none:* ]] || break
        echo "job $id did not run ($code)"
    done
    [ "$code" = 0 ] || { echo "step failed (exit $code)"; exit 1; }
}

w sync "$repo" | tail -1
# However the run ends (a failed step stops it), no setup or uninstaller is
# left on the desktop: elevated, so all-users ones end too.
end_setups() {
    echo "## inst-admin ACTION=end-setups"
    local id
    id=$(w run inst-admin --json --env ACTION=end-setups | head -1 | python3 -c 'import json,sys; print(json.loads(sys.stdin.readline())["id"])') || return 0
    w job wait "$id" > /dev/null || true
    winix job logs "$id" --all | sed 's/^\[out\] //' || true
}
trap end_setups EXIT
for v in "$legacy" "$v1" "$v2" "$v3"; do
    w push "$setups/HikariSub-$v-windows-x64-setup.exe" "$guest\\HikariSub-$v-windows-x64-setup.exe" > /dev/null
done
setup() { echo "SETUP=$guest\\HikariSub-$1-windows-x64-setup.exe"; }

# Legacy per-user install, then the app run once (it writes Config/ beside it).
step inst ACTION=run "$(setup "$legacy")" "ARGS=/S /CURRENTUSER"
step inst ACTION=check-installed SCOPE=user VERSION="$legacy" LEGACY=1
step inst-detach ACTION=start-app SCOPE=user
# Silent upgrade while the app runs: 3 after the 30 s wait, nothing changed.
step inst ACTION=run "$(setup "$v1")" ARGS=/S EXPECT=3
step inst ACTION=stop-app
# Legacy to Qt: the old payload's orphans go, the user's files stay, the
# associations open the Qt program.
step inst ACTION=run "$(setup "$v1")" ARGS=/S
step inst ACTION=check-installed SCOPE=user VERSION="$v1" "ORPHANS=$legacy_orphans" 'KEEP=Config\Config.txt'
step inst ACTION=icon-ids
step inst ACTION=open-file SCOPE=user
# Qt over Qt.
step inst ACTION=run "$(setup "$v2")" ARGS=/S
step inst ACTION=check-installed SCOPE=user VERSION="$v2" 'ORPHANS=orphan-test.txt|OrphanDir' 'KEEP=Config\Config.txt'
# The updater's hand-off: gives up with 3 while the app stays, installs and
# relaunches (unelevated) once it exits.
step inst-detach ACTION=start-app SCOPE=user
step inst ACTION=update SCOPE=user VERSION="$v3" CLOSE_APP=0 EXPECT=3 "$(setup "$v3")" "ARGS=/CURRENTUSER /D=$user"
step inst ACTION=update SCOPE=user VERSION="$v3" CLOSE_APP=1 "$(setup "$v3")" "ARGS=/RELAUNCH /CURRENTUSER /D=$user"
step inst ACTION=check-installed SCOPE=user VERSION="$v3"
# Downgrades and a scope conflict are refused.
step inst ACTION=run "$(setup "$v1")" ARGS=/S EXPECT=5
step inst ACTION=run "$(setup "$legacy")" ARGS=/S EXPECT=5
step inst ACTION=run "$(setup "$v3")" "ARGS=/S /ALLUSERS" EXPECT=6
# Silent uninstall keeps the user's files.
step inst ACTION=run "SETUP=$user\\Uninstall HikariSub.exe" ARGS=/S
step inst ACTION=check-removed SCOPE=user 'KEEP=Config\Config.txt'
step inst ACTION=cleanup SCOPE=user

# All users: refused unelevated when silent, then elevated installs, update
# with an unelevated relaunch, and uninstall.
step inst ACTION=run "$(setup "$v1")" "ARGS=/S /ALLUSERS" EXPECT=4
step inst-admin ACTION=run "$(setup "$v1")" "ARGS=/S /ALLUSERS /COMPONENTS=assoc,desktop"
step inst-admin ACTION=check-installed SCOPE=machine VERSION="$v1"
step inst ACTION=run "$(setup "$v2")" "ARGS=/S /CURRENTUSER" EXPECT=6
step inst-admin ACTION=run "$(setup "$v2")" ARGS=/S
step inst-admin ACTION=check-installed SCOPE=machine VERSION="$v2" 'ORPHANS=orphan-test.txt|OrphanDir'
step inst-detach ACTION=start-app SCOPE=machine
step inst-admin ACTION=update SCOPE=machine VERSION="$v3" CLOSE_APP=1 "$(setup "$v3")" "ARGS=/RELAUNCH /ALLUSERS /D=$machine"
step inst ACTION=stop-app
step inst ACTION=run "SETUP=$machine\\Uninstall HikariSub.exe" "ARGS=/S _?=$machine" EXPECT=4
step inst-admin ACTION=run "SETUP=$machine\\Uninstall HikariSub.exe" ARGS=/S
step inst-admin ACTION=check-removed SCOPE=machine
echo "all steps passed"
