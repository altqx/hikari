#!/bin/bash
# The AUR package hikarisub-beta-bin in archlinux:latest (installers spec
# 8.2), which every release goes to; hikarisub-stable-bin, rendered from the
# same template, differs in its name and channel only
# (tests/packaging/linux/aur-scripts-test.sh). Run as root; the Linux
# packages workflow calls it.
#
#   aur-test.sh --dist <dir> --version <V> [--expect-version-switch] [--published]
#
# 1. Renders the PKGBUILD from <dist>/hikarisub-<V>-linux-x86_64.tar.gz
#    (render-pkgbuild.sh, as the non-root user builder).
# 2. makepkg -s with the tar.gz placed beside the PKGBUILD (makepkg still
#    checks its SHA-256), or, with --published, downloading it from the
#    release (spec 7, step 5).
# 3. namcap on the PKGBUILD and the package; only the accepted findings.
# 4. makepkg --printsrcinfo matches .SRCINFO.
# 5. pacman -U from the repositories alone; the app starts, the markers are
#    "arch" and "beta", no update helper or polkit policy.
# 6. pacman -R: /opt/hikarisub gone, $HOME untouched.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../.." && pwd)
dist='' version='' expect_version=0 published=0
while [ $# -gt 0 ]; do
    case "$1" in
        --dist) dist=$2; shift 2 ;;
        --version) version=$2; shift 2 ;;
        --expect-version-switch) expect_version=1; shift ;;
        --published) published=1; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
pass() { echo "PASS: $*"; }
skip() { echo "SKIP: $*"; }
fail() { echo "FAIL: $*" >&2; exit 1; }

tarball=$dist/hikarisub-$version-linux-x86_64.tar.gz
[ $published = 1 ] || [ -f "$tarball" ] || fail "no $tarball"

pacman -Syu --noconfirm --needed base-devel namcap sudo python xorg-server-xvfb xorg-xauth mesa desktop-file-utils appstream \
    > /dev/null
id builder > /dev/null 2>&1 || useradd -m builder
printf 'builder ALL=(ALL) NOPASSWD: /usr/bin/pacman\n' > /etc/sudoers.d/builder
chmod 0440 /etc/sudoers.d/builder
id tester > /dev/null 2>&1 || useradd -m tester
runuser -u tester -- sh -c 'mkdir -p ~/.local/share && echo keep > ~/.local/share/hikari-test-marker'

work=/home/builder/aur
rm -rf "$work" && mkdir -p "$work/src" && cp -r "$src/packaging" "$src/tools" "$work/src/"
if [ $published = 1 ]; then
    curl -fsSL -o "$work/release.tar.gz" \
        "https://github.com/altqx/hikari/releases/download/v$version/hikarisub-$version-linux-x86_64.tar.gz"
    tarball=$work/release.tar.gz
else
    cp "$tarball" "$work/"
    tarball=$work/${tarball##*/}
fi
chown -R builder "$work"

# 1. Render.
pkgver=$("$work/src/packaging/linux/aur/render-pkgbuild.sh" --pkgver "$version")
runuser -u builder -- "$work/src/packaging/linux/aur/render-pkgbuild.sh" --offline hikarisub-beta-bin "$version" \
    "$tarball" "$work/pkg"
cat "$work/pkg/PKGBUILD"
grep -q "^pkgver=$pkgver\$" "$work/pkg/PKGBUILD" || fail "pkgver"
grep -q "^_version=$version\$" "$work/pkg/PKGBUILD" || fail "_version"
grep -q "^sha256sums=('$(sha256sum "$tarball" | cut -d' ' -f1)')\$" "$work/pkg/PKGBUILD" || fail "sha256sums"
pass "rendered PKGBUILD $version"

# hikarisub-stable-bin refuses a prerelease.
if runuser -u builder -- "$work/src/packaging/linux/aur/render-pkgbuild.sh" --offline hikarisub-stable-bin \
        "${version%%-*}-rc.1" "$tarball" "$work/pre" > /dev/null 2>&1; then
    fail "render-pkgbuild.sh accepted a prerelease for hikarisub-stable-bin"
fi
pass "render-pkgbuild.sh refuses a prerelease for hikarisub-stable-bin"

# 2. Build. The local tar.gz stands in for the download under its source name.
[ $published = 1 ] || cp "$tarball" "$work/pkg/hikarisub-$version-linux-x86_64.tar.gz"
chown -R builder "$work"
(cd "$work/pkg" && runuser -u builder -- makepkg -s --noconfirm > "$work/makepkg.log" 2>&1) ||
    { cat "$work/makepkg.log" >&2; fail "makepkg"; }
pkgfile=$(ls "$work"/pkg/hikarisub-beta-bin-"$pkgver"-1-x86_64.pkg.tar.* | head -1)
pass "makepkg built ${pkgfile##*/}"

# 3. namcap: only the /opt error and the $CARCH hint are accepted.
namcap_check() # <file>
{
    local out bad
    out=$(namcap "$1" 2>&1 || true)
    echo "$out"
    bad=$(printf '%s\n' "$out" | grep -E ' (E|W): ' |
        grep -vE "ELF files outside of a valid path \('opt/'\)|Reference to x86_64 should be changed to \\\$CARCH" ||
        true)
    # Warnings other than the two above are reported; errors fail.
    if printf '%s\n' "$bad" | grep -q ' E: '; then
        fail "namcap errors on ${1##*/}: $(printf '%s\n' "$bad" | grep ' E: ')"
    fi
    if [ -n "$bad" ]; then
        echo "namcap warnings (reported, not fatal):"
        printf '%s\n' "$bad"
    fi
    pass "namcap ${1##*/}"
}
namcap_check "$work/pkg/PKGBUILD"
namcap_check "$pkgfile"

# 4. .SRCINFO is what makepkg prints.
(cd "$work/pkg" && runuser -u builder -- makepkg --printsrcinfo | diff - .SRCINFO) || fail ".SRCINFO is stale"
pass ".SRCINFO matches"

# 5. Install with pacman alone, then the checks.
pacman -U --noconfirm "$pkgfile" > /dev/null
[ "$(cat /opt/hikarisub/package-kind.txt)" = arch ] || fail "package-kind.txt is not arch"
[ "$(readlink /usr/bin/hikarisub)" = /opt/hikarisub/hikarisub ] || fail "/usr/bin/hikarisub"
[ ! -e /opt/hikarisub/libexec/hikarisub-update-helper ] || fail "the AUR package ships the update helper"
[ ! -e /usr/share/polkit-1/actions/io.github.altqx.HikariSub.update.policy ] || fail "the AUR package ships the polkit policy"
[ -f /usr/share/licenses/hikarisub-beta-bin/LICENSE ] || fail "no license"
[ "$(cat /opt/hikarisub/aur-channel.txt)" = beta ] || fail "aur-channel.txt is not beta"
grep -q '^Exec=hikarisub %f$' /usr/share/applications/io.github.altqx.HikariSub.desktop || fail "Exec="
desktop-file-validate /usr/share/applications/io.github.altqx.HikariSub.desktop || fail "desktop-file-validate"
appstreamcli validate --no-net /usr/share/metainfo/io.github.altqx.HikariSub.metainfo.xml || fail "appstreamcli"
unresolved=$(find /opt/hikarisub -type f \( -name '*.so*' -o -perm -u+x \) -print0 | xargs -0 -r ldd 2>/dev/null |
             grep 'not found' | sort -u || true)
[ -z "$unresolved" ] || fail "missing after pacman -U: $unresolved"
pass "pacman -U: markers arch and beta, no helper or policy, every library resolves, desktop files valid"

home=/home/tester
as_tester() { runuser -u tester -- env HOME=$home "$@"; }
if [ $expect_version = 1 ]; then
    # The line is on stdout; Qt and the loader may warn on stderr.
    out=$(as_tester timeout 20 /usr/bin/hikarisub --version 2>/tmp/version-stderr.txt) ||
        fail "--version exited $?: $out $(cat /tmp/version-stderr.txt)"
    [[ $out =~ ^${version//./\\.}\ \((arch|AUR)\)$ ]] || fail "--version printed '$out' (stderr: $(cat /tmp/version-stderr.txt))"
    pass "--version: $out"
else
    skip "--version (the build has no --version switch yet)"
fi
for platform in xcb offscreen; do
    set +e
    if [ $platform = xcb ]; then
        as_tester env QT_QPA_PLATFORM=xcb xvfb-run -a -s '-screen 0 1280x1024x24' timeout 15 /usr/bin/hikarisub \
            > /tmp/aur-gui.log 2>&1
    else
        as_tester env QT_QPA_PLATFORM=offscreen timeout 15 /usr/bin/hikarisub > /tmp/aur-gui.log 2>&1
    fi
    rc=$?
    set -e
    [ $rc -eq 124 ] || { cat /tmp/aur-gui.log >&2; fail "the app did not keep running on $platform (exit $rc)"; }
    pass "the installed app starts on $platform"
done

# 6. Remove.
pacman -R --noconfirm hikarisub-beta-bin > /dev/null
[ ! -e /opt/hikarisub ] || fail "/opt/hikarisub is still there"
[ "$(cat $home/.local/share/hikari-test-marker)" = keep ] || fail "\$HOME changed"
pass "pacman -R: /opt/hikarisub gone, \$HOME untouched"
