#!/bin/sh
# One package test, run as root inside a clean distribution container by
# tests/packaging/linux/run.sh (installers spec, section 8). The container
# has these mounts:
#   /dist   the packages (read-only)
#   /tests  tests/packaging/linux, with gst-check built for the floor
#   /aur    the rendered AUR package (PKGBUILD, .SRCINFO), for the aur test
#   /upgrade  older or newer test packages, for the upgrade steps (optional)
#
#   in-container.sh <appimage|deb|rpm|tar|aur> <version>
#
# Prints PASS/FAIL lines and exits non-zero on the first failure.
set -eu

mode=$1 version=$2
. /etc/os-release
distro=$ID
user=tester
uhome=/home/$user
log=/tmp/hikari-test
mkdir -p "$log"

say() { printf '%s\n' "== $*"; }
pass() { printf 'PASS: %s\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*"; exit 1; }

# ---------------------------------------------------------------- setup ----
# The host libraries every desktop has (packaging/linux/appimage/host-libs.txt:
# GL, X11, Wayland, fontconfig, audio, D-Bus), Xvfb, and the test tools.
# Nothing else: the AppImage must bring the rest, a native package must pull
# the rest in through its dependencies.
install_base() {
    case $distro in
    ubuntu | debian)
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        asound=libasound2
        apt-cache show libasound2t64 >/dev/null 2>&1 && asound=libasound2t64
        apt-get install -y -qq --no-install-recommends \
            xvfb xauth libgl1 libegl1 libx11-6 libx11-xcb1 libxcb1 \
            libfontconfig1 libfreetype6 "$asound" libpulse0 libdbus-1-3 \
            libsystemd0 libudev1 libwayland-client0 libwayland-cursor0 \
            libwayland-egl1 libsm6 libice6 zlib1g libexpat1 libdrm2 libgbm1 \
            libxshmfence1 libxcb-render0 libxcb-shm0 libxcb-xfixes0 libxcb-dri2-0 \
            libxcb-dri3-0 libxcb-glx0 libxcb-present0 libxcb-sync1 libxcb-randr0 \
            libxcb-shape0 fonts-dejavu-core binutils \
            ca-certificates >/dev/null ;;
    fedora)
        dnf -y -q install --setopt=install_weak_deps=False \
            xorg-x11-server-Xvfb xorg-x11-xauth mesa-libGL mesa-libEGL \
            libX11 libX11-xcb libxcb fontconfig freetype alsa-lib \
            pulseaudio-libs dbus-libs systemd-libs libwayland-client \
            libwayland-cursor libwayland-egl libSM libICE zlib-ng-compat expat \
            libdrm mesa-libgbm libxshmfence dejavu-sans-fonts binutils \
            util-linux which findutils >/dev/null ;;
    opensuse-tumbleweed)
        zypper -n -q install --no-recommends \
            xvfb-run xorg-x11-server-Xvfb xauth Mesa-libGL1 Mesa-libEGL1 \
            libX11-6 libX11-xcb1 libxcb1 libfontconfig1 libfreetype6 \
            libasound2 libpulse0 libdbus-1-3 libsystemd0 libudev1 \
            libwayland-client0 libwayland-cursor0 libwayland-egl1 libSM6 \
            libICE6 libz1 libexpat1 libdrm2 libgbm1 libxshmfence1 \
            libxcb-render0 libxcb-shm0 libxcb-xfixes0 libxcb-dri2-0 libxcb-dri3-0 \
            libxcb-glx0 libxcb-present0 libxcb-sync1 libxcb-randr0 libxcb-shape0 \
            dejavu-fonts binutils shadow which findutils >/dev/null ;;
    arch)
        pacman -Sy --noconfirm --needed -q \
            xorg-server-xvfb xorg-xauth libglvnd mesa libx11 libxcb \
            fontconfig freetype2 alsa-lib libpulse dbus systemd-libs wayland \
            libsm libice zlib expat libdrm libxshmfence ttf-dejavu binutils \
            which >/dev/null ;;
    *) fail "no setup for $distro" ;;
    esac
    id "$user" >/dev/null 2>&1 || useradd -m "$user"
    # A user file that no install, upgrade or removal may touch.
    echo keep > "$uhome/keep.txt"
    chown "$user" "$uhome/keep.txt"
    say "base system ready on $PRETTY_NAME"
}

as_user() {
    su "$user" -s /bin/sh -c "cd $uhome && $*"
}

# Starts the GUI under Xvfb for 10 s as the test user. Still running at the
# end (exit 124 from timeout) is success; a crash or a loader error fails.
gui_start() {
    what=$1
    shift
    rc=0
    as_user "HOME=$uhome APPIMAGE_EXTRACT_AND_RUN=1 xvfb-run -a -s '-screen 0 1280x1024x24' timeout 10 $*" \
        > "$log/gui-$what.txt" 2>&1 || rc=$?
    if grep -Eq 'error while loading shared libraries|cannot open shared object|undefined symbol|version `[^`]*'"'"' not found|symbol lookup error' "$log/gui-$what.txt"; then
        cat "$log/gui-$what.txt"
        fail "$what: the loader failed"
    fi
    if [ $rc -ne 124 ]; then
        tail -n 30 "$log/gui-$what.txt"
        fail "$what: the GUI exited with $rc within 10 s (124 = still running is expected)"
    fi
    pass "$what: the GUI starts and runs under Xvfb"
}

# --version prints "<version> (<kind>)" and exits 0 without a display
# (installers spec, 8.1).
version_check() {
    what=$1 kind=$2 runner=$3
    out=$(as_user "APPIMAGE_EXTRACT_AND_RUN=1 timeout 20 $runner --version") ||
        fail "$what: --version failed"
    [ "$out" = "$version ($kind)" ] || fail "$what: --version printed '$out', not '$version ($kind)'"
    pass "$what: --version prints $out"
}

# Runs the GStreamer check through <runner> with the package's environment:
# the app's own --hikari-gst-check=<file>, or a runner that takes the file as
# its argument (gst-check, which also prints where each element came from).
# Exit 1 is a missing element, exit 2 a file that does not decode. The
# VP8/Vorbis sample must decode everywhere. The H.264/AAC sample must decode
# too, except from an rpm: Fedora and openSUSE ship no H.264 or AAC decoder
# (RPM Fusion and Packman have them), and the rpm only recommends gst-libav.
gst_check() {
    what=$1
    shift
    case $* in
    *--hikari-gst-check) sep== ;;
    *) sep=' ' ;;
    esac
    as_user "$*$sep/tests/sample-free.webm" > "$log/gst-$what.txt" 2>&1 || {
        cat "$log/gst-$what.txt"
        fail "$what: the GStreamer check failed on the VP8/Vorbis sample"
    }
    cat "$log/gst-$what.txt"
    pass "$what: playbin, appsrc, appsink, audioconvert, audioresample, autoaudiosink and videoconvert load; the VP8/Vorbis sample decodes"
    rc=0
    as_user "$*$sep/tests/sample.mkv" > "$log/gst-$what-h264.txt" 2>&1 || rc=$?
    if [ $rc -eq 0 ]; then
        pass "$what: the H.264/AAC sample decodes"
    elif [ $rc -eq 2 ] && [ "$mode" = rpm ]; then
        grep -i 'decode:\|missing decoder' "$log/gst-$what-h264.txt" || true
        echo "SKIP: $what: $PRETTY_NAME has no H.264/AAC decoder of its own"
    else
        cat "$log/gst-$what-h264.txt"
        fail "$what: the GStreamer check failed on the H.264/AAC sample"
    fi
}

# No missing library and no missing symbol version for any ELF of a tree,
# resolved the way the app resolves them (its own directory first).
ldd_check() {
    what=$1 dir=$2
    : > "$log/ldd-$what.txt"
    find "$dir" -type f | while IFS= read -r f; do
        readelf -h "$f" >/dev/null 2>&1 || continue
        LD_LIBRARY_PATH=$dir ldd "$f" 2>&1 | grep -E 'not found' | sed "s|^|$f: |" >> "$log/ldd-$what.txt" || true
    done
    if [ -s "$log/ldd-$what.txt" ]; then
        sort -u "$log/ldd-$what.txt" | head -n 40
        fail "$what: ldd reports missing libraries"
    fi
    pass "$what: ldd finds every library of every ELF file"
}

validate_desktop_files() {
    what=$1
    case $distro in
    ubuntu | debian) apt-get install -y -qq --no-install-recommends desktop-file-utils appstream >/dev/null ;;
    fedora) dnf -y -q install desktop-file-utils appstream >/dev/null ;;
    opensuse-tumbleweed) zypper -n -q install --no-recommends desktop-file-utils AppStream >/dev/null ;;
    arch) pacman -S --noconfirm --needed -q desktop-file-utils appstream >/dev/null ;;
    esac
    desktop-file-validate /usr/share/applications/io.github.altqx.HikariSub.desktop ||
        fail "$what: desktop-file-validate"
    appstreamcli validate --no-net /usr/share/metainfo/io.github.altqx.HikariSub.metainfo.xml ||
        fail "$what: appstreamcli validate"
    pass "$what: the desktop file and the metainfo validate"
}

installed_layout() {
    what=$1 kind=$2
    [ "$(readlink -f /usr/bin/hikarisub)" = /opt/hikarisub/hikarisub ] ||
        fail "$what: /usr/bin/hikarisub does not resolve to /opt/hikarisub/hikarisub"
    [ "$(cat /opt/hikarisub/package-kind.txt)" = "$kind" ] ||
        fail "$what: package-kind.txt is not $kind"
    for f in /usr/share/applications/io.github.altqx.HikariSub.desktop \
             /usr/share/metainfo/io.github.altqx.HikariSub.metainfo.xml \
             /usr/share/mime/packages/hikarisub.xml \
             /usr/share/icons/hicolor/256x256/apps/hikarisub.png; do
        [ -f "$f" ] || fail "$what: $f is missing"
    done
    grep -qx 'Exec=hikarisub %f' /usr/share/applications/io.github.altqx.HikariSub.desktop ||
        fail "$what: the desktop file's Exec= is not 'hikarisub %f'"
    if [ "$kind" = arch ]; then
        [ ! -e /opt/hikarisub/libexec/hikarisub-update-helper ] ||
            fail "$what: the AUR package carries the update helper"
        [ ! -e /usr/share/polkit-1/actions/io.github.altqx.HikariSub.update.policy ] ||
            fail "$what: the AUR package carries the polkit action"
    elif [ -e /opt/hikarisub/libexec/hikarisub-update-helper ]; then
        [ "$(stat -c '%U %a' /opt/hikarisub/libexec/hikarisub-update-helper)" = "root 755" ] ||
            fail "$what: the update helper is not root-owned 0755"
        [ -f /usr/share/polkit-1/actions/io.github.altqx.HikariSub.update.policy ] ||
            fail "$what: the polkit action is missing"
    else
        fail "$what: the update helper /opt/hikarisub/libexec/hikarisub-update-helper is missing"
    fi
    pass "$what: /opt/hikarisub, /usr/bin/hikarisub, package-kind.txt = $kind and the desktop files are in place"
}

removed() {
    what=$1
    [ ! -e /opt/hikarisub ] || { find /opt/hikarisub | head; fail "$what: /opt/hikarisub is still there after removal"; }
    [ ! -e /usr/bin/hikarisub ] || fail "$what: /usr/bin/hikarisub is still there after removal"
    [ "$(cat "$uhome/keep.txt")" = keep ] || fail "$what: a file in \$HOME changed"
    pass "$what: removed; /opt/hikarisub is gone and \$HOME is untouched"
}

native_checks() {
    what=$1 kind=$2
    installed_layout "$what" "$kind"
    ldd_check "$what" /opt/hikarisub
    version_check "$what" "$kind" /usr/bin/hikarisub
    gui_start "$what" /usr/bin/hikarisub
    gst_check "$what" /usr/bin/hikarisub --hikari-gst-check
}

# ------------------------------------------------------------- AppImage ----
test_appimage() {
    image=$(ls /dist/HikariSub-*-linux-x86_64.AppImage)
    cp "$image" "$uhome/HikariSub.AppImage"
    chown "$user" "$uhome/HikariSub.AppImage"
    chmod 0755 "$uhome/HikariSub.AppImage"
    as_user "./HikariSub.AppImage --appimage-extract >/dev/null"
    appdir=$uhome/squashfs-root
    [ "$(cat "$appdir/usr/lib/hikarisub/package-kind.txt")" = appimage ] ||
        fail "appimage: package-kind.txt is not appimage"
    version_check appimage appimage ./HikariSub.AppImage
    gui_start appimage ./HikariSub.AppImage

    # Which libraries the running app loads, and from where: nothing on the
    # host list may come from inside the AppImage.
    rm -f /tmp/lddebug.*
    as_user "HOME=$uhome LD_DEBUG=libs LD_DEBUG_OUTPUT=/tmp/lddebug xvfb-run -a timeout 8 ./squashfs-root/AppRun" >/dev/null 2>&1 || true
    grep -h 'calling init:' /tmp/lddebug.* 2>/dev/null | sed 's/.*calling init: //' | sort -u > "$log/loaded.txt"
    [ -s "$log/loaded.txt" ] || fail "appimage: LD_DEBUG recorded nothing"
    grep -v '^[[:space:]]*\(#\|$\)' /tests/host-libs.txt > "$log/globs.txt"
    bad=0
    while IFS= read -r lib; do
        case $lib in "$appdir"/*) ;; *) continue ;; esac
        name=$(basename "$lib")
        while IFS= read -r glob; do
            # shellcheck disable=SC2254
            case $name in $glob) echo "loaded from the AppImage: $lib"; bad=1 ;; esac
        done < "$log/globs.txt"
    done < "$log/loaded.txt"
    [ $bad -eq 0 ] || fail "appimage: host libraries were loaded from the AppImage"
    pass "appimage: $(grep -c "^$appdir/" "$log/loaded.txt") libraries loaded from the AppImage, none on the host list; $(grep -vc "^$appdir/" "$log/loaded.txt") from the host"

    # The GStreamer check with the AppImage's own environment: the app's
    # switch through the AppImage, then gst-check in the app's place, which
    # also says where each element was loaded from.
    gst_check appimage "APPIMAGE_EXTRACT_AND_RUN=1 ./HikariSub.AppImage --hikari-gst-check"
    mv "$appdir/usr/lib/hikarisub/hikarisub" "$appdir/usr/lib/hikarisub/hikarisub.real"
    printf '#!/bin/sh\nexec /tests/gst-check "$@"\n' > "$appdir/usr/lib/hikarisub/hikarisub"
    chmod 0755 "$appdir/usr/lib/hikarisub/hikarisub"
    gst_check appimage-elements "./squashfs-root/AppRun"
    grep '^element ' "$log/gst-appimage-elements.txt" | grep -v "$appdir/usr/lib/hikarisub-runtime/gstreamer-1.0/" |
        grep -v '(built in)' && fail "appimage: an element came from outside the AppImage"
    mv "$appdir/usr/lib/hikarisub/hikarisub.real" "$appdir/usr/lib/hikarisub/hikarisub"
    pass "appimage: every element comes from the AppImage's own plugins"
}

# ------------------------------------------------------------ deb / rpm ----
pkg_install() {
    case $1 in
    deb) apt-get install -y -qq "$2" > "$log/install.txt" 2>&1 || { cat "$log/install.txt"; fail "apt-get install $2"; } ;;
    rpm)
        case $distro in
        fedora) dnf -y -q install "$2" > "$log/install.txt" 2>&1 || { cat "$log/install.txt"; fail "dnf install $2"; } ;;
        *) zypper -n install --allow-unsigned-rpm "$2" > "$log/install.txt" 2>&1 || { cat "$log/install.txt"; fail "zypper install $2"; } ;;
        esac ;;
    esac
}

pkg_remove() {
    case $1 in
    deb) apt-get remove -y -qq hikarisub >/dev/null ;;
    rpm)
        case $distro in
        fedora) dnf -y -q remove hikarisub >/dev/null ;;
        *) zypper -n rm hikarisub >/dev/null ;;
        esac ;;
    esac
}

pkg_version() {
    case $1 in
    deb) dpkg-query -W -f '${Version}' hikarisub ;;
    rpm) rpm -q --qf '%{VERSION}-%{RELEASE}' hikarisub ;;
    esac
}

version_order() {
    case $1 in
    deb)
        dpkg --compare-versions 0.0.9-1 lt 0.1.0~beta.1-1 &&
        dpkg --compare-versions 0.1.0~beta.1-1 lt 0.1.0-1 &&
        dpkg --compare-versions 0.0.1-1 lt 0.0.2~rc.1-1 || fail "deb version ordering" ;;
    rpm)
        lua='%{lua: print(rpm.vercmp("0.0.9-1", "0.1.0~beta.1-1") .. rpm.vercmp("0.1.0~beta.1-1", "0.1.0-1"))}'
        [ "$(rpm --eval "$lua")" = "-1-1" ] || fail "rpm version ordering: $(rpm --eval "$lua")" ;;
    esac
    pass "$1: 0.0.9 < 0.1.0-beta.1 < 0.1.0 in the package manager's own ordering"
}

test_native() {
    kind=$1
    case $kind in
    deb) file=$(ls /dist/hikarisub-*-linux-amd64.deb) ;;
    rpm) file=$(ls /dist/hikarisub-*-linux-x86_64.rpm) ;;
    esac
    cp "$file" /tmp/
    file=/tmp/$(basename "$file")
    version_order "$kind"
    pkg_install "$kind" "$file"
    pass "$kind: $(basename "$file") installs, $(pkg_version "$kind")"
    native_checks "$kind" "$kind"
    validate_desktop_files "$kind"

    if [ -d /upgrade ] && ls /upgrade/*."$kind" >/dev/null 2>&1; then
        before=$(pkg_version "$kind")
        newer=$(ls /upgrade/*."$kind" | head -n 1)
        # The upgrade goes through the installed update helper, as the app
        # runs it through pkexec (installers spec, 4.4): the user's own
        # download and the release version. The helper downloads that
        # release's SHA256SUMS itself; fake-curl.sh stands in for GitHub
        # with one that lists the newer package.
        cp "$newer" "$uhome/"
        newer=$uhome/$(basename "$newer")
        chown "$user:" "$newer"
        newer_version=$(basename "$newer" | sed 's/^hikarisub-\(.*\)-linux-[^-]*$/\1/')
        mkdir -p "/srv/hikarisub-releases/v$newer_version"
        (cd "$uhome" && sha256sum "$(basename "$newer")") > "/srv/hikarisub-releases/v$newer_version/SHA256SUMS"
        install -m 0755 /tests/fake-curl.sh /usr/local/bin/curl
        rc=0
        PKEXEC_UID=$(id -u "$user") /opt/hikarisub/libexec/hikarisub-update-helper install "$newer" \
            "$newer_version" >"$log/helper-$kind.txt" 2>&1 || rc=$?
        rm -f /usr/local/bin/curl
        if [ "$rc" -ne 0 ]; then
            cat "$log/helper-$kind.txt"
            fail "$kind: the update helper did not install $(basename "$newer") (exit $rc)"
        fi
        [ -z "$(ls -A /var/cache/hikarisub-update)" ] || fail "$kind: the update helper left files in its cache"
        after=$(pkg_version "$kind")
        [ "$after" != "$before" ] || fail "$kind: the upgrade did not change the version ($before)"
        [ "$(cat "$uhome/keep.txt")" = keep ] || fail "$kind: the upgrade touched \$HOME"
        installed_layout "$kind upgrade" "$kind"
        pass "$kind: the update helper upgraded $before -> $after, \$HOME untouched"
    fi

    pkg_remove "$kind"
    removed "$kind"
}

# ----------------------------------------------------------------- tar ----
test_tar() {
    # The tar.gz brings no dependencies: take the deb's, then drop the deb.
    file=$(ls /dist/hikarisub-*-linux-amd64.deb)
    cp "$file" /tmp/
    pkg_install deb "/tmp/$(basename "$file")"
    apt-get remove -y -qq hikarisub >/dev/null
    tarball=$(ls /dist/hikarisub-*-linux-x86_64.tar.gz /dist/hikarisub-linux-x86_64.tar.gz 2>/dev/null | head -n 1)
    as_user "tar -xzf $tarball"
    tree=$uhome/hikarisub-linux-x86_64
    [ ! -e "$tree/package-kind.txt" ] || fail "tar: the portable tree has a package-kind.txt"
    (cd "$tree" && sha256sum -c --quiet SHA256SUMS) || fail "tar: SHA256SUMS"
    ldd_check tar "$tree"
    gui_start tar "$tree/hikarisub"
    pass "tar: the portable tree runs"
}

# ----------------------------------------------------------------- AUR ----
test_aur() {
    pacman -S --noconfirm --needed -q base-devel namcap sudo >/dev/null
    echo "$user ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/tester
    work=$uhome/aur
    mkdir -p "$work"
    cp /aur/PKGBUILD /aur/.SRCINFO "$work/"
    # hikarisub-stable-bin or hikarisub-beta-bin, and its channel.
    pkgname=$(sed -n 's/^pkgname = //p' /aur/.SRCINFO)
    channel=$(sed -n "s/^_channel=//p" /aur/PKGBUILD)
    # makepkg takes the source from beside the PKGBUILD and still checks its
    # SHA-256 (installers spec, 8.2).
    cp "$(ls /dist/hikarisub-"$version"-linux-x86_64.tar.gz)" "$work/"
    chown -R "$user" "$work"
    as_user "cd aur && makepkg --printsrcinfo | diff - .SRCINFO" || fail "aur: .SRCINFO is not makepkg --printsrcinfo"
    pass "aur: .SRCINFO matches the PKGBUILD"
    as_user "cd aur && makepkg -s --noconfirm" > "$log/makepkg.txt" 2>&1 || { tail -n 40 "$log/makepkg.txt"; fail "aur: makepkg"; }
    pkg=$(ls "$work"/"$pkgname"-*.pkg.tar.zst)
    pass "aur: makepkg built $(basename "$pkg")"

    as_user "cd aur && namcap PKGBUILD; namcap $(basename "$pkg")" > "$log/namcap.txt" 2>&1 || true
    cat "$log/namcap.txt"
    # Accepted: the /opt tree and the x86_64 literal in the source URL.
    if grep -E ' E: ' "$log/namcap.txt" | grep -v "ELF files outside of a valid path" | grep -q .; then
        fail "aur: namcap reports errors"
    fi
    if grep -E 'detected and not included|Insecure RPATH' "$log/namcap.txt" | grep -q .; then
        fail "aur: namcap finds an undeclared dependency or an insecure RPATH"
    fi
    pass "aur: namcap reports only the accepted /opt and \$CARCH findings"

    pacman -U --noconfirm "$pkg" > "$log/install.txt" 2>&1 || { cat "$log/install.txt"; fail "aur: pacman -U"; }
    pass "aur: pacman -U installs it with its dependencies"
    [ -f "/usr/share/licenses/$pkgname/LICENSE" ] || fail "aur: the license is not in /usr/share/licenses/$pkgname"
    [ "$(cat /opt/hikarisub/aur-channel.txt)" = "$channel" ] || fail "aur: aur-channel.txt is not $channel"
    pass "aur: $pkgname carries its license and the channel marker $channel"
    native_checks aur arch
    validate_desktop_files aur
    pacman -R --noconfirm "$pkgname" >/dev/null
    removed aur
}

install_base
case $mode in
appimage) test_appimage ;;
deb) test_native deb ;;
rpm) test_native rpm ;;
tar) test_tar ;;
aur) test_aur ;;
*) fail "unknown test $mode" ;;
esac
say "all $mode checks passed on $PRETTY_NAME"
