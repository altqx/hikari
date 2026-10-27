#!/bin/bash
# Package tests inside one distro container (installers spec 8.1, 8.2), Qt
# rewrite. Run as root in a fresh container with the packages mounted:
#
#   container-test.sh --dist <dir> --version <V> --tests <list> [--expect-version-switch]
#                     [--old <dir>]
#
# <list> is a comma list of:
#   appimage   AppImage start (8.1.1, 8.1.3): first, on the bare image, every
#              library the AppImage cannot resolve must be on the host list;
#              then, with the desktop baseline (the host list's libraries),
#              --version and a GUI start under Xvfb (xcb) and offscreen, with
#              no host-list library loaded from inside the AppImage.
#   tar        the portable tar.gz starts the same way.
#   native     the deb or rpm (8.1.4): installed with the distro's package
#              manager alone, it starts with only its declared dependencies;
#              desktop-file-validate, appstreamcli, /usr/bin/hikarisub.
#   upgrade    (8.1.5) the packages in --old (an older Qt test version and a
#              stand-in legacy 0.0.x), each upgraded by this version; files
#              under $HOME survive.
#   remove     (8.1.6) removal leaves no /opt/hikarisub and $HOME untouched
#              (the user folder ~/.config/hikarisub included, UD1).
#   helper     (8.1.7) the update helper, run as root with PKEXEC_UID, from
#              the --old 0.0.9 stand-in to this version, with fake-curl.sh
#              serving made-up releases' SHA256SUMS.
#   aur        (8.2) render the PKGBUILD from the tar.gz, makepkg, namcap,
#              .SRCINFO, pacman -U, start, remove. archlinux only.
#
# --expect-version-switch: the app has the --version switch (spec 8.1.1);
# without it, --version checks are skipped and reported. Exits non-zero on
# the first failure; prints PASS/SKIP lines.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../../.." && pwd)
dist='' version='' tests='' old='' expect_version=0
while [ $# -gt 0 ]; do
    case "$1" in
        --dist) dist=$2; shift 2 ;;
        --version) version=$2; shift 2 ;;
        --tests) tests=$2; shift 2 ;;
        --old) old=$2; shift 2 ;;
        --expect-version-switch) expect_version=1; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done
[ -n "$dist" ] && [ -n "$version" ] && [ -n "$tests" ] || { echo "usage: see $0" >&2; exit 2; }
want() { [[ ",$tests," == *",$1,"* ]]; }
pass() { echo "PASS: $*"; }
skip() { echo "SKIP: $*"; }
fail() { echo "FAIL: $*" >&2; exit 1; }

. /etc/os-release
case " $ID ${ID_LIKE-} " in
    *" arch "*) family=arch ;;
    *" debian "*|*" ubuntu "*) family=deb ;;
    *" suse "*|*" opensuse "*) family=zypper ;;
    *" fedora "*|*" rhel "*) family=dnf ;;
    *) fail "unknown distribution $ID" ;;
esac
echo "== $PRETTY_NAME ($family): $tests"

install_pkgs()
{
    case $family in
        deb) DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends "$@" > /dev/null ;;
        dnf) dnf -y -q install "$@" > /dev/null ;;
        zypper) zypper -q --non-interactive install --no-recommends "$@" > /dev/null ;;
        arch) pacman -S --noconfirm --needed "$@" > /dev/null ;;
    esac
}
case $family in
    deb) apt-get update -qq > /dev/null ;;
    arch) pacman -Syu --noconfirm > /dev/null ;;
    zypper) zypper -q --non-interactive refresh > /dev/null ;;
esac

# The test tools: Xvfb with xvfb-run and a software GL driver (Xvfb's GLX),
# binutils for the library checks, the desktop file and AppStream
# validators. None of them is a dependency of the app.
case $family in
    deb) tools=(xvfb xauth libgl1-mesa-dri binutils desktop-file-utils appstream procps) ;;
    dnf) tools=(xorg-x11-server-Xvfb xorg-x11-xauth mesa-dri-drivers binutils desktop-file-utils appstream procps-ng
                util-linux shadow-utils findutils) ;;
    zypper) tools=(xvfb-run xorg-x11-server-Xvfb xauth Mesa-dri binutils desktop-file-utils AppStream procps
                   util-linux shadow findutils gzip tar) ;;
    arch) tools=(xorg-server-xvfb xorg-xauth mesa binutils desktop-file-utils appstream procps-ng) ;;
esac
# The desktop baseline: what a desktop always has, the libraries of the
# host list (packaging/linux/appimage/host-libs.txt) the AppImage and the
# tar.gz leave to the host.
case $family in
    deb) baseline=(libgl1 libegl1 libopengl0 libglx0 libx11-6 libx11-xcb1 libxcb1 libxcb-randr0 libxcb-shm0
                   libxcb-sync1 libxcb-xfixes0 libxcb-render0 libxcb-shape0 libfontconfig1 libfreetype6
                   libwayland-client0 libwayland-cursor0 libwayland-egl1 libasound2t64 libpulse0 libdbus-1-3
                   libsm6 libice6 libglib2.0-0t64)
        # Jammy and bookworm name the time_t libraries without t64.
        if ! apt-cache show libasound2t64 > /dev/null 2>&1; then
            baseline=("${baseline[@]/libasound2t64/libasound2}")
            baseline=("${baseline[@]/libglib2.0-0t64/libglib2.0-0}")
        fi ;;
    dnf) baseline=(libglvnd-opengl libglvnd-egl libglvnd-glx libX11 libX11-xcb libxcb fontconfig freetype libwayland-client
                   libwayland-cursor libwayland-egl alsa-lib pulseaudio-libs dbus-libs libSM libICE glib2) ;;
    zypper) baseline=(libglvnd libX11-6 libX11-xcb1 libxcb1 libxcb-randr0 libxcb-shm0 libxcb-sync1
                      libxcb-xfixes0 libxcb-render0 libxcb-shape0 libfontconfig1 libfreetype6 libwayland-client0
                      libwayland-cursor0 libwayland-egl1 libasound2 libpulse0 libdbus-1-3 libSM6 libICE6 libglib-2_0-0) ;;
    arch) baseline=(libglvnd libx11 libxcb fontconfig freetype2 wayland alsa-lib libpulse dbus libsm libice glib2) ;;
esac

# A test user owns $HOME; the app never runs as root here. Minimal images
# (Fedora, openSUSE) lack runuser and useradd.
case $family in
    dnf) install_pkgs util-linux shadow-utils ;;
    zypper) install_pkgs util-linux shadow ;;
esac
id tester > /dev/null 2>&1 || useradd -m tester
home=/home/tester
marker=$home/.local/share/hikari-test-marker
# UD1: and a file in the user folder, which removal never deletes either.
user_folder=$home/.config/hikarisub
runuser -u tester -- mkdir -p "$home/.local/share" "$home/.config/HikariSub" "$user_folder"
runuser -u tester -- sh -c "echo keep > '$marker'; echo keep > '$home/.config/HikariSub/user-file.ini'; echo keep > '$user_folder/uninstall-keeps-this.txt'"
check_home() {
    [ "$(cat "$marker")" = keep ] && [ -f "$home/.config/HikariSub/user-file.ini" ] &&
        [ "$(cat "$user_folder/uninstall-keeps-this.txt")" = keep ] || fail "files under \$HOME changed"
}

kind_name() # token -> the names --version may print for it
{
    case $1 in
        appimage) echo 'appimage|AppImage' ;;
        arch) echo 'arch|AUR' ;;
        portable-linux) echo 'portable-linux|portable|Portable' ;;
        *) echo "$1" ;;
    esac
}
as_tester() { runuser -u tester -- env HOME=$home XDG_RUNTIME_DIR=/tmp/xdg-tester "$@"; }
mkdir -p /tmp/xdg-tester && chown tester /tmp/xdg-tester && chmod 0700 /tmp/xdg-tester

# check_version <command...>: --version prints "<V> (<kind>)" and exits 0.
check_version() # <kind> <command...>
{
    local kind=$1; shift
    if [ $expect_version = 0 ]; then
        skip "--version of $kind (the build has no --version switch yet)"
        return 0
    fi
    # The line is on stdout; Qt and the loader may warn on stderr.
    local out err=/tmp/version-stderr.txt
    out=$(as_tester timeout 20 "$@" --version 2>"$err") || fail "$* --version exited $? : $out $(cat "$err")"
    [[ $out =~ ^${version//./\\.}\ \(($(kind_name "$kind"))\)$ ]] || fail "$* --version printed '$out' (stderr: $(cat "$err"))"
    pass "--version: $out"
}

# gui_start <label> <command...>: the main window starts under Xvfb (xcb)
# and offscreen, and runs until the timeout (exit 124).
gui_start()
{
    local label=$1; shift
    local log=/tmp/gui-$label.log rc
    for platform in xcb offscreen; do
        set +e
        if [ $platform = xcb ]; then
            as_tester env QT_QPA_PLATFORM=xcb LD_DEBUG=libs LD_DEBUG_OUTPUT=/tmp/lddebug-$label \
                xvfb-run -a -s '-screen 0 1280x1024x24' timeout 15 "$@" > "$log" 2>&1
        else
            as_tester env QT_QPA_PLATFORM=offscreen timeout 15 "$@" > "$log" 2>&1
        fi
        rc=$?
        set -e
        if [ $rc -ne 124 ]; then
            cat "$log" >&2
            fail "$label did not keep running on $platform (exit $rc)"
        fi
        if grep -E 'error while loading shared libraries|Could not load the Qt platform plugin|cannot open shared object' "$log" >&2; then
            fail "$label: a library or plugin is missing on $platform"
        fi
        pass "$label starts on $platform"
    done
}

# host_list_not_bundled <dir pattern>: LD_DEBUG shows no host-list library
# loaded from inside the AppImage or the tree.
host_list_not_bundled()
{
    local label=$1 pattern=$2
    local globs
    globs=$(grep -v '^[[:space:]]*#' "$src/packaging/linux/appimage/host-libs.txt" | grep -v '^[[:space:]]*$')
    set -f
    local bad=0 path name g
    while IFS= read -r path; do
        name=${path##*/}
        for g in $globs; do
            # shellcheck disable=SC2254
            case "$name" in $g) echo "host-list library loaded from the payload: $path" >&2; bad=1 ;; esac
        done
    done < <(cat /tmp/lddebug-"$label".* 2>/dev/null | sed -n 's/.*calling init: \(.*\)$/\1/p' | grep -E "$pattern" | sort -u)
    set +f
    [ $bad = 0 ] || fail "$label loads a host-list library from its own payload"
    ls /tmp/lddebug-"$label".* > /dev/null 2>&1 || fail "$label: no LD_DEBUG output"
    pass "$label loads no host-list library from its payload"
}

# unresolved_on_host_list <dir> [<extra lib dir> [<host-side list>]]: on the
# bare image, every library ldd cannot find for the payload's ELF files is
# on the host list, or (closure rule) in the AppImage's list of what the
# host-list libraries need.
unresolved_on_host_list()
{
    local dir=$1 extra=${2-} host_side=${3-}
    local globs
    globs=$(grep -v '^[[:space:]]*#' "$src/packaging/linux/appimage/host-libs.txt" | grep -v '^[[:space:]]*$')
    local bad=0 name g ok
    set -f
    while IFS= read -r name; do
        ok=0
        for g in $globs; do
            # shellcheck disable=SC2254
            case "$name" in $g) ok=1 ;; esac
        done
        if [ $ok = 0 ] && [ -n "$host_side" ] && grep -qxF "$name" "$host_side"; then
            ok=1
        fi
        [ $ok = 1 ] || { echo "not found and not on the host list: $name" >&2; bad=1; }
    done < <(find "$dir" -type f \( -name '*.so*' -o -perm -u+x \) -print0 |
             xargs -0 -r env LD_LIBRARY_PATH="$extra" ldd 2>/dev/null | sed -n 's/^[[:space:]]*\(\S*\) => not found$/\1/p' |
             sort -u)
    set +f
    [ $bad = 0 ] || fail "$dir needs libraries that are neither bundled nor on the host list"
    pass "$dir: every unresolved library is on the host list"
}

native_file()
{
    case $family in
        deb) echo "$1/hikarisub-$2-linux-amd64.deb" ;;
        dnf|zypper) echo "$1/hikarisub-$2-linux-x86_64.rpm" ;;
        *) fail "no native package for $family" ;;
    esac
}

if want appimage || want tar; then
    install_pkgs "${tools[@]}"
fi

if want appimage; then
    appimage=$dist/HikariSub-$version-linux-x86_64.AppImage
    [ -f "$appimage" ] || fail "no $appimage"
    work=/tmp/appimage
    rm -rf "$work" && mkdir -p "$work" && cp "$appimage" "$work/" && chmod 0755 "$work"/*.AppImage && chown -R tester "$work"
    (cd "$work" && runuser -u tester -- ./"${appimage##*/}" --appimage-extract > /dev/null)
    unresolved_on_host_list "$work/squashfs-root" "$work/squashfs-root/usr/lib/hikarisub-runtime/lib" \
        "$work/squashfs-root/usr/lib/hikarisub-runtime/host-libraries.txt"
    install_pkgs "${baseline[@]}"
    export APPIMAGE_EXTRACT_AND_RUN=1
    check_version appimage "$work/${appimage##*/}"
    gui_start appimage env APPIMAGE_EXTRACT_AND_RUN=1 "$work/${appimage##*/}"
    host_list_not_bundled appimage 'appimage_extracted|squashfs-root|/tmp/\.mount_'
    unset APPIMAGE_EXTRACT_AND_RUN
fi

if want tar; then
    tarball=$dist/hikarisub-$version-linux-x86_64.tar.gz
    [ -f "$tarball" ] || fail "no $tarball"
    rm -rf /tmp/tar && mkdir -p /tmp/tar && tar -xzf "$tarball" -C /tmp/tar && chown -R tester /tmp/tar
    (cd /tmp/tar/hikarisub-linux-x86_64 && sha256sum --quiet -c SHA256SUMS) || fail "the tar.gz SHA256SUMS do not match"
    [ ! -e /tmp/tar/hikarisub-linux-x86_64/package-kind.txt ] || fail "the tar.gz carries a package-kind.txt"
    # The tar.gz is the host profile, the payload of the deb: it needs what
    # the deb declares, which the AUR package and a desktop also provide.
    [ "$family" = deb ] || fail "the tar test runs on a deb-family image (it installs the deb's Depends)"
    deb=$(native_file "$dist" "$version")
    [ -f "$deb" ] || fail "no $deb for the tar test's dependencies"
    DEBIAN_FRONTEND=noninteractive apt-get satisfy -y -qq --no-install-recommends \
        "$(dpkg-deb -f "$deb" Depends)" > /dev/null
    check_version portable-linux /tmp/tar/hikarisub-linux-x86_64/hikarisub
    gui_start tar /tmp/tar/hikarisub-linux-x86_64/hikarisub
    host_list_not_bundled tar '/tmp/tar/'
fi

native_install() # <file>
{
    local f=/tmp/pkg/${1##*/}
    mkdir -p /tmp/pkg && cp "$1" "$f"
    case $family in
        deb) DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends "$f" > /dev/null ;;
        dnf) dnf -y -q install "$f" > /dev/null ;;
        zypper) zypper -q --non-interactive install --allow-unsigned-rpm "$f" > /dev/null ;;
    esac
}
native_remove()
{
    case $family in
        deb) DEBIAN_FRONTEND=noninteractive apt-get remove -y -qq hikarisub > /dev/null ;;
        dnf) dnf -y -q remove hikarisub > /dev/null ;;
        zypper) zypper -q --non-interactive rm hikarisub > /dev/null ;;
    esac
}
installed_version()
{
    case $family in
        deb) dpkg-query -W -f '${Version}' hikarisub ;;
        *) rpm -q --qf '%{VERSION}-%{RELEASE}' hikarisub ;;
    esac
}
expected_native_version() # <semver>
{
    local core=${1%%-*} pre=''
    [ "$core" = "$1" ] || pre="~${1#*-}"
    echo "$core$pre-1"
}
native_checks() # after an install
{
    local kind=$family
    [ $family = deb ] || kind=rpm
    [ "$(readlink -f /usr/bin/hikarisub)" = /opt/hikarisub/hikarisub ] || fail "/usr/bin/hikarisub does not resolve"
    [ "$(cat /opt/hikarisub/package-kind.txt)" = "$kind" ] || fail "package-kind.txt is not $kind"
    [ "$(installed_version)" = "$(expected_native_version "$version")" ] ||
        fail "installed version $(installed_version), expected $(expected_native_version "$version")"
    [ -x /opt/hikarisub/libexec/hikarisub-update-helper ] || fail "no update helper"
    [ -f /usr/share/polkit-1/actions/io.github.altqx.HikariSub.update.policy ] || fail "no polkit policy"
    desktop-file-validate /usr/share/applications/io.github.altqx.HikariSub.desktop || fail "desktop-file-validate"
    appstreamcli validate --no-net /usr/share/metainfo/io.github.altqx.HikariSub.metainfo.xml ||
        fail "appstreamcli validate"
    # DX1 (#367): the site is the homepage, the guide the help; GitHub the tracker.
    grep -q '<url type="homepage">https://hikarisub.altqx.com</url>' \
        /usr/share/metainfo/io.github.altqx.HikariSub.metainfo.xml || fail "metainfo homepage"
    grep -q '<url type="help">https://hikarisub.altqx.com/en/docs</url>' \
        /usr/share/metainfo/io.github.altqx.HikariSub.metainfo.xml || fail "metainfo help"
    local homepage
    case $family in
        deb) homepage=$(dpkg-query -W -f '${Homepage}' hikarisub) ;;
        *) homepage=$(rpm -q --qf '%{URL}' hikarisub) ;;
    esac
    [ "$homepage" = https://hikarisub.altqx.com ] || fail "package homepage $homepage"
    pass "native install: version $(installed_version), kind $kind, desktop files valid"
    unresolved=$(find /opt/hikarisub -type f \( -name '*.so*' -o -perm -u+x \) -print0 | xargs -0 -r ldd 2>/dev/null |
                 grep 'not found' | sort -u || true)
    [ -z "$unresolved" ] || fail "missing after install with the declared dependencies: $unresolved"
    pass "native install: every library resolves"
    check_version "$kind" /usr/bin/hikarisub
    gui_start native /usr/bin/hikarisub
    host_list_not_bundled native '^/opt/hikarisub/'
}

if want native; then
    pkg=$(native_file "$dist" "$version")
    [ -f "$pkg" ] || fail "no $pkg"
    # The tools only; the app's own dependencies come from the package.
    install_pkgs "${tools[@]}"
    native_install "$pkg"
    native_checks
fi

if want helper; then
    helper=/opt/hikarisub/libexec/hikarisub-update-helper
    if grep -q 'not part of this build' "$helper"; then
        skip "update helper tests (this build packages a placeholder helper)"
    else
        [ -n "$old" ] || fail "helper needs --old (the 0.0.9 stand-in it upgrades from)"
        kind=$family; [ $family = deb ] || kind=rpm
        ext=rpm; [ "$kind" = deb ] && ext=deb
        pkg=$(native_file "$dist" "$version")
        asset=${pkg##*/}
        stand_in=$(native_file "$old" 0.0.9)
        uid=$(id -u tester)
        # The helper of this version, root's, run from outside /opt so the
        # older stand-in it upgrades from can be installed under it.
        install -m 0755 "$helper" /usr/local/sbin/hikarisub-update-helper
        helper=/usr/local/sbin/hikarisub-update-helper
        native_remove > /dev/null 2>&1 || true
        native_install "$stand_in"
        # The stand-in for GitHub: the helper's curl serves made-up
        # releases' SHA256SUMS from /srv/hikarisub-releases.
        install -m 0755 "$src/tests/packaging/linux/fake-curl.sh" /usr/local/bin/curl
        releases=/srv/hikarisub-releases
        rm -rf "$releases"
        publish() # <version> <digest> <name>
        {
            mkdir -p "$releases/v$1"
            printf '%s  %s\n' "$2" "$3" >> "$releases/v$1/SHA256SUMS"
        }
        for f in "$pkg" "$stand_in"; do
            cp "$f" "/tmp/helper-${f##*/}" && chown tester "/tmp/helper-${f##*/}"
        done
        good=/tmp/helper-$asset
        forged=/tmp/helper-${stand_in##*/}
        sha=$(sha256sum "$good" | cut -d' ' -f1)
        publish "$version" "$sha" "$asset"
        expect_rc() # <rc> <label> <args...>
        {
            local want_rc=$1 label=$2; shift 2
            set +e; env PKEXEC_UID=$uid "$helper" install "$@" > /tmp/helper.log 2>&1; local rc=$?; set -e
            [ $rc -eq "$want_rc" ] || { cat /tmp/helper.log >&2; fail "helper $label: exit $rc, expected $want_rc"; }
            pass "helper $label: exit $rc"
        }
        # The caller has no say in the digest: the old form that passed one
        # is refused, and the caller's own package, at a released version,
        # is not that release's file.
        expect_rc 2 'a caller-supplied kind and SHA-256' "$kind" "$forged" "$(sha256sum "$forged" | cut -d' ' -f1)" "$version"
        expect_rc 2 'a caller-supplied SHA-256' "$forged" "$(sha256sum "$forged" | cut -d' ' -f1)" "$version"
        expect_rc 3 "the caller's own package" "$forged" "$version"
        # The command the app shows without pkexec runs the helper through
        # sudo, which names the user in SUDO_UID: the same checks apply.
        set +e; env -u PKEXEC_UID SUDO_UID=$uid "$helper" install "$forged" "$version" > /tmp/helper.log 2>&1
        rc=$?; set -e
        [ $rc -eq 3 ] || { cat /tmp/helper.log >&2; fail "helper the caller's own package, through sudo: exit $rc, expected 3"; }
        pass "helper the caller's own package, through sudo: exit 3"
        set +e; env -u PKEXEC_UID -u SUDO_UID "$helper" install "$good" "$version" > /tmp/helper.log 2>&1
        rc=$?; set -e
        [ $rc -eq 2 ] || { cat /tmp/helper.log >&2; fail "helper with no asking user: exit $rc, expected 2"; }
        pass "helper with no asking user: exit 2"
        grep -qx "https://github.com/altqx/hikari/releases/download/v$version/SHA256SUMS" "$releases/requests.log" ||
            fail "helper: it did not ask GitHub for the SHA256SUMS of v$version"
        pass "helper: it asked GitHub for the SHA256SUMS of v$version"
        for bad in '' "v$version" "$version+build.1" 01.2.3 1.2 '1.2.3;id' ../1.2.3; do
            expect_rc 2 "the bad version '$bad'" "$good" "$bad"
        done
        expect_rc 2 'a version not newer than the installed one' "$forged" 0.0.9
        publish 99.0.0 "$(printf '%064d' 0)" "hikarisub-99.0.0-linux-amd64.deb"
        publish 99.0.0 "$(printf '%064d' 0)" "hikarisub-99.0.0-linux-x86_64.rpm"
        expect_rc 3 'a SHA256SUMS whose digest does not match' "$good" 99.0.0
        publish 98.0.0 "$sha" "hikarisub-98.0.0-windows-x64-setup.exe"
        expect_rc 3 "a digest listed under another file's name" "$good" 98.0.0
        publish 97.0.0 "$sha" "hikarisub-97.0.0-linux-amd64.deb"
        publish 97.0.0 "$sha" "hikarisub-97.0.0-linux-x86_64.rpm"
        expect_rc 3 'a listed package of another version' "$good" 97.0.0
        expect_rc 6 'a version with no release' "$good" 96.0.0
        set +e; env FAKE_CURL_OFFLINE=1 PKEXEC_UID=$uid "$helper" install "$good" "$version" > /tmp/helper.log 2>&1
        rc=$?; set -e
        [ $rc -eq 6 ] || { cat /tmp/helper.log >&2; fail "helper GitHub cannot be reached: exit $rc, expected 6"; }
        pass "helper GitHub cannot be reached: exit 6"
        cp /tmp/helper-$asset /tmp/helper-root && chown root /tmp/helper-root
        expect_rc 2 'a file owned by another user' /tmp/helper-root "$version"
        wrong=''
        for f in "$old"/hikarisub-wrongname-*."$ext"; do [ -f "$f" ] && wrong=$f; done
        if [ -n "$wrong" ]; then
            cp "$wrong" /tmp/helper-wrong && chown tester /tmp/helper-wrong
            publish 95.0.0 "$(sha256sum /tmp/helper-wrong | cut -d' ' -f1)" "hikarisub-95.0.0-linux-amd64.deb"
            publish 95.0.0 "$(sha256sum /tmp/helper-wrong | cut -d' ' -f1)" "hikarisub-95.0.0-linux-x86_64.rpm"
            expect_rc 3 'a listed package that is not hikarisub' /tmp/helper-wrong 95.0.0
        fi
        [ "$(installed_version)" = 0.0.9-1 ] || fail "helper: a refused package changed the install ($(installed_version))"
        expect_rc 0 "the release's own package" "$good" "$version"
        [ "$(installed_version)" = "$(expected_native_version "$version")" ] ||
            fail "helper: $version is not installed ($(installed_version))"
        [ -z "$(ls -A /var/cache/hikarisub-update)" ] || fail "helper: it left files in /var/cache/hikarisub-update"
        [ "$(stat -c '%U %a' /var/cache/hikarisub-update)" = "root 700" ] || fail "helper: its cache is not root's 0700"
        rm -f /usr/local/bin/curl /usr/local/sbin/hikarisub-update-helper
    fi
fi

if want upgrade; then
    [ -n "$old" ] || fail "upgrade needs --old"
    install_pkgs "${tools[@]}"
    for from in "$old"/hikarisub-0.0.*-linux-* "$old"/hikarisub-0.1.0-*-linux-*; do
        case "$from" in
            *.deb) [ $family = deb ] || continue ;;
            *.rpm) [ $family != deb ] || continue ;;
            *) continue ;;
        esac
        [ -f "$from" ] || continue
        native_remove > /dev/null 2>&1 || true
        native_install "$from"
        before=$(installed_version)
        [ -e /opt/hikarisub/hikarisub ] || fail "$from did not install"
        native_install "$(native_file "$dist" "$version")"
        [ "$(installed_version)" = "$(expected_native_version "$version")" ] || fail "upgrade from $before failed"
        [ ! -e /opt/hikarisub/legacy-only.txt ] || fail "a file of the old payload survived the upgrade"
        check_home
        pass "upgrade $before -> $(installed_version), \$HOME untouched"
    done
    if [ $family = deb ]; then
        dpkg --compare-versions 0.0.9-1 lt 0.1.0~beta.1-1 && dpkg --compare-versions 0.1.0~beta.1-1 lt 0.1.0-1 ||
            fail "dpkg version order"
        pass "dpkg orders 0.0.9-1 < 0.1.0~beta.1-1 < 0.1.0-1"
    else
        [ "$(rpm --eval '%{lua: print(rpm.vercmp("0.0.9-1", "0.1.0~beta.1-1"))}')" = -1 ] &&
        [ "$(rpm --eval '%{lua: print(rpm.vercmp("0.1.0~beta.1-1", "0.1.0-1"))}')" = -1 ] || fail "rpm version order"
        pass "rpm orders 0.0.9-1 < 0.1.0~beta.1-1 < 0.1.0-1"
    fi
fi

if want remove; then
    native_remove
    [ ! -e /opt/hikarisub ] || { find /opt/hikarisub >&2; fail "/opt/hikarisub is still there"; }
    [ ! -e /usr/bin/hikarisub ] || fail "/usr/bin/hikarisub is still there"
    check_home
    pass "remove: /opt/hikarisub gone, \$HOME untouched"
fi

if want aur; then
    [ $family = arch ] || fail "the AUR test runs on archlinux only"
    aur_args=(--dist "$dist" --version "$version")
    [ $expect_version = 0 ] || aur_args+=(--expect-version-switch)
    "$here/aur-test.sh" "${aur_args[@]}"
fi
echo "== all requested tests passed on $PRETTY_NAME"
