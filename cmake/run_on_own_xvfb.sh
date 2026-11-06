#!/bin/sh
# run_on_own_xvfb.sh COMMAND [ARGS...]: COMMAND on an Xvfb no other run shares.
# xvfb-run -a picks a display before Xvfb locks it, so suites started together shared one (#716).
n=99
while [ "$n" -lt 600 ]; do
    if [ ! -e "/tmp/.X$n-lock" ] && [ ! -e "/tmp/.X11-unix/X$n" ]; then
        Xvfb ":$n" -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
        xvfb=$!
        tries=0
        while kill -0 "$xvfb" 2>/dev/null && [ "$tries" -lt 100 ]; do
            owner=$(cat "/tmp/.X$n-lock" 2>/dev/null | tr -d ' \n')
            [ -n "$owner" ] && [ "$owner" != "$xvfb" ] && break
            if [ "$owner" = "$xvfb" ] && [ -S "/tmp/.X11-unix/X$n" ]; then
                trap 'kill "$xvfb" 2>/dev/null; exit 143' TERM
                trap 'kill "$xvfb" 2>/dev/null; exit 130' INT
                DISPLAY=":$n" "$@"
                status=$?
                kill "$xvfb" 2>/dev/null
                wait "$xvfb" 2>/dev/null
                exit "$status"
            fi
            sleep 0.1
            tries=$((tries + 1))
        done
        kill "$xvfb" 2>/dev/null
        wait "$xvfb" 2>/dev/null
    fi
    n=$((n + 1))
done
echo "run_on_own_xvfb: no free display in :99-:599" >&2
exit 1
