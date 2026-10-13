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
# /src is the repository. Exits 0 when every case passes. The helper's
# download of a release's SHA256SUMS goes to fake-curl.sh, which serves
# the releases this script makes up.

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

# The fake GitHub: the helper's curl is tests/update/helper/fake-curl.sh,
# which serves /srv/hikarisub-releases/<tag>/<name>.
RELEASES=/srv/hikarisub-releases
rm -rf "$RELEASES"
mkdir -p "$RELEASES"
install -m 0755 "$SRC/tests/update/helper/fake-curl.sh" /usr/local/bin/curl

if [ "$kind" = deb ]; then arch=amd64; else arch=x86_64; fi
# The release file name of a version, for this kind.
asset() {
	if [ "$kind" = deb ]; then echo "hikarisub-$1-linux-amd64.deb"; else echo "hikarisub-$1-linux-x86_64.rpm"; fi
}

# A package of a name and version, for an architecture (default this
# kind's): /opt/hikarisub with the marker, the helper and a stand-in
# executable that prints its version and a tag. Written to
# $WORK/out/<tag>.<kind>.
make_package() {
	tag=$1
	name=$2
	version=$3
	parch=${4:-$arch}
	pkgversion=$(printf '%s' "$version" | sed 's/-/~/')
	root="$WORK/root-$tag"
	rm -rf "$root"
	mkdir -p "$root/opt/hikarisub/libexec" "$root/usr/bin"
	printf '%s\n' "$kind" > "$root/opt/hikarisub/package-kind.txt"
	install -m 0755 "$HELPER" "$root/opt/hikarisub/libexec/hikarisub-update-helper"
	printf '#!/bin/sh\necho "%s (%s)"\n' "$tag" "$kind" > "$root/opt/hikarisub/hikarisub"
	chmod 0755 "$root/opt/hikarisub/hikarisub"
	if [ "$kind" = deb ]; then
		mkdir -p "$root/DEBIAN"
		cat > "$root/DEBIAN/control" <<EOF
Package: $name
Version: $pkgversion-1
Architecture: $parch
Maintainer: altqx
Description: stand-in for the update helper's tests
EOF
		dpkg-deb --root-owner-group --build "$root" "$WORK/out/$tag.deb" >/dev/null
	else
		rm -rf "$WORK/rpmbuild"
		mkdir -p "$WORK/rpmbuild/SPECS"
		cat > "$WORK/rpmbuild/SPECS/$name.spec" <<EOF
Name: $name
Version: $pkgversion
Release: 1
Summary: stand-in for the update helper's tests
License: GPL-3.0-or-later
BuildArch: $parch
%global debug_package %{nil}
%global __os_install_post %{nil}
%description
stand-in
%install
cp -a $root/opt %{buildroot}/
%files
/opt/hikarisub
EOF
		rpmbuild -bb --quiet --target "$parch" --define "_topdir $WORK/rpmbuild" "$WORK/rpmbuild/SPECS/$name.spec" >/dev/null 2>&1 ||
			{ say "rpmbuild failed for $tag"; exit 1; }
		cp "$WORK"/rpmbuild/RPMS/"$parch"/"$name"-"$pkgversion"-1."$parch".rpm "$WORK/out/$tag.rpm"
	fi
}

sha() { sha256sum "$1" | cut -d' ' -f1; }

# publish <version> <file> [<name>]: lists the file's SHA-256 under the
# release file name (or <name>) in that release's SHA256SUMS.
publish() {
	mkdir -p "$RELEASES/v$1"
	printf '%s  %s\n' "$(sha "$2")" "${3:-$(asset "$1")}" >> "$RELEASES/v$1/SHA256SUMS"
}

make_package installed hikarisub 0.0.1
make_package good hikarisub 0.0.2
make_package beta hikarisub 0.1.0-beta.1
# The caller's own package: a hikarisub 0.0.3 the release does not have.
make_package forged hikarisub 0.0.3
make_package official3 hikarisub 0.0.3
# Listed in a release, but not hikarisub, or not for this architecture.
make_package other notsub 0.0.4
if [ "$kind" = deb ]; then otherarch=all; else otherarch=noarch; fi
make_package wrongarch hikarisub 0.0.5 "$otherarch"
# A release whose list carries a wrong digest, one that lists the file
# twice, and one that lists it under the other kind's name only.
make_package wrongsum hikarisub 0.0.6
make_package twice hikarisub 0.0.7
make_package othername hikarisub 0.0.8
# Listed under the version the caller names, but the package is another
# version.
make_package misversion hikarisub 0.0.9

publish 0.0.1 "$WORK/out/installed.$kind"
publish 0.0.2 "$WORK/out/good.$kind"
publish 0.0.2 "$WORK/out/beta.$kind" "HikariSub-0.0.2-windows-x64-setup.exe"
publish 0.1.0-beta.1 "$WORK/out/beta.$kind"
publish 0.0.3 "$WORK/out/official3.$kind"
publish 0.0.4 "$WORK/out/other.$kind"
publish 0.0.5 "$WORK/out/wrongarch.$kind"
mkdir -p "$RELEASES/v0.0.6"
printf '%064d  %s\n' 0 "$(asset 0.0.6)" > "$RELEASES/v0.0.6/SHA256SUMS"
publish 0.0.7 "$WORK/out/twice.$kind"
publish 0.0.7 "$WORK/out/twice.$kind"
if [ "$kind" = deb ]; then publish 0.0.8 "$WORK/out/othername.$kind" "hikarisub-0.0.8-linux-x86_64.rpm"
else publish 0.0.8 "$WORK/out/othername.$kind" "hikarisub-0.0.8-linux-amd64.deb"; fi
publish 0.1.1 "$WORK/out/misversion.$kind"
# 0.0.10 has no release at all.
make_package norelease hikarisub 0.0.10

# The installed copy.
if [ "$kind" = deb ]; then
	apt-get install -y -qq "$WORK/out/installed.deb" >/dev/null 2>&1
elif command -v dnf >/dev/null; then
	dnf install -y -q "$WORK/out/installed.rpm" >/dev/null
else
	zypper --non-interactive -q install --allow-unsigned-rpm "$WORK/out/installed.rpm" >/dev/null
fi
installed_helper=/opt/hikarisub/libexec/hikarisub-update-helper
[ -x "$installed_helper" ] || { say "the stand-in 0.0.1 did not install"; exit 1; }
[ "$(stat -c '%U %a' "$installed_helper")" = "root 755" ] || { say "the helper is not root-owned 0755"; exit 1; }

# The user's downloads, as the app leaves them.
home=$(getent passwd tester | cut -d: -f6)
for f in "$WORK"/out/*; do
	cp "$f" "$home/"
	chown tester "$home/$(basename "$f")"
done

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
unchanged() {
	[ "$(installed_version)" = "installed ($kind)" ] ||
		{ say "FAIL $1 changed the install: $(installed_version)"; failures=$((failures + 1)); }
}

f() { echo "$home/$1.$kind"; }

expect 2 "no arguments" run
expect 2 "an unknown command" run remove "$(f good)" 0.0.2
expect 2 "no PKEXEC_UID" env -u PKEXEC_UID "$installed_helper" install "$(f good)" 0.0.2
# The old form, with the caller's kind and digest: refused, the caller has
# no say in the digest.
expect 2 "a caller-supplied kind and SHA-256" run install "$kind" "$(f forged)" "$(sha "$(f forged)")" 0.0.3
expect 2 "a caller-supplied SHA-256" run install "$(f forged)" "$(sha "$(f forged)")" 0.0.3
expect 2 "a relative path" sh -c "cd '$home' && PKEXEC_UID=$TESTER_UID '$installed_helper' install good.$kind 0.0.2"
ln -sf "$(f good)" "$home/link.$kind"
expect 2 "a symbolic link" run install "$home/link.$kind" 0.0.2
cp "$(f good)" "$WORK/root-owned.$kind"
expect 2 "a file owned by another user" run install "$WORK/root-owned.$kind" 0.0.2
for bad in '' v0.0.2 0.0.2+build.1 01.0.2 0.0 0.0.2.1 '0.0.2;id' '0.0.2 ' "$(printf '0.0.2\n0.0.3')" 0.0.2-beta..1 0.0.2-01 ../0.0.2; do
	expect 2 "the bad version '$bad'" run install "$(f good)" "$bad"
done
expect 2 "the installed version again" run install "$(f installed)" 0.0.1
: > "$RELEASES/requests.log"
expect 3 "the caller's own package, at a version that is released" run install "$(f forged)" 0.0.3
grep -qx "https://github.com/altqx/hikari/releases/download/v0.0.3/SHA256SUMS" "$RELEASES/requests.log" ||
	{ say "FAIL the helper did not ask GitHub for v0.0.3's SHA256SUMS"; failures=$((failures + 1)); }
expect 3 "a SHA256SUMS whose digest does not match" run install "$(f wrongsum)" 0.0.6
expect 3 "a SHA256SUMS that lists the file twice" run install "$(f twice)" 0.0.7
expect 3 "a digest listed under another file's name" run install "$(f othername)" 0.0.8
expect 3 "the windows asset's digest" run install "$(f beta)" 0.0.2
expect 3 "a listed package that is not hikarisub" run install "$(f other)" 0.0.4
expect 3 "a listed package for another architecture" run install "$(f wrongarch)" 0.0.5
expect 3 "a listed package of another version" run install "$(f misversion)" 0.1.1
expect 6 "a version with no release" run install "$(f norelease)" 0.0.10
expect 6 "GitHub cannot be reached" env FAKE_CURL_OFFLINE=1 PKEXEC_UID="$TESTER_UID" "$installed_helper" install "$(f good)" 0.0.2
unchanged "a refused package"

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
		"$installed_helper" install "$(f good)" 0.0.2
	kill "$locker" 2>/dev/null
	wait "$locker" 2>/dev/null
fi

expect 0 "the release's own package" run install "$(f good)" 0.0.2
[ "$(installed_version)" = "good ($kind)" ] || { say "FAIL 0.0.2 is not installed: $(installed_version)"; failures=$((failures + 1)); }
expect 2 "a downgrade to a released version" run install "$(f installed)" 0.0.1
expect 0 "a prerelease, as 0.1.0~beta.1" run install "$(f beta)" 0.1.0-beta.1
[ "$(installed_version)" = "beta ($kind)" ] || { say "FAIL the prerelease is not installed: $(installed_version)"; failures=$((failures + 1)); }
[ -z "$(ls -A /var/cache/hikarisub-update 2>/dev/null)" ] || { say "FAIL the helper left files in /var/cache/hikarisub-update"; failures=$((failures + 1)); }
[ "$(stat -c '%U %a' /var/cache/hikarisub-update)" = "root 700" ] || { say "FAIL /var/cache/hikarisub-update is not root's 0700"; failures=$((failures + 1)); }

if [ "$failures" -eq 0 ]; then
	say "all update helper cases passed ($kind)"
	exit 0
fi
say "$failures update helper case(s) failed ($kind)"
exit 1
