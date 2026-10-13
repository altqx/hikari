#!/bin/sh
# Tests packaging/linux/update-helper/hikarisub-update-helper (installers
# spec, 8.1 step 7) in a distro container, as root, with PKEXEC_UID set by
# hand, since a container has no polkit session. It builds small stand-in
# hikarisub packages with the distro's own tools, installs the oldest, and
# feeds the helper good and bad packages.
#
#   docker run --rm -v "$PWD:/src:ro" ubuntu:24.04 sh /src/tests/update/helper/test-update-helper.sh deb
#   docker run --rm -v "$PWD:/src:ro" fedora:latest sh /src/tests/update/helper/test-update-helper.sh rpm
#   docker run --rm -v "$PWD:/src:ro" opensuse/tumbleweed sh /src/tests/update/helper/test-update-helper.sh rpm
#
# /src is the repository. Exits 0 when every case passes.

set -u
kind=${1:?usage: test-update-helper.sh deb|rpm}
SRC=${SRC:-/src}
HELPER="$SRC/packaging/linux/update-helper/hikarisub-update-helper"
WORK=/tmp/helper-test
failures=0

say() { printf '%s\n' "$*"; }

# Tools to build packages with, from the distro.
if [ "$kind" = deb ]; then
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -qq >/dev/null && apt-get install -y -qq --no-install-recommends util-linux >/dev/null
elif command -v dnf >/dev/null; then
	dnf install -y -q rpm-build util-linux >/dev/null
elif command -v zypper >/dev/null; then
	zypper --non-interactive -q install rpm-build util-linux shadow >/dev/null
fi

id tester >/dev/null 2>&1 || useradd -m -u 1000 tester 2>/dev/null || useradd -m tester
TESTER_UID=$(id -u tester)
rm -rf "$WORK"
mkdir -p "$WORK/out"

# A hikarisub package of a version: /opt/hikarisub with the marker, the
# helper and a stand-in executable.
make_package() {
	name=$1
	version=$2
	pkgversion=$(printf '%s' "$version" | sed 's/-/~/')
	root="$WORK/root-$name-$version"
	mkdir -p "$root/opt/hikarisub/libexec" "$root/usr/bin"
	printf '%s\n' "$kind" > "$root/opt/hikarisub/package-kind.txt"
	install -m 0755 "$HELPER" "$root/opt/hikarisub/libexec/hikarisub-update-helper"
	printf '#!/bin/sh\necho "%s (%s)"\n' "$version" "$kind" > "$root/opt/hikarisub/hikarisub"
	chmod 0755 "$root/opt/hikarisub/hikarisub"
	if [ "$kind" = deb ]; then
		mkdir -p "$root/DEBIAN"
		cat > "$root/DEBIAN/control" <<EOF
Package: $name
Version: $pkgversion-1
Architecture: all
Maintainer: altqx
Description: stand-in for the update helper's tests
EOF
		dpkg-deb --root-owner-group --build "$root" "$WORK/out/$name-$version.deb" >/dev/null
	else
		mkdir -p "$WORK/rpmbuild/SPECS"
		cat > "$WORK/rpmbuild/SPECS/$name.spec" <<EOF
Name: $name
Version: $pkgversion
Release: 1
Summary: stand-in for the update helper's tests
License: GPL-3.0-or-later
BuildArch: noarch
%description
stand-in
%install
cp -a $root/opt %{buildroot}/
%files
/opt/hikarisub
EOF
		rpmbuild -bb --quiet --define "_topdir $WORK/rpmbuild" "$WORK/rpmbuild/SPECS/$name.spec" >/dev/null 2>&1 ||
			{ say "rpmbuild failed for $name $version"; exit 1; }
		cp "$WORK"/rpmbuild/RPMS/noarch/"$name"-"$pkgversion"-1.noarch.rpm "$WORK/out/$name-$version.rpm"
		rm -rf "$WORK/rpmbuild/RPMS"
	fi
}

make_package hikarisub 0.0.1
make_package hikarisub 0.0.2
make_package hikarisub 0.1.0-beta.1
make_package notsub 0.0.3

# The installed copy.
if [ "$kind" = deb ]; then
	apt-get install -y -qq "$WORK/out/hikarisub-0.0.1.deb" >/dev/null 2>&1
elif command -v dnf >/dev/null; then
	dnf install -y -q "$WORK/out/hikarisub-0.0.1.rpm" >/dev/null
else
	zypper --non-interactive -q install --allow-unsigned-rpm "$WORK/out/hikarisub-0.0.1.rpm" >/dev/null
fi
installed_helper=/opt/hikarisub/libexec/hikarisub-update-helper
[ -x "$installed_helper" ] || { say "the stand-in 0.0.1 did not install"; exit 1; }

# The user's downloads, as the app leaves them.
home=$(getent passwd tester | cut -d: -f6)
for f in "$WORK"/out/*; do
	cp "$f" "$home/"
	chown tester "$home/$(basename "$f")"
done
sha() { sha256sum "$1" | cut -d' ' -f1; }

installed_version() { /opt/hikarisub/hikarisub; }

# expect <code> <name> <command...>: runs the helper as pkexec would.
expect() {
	want=$1
	what=$2
	shift 2
	"$@" >"$WORK/last.log" 2>&1
	got=$?
	if [ "$got" -eq "$want" ]; then
		say "ok   $what (exit $got)"
	else
		say "FAIL $what: exit $got, expected $want"
		sed 's/^/     /' "$WORK/last.log"
		failures=$((failures + 1))
	fi
}

run() { env PKEXEC_UID="$TESTER_UID" "$installed_helper" "$@"; }

new="$home/hikarisub-0.0.2.$kind"
beta="$home/hikarisub-0.1.0-beta.1.$kind"
other="$home/notsub-0.0.3.$kind"

expect 2 "no arguments" run
expect 2 "an unknown command" run remove "$kind" "$new" "$(sha "$new")" 0.0.2
expect 2 "no PKEXEC_UID" env -u PKEXEC_UID "$installed_helper" install "$kind" "$new" "$(sha "$new")" 0.0.2
expect 2 "the kind arch" run install arch "$new" "$(sha "$new")" 0.0.2
if [ "$kind" = deb ]; then otherkind=rpm; else otherkind=deb; fi
expect 2 "a kind that differs from the marker" run install "$otherkind" "$new" "$(sha "$new")" 0.0.2
expect 2 "a relative path" sh -c "cd '$home' && PKEXEC_UID=$TESTER_UID '$installed_helper' install $kind hikarisub-0.0.2.$kind $(sha "$new") 0.0.2"
ln -sf "$new" "$home/link.$kind"
expect 2 "a symbolic link" run install "$kind" "$home/link.$kind" "$(sha "$new")" 0.0.2
cp "$new" "$WORK/root-owned.$kind"
expect 2 "a file owned by another user" run install "$kind" "$WORK/root-owned.$kind" "$(sha "$new")" 0.0.2
expect 2 "a malformed hash" run install "$kind" "$new" nothex 0.0.2
expect 3 "a wrong hash" run install "$kind" "$new" "$(printf '%064d' 0)" 0.0.2
expect 3 "a wrong package name" run install "$kind" "$other" "$(sha "$other")" 0.0.3
expect 3 "a wrong version" run install "$kind" "$new" "$(sha "$new")" 0.0.3
[ "$(installed_version)" = "0.0.1 ($kind)" ] || { say "FAIL a refused package changed the install"; failures=$((failures + 1)); }

if [ "$kind" = deb ]; then
	# Another package manager holds the lock; the helper waits, then gives
	# up with 5. apt and dpkg lock with fcntl, which flock(1) does not take,
	# so perl holds it as they would.
	perl -MFcntl -e 'open(my $f, ">>", "/var/lib/dpkg/lock-frontend") or die;
		my $lock = pack("s s x4 q q i x4", F_WRLCK, 0, 0, 0, 0);
		fcntl($f, F_SETLKW, $lock) or die;
		sleep 30' &
	locker=$!
	sleep 1
	expect 5 "the package manager stays locked" env HIKARISUB_HELPER_LOCK_WAIT=5 PKEXEC_UID="$TESTER_UID" \
		"$installed_helper" install "$kind" "$new" "$(sha "$new")" 0.0.2
	kill "$locker" 2>/dev/null
	wait "$locker" 2>/dev/null
fi

expect 0 "a good package" run install "$kind" "$new" "$(sha "$new")" 0.0.2
[ "$(installed_version)" = "0.0.2 ($kind)" ] || { say "FAIL 0.0.2 is not installed: $(installed_version)"; failures=$((failures + 1)); }
expect 0 "a prerelease, as 0.1.0~beta.1" run install "$kind" "$beta" "$(sha "$beta")" 0.1.0-beta.1
[ "$(installed_version)" = "0.1.0-beta.1 ($kind)" ] || { say "FAIL the prerelease is not installed: $(installed_version)"; failures=$((failures + 1)); }
[ -z "$(ls -A /var/cache/hikarisub-update 2>/dev/null)" ] || { say "FAIL the helper left files in /var/cache/hikarisub-update"; failures=$((failures + 1)); }
[ "$(stat -c %a /var/cache/hikarisub-update)" = 700 ] || { say "FAIL /var/cache/hikarisub-update is not 0700"; failures=$((failures + 1)); }

if [ "$failures" -eq 0 ]; then
	say "all update helper cases passed ($kind)"
	exit 0
fi
say "$failures update helper case(s) failed ($kind)"
exit 1
