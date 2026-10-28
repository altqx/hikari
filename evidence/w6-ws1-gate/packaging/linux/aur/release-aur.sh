#!/bin/sh
# Publishes one release to the AUR packages it belongs to (installers spec,
# 5.5): a stable X.Y.Z to hikarisub-stable-bin and hikarisub-beta-bin, a
# prerelease to hikarisub-beta-bin only.
#
#   packaging/linux/aur/release-aur.sh [options] <version> <tar.gz> <out dir>
#
# <tar.gz> is the release's hikarisub-<version>-linux-x86_64.tar.gz, already
# checked against the release's SHA256SUMS. For each package:
#
# 1. With --releases, a package is skipped when the release list has a newer
#    release of the same codebase (0.0.x legacy, 0.1.0 and up Qt) for its
#    channel: any release for hikarisub-beta-bin, a stable one for
#    hikarisub-stable-bin. So an older tag never replaces a newer one, even
#    on a package the AUR does not have yet.
# 2. render-pkgbuild.sh renders <out dir>/<package>/PKGBUILD and .SRCINFO.
#    A version lower than the package's on the AUR is skipped.
# 3. The package is built with makepkg as a non-root user in the archlinux
#    container pinned in packaging/linux/tools.lock, from the release's
#    published URL (or, with --local-source, from <tar.gz> placed beside the
#    PKGBUILD), installed with pacman, and checked: hikarisub --version
#    prints "<version> (arch)", and the markers say "arch" and the channel.
# 4. publish-aur.sh pushes it.
#
# Options:
#   --releases <file>    GitHub's release list, a JSON array of objects with
#                        tag_name, draft and prerelease (gh api's pages, merged)
#   --deps-map <file>    for render-pkgbuild.sh (the tag's own deps-arch.map)
#   --offline            no AUR RPC check in render-pkgbuild.sh (tests)
#   --no-build           skip step 3 (tests)
#   --local-source       see step 3
#   --dry-run, --strict  for publish-aur.sh
#   --remote-base <url>  publish to <url>/<package>.git instead of the AUR
#
# A package refused in step 1 or 2 is a ::warning::, and so are a missing key
# and a refused push (publish-aur.sh). A failed render or build is an error.
# With --strict, a missing key or a refused push is an error too, and so is a
# run that refused every package.
#
# This file has the same bytes on the legacy (main) and Qt (qt) branches.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
linux=$(cd "$here/.." && pwd)
releases= render_opts= publish_opts= build=true local_source=false remote_base= strict=false
while [ $# -gt 0 ]; do
    case $1 in
    --releases) releases=$2; shift 2 ;;
    --deps-map) render_opts="$render_opts --deps-map $2"; shift 2 ;;
    --offline) render_opts="$render_opts --offline"; shift ;;
    --no-build) build=false; shift ;;
    --local-source) local_source=true; shift ;;
    --dry-run) publish_opts="$publish_opts --dry-run"; shift ;;
    --strict) publish_opts="$publish_opts --strict"; strict=true; shift ;;
    --remote-base) remote_base=$2; shift 2 ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) break ;;
    esac
done
if [ $# -ne 3 ]; then
    echo "usage: $0 [options] <version> <tar.gz> <out dir>" >&2
    exit 2
fi
version=$1 tarball=$(cd "$(dirname "$2")" && pwd)/$(basename "$2") out=$3
[ -f "$tarball" ] || { echo "error: no such file: $tarball" >&2; exit 2; }
[ -z "$releases" ] || [ -f "$releases" ] || { echo "error: no such file: $releases" >&2; exit 2; }
packages=$("$here/render-pkgbuild.sh" --packages "$version")
mkdir -p "$out"
out=$(cd "$out" && pwd)

skip() { # <message>
    echo "::warning::$1"
}

# The newest release of <version>'s codebase for a channel, or nothing.
newest() { # <stable|beta>
    python3 - "$releases" "$version" "$1" <<'EOF'
import json, re, sys
path, version, channel = sys.argv[1:]
pattern = re.compile(r"^(\d+)\.(\d+)\.(\d+)(?:-(alpha|beta|rc)\.(\d+))?$")
def key(v):
    m = pattern.match(v)
    if not m:
        return None
    stage = {"alpha": 0, "beta": 1, "rc": 2, None: 3}[m.group(4)]
    return (int(m.group(1)), int(m.group(2)), int(m.group(3)), stage, int(m.group(5) or 0))
def legacy(k):
    return k[:2] == (0, 0)
mine = key(version)
best = None
for release in json.load(open(path)):
    tag = release.get("tag_name", "")
    if release.get("draft") or not tag.startswith("v"):
        continue
    k = key(tag[1:])
    if k is None or legacy(k) != legacy(mine):
        continue
    if channel == "stable" and (release.get("prerelease") or k[3] != 3):
        continue
    if best is None or k > best[0]:
        best = (k, tag[1:])
if best and best[0] > mine:
    print(best[1])
EOF
}

published=0
for package in $packages; do
    channel=${package#hikarisub-} channel=${channel%-bin}
    echo "== $package ($channel) $version"
    if [ -n "$releases" ]; then
        newer=$(newest "$channel")
        if [ -n "$newer" ]; then
            skip "$package: v$newer is newer than v$version, so v$version is not published to it."
            continue
        fi
    fi

    rc=0
    # shellcheck disable=SC2086
    "$here/render-pkgbuild.sh" $render_opts "$package" "$version" "$tarball" "$out/$package" || rc=$?
    if [ $rc = 3 ]; then
        skip "$package: the AUR has a newer version than $version, so it is not published to it."
        continue
    fi
    [ $rc = 0 ] || { echo "::error::render-pkgbuild.sh $package $version failed ($rc)"; exit 1; }

    if [ "$build" = true ]; then
        mounts="-v $out/$package:/aur:ro"
        [ "$local_source" = true ] && mounts="$mounts -v $tarball:/source.tar.gz:ro"
        # shellcheck disable=SC2086
        docker run --rm $mounts -e VERSION="$version" -e CHANNEL="$channel" -e LOCAL_SOURCE="$local_source" \
            "$("$linux/fetch-tool.sh" --image archlinux)" bash -euo pipefail -c '
            pacman -Syu --noconfirm --needed base-devel sudo > /dev/null
            useradd -m builder
            echo "builder ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/builder
            cp /aur/PKGBUILD /aur/.SRCINFO /home/builder/
            if [ "$LOCAL_SOURCE" = true ]; then
                cp /source.tar.gz "/home/builder/hikarisub-$VERSION-linux-x86_64.tar.gz"
            fi
            chown -R builder /home/builder
            cd /home/builder
            sudo -u builder makepkg --printsrcinfo | diff - .SRCINFO
            sudo -u builder makepkg -si --noconfirm
            got=$(/usr/bin/hikarisub --version)
            test "$got" = "$VERSION (arch)" || { echo "hikarisub --version: $got"; exit 1; }
            test "$(cat /opt/hikarisub/package-kind.txt)" = arch
            test "$(cat /opt/hikarisub/aur-channel.txt)" = "$CHANNEL"
            echo "Built and installed: hikarisub --version prints $got, channel $CHANNEL"
        ' || { echo "::error::$package $version did not build or install in the archlinux container"; exit 1; }
    fi

    remote_opt=
    [ -z "$remote_base" ] || remote_opt="--remote $remote_base/$package.git"
    # shellcheck disable=SC2086
    "$here/publish-aur.sh" $publish_opts $remote_opt "$package" "$version" "$out/$package"
    published=$((published + 1))
done
if [ "$strict" = true ] && [ $published = 0 ]; then
    echo "::error::$version was published to no AUR package."
    exit 1
fi
