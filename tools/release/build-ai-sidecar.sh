#!/usr/bin/env bash
# Builds the release's AI sidecar assets, hikari-ai-<version>-{linux-x64,windows-x64}.gz, into <out>.
#   tools/release/build-ai-sidecar.sh <version> <out> [<commit>]
# Each target is built twice and must come out byte-identical; the notices must name Bun, JavaScriptCore and
# tinycc. HIKARI_BUN_DOWNLOADS seeds the pinned Bun archives for an offline build.
set -euo pipefail

version=${1:?usage: build-ai-sidecar.sh <version> <out> [<commit>]}
out=$(mkdir -p "${2:?usage: build-ai-sidecar.sh <version> <out> [<commit>]}" && cd "$2" && pwd)
root=$(cd "$(dirname "$0")/../.." && pwd)
commit=${3:-${GITHUB_SHA:-$(git -C "$root" rev-parse HEAD)}}
downloads=${HIKARI_BUN_DOWNLOADS:-$out/bun-downloads}

bun_for() {
    cmake -DDOWNLOADS="$downloads" -DPLATFORM="$1" -P "$root/cmake/HikariBun.cmake" 2>&1 | tail -n 1
}
bun=$(bun_for linux-x64)
windows_bun=$(bun_for windows-x64)
api=$(sed -n 's/^inline constexpr int kApiVersion = \([0-9]*\);$/\1/p' "$root/src/api/include/hikari/api/api.h")
[[ -n "$api" ]] || { echo "::error::no kApiVersion in src/api/include/hikari/api/api.h"; exit 1; }
build_id="$version+${commit:0:12}"

cd "$root/src/ai-sidecar"
"$bun" ci --ignore-scripts
for platform in linux-x64 windows-x64; do
    runtime=$bun exe=hikari-ai
    if [[ $platform == windows-x64 ]]; then
        runtime=$windows_bun exe=hikari-ai.exe
    fi
    for pass in 1 2; do
        "$bun" tools/build.ts --target "$platform" --runtime "$runtime" --out "$out/pass-$pass/$platform/$exe" \
            --build-id "$build_id" --api-version "$api"
    done
    if ! cmp "$out/pass-1/$platform/$exe" "$out/pass-2/$platform/$exe"; then
        echo "::error::the two $platform builds of the sidecar differ"
        exit 1
    fi
    if ! grep -qF '"NOTICES.txt"' "$out/pass-1/$platform/$exe.meta.json"; then
        echo "::error::the $platform sidecar doesn't bundle NOTICES.txt"
        exit 1
    fi
    gzip -n -9 -c "$out/pass-1/$platform/$exe" > "$out/hikari-ai-$version-$platform.gz"
done
"$out/pass-1/linux-x64/hikari-ai" --licenses > "$out/hikari-ai-notices.txt"
for notice in "Bun itself is MIT-licensed" "JavaScriptCore and WebCore" "GNU LIBRARY GENERAL PUBLIC LICENSE" \
    "tinycc, statically linked into Bun (LGPL-2.1)" "GNU LESSER GENERAL PUBLIC LICENSE"; do
    if ! grep -qF "$notice" "$out/hikari-ai-notices.txt"; then
        echo "::error::the sidecar's --licenses lacks \"$notice\""
        exit 1
    fi
done
(cd "$out" && sha256sum -- hikari-ai-*.gz)
