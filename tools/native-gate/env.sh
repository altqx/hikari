# Sourced by the session scripts: a private HOME/XDG tree per run, the
# HikariSub build under test, and accessibility always on for Qt.
set -u
: "${HIKARI_TREE:=/home/altq/Work/hikari-qt}"
: "${GATE_DIR:=/home/altq/Work/hikari-wt/D1-gate/tools/native-gate}"
: "${EVIDENCE:=/home/altq/Work/hikari-wt/D1-gate/out/native-gate-evidence}"
BUILD="$HIKARI_TREE/out/build/ubuntu-x64-release"
APP="$BUILD/src/app/hikarisub"
UI_TESTS="$BUILD/tests/ui"
FIXTURES="$BUILD/tests/backends/media-fixtures"
QT_PREFIX="$HIKARI_TREE/out/sdk/qt-6.11.2-linux/6.11.2/gcc_64"
export HIKARI_TREE GATE_DIR EVIDENCE BUILD APP UI_TESTS FIXTURES QT_PREFIX

# A clean profile: never the user's settings or layout.
export HOME="/tmp/gate-home-${GATE_SESSION:-x}"
export XDG_CONFIG_HOME="$HOME/.config" XDG_DATA_HOME="$HOME/.local/share" XDG_CACHE_HOME="$HOME/.cache"
export XDG_RUNTIME_DIR="/tmp/gate-runtime-${GATE_SESSION:-x}"
mkdir -p "$HOME" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

# Qt: the AT-SPI bridge on without a desktop's a11y setting, and the
# QPA plugins from the SDK the build links.
export QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1
export QT_ACCESSIBILITY=1
export NO_AT_BRIDGE=0
export LIBGL_ALWAYS_SOFTWARE=1
export PYTHONUNBUFFERED=1

EIINJECT=/tmp/eiinject
if [ ! -x "$EIINJECT" ]; then
    gcc -O1 -o "$EIINJECT" "$GATE_DIR/eiinject.c" $(pkg-config --cflags --libs libei-1.0)
fi
export EIINJECT
