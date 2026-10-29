#!/bin/sh
# Renders one of the two AUR packages for one release:
#
#   hikarisub-stable-bin  stable releases (X.Y.Z) only; channel "stable"
#   hikarisub-beta-bin    every release, prereleases included; channel "beta"
#
#   packaging/linux/aur/render-pkgbuild.sh [--offline] [--deps-map <file>] <package> <version> <tar.gz> [out dir]
#   packaging/linux/aur/render-pkgbuild.sh --packages <version>
#   packaging/linux/aur/render-pkgbuild.sh --pkgver <version>
#
# <version> is X.Y.Z or X.Y.Z-{alpha,beta,rc}.N, without the "v"; <out dir> defaults to packaging/linux/aur/<package>.
# pkgver cannot hold a "-": 0.0.2-rc.1 is 0.0.2rc.1, which vercmp sorts below 0.0.2 and above 0.0.1.
#
# Exits 1 for a version the package does not carry, and 3 for one lower than the AUR's ($AUR_RPC or the AUR RPC),
# so a late release cannot downgrade AUR users; --offline skips that check (test renders).
#
# Same bytes on the main and legacy branches.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
linux=$(cd "$here/.." && pwd)

valid_version() {
    printf '%s\n' "$1" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+(-(alpha|beta|rc)\.[0-9]+)?$'
}
is_prerelease() {
    case $1 in *-*) return 0 ;; *) return 1 ;; esac
}
pkgver_of() {
    printf '%s\n' "$1" | sed 's/-//'
}
run_vercmp() {
    if command -v vercmp >/dev/null 2>&1; then
        vercmp "$1" "$2"
    else
        docker run --rm "$("$linux/fetch-tool.sh" --image archlinux)" vercmp "$1" "$2"
    fi
}

case ${1-} in
--packages | --pkgver)
    [ $# -eq 2 ] || { echo "usage: $0 $1 <version>" >&2; exit 2; }
    valid_version "$2" || { echo "error: $2 is not X.Y.Z or X.Y.Z-{alpha,beta,rc}.N" >&2; exit 1; }
    if [ "$1" = --pkgver ]; then
        pkgver_of "$2"
    else
        is_prerelease "$2" || echo hikarisub-stable-bin
        echo hikarisub-beta-bin
    fi
    exit 0
    ;;
esac

offline=false
map=$linux/deps-arch.map
while [ $# -gt 0 ]; do
    case $1 in
    --offline) offline=true; shift ;;
    --deps-map) map=$2; shift 2 ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) break ;;
    esac
done
if [ $# -lt 3 ] || [ $# -gt 4 ]; then
    echo "usage: $0 [--offline] [--deps-map <file>] <package> <version> <tar.gz> [out dir]" >&2
    exit 2
fi
package=$1 version=$2 tarball=$3 out=${4:-$here/$1}

case $package in
hikarisub-stable-bin)
    channel=stable
    pkgdesc='Subtitle editor for ASS/SSA, SRT and MicroDVD (prebuilt from the GitHub release)'
    ;;
hikarisub-beta-bin)
    channel=beta
    pkgdesc='Subtitle editor for ASS/SSA, SRT and MicroDVD (prebuilt from the newest GitHub release, prereleases included)'
    ;;
*) echo "error: unknown package $package (hikarisub-stable-bin or hikarisub-beta-bin)" >&2; exit 2 ;;
esac
if ! valid_version "$version"; then
    echo "error: $version is not X.Y.Z or X.Y.Z-{alpha,beta,rc}.N" >&2
    exit 1
fi
if [ $channel = stable ] && is_prerelease "$version"; then
    echo "error: $version is a prerelease; $package carries stable releases only" >&2
    exit 1
fi
[ -f "$tarball" ] || { echo "error: no such file: $tarball" >&2; exit 2; }
[ -f "$map" ] || { echo "error: no such map file: $map" >&2; exit 2; }
pkgver=$(pkgver_of "$version")

if [ "$offline" = false ]; then
    reply=$(curl -fsSL --retry 3 "${AUR_RPC:-https://aur.archlinux.org/rpc/v5/info}?arg[]=$package")
    aur=$(printf '%s' "$reply" | python3 -c 'import json, sys
r = json.load(sys.stdin)["results"]
print(r[0]["Version"].rsplit("-", 1)[0] if r else "")')
    if [ -n "$aur" ]; then
        if [ "$(run_vercmp "$pkgver" "$aur")" -lt 0 ]; then
            echo "error: $pkgver is lower than $package $aur on the AUR" >&2
            exit 3
        fi
        echo "The AUR has $package $aur"
    else
        echo "The AUR has no $package yet"
    fi
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
tar -xzf "$tarball" -C "$work"
tree=$work/hikarisub-linux-x86_64
[ -x "$tree/hikarisub" ] || { echo "error: $tarball is not a hikarisub-linux-x86_64 tree" >&2; exit 1; }
rm -rf "$tree/share" "$tree/install-desktop-integration.sh" "$tree/SHA256SUMS"

"$linux/resolve-deps.sh" arch "$map" 0 "$tree" > "$work/deps"
quote() { sed "s/'/'\\\\''/g; s/.*/'&'/" | tr '\n' ' ' | sed 's/ $//'; }
depends=$(awk -F '\t' '$1 == "depends" { print $2 }' "$work/deps" | quote)
optdepends=$(awk -F '\t' '$1 == "optdepends" { print $2 }' "$work/deps" | quote)
sha256=$(sha256sum "$tarball" | cut -d ' ' -f 1)

mkdir -p "$out"
out=$(cd "$out" && pwd)
python3 - "$here/PKGBUILD.in" "$out/PKGBUILD" "$package" "$version" "$channel" "$pkgver" "'$pkgdesc'" \
    "$sha256" "$depends" "$optdepends" <<'EOF'
import sys
src, dst = sys.argv[1:3]
keys = ("@PKGNAME@", "@VERSION@", "@CHANNEL@", "@PKGVER@", "@PKGDESC@", "@SHA256@", "@DEPENDS@", "@OPTDEPENDS@")
text = open(src).read()
for key, value in zip(keys, sys.argv[3:]):
    text = text.replace(key, value)
for key in keys:
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
echo "Rendered $out/PKGBUILD and $out/.SRCINFO for $package $pkgver ($version, channel $channel)"
