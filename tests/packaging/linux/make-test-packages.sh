#!/bin/sh
#   make-test-packages.sh --tools <dir> --out <dir> --version <V>
set -eu

tools='' out='' version=''
while [ $# -gt 0 ]; do
    case "$1" in
        --tools) tools=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        --version) version=$2; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[ -n "$tools" ] && [ -n "$out" ] && [ -n "$version" ] || { echo "usage: see $0" >&2; exit 2; }
mkdir -p "$out"
out=$(cd "$out" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
nfpm=$("$(cd "$(dirname "$0")" && pwd)/../../../packaging/linux/fetch-tool.sh" nfpm "$tools")

printf '#!/bin/sh\necho "legacy stand-in"\n' > "$work/hikarisub"
chmod 0755 "$work/hikarisub"
echo "only in the legacy stand-in" > "$work/legacy-only.txt"
printf 'deb\n' > "$work/kind-deb"
printf 'rpm\n' > "$work/kind-rpm"

package() # <name> <semver> <file base>
{
    core=${2%%-*}
    pre=''
    [ "$core" = "$2" ] || pre=${2#*-}
    cat > "$work/nfpm.yaml" <<EOF
name: $1
arch: amd64
platform: linux
version: "$core"
version_schema: none
prerelease: "$pre"
release: "1"
maintainer: altqx
description: HikariSub package test stand-in
license: GPL-3.0-or-later
contents:
  - src: $work/hikarisub
    dst: /opt/hikarisub/hikarisub
    file_info: {mode: 0755}
  - src: $work/legacy-only.txt
    dst: /opt/hikarisub/legacy-only.txt
  - src: $work/kind-deb
    dst: /opt/hikarisub/package-kind.txt
    packager: deb
  - src: $work/kind-rpm
    dst: /opt/hikarisub/package-kind.txt
    packager: rpm
  - src: /opt/hikarisub/hikarisub
    dst: /usr/bin/hikarisub
    type: symlink
EOF
    SOURCE_DATE_EPOCH=0 "$nfpm" package --config "$work/nfpm.yaml" --packager deb --target "$out/$3-linux-amd64.deb"
    SOURCE_DATE_EPOCH=0 "$nfpm" package --config "$work/nfpm.yaml" --packager rpm --target "$out/$3-linux-x86_64.rpm"
}

package hikarisub 0.0.9 hikarisub-0.0.9
package hikarisub-other "$version" "hikarisub-wrongname-$version"
