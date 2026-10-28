#!/bin/bash
# app.sh start|stop|kill|fresh [ARGS...]: HikariSub in the current session.
#   start  launch (QT_QPA_PLATFORM from the session), log to $XDG_RUNTIME_DIR/app.log
#   stop   close the main window politely (SIGTERM) so the layout is saved
#   kill   SIGKILL, nothing saved
#   fresh  kill and set the private profile (settings, layout) aside
#   alive  "yes" while this session's HikariSub runs
cmd=$1; shift
# This session's HikariSub processes only (sessions share the container).
mine() {
    for p in $(pgrep -x hikarisub); do
        grep -qz "^XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR\$" /proc/$p/environ 2>/dev/null && \
            [ "$(ps -o stat= -p $p | cut -c1)" != Z ] && echo $p
    done
}
case "$cmd" in
start)
    nohup "$APP" "$@" >> "$XDG_RUNTIME_DIR/app.log" 2>&1 &
    echo $! > "$XDG_RUNTIME_DIR/app.pid"
    for i in $(seq 1 60); do
        python3 "$GATE_DIR/atspi_tool.py" windows 2>/dev/null | grep -q "HikariSub' <" && break
        sleep 0.5
    done
    sleep 1 ;;
stop) for p in $(mine); do kill -TERM $p; done; for i in $(seq 1 20); do [ -z "$(mine)" ] && break; sleep 0.5; done ;;
kill) for p in $(mine); do kill -KILL $p; done; sleep 0.5 ;;
fresh) # the private profile is set aside, not deleted
    for p in $(mine); do kill -KILL $p; done; sleep 0.5
    stamp=$(date +%s%N)
    mkdir -p "$HOME/old-profiles/$stamp"
    [ -d "$XDG_CONFIG_HOME/HikariSub" ] && mv "$XDG_CONFIG_HOME/HikariSub" "$HOME/old-profiles/$stamp/config"
    [ -d "$XDG_DATA_HOME/HikariSub" ] && mv "$XDG_DATA_HOME/HikariSub" "$HOME/old-profiles/$stamp/data"
    true ;;
alive) [ -n "$(mine)" ] && echo yes || echo no ;;
esac
