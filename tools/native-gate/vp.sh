#!/bin/bash
# vp.sh CMD...: send commands to the running virtual pointer (sway session).
for c in "$@"; do echo "$c"; done > "$XDG_RUNTIME_DIR/vpointer.in"
