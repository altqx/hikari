#!/usr/bin/env bash
# Checks the libSubInspector.so a Linux package carries: how it links, what it
# exports, and that the unchanged Inspector.moon loads it through requireffi
# and measures lines with it.
#
# Usage: tests/subinspector/check-package.sh <extracted package directory>
#
# Needs luajit (the interpreter), a C compiler, pkg-config with luajit's
# headers, readelf, nm and ldd, and at least one font fontconfig can find.
set -euo pipefail

if [[ $# -ne 1 || ! -d $1 ]]; then
    echo "usage: $0 <extracted package directory>" >&2
    exit 2
fi
package=$(cd "$1" && pwd -P)
source_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
include="$package/Automation/automation/Include"
library="$include/SubInspector/Inspector/libSubInspector.so"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

[[ -f $library ]] || fail "the package has no $library"

# requireffi resolves the library by path, and the library then resolves
# libass through its own DT_RPATH, five levels up at the package root.
dynamic=$(readelf -d "$library")
grep -Eq '\(NEEDED\).*\[libass\.so\.[0-9]+\]' <<<"$dynamic" ||
    fail "libSubInspector.so does not link libass"$'\n'"$dynamic"
grep -Eq '\(RPATH\).*\[\$ORIGIN/\.\./\.\./\.\./\.\./\.\.\]' <<<"$dynamic" ||
    fail "libSubInspector.so's RPATH is not \$ORIGIN/../../../../.."$'\n'"$dynamic"

# Exactly the nine functions the Windows SubInspector.dll exports.
expected_exports="si_calculateBounds si_changeResolution si_cleanup si_getErrorString si_getVersion si_init si_reloadFonts si_setHeader si_setScript"
exports=$(nm -D --defined-only "$library" | awk '$2 == "T" { print $3 }' | sort | xargs)
[[ $exports == "$expected_exports" ]] ||
    fail "libSubInspector.so exports [$exports], expected [$expected_exports]"

libass_line=$(ldd "$library" 2>/dev/null | grep -E '^\s*libass\.so\.' || true)
[[ -n $libass_line && $libass_line != *"not found"* ]] ||
    fail "libass does not resolve for libSubInspector.so: ${libass_line:-no libass line}"
if grep -Eqx 'libass\.so\.[0-9]+' "$package/.hikarisub-runtime-dependencies" 2>/dev/null; then
    resolved=$(awk '{ print $3 }' <<<"$libass_line")
    [[ $(realpath "$resolved") == "$package"/libass.so.* ]] ||
        fail "libSubInspector.so resolves libass to $resolved, not the package's copy"
fi
echo "linkage: libass resolves to $(awk '{ print $3 }' <<<"$libass_line")"

# The app links LPeg into its executable; MoonScript needs it as a module here.
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/lpeg" "$work/fonts"
# shellcheck disable=SC2046
"${CC:-cc}" -shared -fPIC -O2 $(pkg-config --cflags luajit) -I"$source_dir/HikariSub" \
    "$source_dir/HikariSub/AutomationLPeg.c" -o "$work/lpeg/lpeg.so"

luajit "$source_dir/tests/subinspector/inspector_bounds.lua" \
    "$include" "$work/lpeg" "$work/fonts" "$library"
