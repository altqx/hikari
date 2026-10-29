#!/bin/sh
# Pushes one rendered AUR package (render-pkgbuild.sh's PKGBUILD and .SRCINFO in <dir>) to its AUR git repository,
# with the key in $AUR_SSH_PRIVATE_KEY. Same pkgver or files already there: nothing is pushed.
#
#   packaging/linux/aur/publish-aur.sh [--dry-run] [--strict] [--remote <url>] <package> <version> <dir>
#
#   --dry-run  everything but the push; without a key it clones over anonymous https.
#   --remote   another repository in place of the AUR's (tests).
#   --strict   a missing key and a failed clone or push are errors (exit 1), not ::warning:: lines with exit 0.
#
# Same bytes on the main and legacy branches.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
dry_run=false strict=false remote=
while [ $# -gt 0 ]; do
    case $1 in
    --dry-run) dry_run=true; shift ;;
    --strict) strict=true; shift ;;
    --remote) remote=$2; shift 2 ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) break ;;
    esac
done
if [ $# -ne 3 ]; then
    echo "usage: $0 [--dry-run] [--strict] [--remote <url>] <package> <version> <dir>" >&2
    exit 2
fi
package=$1 version=$2 dir=$3
for f in PKGBUILD .SRCINFO; do
    [ -f "$dir/$f" ] || { echo "error: no $dir/$f" >&2; exit 2; }
done
srcinfo_value() { # <file> <key>
    sed -n "s/^[[:space:]]*$2 = //p" "$1" | head -n 1
}
pkgver=$(srcinfo_value "$dir/.SRCINFO" pkgver)
pkgrel=$(srcinfo_value "$dir/.SRCINFO" pkgrel)
[ "$(srcinfo_value "$dir/.SRCINFO" pkgbase)" = "$package" ] ||
    { echo "error: $dir/.SRCINFO is not $package" >&2; exit 2; }

setup_hint="If the AUR account or its key is not set up yet: register at https://aur.archlinux.org/register, add the public key ~/.ssh/aur_ed25519.pub to the account (My Account, SSH Public Key), then publish the newest release with the AUR publish workflow (gh workflow run aur-publish.yml -f tag=v<newest version>)."
give_up() { # <message>
    if [ "$strict" = true ]; then
        echo "::error::$1"
        exit 1
    fi
    echo "::warning::$1 $setup_hint"
    exit 0
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
key_present=false
[ -n "${AUR_SSH_PRIVATE_KEY-}" ] && key_present=true
if [ -z "$remote" ]; then
    if [ "$key_present" = true ]; then
        remote=ssh://aur@aur.archlinux.org/$package.git
    elif [ "$dry_run" = true ]; then
        remote=https://aur.archlinux.org/$package.git
    else
        give_up "AUR_SSH_PRIVATE_KEY is not set, so $package $pkgver-$pkgrel was not published to the AUR."
    fi
fi
if [ "$key_present" = true ]; then
    (umask 077 && printf '%s\n' "$AUR_SSH_PRIVATE_KEY" > "$work/key")
    GIT_SSH_COMMAND="ssh -i '$work/key' -o IdentitiesOnly=yes -o BatchMode=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile='$here/aur.known_hosts' -o HostKeyAlgorithms=ssh-ed25519"
    export GIT_SSH_COMMAND
fi
git_() { git -c user.name=altqx -c user.email=altqx@users.noreply.github.com -c init.defaultBranch=master \
    -c advice.detachedHead=false "$@"; }

if ! git_ clone -q "$remote" "$work/repo" 2>"$work/clone.txt"; then
    sed 's/^/  /' "$work/clone.txt"
    give_up "Could not clone $remote, so $package $pkgver-$pkgrel was not published to the AUR."
fi
if [ -f "$work/repo/.SRCINFO" ]; then
    have=$(srcinfo_value "$work/repo/.SRCINFO" pkgver)-$(srcinfo_value "$work/repo/.SRCINFO" pkgrel)
    if [ "${have%-*}" = "$pkgver" ]; then
        echo "$package already has $have on the AUR; nothing to publish"
        exit 0
    fi
    echo "$package has $have on the AUR"
else
    echo "$package is new on the AUR"
fi
cp "$dir/PKGBUILD" "$dir/.SRCINFO" "$work/repo/"
git_ -C "$work/repo" add PKGBUILD .SRCINFO
if git_ -C "$work/repo" diff --cached --quiet; then
    echo "$package: the AUR has the same files; nothing to publish"
    exit 0
fi
git_ -C "$work/repo" commit -q -m "Update to $version"
git_ -C "$work/repo" --no-pager show --stat --format='commit %H%nAuthor: %an <%ae>%n%n    %s' HEAD
if [ "$dry_run" = true ]; then
    echo "Dry run: $package $pkgver-$pkgrel was not pushed to $remote"
    exit 0
fi
if ! git_ -C "$work/repo" push -q origin HEAD:refs/heads/master 2>"$work/push.txt"; then
    sed 's/^/  /' "$work/push.txt"
    give_up "The AUR refused the push of $package $pkgver-$pkgrel."
fi
echo "Published $package $pkgver-$pkgrel to $remote"
