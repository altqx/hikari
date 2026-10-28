#!/bin/sh
# Renders the AUR package hikarisub-bin for one stable release.
#
#   packaging/linux/aur/render-pkgbuild.sh [--offline] <version> <tar.gz> [out dir]
#
# <tar.gz> is the release's hikarisub-<version>-linux-x86_64.tar.gz (the
# package's source). Writes <out dir>/PKGBUILD from PKGBUILD.in, with the
# version, the tarball's SHA-256 and the dependencies: the host libraries the
# tree needs (tools/release/host-sonames.sh) mapped by
# packaging/linux/deps-arch.map, plus its runtime section. Then writes
# <out dir>/.SRCINFO with makepkg --printsrcinfo, as a non-root user; without
# a local makepkg that runs in the archlinux container pinned in
# packaging/linux/tools.lock. <out dir> defaults to
# packaging/linux/aur/hikarisub-bin, the folder the owner pushes to the AUR.
#
# Refuses a prerelease (the AUR package carries stable releases only) and a
# version lower than the one on the AUR, so that a late legacy release cannot
# downgrade AUR users once Qt has taken the package over. --offline skips
# that AUR check, for test renders only.
#
# This file has the same bytes on the legacy (main) and Qt (qt) branches.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
linux=$(cd "$here/.." && pwd)
offline=false
if [ "${1-}" = --offline ]; then
    offline=true
    shift
fi
if [ $# -lt 2 ] || [ $# -gt 3 ]; then
    echo "usage: $0 [--offline] <version> <tar.gz> [out dir]" >&2
    exit 2
fi
version=$1 tarball=$2 out=${3:-$here/hikarisub-bin}

if ! printf '%s\n' "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "error: $version is not a stable X.Y.Z version; the AUR package carries stable releases only" >&2
    exit 1
fi
[ -f "$tarball" ] || { echo "error: no such file: $tarball" >&2; exit 2; }

# Lower than the AUR's version: refuse.
if [ "$offline" = false ]; then
    reply=$(curl -fsSL --retry 3 'https://aur.archlinux.org/rpc/v5/info?arg[]=hikarisub-bin')
    aur=$(printf '%s' "$reply" | python3 -c 'import json, sys
r = json.load(sys.stdin)["results"]
print(r[0]["Version"].split("-")[0] if r else "")')
    if [ -n "$aur" ]; then
        lowest=$(printf '%s\n%s\n' "$aur" "$version" | sort -V | head -n 1)
        if [ "$lowest" = "$version" ] && [ "$aur" != "$version" ]; then
            echo "error: $version is lower than hikarisub-bin $aur on the AUR" >&2
            exit 1
        fi
        echo "The AUR has hikarisub-bin $aur"
    else
        echo "The AUR has no hikarisub-bin yet"
    fi
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
tar -xzf "$tarball" -C "$work"
tree=$work/hikarisub-linux-x86_64
[ -x "$tree/hikarisub" ] || { echo "error: $tarball is not a hikarisub-linux-x86_64 tree" >&2; exit 1; }
rm -rf "$tree/share" "$tree/install-desktop-integration.sh" "$tree/SHA256SUMS"

"$linux/resolve-deps.sh" arch "$linux/deps-arch.map" 0 "$tree" > "$work/deps"
quote() { sed "s/'/'\\\\''/g; s/.*/'&'/" | tr '\n' ' ' | sed 's/ $//'; }
depends=$(awk -F '\t' '$1 == "depends" { print $2 }' "$work/deps" | quote)
optdepends=$(awk -F '\t' '$1 == "optdepends" { print $2 }' "$work/deps" | quote)
sha256=$(sha256sum "$tarball" | cut -d ' ' -f 1)

mkdir -p "$out"
out=$(cd "$out" && pwd)
python3 - "$here/PKGBUILD.in" "$out/PKGBUILD" "$version" "$sha256" "$depends" "$optdepends" <<'EOF'
import sys
src, dst, version, sha256, depends, optdepends = sys.argv[1:]
text = open(src).read()
for key, value in (("@VERSION@", version), ("@SHA256@", sha256),
                   ("@DEPENDS@", depends), ("@OPTDEPENDS@", optdepends)):
    text = text.replace(key, value)
for key in ("@VERSION@", "@SHA256@", "@DEPENDS@", "@OPTDEPENDS@"):
    if key in text:
        sys.exit("unfilled placeholder " + key)
open(dst, "w").write(text)
EOF

if command -v makepkg >/dev/null 2>&1 && [ "$(id -u)" != 0 ]; then
    (cd "$out" && makepkg --printsrcinfo > .SRCINFO)
else
    image=$("$linux/fetch-tool.sh" --image archlinux)
    docker run --rm -v "$out:/pkg" "$image" sh -c '
        set -e
        useradd -m builder
        cp /pkg/PKGBUILD /home/builder/
        chown builder /home/builder/PKGBUILD
        su builder -c "cd /home/builder && makepkg --printsrcinfo" > /pkg/.SRCINFO
        chown --reference=/pkg/PKGBUILD /pkg/.SRCINFO'
fi
echo "Rendered $out/PKGBUILD and $out/.SRCINFO for hikarisub-bin $version"
