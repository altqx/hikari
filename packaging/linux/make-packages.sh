#!/bin/bash
# Builds the Qt rewrite's Linux release packages from a configured and built
# build tree (installers spec 2.1, 5.2 to 5.4):
#
#   hikarisub-<V>-linux-x86_64.tar.gz      the portable tree; also the AUR source
#   HikariSub-<V>-linux-x86_64.AppImage    (+ .zsync)
#   hikarisub-<V>-linux-amd64.deb
#   hikarisub-<V>-linux-x86_64.rpm
#
#   make-packages.sh --build-dir <dir> --version <V> --out <dir> --tools <dir>
#                    [--formats tar,appimage,native] [--allow-missing-update-helper]
#
# Run it on the build floor (packaging/linux/floor/almalinux9.sh): the
# AppImage collects its extra libraries from the build host. --tools is
# where fetch-tool.sh keeps the tools pinned in tools.lock.
# --version may differ from the build's own version only for test packages.
set -euo pipefail
LC_ALL=C
export LC_ALL
umask 022

here=$(cd "$(dirname "$0")" && pwd)
source_dir=$(cd "$here/../.." && pwd)
min_glibc=2.34
updinfo='gh-releases-zsync|altqx|hikari|latest|HikariSub-*-linux-x86_64.AppImage.zsync'

build_dir='' version='' out_dir='' tools_dir='' formats=tar,appimage,native allow_missing_helper=0
while [ $# -gt 0 ]; do
    case "$1" in
        --build-dir) build_dir=$2; shift 2 ;;
        --version) version=$2; shift 2 ;;
        --out) out_dir=$2; shift 2 ;;
        --tools) tools_dir=$2; shift 2 ;;
        --formats) formats=$2; shift 2 ;;
        --allow-missing-update-helper) allow_missing_helper=1; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[ -n "$build_dir" ] && [ -n "$version" ] && [ -n "$out_dir" ] && [ -n "$tools_dir" ] ||
    { echo "usage: $0 --build-dir <dir> --version <V> --out <dir> --tools <dir> [--formats ...]" >&2; exit 2; }
# SemVer 2.0.0 without build metadata (spec 1.2).
if ! [[ $version =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$ ]]; then
    echo "error: $version is not a SemVer release version without build metadata" >&2
    exit 2
fi
build_dir=$(cd "$build_dir" && pwd)
mkdir -p "$out_dir" "$tools_dir"
out_dir=$(cd "$out_dir" && pwd)
tools_dir=$(cd "$tools_dir" && pwd)
gen_dir=$build_dir/packaging
stage=$build_dir/package-stage
want() { [[ ",$formats," == *",$1,"* ]]; }

for f in "$gen_dir/io.github.altqx.HikariSub.desktop" "$gen_dir/io.github.altqx.HikariSub.metainfo.xml"; do
    [ -f "$f" ] || { echo "error: $f is missing; configure with HIKARI_PACKAGE_LINUX=ON" >&2; exit 1; }
done
appimagetool=$("$here/fetch-tool.sh" appimagetool "$tools_dir")
runtime_file=$("$here/fetch-tool.sh" type2-runtime "$tools_dir")
nfpm=$("$here/fetch-tool.sh" nfpm "$tools_dir")

# 1. The host-profile app tree (spec 5.2), from the install rules.
rm -rf "$stage"
mkdir -p "$stage"
tree=$stage/tree
cmake --install "$build_dir" --prefix "$tree" --component hikari_app
[ -x "$tree/hikarisub" ] || { echo "error: the install produced no hikarisub" >&2; exit 1; }
# GR1 (#364): the grammar checker's helper.
[ -x "$tree/harper-ls" ] || { echo "error: the install produced no harper-ls" >&2; exit 1; }
# KA4 (#279): the furigana suggestions' dictionary and the BSD notices.
for f in Dictionary/unidic-lite/sys.dic Dictionary/unidic-lite/matrix.bin Dictionary/unidic-lite/char.bin \
    Dictionary/unidic-lite/unk.dic Dictionary/unidic-lite/dicrc Dictionary/unidic-lite/mecabrc \
    Dictionary/unidic-lite/BSD licenses/mecab/LICENSE licenses/unidic/LICENSE \
    licenses/harper/LICENSE licenses/harper/THIRD-PARTY.txt; do
    [ -f "$tree/$f" ] || { echo "error: the install produced no $f" >&2; exit 1; }
done
# DK1 (#473): the pinned spell-check dictionaries and their notices.
for name in en_US pl th_TH ko_KR ta_IN; do
    for f in "Dictionary/$name.aff" "Dictionary/$name.dic"; do
        [ -f "$tree/$f" ] || { echo "error: the install produced no $f" >&2; exit 1; }
    done
    [ -n "$(ls -A "$tree/licenses/dictionaries/$name" 2>/dev/null)" ] ||
        { echo "error: the install produced no licenses/dictionaries/$name" >&2; exit 1; }
done
"$here/check-payload.sh" --min-glibc "$min_glibc" "$tree"

# CR1 (#452): debug info a build carries (HIKARI_DEBUG_SYMBOLS) leaves the
# packages: each ELF file's goes to package-stage/symbols/<path>.debug, and
# under .build-id/ for debuggers, linked back by .gnu_debuglink. Release CI
# keeps that folder as the version's symbols (tools/crash/symbolize.py).
symbols=$stage/symbols
while IFS= read -r -d '' f; do
    [ "$(od -An -tx1 -N4 "$f" | tr -d ' \n')" = 7f454c46 ] || continue
    # Not `readelf | grep -q`: grep leaving early can kill readelf by
    # SIGPIPE, and pipefail then skips a file that has debug info.
    [[ $(readelf -S --wide "$f" 2>/dev/null) == *' .debug_info '* ]] || continue
    rel=${f#"$tree"/}
    mkdir -p "$symbols/$(dirname "$rel")"
    objcopy --only-keep-debug --compress-debug-sections=zlib "$f" "$symbols/$rel.debug"
    objcopy --strip-debug --add-gnu-debuglink="$symbols/$rel.debug" "$f"
    id=$(readelf -n "$f" 2>/dev/null | sed -n 's/^ *Build ID: \([0-9a-f]*\)$/\1/p')
    id=${id%%$'\n'*}
    if [ -n "$id" ]; then
        mkdir -p "$symbols/.build-id/${id:0:2}"
        ln -sf "../../$rel.debug" "$symbols/.build-id/${id:0:2}/${id:2}.debug"
    fi
    echo "symbols: $rel${id:+ (build-id $id)}"
done < <(find "$tree" -type f -print0)

normalize_modes() # <dir>: directories and executables 0755, other files 0644
{
    find "$1" -type d -exec chmod 0755 {} +
    find "$1" -type f -exec sh -c 'for f; do
        if [ "$(od -An -tx1 -N4 "$f" | tr -d " \n")" = 7f454c46 ] && [ "${f##*.}" != o ]; then
            chmod 0755 "$f"; else chmod 0644 "$f"; fi; done' sh {} +
}
normalize_modes "$tree"
# Shared libraries and plugins need no execute bit; executables keep it.
find "$tree" -type f \( -name '*.so' -o -name '*.so.*' \) -exec chmod 0644 {} +

reproducible_tar() # <archive> <dir> <top>: a deterministic .tar.gz of <dir>/<top>
{
    tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner --format=gnu \
        -C "$2" -cf - "$3" | gzip -n -9 > "$1"
}

# 2. The portable tar.gz, in the layout the AUR PKGBUILD relies on (spec 5.5).
if want tar; then
    top=hikarisub-linux-x86_64
    root=$stage/tar/$top
    mkdir -p "$root"
    cp -a "$tree/." "$root/"
    mkdir -p "$root/share/applications" "$root/share/metainfo" "$root/share/mime/packages" "$root/share/icons"
    install -m 0644 "$gen_dir/io.github.altqx.HikariSub.desktop" "$root/share/applications/"
    install -m 0644 "$gen_dir/io.github.altqx.HikariSub.metainfo.xml" "$root/share/metainfo/"
    install -m 0644 "$source_dir/packaging/linux/mime/hikarisub.xml" "$root/share/mime/packages/"
    cp -r "$source_dir/packaging/linux/icons/hicolor" "$root/share/icons/"
    find "$root/share/icons" -type f ! -name '*.png' -delete
    install -m 0755 "$source_dir/packaging/linux/install-desktop-integration.sh" "$root/"
    normalize_modes "$root/share"
    (cd "$root" && find . -type f ! -name SHA256SUMS -printf '%P\n' | sort | xargs -d '\n' sha256sum) > "$root/SHA256SUMS"
    chmod 0644 "$root/SHA256SUMS"
    reproducible_tar "$out_dir/hikarisub-$version-linux-x86_64.tar.gz" "$stage/tar" "$top"
    echo "built hikarisub-$version-linux-x86_64.tar.gz"
fi

# 3. The AppImage: the same tree plus the full-profile libraries (spec 5.3).
if want appimage; then
    appdir=$stage/AppDir
    mkdir -p "$appdir/usr/lib/hikarisub" "$appdir/usr/lib/hikarisub-runtime/lib" "$appdir/usr/share/metainfo"
    cp -a "$tree/." "$appdir/usr/lib/hikarisub/"
    printf 'appimage\n' > "$appdir/usr/lib/hikarisub/package-kind.txt"
    # host-libraries.txt records what is left to the host: the host list's
    # matches and everything they need (the closure rule); the package
    # tests read it.
    python3 "$here/appimage/collect-runtime.py" "$tree" "$appdir/usr/lib/hikarisub-runtime/lib" \
        "$here/appimage/host-libs.txt" "$appdir/usr/lib/hikarisub-runtime/host-libraries.txt"
    install -m 0755 "$here/appimage/AppRun" "$appdir/AppRun"
    install -m 0644 "$gen_dir/io.github.altqx.HikariSub.desktop" "$appdir/"
    install -m 0644 "$gen_dir/io.github.altqx.HikariSub.metainfo.xml" \
        "$appdir/usr/share/metainfo/io.github.altqx.HikariSub.appdata.xml"
    mkdir -p "$appdir/usr/share/applications"
    install -m 0644 "$gen_dir/io.github.altqx.HikariSub.desktop" "$appdir/usr/share/applications/"
    install -m 0644 "$source_dir/packaging/linux/icons/hicolor/256x256/apps/hikarisub.png" "$appdir/hikarisub.png"
    ln -s hikarisub.png "$appdir/.DirIcon"
    "$here/check-payload.sh" --min-glibc "$min_glibc" "$appdir"
    name=HikariSub-$version-linux-x86_64.AppImage
    rm -f "$out_dir/$name" "$out_dir/$name.zsync"
    # appimagetool is itself an AppImage; extract-and-run needs no FUSE.
    (cd "$out_dir" && APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 VERSION=$version "$appimagetool" --no-appstream \
        --runtime-file "$runtime_file" -u "$updinfo" "$appdir" "$name")
    [ -f "$out_dir/$name.zsync" ] || { echo "error: no $name.zsync (is zsyncmake installed?)" >&2; exit 1; }
    echo "built $name"
fi

# 4. deb and rpm with nFPM (spec 5.4).
if want native; then
    helper=$source_dir/packaging/linux/update-helper/hikarisub-update-helper
    policy=$source_dir/packaging/linux/update-helper/io.github.altqx.HikariSub.update.policy
    if [ ! -f "$helper" ] || [ ! -f "$policy" ]; then
        if [ "$allow_missing_helper" != 1 ]; then
            echo "error: the update helper or its polkit policy is missing under packaging/linux/update-helper" >&2
            exit 1
        fi
        echo "warning: packaging a placeholder update helper (packaging/linux/update-helper is missing)" >&2
        helper=$stage/placeholder-helper
        policy=$stage/placeholder.policy
        printf '#!/bin/sh\necho "hikarisub-update-helper: not part of this build" >&2\nexit 2\n' > "$helper"
        printf '<?xml version="1.0" encoding="UTF-8"?>\n<policyconfig/>\n' > "$policy"
    fi
    printf 'deb\n' > "$stage/package-kind-deb.txt"
    printf 'rpm\n' > "$stage/package-kind-rpm.txt"
    mkdir -p "$stage/deps"
    for family in deb rpm; do
        "$here/resolve-deps.sh" $family "$here/deps-$family.map" "$min_glibc" "$tree" > "$stage/deps/$family.tsv"
        for kind in depends recommends; do
            awk -F '\t' -v k=$kind '$1 == k { print $2 }' "$stage/deps/$family.tsv" > "$stage/deps/$family.$kind"
        done
    done
    core=${version%%-*}
    prerelease=''
    [ "$core" = "$version" ] || prerelease=${version#*-}
    python3 - "$here/nfpm.yaml.in" "$stage/nfpm.yaml" <<PY
import json, sys
src, dst = sys.argv[1:]
def lst(path):
    with open(path, encoding="utf-8") as f:
        return json.dumps([l.strip() for l in f if l.strip()])
text = open(src, encoding="utf-8").read()
for key, value in {
    "@VERSION@": "$core", "@PRERELEASE@": "$prerelease", "@TREE@": "$tree", "@STAGE@": "$stage",
    "@HELPER@": "$helper", "@POLICY@": "$policy", "@GEN@": "$gen_dir", "@SOURCE@": "$source_dir",
    "@DEB_DEPENDS@": lst("$stage/deps/deb.depends"), "@DEB_RECOMMENDS@": lst("$stage/deps/deb.recommends"),
    "@RPM_DEPENDS@": lst("$stage/deps/rpm.depends"), "@RPM_RECOMMENDS@": lst("$stage/deps/rpm.recommends"),
}.items():
    text = text.replace(key, value)
if "@" in "".join(l.split("#")[0] for l in text.splitlines()):
    sys.exit("unfilled placeholder in nfpm.yaml")
open(dst, "w", encoding="utf-8").write(text)
PY
    rm -f "$out_dir/hikarisub-$version-linux-amd64.deb" "$out_dir/hikarisub-$version-linux-x86_64.rpm"
    SOURCE_DATE_EPOCH=0 "$nfpm" package --config "$stage/nfpm.yaml" --packager deb \
        --target "$out_dir/hikarisub-$version-linux-amd64.deb"
    SOURCE_DATE_EPOCH=0 "$nfpm" package --config "$stage/nfpm.yaml" --packager rpm \
        --target "$out_dir/hikarisub-$version-linux-x86_64.rpm"
    echo "built hikarisub-$version-linux-amd64.deb and hikarisub-$version-linux-x86_64.rpm"
fi
