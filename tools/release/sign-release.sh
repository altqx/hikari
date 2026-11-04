#!/usr/bin/env bash
# Signs release files with minisign, each under the trusted comment "hikarisub <tag> <file name>" (ADR 0027), into
# <file>.minisig beside it.
#
#   MINISIGN_SECRET_KEY=<the .key file> MINISIGN_PASSWORD=<its password> \
#     tools/release/sign-release.sh <minisign> <tag> <file>...
set -euo pipefail

if [[ $# -lt 3 ]]; then
  echo "usage: $0 <minisign> <tag> <file>..." >&2
  exit 2
fi
minisign=$1 tag=$2
shift 2
if [[ -z "${MINISIGN_SECRET_KEY:-}" ]]; then
  echo "::error::MINISIGN_SECRET_KEY is not set, so the release can't be signed." >&2
  exit 1
fi

umask 077
key=$(mktemp "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/minisign-key.XXXXXX")
trap 'rm -f "$key"' EXIT
printf '%s\n' "$MINISIGN_SECRET_KEY" > "$key"
for file in "$@"; do
  name=$(basename "$file")
  printf '%s\n' "${MINISIGN_PASSWORD:-}" |
    "$minisign" -S -s "$key" -m "$file" -x "$file.minisig" -t "hikarisub $tag $name" > /dev/null
  echo "signed $name for $tag"
done
