#!/bin/sh
# ctest.sh CARGO MANIFEST TARGET_DIR: the cargo tests, or exit 77 (skipped) when crates.io is out of reach.
set -u
cargo=$1 manifest=$2 target=$3
if ! out=$("$cargo" fetch --locked --manifest-path "$manifest" 2>&1); then
    printf '%s\n' "$out"
    case $out in
    *"lock file"*) exit 1 ;;
    esac
    echo "dwarf-reduce: crates.io is out of reach; skipped"
    exit 77
fi
exec "$cargo" test --release --locked --offline --manifest-path "$manifest" --target-dir "$target"
