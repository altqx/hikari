#!/usr/bin/env bash
# Build the small installers of installer-tests.ps1's semver phase: one per
# version in SEMVER_SEQUENCE, over a two-file payload, into <out dir>.
#
#   make-semver-installers.sh <out dir>
set -euo pipefail
out=$1
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
mkdir -p "$out"
out=$(cd "$out" && pwd)
payload="$out/work/payload"
rm -rf "$out/work"
mkdir -p "$payload"
printf 'not a program\n' > "$payload/HikariSub.exe"
printf 'semver test payload\n' > "$payload/readme.txt"
# Kept in step with $SemverSequence in installer-tests.ps1.
for version in 0.0.3-rc.2 0.0.3-rc.10 0.0.3-rc.9 0.0.3 0.0.10 0.0.9 \
  0.1.0-alpha 0.1.0-alpha.1 0.1.0-alpha.beta 0.1.0-beta 0.1.0-BETA.1 \
  0.1.0-beta.11 0.1.0-rc.1 0.1.0; do
  codebase=legacy
  [[ $version == 0.0.* ]] || codebase=qt
  "$repo/packaging/windows/build-installer.sh" --payload "$payload" --version "$version" \
    --codebase "$codebase" --out "$out/HikariSub-$version-windows-x64-setup.exe" --work "$out/work/nsis"
done
