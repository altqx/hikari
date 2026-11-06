#!/usr/bin/env bash
# Builds the AI sidecar's release assets into <out> (ADR 0031):
#   hikari-ai-<version>.zip                            the bundle, hikari-ai.js and its bytecode, for every platform
#   hikari-ai-runtime-<runtime>-{linux-x64,windows-x64}.gz   the runtime that runs it
#   build.json                                         what the manifest says about this build
#   tools/release/build-ai-sidecar.sh <version> <out> [<commit>]
# Each part is built twice and must come out byte-identical, the split must start about as fast as one executable,
# and the notices must name Bun, JavaScriptCore and tinycc. HIKARI_BUN_DOWNLOADS seeds the pinned Bun archives.
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
    runtime=$bun exe=
    if [[ $platform == windows-x64 ]]; then
        runtime=$windows_bun exe=.exe
    fi
    single=()
    [[ $platform == linux-x64 ]] && single=(--single)
    for pass in 1 2; do
        "$bun" tools/build.ts --target "$platform" --runtime "$runtime" --out "$out/pass-$pass/$platform" \
            --version "$version" --build-id "$build_id" --api-version "$api" "${single[@]}"
    done
    for file in "hikari-ai-runtime$exe" hikari-ai.js hikari-ai.js.jsc build.json; do
        if ! cmp "$out/pass-1/$platform/$file" "$out/pass-2/$platform/$file"; then
            echo "::error::the two $platform builds of $file differ"
            exit 1
        fi
    done
    if ! grep -qF '"NOTICES.txt"' "$out/pass-1/$platform/hikari-ai.meta.json"; then
        echo "::error::the $platform bundle doesn't hold NOTICES.txt"
        exit 1
    fi
done
for file in hikari-ai.js hikari-ai.js.jsc build.json; do
    if ! cmp "$out/pass-1/linux-x64/$file" "$out/pass-1/windows-x64/$file"; then
        echo "::error::$file differs between the platforms, so one bundle can't serve both"
        exit 1
    fi
done

built="$out/pass-1/linux-x64"
cp "$built/build.json" "$out/build.json"
runtime_id=$(sed -n 's/^  "runtime": "\(.*\)",$/\1/p' "$out/build.json")
python3 - "$built" "$out/hikari-ai-$version.zip" <<'EOF'
import sys, zipfile
built, archive = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for name in ("hikari-ai.js", "hikari-ai.js.jsc"):
        info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o644 << 16
        with open(f"{built}/{name}", "rb") as f:
            z.writestr(info, f.read(), compresslevel=9)
EOF
gzip -n -9 -c "$built/hikari-ai-runtime" > "$out/hikari-ai-runtime-$runtime_id-linux-x64.gz"
gzip -n -9 -c "$out/pass-1/windows-x64/hikari-ai-runtime.exe" > "$out/hikari-ai-runtime-$runtime_id-windows-x64.gz"

"$bun" tools/startup.ts --runtime "$built/hikari-ai-runtime" --bundle "$built/hikari-ai.js" \
    --single "$built/hikari-ai-single"

"$built/hikari-ai-runtime" --licenses > "$out/hikari-ai-runtime-notices.txt"
"$built/hikari-ai-runtime" "$built/hikari-ai.js" --licenses > "$out/hikari-ai-notices.txt"
for notices in "$out/hikari-ai-runtime-notices.txt" "$out/hikari-ai-notices.txt"; do
    for notice in "Bun itself is MIT-licensed" "JavaScriptCore and WebCore" "GNU LIBRARY GENERAL PUBLIC LICENSE" \
        "tinycc, statically linked into Bun (LGPL-2.1)" "GNU LESSER GENERAL PUBLIC LICENSE"; do
        if ! grep -qF "$notice" "$notices"; then
            echo "::error::$(basename "$notices") lacks \"$notice\""
            exit 1
        fi
    done
done
(cd "$out" && sha256sum -- hikari-ai-*.zip hikari-ai-runtime-*.gz)
