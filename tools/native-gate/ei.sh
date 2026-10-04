#!/bin/bash
# ei.sh CMD...: send commands to the running libei injector (mutter / KWin sessions).
for c in "$@"; do echo "$c"; done > "$XDG_RUNTIME_DIR/ei.in"
