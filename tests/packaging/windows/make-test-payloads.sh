#!/usr/bin/env bash
# Build the two test installers installer-tests.ps1 expects, from one staged
# Windows payload (a HikariSub_x64 folder, e.g. the extracted portable zip):
#
#   make-test-payloads.sh <payload dir> <out dir> [codebase] [old] [new]
#
# <out>/HikariSub-<old>-windows-x64-setup.exe  the payload as it is
# <out>/HikariSub-<new>-windows-x64-setup.exe  the payload without Themes\ and
#   HikariSub_AVX.exe/.pdb, plus NewInB.txt and Extra\sub\file.txt, so an
#   upgrade has orphans to remove and new files to add.
#
# Defaults: legacy 0.0.2 -> 0.0.3. Needs what build-installer.sh needs.
set -euo pipefail
payload=$1 out=$2 codebase=${3:-legacy} old=${4:-0.0.2} new=${5:-0.0.3}
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
mkdir -p "$out"
out=$(cd "$out" && pwd)
work="$out/work"
rm -rf "$work"
mkdir -p "$work"
cp -a "$payload" "$work/payload-old"
cp -a "$payload" "$work/payload-new"
rm -rf "$work/payload-new/Themes" "$work/payload-new/HikariSub_AVX.exe" "$work/payload-new/HikariSub_AVX.pdb"
echo "new in b" > "$work/payload-new/NewInB.txt"
mkdir -p "$work/payload-new/Extra/sub"
echo e > "$work/payload-new/Extra/sub/file.txt"

icon=()
[[ -f $repo/HikariSub/Bitmaps/HikariLargeIcon.ico ]] && icon=(--icon "$repo/HikariSub/Bitmaps/HikariLargeIcon.ico")
for which in old new; do
  version=$old
  [[ $which == new ]] && version=$new
  "$repo/packaging/windows/build-installer.sh" --payload "$work/payload-$which" --version "$version" \
    --codebase "$codebase" --out "$out/HikariSub-$version-windows-x64-setup.exe" --work "$work/nsis-$which" "${icon[@]}"
done
rm -rf "$work/payload-old" "$work/payload-new"
