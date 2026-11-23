#!/usr/bin/env bash
# Uploads each file to the vcpkg-assets release as an asset named by its SHA512, which AssetSources.cmake falls back to.
#
#   cmake/provision/fill-asset-mirror.sh <downloads/file>...
set -euo pipefail

repo=altqx/hikari
tag=vcpkg-assets
[ $# -gt 0 ] || { echo "usage: $0 <file>..." >&2; exit 2; }

have=$(gh release view "$tag" -R "$repo" --json assets -q '.assets[].name')
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
for file in "$@"; do
    sha=$(sha512sum "$file" | cut -d' ' -f1)
    if grep -qx "$sha" <<<"$have"; then
        echo "have   $sha  $file"
        continue
    fi
    ln -s "$(realpath "$file")" "$stage/$sha"
    gh release upload "$tag" -R "$repo" "$stage/$sha"
    echo "upload $sha  $file"
done
