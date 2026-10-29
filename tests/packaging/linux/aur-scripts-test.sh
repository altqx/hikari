#!/bin/sh
# The AUR scripts in packaging/linux/aur (installers spec, 5.5 and 8.2),
# without the AUR: a stand-in tar.gz, a fake curl for the AUR RPC and local
# bare repositories in place of the AUR's.
#
#   tests/packaging/linux/aur-scripts-test.sh
#
# Checks: the packages of a stable release and of a prerelease, the pkgver
# mapping and its vercmp ordering, both rendered packages, the downgrade
# refusal per package, the newest-release check, the codebase each package
# follows, and publish-aur.sh's paths: no key, first push, an idempotent
# second run, dry run, a refused clone.
# Needs python3, git, and makepkg and vercmp as a non-root user, or docker.
#
# This file has the same bytes on the main and legacy branches.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../../.." && pwd)
aur=$repo/packaging/linux/aur
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*" >&2; exit 1; }
unset AUR_SSH_PRIVATE_KEY || true
image=$("$repo/packaging/linux/fetch-tool.sh" --image archlinux)
run_vercmp() {
    if command -v vercmp > /dev/null 2>&1; then vercmp "$1" "$2"; else docker run --rm "$image" vercmp "$1" "$2"; fi
}

# A stand-in for the release tar.gz: a shell script as the app, so no host
# library is needed (resolve-deps.sh gives the runtime section only).
make_tarball() { # <version>
    t=$work/tree-$1/hikarisub-linux-x86_64
    mkdir -p "$t/share/applications"
    printf '#!/bin/sh\necho "%s ($(cat /opt/hikarisub/package-kind.txt))"\n' "$1" > "$t/hikarisub"
    chmod 0755 "$t/hikarisub"
    echo GPL > "$t/LICENSE"
    : > "$t/SHA256SUMS"
    : > "$t/install-desktop-integration.sh"
    printf '[Desktop Entry]\nType=Application\nName=HikariSub\nExec=hikarisub %%f\n' \
        > "$t/share/applications/io.github.altqx.HikariSub.desktop"
    tar -czf "$work/hikarisub-$1-linux-x86_64.tar.gz" -C "$work/tree-$1" hikarisub-linux-x86_64
    echo "$work/hikarisub-$1-linux-x86_64.tar.gz"
}

# The fake AUR RPC: $work/rpc/<package> holds the package's "pkgver-pkgrel",
# no file is a package the AUR does not have. Every query is logged.
mkdir -p "$work/bin" "$work/rpc"
cat > "$work/bin/curl" <<EOF
#!/bin/sh
for arg; do url=\$arg; done
echo "\$url" >> "$work/curl.log"
package=\${url##*=}
if [ -f "$work/rpc/\$package" ]; then
    printf '{"resultcount":1,"results":[{"Name":"%s","Version":"%s"}]}' "\$package" "\$(cat "$work/rpc/\$package")"
else
    printf '{"resultcount":0,"results":[]}'
fi
EOF
chmod 0755 "$work/bin/curl"
PATH=$work/bin:$PATH
export PATH

# --- The packages of a release, and pkgver.
[ "$("$aur/render-pkgbuild.sh" --packages 0.0.2-rc.1)" = hikarisub-beta-bin ] || fail "a prerelease goes to beta only"
[ "$("$aur/render-pkgbuild.sh" --packages 0.0.2 | tr '\n' ' ')" = "hikarisub-stable-bin hikarisub-beta-bin " ] ||
    fail "a stable release goes to stable and beta"
[ "$("$aur/render-pkgbuild.sh" --packages 0.1.0-beta.2)" = hikarisub-beta-bin ] || fail "a Qt prerelease goes to beta only"
pass "a stable release goes to both packages, a prerelease to hikarisub-beta-bin only"
for pair in 0.0.2-rc.1=0.0.2rc.1 0.0.2=0.0.2 0.1.0-beta.2=0.1.0beta.2 0.1.0-alpha.1=0.1.0alpha.1 0.0.3-rc.10=0.0.3rc.10; do
    [ "$("$aur/render-pkgbuild.sh" --pkgver "${pair%=*}")" = "${pair#*=}" ] || fail "pkgver of ${pair%=*}"
done
for bad in 0.0.2-pre.1 0.0.2-rc 0.0.2-rc.1+b 1.2 v0.0.2; do
    if "$aur/render-pkgbuild.sh" --pkgver "$bad" > /dev/null 2>&1; then fail "accepted $bad"; fi
done
pass "pkgver drops the '-': 0.0.2-rc.1 is 0.0.2rc.1; other prerelease forms are refused"
order="0.0.1 0.0.2-rc.1 0.0.2-rc.2 0.0.2-rc.10 0.0.2 0.0.3-alpha.1 0.0.3-beta.1 0.0.3-rc.1 0.0.3 0.1.0-alpha.1 0.1.0-beta.1 0.1.0-beta.2 0.1.0-rc.1 0.1.0 0.1.1-rc.1"
prev=
for v in $order; do
    p=$("$aur/render-pkgbuild.sh" --pkgver "$v")
    if [ -n "$prev" ]; then
        [ "$(run_vercmp "$prev" "$p")" = -1 ] || fail "vercmp $prev $p is not -1"
    fi
    prev=$p
done
pass "vercmp keeps the release order: rc -> stable -> next rc, legacy 0.0.x below Qt 0.1.0 prereleases"

# --- Both packages rendered.
tar_rc=$(make_tarball 0.0.2-rc.1)
tar_st=$(make_tarball 0.0.2)
"$aur/render-pkgbuild.sh" hikarisub-beta-bin 0.0.2-rc.1 "$tar_rc" "$work/out/beta" > /dev/null
p=$work/out/beta
grep -qx 'pkgname=hikarisub-beta-bin' "$p/PKGBUILD" || fail "beta pkgname"
grep -qx 'pkgver=0.0.2rc.1' "$p/PKGBUILD" || fail "beta pkgver"
grep -qx '_version=0.0.2-rc.1' "$p/PKGBUILD" || fail "beta _version"
grep -qx '_channel=beta' "$p/PKGBUILD" || fail "beta channel"
grep -qx "sha256sums=('$(sha256sum "$tar_rc" | cut -d ' ' -f 1)')" "$p/PKGBUILD" || fail "beta sha256sums"
grep -qx "	source = hikarisub-0.0.2-rc.1-linux-x86_64.tar.gz::https://github.com/altqx/hikari/releases/download/v0.0.2-rc.1/hikarisub-0.0.2-rc.1-linux-x86_64.tar.gz" \
    "$p/.SRCINFO" || fail "beta source URL in .SRCINFO"
grep -qx '	provides = hikarisub' "$p/.SRCINFO" && grep -qx '	conflicts = hikarisub' "$p/.SRCINFO" ||
    fail "beta provides and conflicts"
grep -q 'licenses/$pkgname/LICENSE' "$p/PKGBUILD" || fail "license path"
pass "hikarisub-beta-bin 0.0.2-rc.1: pkgver 0.0.2rc.1, the tag's source URL, channel beta"
"$aur/render-pkgbuild.sh" hikarisub-stable-bin 0.0.2 "$tar_st" "$work/out/stable" > /dev/null
p=$work/out/stable
grep -qx 'pkgname=hikarisub-stable-bin' "$p/PKGBUILD" && grep -qx 'pkgver=0.0.2' "$p/PKGBUILD" &&
    grep -qx '_channel=stable' "$p/PKGBUILD" || fail "stable package"
grep -qx 'pkgbase = hikarisub-stable-bin' "$p/.SRCINFO" || fail "stable .SRCINFO"
rc=0
"$aur/render-pkgbuild.sh" hikarisub-stable-bin 0.0.2-rc.1 "$tar_rc" "$work/out/no" > /dev/null 2>&1 || rc=$?
[ $rc = 1 ] || fail "hikarisub-stable-bin took a prerelease (exit $rc)"
rc=0
"$aur/render-pkgbuild.sh" hikarisub-bin 0.0.2 "$tar_st" "$work/out/no" > /dev/null 2>&1 || rc=$?
[ $rc = 2 ] || fail "an unknown package name was rendered (exit $rc)"
pass "hikarisub-stable-bin 0.0.2: channel stable; it refuses a prerelease"

# --- The downgrade refusal reads each package's own AUR version.
: > "$work/curl.log"
echo 0.0.2-1 > "$work/rpc/hikarisub-beta-bin"
rc=0
"$aur/render-pkgbuild.sh" hikarisub-beta-bin 0.0.2-rc.1 "$tar_rc" "$work/out/d" > /dev/null 2>&1 || rc=$?
[ $rc = 3 ] || fail "beta 0.0.2-rc.1 over the AUR's 0.0.2 (exit $rc)"
grep -q 'arg\[\]=hikarisub-beta-bin$' "$work/curl.log" || fail "the RPC was not asked for hikarisub-beta-bin"
"$aur/render-pkgbuild.sh" hikarisub-stable-bin 0.0.2 "$tar_st" "$work/out/d" > /dev/null ||
    fail "stable-bin was refused for beta-bin's version"
grep -q 'arg\[\]=hikarisub-stable-bin$' "$work/curl.log" || fail "the RPC was not asked for hikarisub-stable-bin"
echo 0.0.2rc.1-1 > "$work/rpc/hikarisub-beta-bin"
"$aur/render-pkgbuild.sh" hikarisub-beta-bin 0.0.2 "$tar_st" "$work/out/d" > /dev/null || fail "beta 0.0.2 over 0.0.2rc.1"
"$aur/render-pkgbuild.sh" hikarisub-beta-bin 0.0.2-rc.1 "$tar_rc" "$work/out/d" > /dev/null ||
    fail "the same version again (a re-run) was refused"
echo 0.1.0beta.1-1 > "$work/rpc/hikarisub-beta-bin"
rc=0
"$aur/render-pkgbuild.sh" hikarisub-beta-bin 0.0.2 "$tar_st" "$work/out/d" > /dev/null 2>&1 || rc=$?
[ $rc = 3 ] || fail "legacy 0.0.2 over Qt 0.1.0beta.1 (exit $rc)"
echo 0.1.0-2 > "$work/rpc/hikarisub-stable-bin"
rc=0
"$aur/render-pkgbuild.sh" hikarisub-stable-bin 0.0.2 "$tar_st" "$work/out/d" > /dev/null 2>&1 || rc=$?
[ $rc = 3 ] || fail "legacy 0.0.2 over Qt 0.1.0-2 on stable (exit $rc)"
rm -f "$work/rpc/"*
pass "each package is checked against its own AUR version; lower is refused (exit 3), equal or higher is not"

# --- publish-aur.sh.
"$aur/publish-aur.sh" hikarisub-beta-bin 0.0.2-rc.1 "$work/out/beta" > "$work/nokey.txt" 2>&1 ||
    fail "publish-aur.sh without a key failed"
grep -q '^::warning::AUR_SSH_PRIVATE_KEY is not set, so hikarisub-beta-bin 0.0.2rc.1-1 was not published' "$work/nokey.txt" &&
    grep -q 'aur_ed25519.pub' "$work/nokey.txt" || { cat "$work/nokey.txt"; fail "no-key warning"; }
rc=0
"$aur/publish-aur.sh" --strict hikarisub-beta-bin 0.0.2-rc.1 "$work/out/beta" > /dev/null 2>&1 || rc=$?
[ $rc = 1 ] || fail "--strict without a key exited $rc"
pass "without AUR_SSH_PRIVATE_KEY: a ::warning:: with the setup steps and exit 0 (exit 1 with --strict)"

remotes=$work/remotes
mkdir -p "$remotes"
git init -q --bare "$remotes/hikarisub-beta-bin.git"
git init -q --bare "$remotes/hikarisub-stable-bin.git"
commits() { git --git-dir="$remotes/$1.git" rev-list --count refs/heads/master 2>/dev/null || echo 0; }
"$aur/publish-aur.sh" --remote "$remotes/hikarisub-beta-bin.git" hikarisub-beta-bin 0.0.2-rc.1 "$work/out/beta" > /dev/null
[ "$(commits hikarisub-beta-bin)" = 1 ] || fail "the first publish made no commit"
log=$(git --git-dir="$remotes/hikarisub-beta-bin.git" log -1 --format='%an <%ae>|%s' master)
[ "$log" = "altqx <altqx@users.noreply.github.com>|Update to 0.0.2-rc.1" ] || fail "commit: $log"
[ "$(git --git-dir="$remotes/hikarisub-beta-bin.git" ls-tree --name-only master | tr '\n' ' ')" = ".SRCINFO PKGBUILD " ] ||
    fail "the commit does not hold exactly PKGBUILD and .SRCINFO"
"$aur/publish-aur.sh" --remote "$remotes/hikarisub-beta-bin.git" hikarisub-beta-bin 0.0.2-rc.1 "$work/out/beta" \
    > "$work/again.txt"
[ "$(commits hikarisub-beta-bin)" = 1 ] || fail "a second run made another commit"
grep -q 'already has 0.0.2rc.1-1' "$work/again.txt" || fail "second run: $(cat "$work/again.txt")"
"$aur/render-pkgbuild.sh" --offline hikarisub-beta-bin 0.0.2 "$tar_st" "$work/out/beta2" > /dev/null
"$aur/publish-aur.sh" --dry-run --remote "$remotes/hikarisub-beta-bin.git" hikarisub-beta-bin 0.0.2 "$work/out/beta2" \
    > "$work/dry.txt"
[ "$(commits hikarisub-beta-bin)" = 1 ] || fail "the dry run pushed"
grep -q 'Update to 0.0.2$' "$work/dry.txt" && grep -q '^Dry run: hikarisub-beta-bin 0.0.2-1 was not pushed' "$work/dry.txt" ||
    fail "dry run: $(cat "$work/dry.txt")"
"$aur/publish-aur.sh" --remote "$work/no-such-repo.git" hikarisub-beta-bin 0.0.2 "$work/out/beta2" > "$work/refused.txt" 2>&1 ||
    fail "a refused clone failed the script"
grep -q '^::warning::Could not clone .*register at https://aur.archlinux.org/register' "$work/refused.txt" ||
    fail "refused clone: $(cat "$work/refused.txt")"
pass "publish-aur.sh: first push creates the package, a re-run commits nothing, --dry-run does not push, a refused clone is a warning"

# --- release-aur.sh: the channel rules end to end, and the newest-release check.
rm -rf "$remotes"/*.git
git init -q --bare "$remotes/hikarisub-beta-bin.git"
git init -q --bare "$remotes/hikarisub-stable-bin.git"
cat > "$work/releases.json" <<'EOF'
[{"tag_name": "v0.0.1", "draft": false, "prerelease": false},
 {"tag_name": "v0.0.2-rc.1", "draft": false, "prerelease": true},
 {"tag_name": "v0.1.0-alpha.1", "draft": false, "prerelease": true}]
EOF
"$aur/release-aur.sh" --no-build --releases "$work/releases.json" --remote-base "$remotes" \
    0.0.2-rc.1 "$tar_rc" "$work/rel1" > "$work/rel1.txt" 2>&1 || { cat "$work/rel1.txt"; fail "release 0.0.2-rc.1"; }
[ "$(commits hikarisub-beta-bin)" = 1 ] && [ "$(commits hikarisub-stable-bin)" = 0 ] ||
    fail "0.0.2-rc.1 did not go to beta only"
pass "release-aur.sh 0.0.2-rc.1: hikarisub-beta-bin only (a newer 0.1.0 alpha does not count against legacy)"
"$aur/release-aur.sh" --no-build --releases "$work/releases.json" --remote-base "$remotes" \
    0.0.2-rc.1 "$tar_rc" "$work/rel1" > /dev/null 2>&1
[ "$(commits hikarisub-beta-bin)" = 1 ] || fail "re-running the release made a commit"
pass "release-aur.sh run again for the same version commits nothing"
cat > "$work/releases.json" <<'EOF'
[{"tag_name": "v0.0.1", "draft": false, "prerelease": false},
 {"tag_name": "v0.0.2-rc.1", "draft": false, "prerelease": true},
 {"tag_name": "v0.0.2", "draft": false, "prerelease": false},
 {"tag_name": "v0.0.3-rc.1", "draft": true, "prerelease": true}]
EOF
"$aur/release-aur.sh" --no-build --releases "$work/releases.json" --remote-base "$remotes" \
    0.0.2 "$tar_st" "$work/rel2" > "$work/rel2.txt" 2>&1 || { cat "$work/rel2.txt"; fail "release 0.0.2"; }
[ "$(commits hikarisub-beta-bin)" = 2 ] && [ "$(commits hikarisub-stable-bin)" = 1 ] || fail "0.0.2 did not go to both"
pass "release-aur.sh 0.0.2: both packages (a draft is not a newer release)"
cat > "$work/releases.json" <<'EOF'
[{"tag_name": "v0.0.2", "draft": false, "prerelease": false},
 {"tag_name": "v0.0.3-rc.1", "draft": false, "prerelease": true}]
EOF
git init -q --bare "$remotes/old/hikarisub-beta-bin.git"
git init -q --bare "$remotes/old/hikarisub-stable-bin.git"
"$aur/release-aur.sh" --no-build --releases "$work/releases.json" --remote-base "$remotes/old" \
    0.0.2 "$tar_st" "$work/rel3" > "$work/rel3.txt" 2>&1 || { cat "$work/rel3.txt"; fail "older tag"; }
grep -q '^::warning::hikarisub-beta-bin: v0.0.3-rc.1 is newer than v0.0.2' "$work/rel3.txt" || fail "no newer-release warning"
[ "$(git --git-dir="$remotes/old/hikarisub-beta-bin.git" rev-list --count --all)" = 0 ] ||
    fail "an older tag was published to an empty hikarisub-beta-bin"
[ "$(git --git-dir="$remotes/old/hikarisub-stable-bin.git" rev-list --count --all)" = 1 ] ||
    fail "0.0.2 is still the newest stable release and belongs on hikarisub-stable-bin"
"$aur/release-aur.sh" --strict --no-build --releases "$work/releases.json" --remote-base "$remotes/old" \
    0.0.2 "$tar_st" "$work/rel3" > /dev/null 2>&1 || fail "--strict failed although hikarisub-stable-bin has 0.0.2"
rc=0
"$aur/release-aur.sh" --strict --no-build --releases "$work/releases.json" --remote-base "$remotes/old" \
    0.0.2-rc.1 "$tar_rc" "$work/rel4" > "$work/rel4.txt" 2>&1 || rc=$?
[ $rc = 1 ] && grep -q '^::error::0.0.2-rc.1 was published to no AUR package' "$work/rel4.txt" ||
    fail "--strict let a run that refused every package pass (exit $rc)"
[ "$(git --git-dir="$remotes/old/hikarisub-beta-bin.git" rev-list --count --all)" = 0 ] || fail "0.0.2-rc.1 reached beta"
pass "an older tag is refused for a channel with a newer release, even on an empty package (--strict: an error when nothing is left)"

# --- release-aur.sh: each package follows its own codebase (ADR-0021).
move=$remotes/move
git init -q --bare "$move/hikarisub-beta-bin.git"
git init -q --bare "$move/hikarisub-stable-bin.git"
mcommits() { git --git-dir="$move/$1.git" rev-list --count --all; }
release() { # <version> [--strict]: publishes to $move against $work/releases.json, output in $work/move.txt
    rc=0
    "$aur/release-aur.sh" ${2:-} --no-build --releases "$work/releases.json" --remote-base "$move" \
        "$1" "$(make_tarball "$1")" "$work/move-$1" > "$work/move.txt" 2>&1 || rc=$?
}
list() { # <tag[:pre]>...: the release list, published
    { echo '['; sep=; for entry; do
        pre=false; [ "${entry#*:}" = pre ] && pre=true
        printf '%s{"tag_name": "%s", "draft": false, "prerelease": %s}\n' "$sep" "${entry%:pre}" "$pre"; sep=,
    done; echo ']'; } > "$work/releases.json"
}
list v0.0.2 v0.1.0-alpha.1:pre
release 0.1.0-alpha.1 --strict
[ $rc = 1 ] && grep -q '^::warning::hikarisub-beta-bin follows legacy until v0.1.0-beta.1, so v0.1.0-alpha.1 is not published' "$work/move.txt" ||
    { cat "$work/move.txt"; fail "an alpha before 0.1.0-beta.1 was not refused for hikarisub-beta-bin (exit $rc)"; }
list v0.0.2 v0.1.0-alpha.1:pre
release 0.1.0-beta.1 --strict
[ $rc = 0 ] && [ "$(mcommits hikarisub-beta-bin)" = 1 ] && [ "$(mcommits hikarisub-stable-bin)" = 0 ] ||
    { cat "$work/move.txt"; fail "0.1.0-beta.1 did not go to hikarisub-beta-bin (exit $rc)"; }
pass "hikarisub-beta-bin follows legacy until 0.1.0-beta.1, which it takes before the release list has it"
list v0.0.2 v0.1.0-alpha.1:pre v0.1.0-beta.1:pre v0.0.3-rc.1:pre
release 0.0.3-rc.1 --strict
[ $rc = 1 ] && grep -q '^::warning::hikarisub-beta-bin follows 0.1.0 and up since v0.1.0-beta.1, so legacy v0.0.3-rc.1 is not published' "$work/move.txt" &&
    [ "$(mcommits hikarisub-beta-bin)" = 1 ] || { cat "$work/move.txt"; fail "legacy 0.0.3-rc.1 reached hikarisub-beta-bin (exit $rc)"; }
list v0.0.2 v0.1.0-beta.1:pre v0.0.3-rc.1:pre v0.0.3
release 0.0.3 --strict
[ $rc = 0 ] && [ "$(mcommits hikarisub-stable-bin)" = 1 ] && [ "$(mcommits hikarisub-beta-bin)" = 1 ] ||
    { cat "$work/move.txt"; fail "legacy 0.0.3 did not go to hikarisub-stable-bin only (exit $rc)"; }
pass "after 0.1.0-beta.1, legacy goes to hikarisub-stable-bin only"
list v0.0.3 v0.1.0-beta.1:pre v0.1.0
release 0.1.0 --strict
[ $rc = 0 ] && [ "$(mcommits hikarisub-stable-bin)" = 2 ] && [ "$(mcommits hikarisub-beta-bin)" = 2 ] ||
    { cat "$work/move.txt"; fail "0.1.0 did not go to both packages (exit $rc)"; }
list v0.0.3 v0.1.0-beta.1:pre v0.1.0 v0.0.4
release 0.0.4 --strict
[ $rc = 1 ] && grep -q '^::warning::hikarisub-stable-bin follows 0.1.0 and up since v0.1.0, so legacy v0.0.4 is not published' "$work/move.txt" &&
    [ "$(mcommits hikarisub-stable-bin)" = 2 ] || { cat "$work/move.txt"; fail "legacy 0.0.4 reached hikarisub-stable-bin (exit $rc)"; }
pass "the first stable 0.1.0 moves hikarisub-stable-bin; legacy then reaches no AUR package (--strict: an error)"

echo "all AUR script checks passed"
