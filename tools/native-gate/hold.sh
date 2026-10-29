#!/bin/bash
# hold.sh SECONDS TEST_EXE FUNCTION [TEST ARGS...]: under gdb, at FUNCTION run the event loop SECONDS for outside input.
secs=$1; exe=$2; fn=$3; shift 3
exec gdb -q -batch \
    -ex "set pagination off" -ex "set breakpoint pending on" \
    -ex "handle SIGPIPE nostop noprint" \
    -ex "break $fn" \
    -ex "run" \
    -ex "echo HOLDING\n" \
    -ex "call ((void(*)(int))_ZN5QTest5qWaitEi)($((secs * 1000)))" \
    -ex "kill" \
    --args "$exe" "$fn" "$@"
