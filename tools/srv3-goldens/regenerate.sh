#!/usr/bin/env bash
# Usage: tools/srv3-goldens/regenerate.sh [work-dir]. Rewrites tests/fixtures/export/srv3/golden with pinned
# YTSubConverter on a pinned .NET 8 runtime. Needs curl, xz, xvfb-run and network access.
set -euo pipefail

YTSC_VERSION=1.6.5
YTSC_URL="https://github.com/arcusmaximus/YTSubConverter/releases/download/${YTSC_VERSION}/YTSubConverter-Linux.tar.xz"
YTSC_SHA256=23d9053161eb8026d255cebd704ce71d7a336b4053e08dd8824b2b5265b217a2
DOTNET_VERSION=8.0.32
DOTNET_URL="https://builds.dotnet.microsoft.com/dotnet/Runtime/${DOTNET_VERSION}/dotnet-runtime-${DOTNET_VERSION}-linux-x64.tar.gz"
DOTNET_SHA512=639a17d84e4d52fecd62c7d4e528e7bc0ae3c9860de2530cb41a2ee7196b69ea18271d8795849180742770e25b4f913df292a5a236fa197cf5d3cd0ec5ba665f

repo=$(cd "$(dirname "$0")/../.." && pwd)
fixtures="$repo/tests/fixtures/export/srv3"
work=${1:-$(mktemp -d)}
mkdir -p "$work"
work=$(cd "$work" && pwd)

fetch() {
    local url=$1 file=$2 sum=$3 tool=$4
    [ -f "$file" ] || curl -fsSL "$url" -o "$file"
    echo "$sum  $file" | "$tool" -c --quiet
}

fetch "$YTSC_URL" "$work/ytsc.tar.xz" "$YTSC_SHA256" sha256sum
fetch "$DOTNET_URL" "$work/dotnet.tar.gz" "$DOTNET_SHA512" sha512sum
rm -rf "$work/ytsc" "$work/ytsc-options" "$work/dotnet" "$work/cases"
mkdir -p "$work/ytsc" "$work/dotnet" "$work/cases"
tar -xJf "$work/ytsc.tar.xz" -C "$work/ytsc"
tar -xzf "$work/dotnet.tar.gz" -C "$work/dotnet"
cp -a "$work/ytsc" "$work/ytsc-options"
cp "$fixtures/StyleOptions.xml" "$work/ytsc-options/"

cp "$fixtures"/inputs/*.ass "$work/cases/"
for f in "$repo"/tests/fixtures/export/*.ass; do
    cp "$f" "$work/cases/hikari-$(basename "$f")"
done

convert() {
    local bin=$1 input=$2 output=$3
    rm -f "$output"
    (cd "$(dirname "$input")" &&
        LANG=C LC_ALL=C DOTNET_ROOT="$work/dotnet" xvfb-run -a "$work/dotnet/dotnet" "$bin/ytsubconverter.dll" \
            "$(basename "$input")" "$output" >/dev/null 2>&1)
    [ -s "$output" ] || { echo "YTSubConverter wrote nothing for $input" >&2; exit 1; }
}

rm -f "$fixtures"/golden/*.ytt
mkdir -p "$fixtures/golden"
for input in "$work"/cases/*.ass; do
    name=$(basename "$input" .ass)
    convert "$work/ytsc" "$input" "$fixtures/golden/$name.ytt"
    convert "$work/ytsc-options" "$input" "$work/cases/$name.options.ytt"
    cmp -s "$fixtures/golden/$name.ytt" "$work/cases/$name.options.ytt" || cp "$work/cases/$name.options.ytt" "$fixtures/golden/$name.options.ytt"
done
echo "Goldens written to $fixtures/golden"
