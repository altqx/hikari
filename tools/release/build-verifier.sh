#!/usr/bin/env bash
# Builds the app's release-signature check (verify_release_signature.cpp) on its own, with the system's g++ and
# libsodium (Ubuntu: libsodium-dev), for the release job.
#
#   tools/release/build-verifier.sh <output>
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <output>" >&2
  exit 2
fi
tree=$(cd "$(dirname "$0")/../.." && pwd)
g++ -std=c++23 -O2 -Wall -Werror -I"$tree/src/application/include" -I"$tree/src/minisign/include" \
  "$tree/tools/release/verify_release_signature.cpp" "$tree/src/application/update_signature.cpp" \
  "$tree/src/minisign/minisign.cpp" -lsodium -o "$1"
