#!/bin/bash
# hold.sh SECONDS TEST_EXE FUNCTION [TEST ARGS...]
# Runs a QtTest executable under gdb, stops on entry to FUNCTION (a test
# slot, after initTestCase built its window) and runs the Qt event loop for
# SECONDS (QTest::qWait called from gdb) so the window can be driven from
# outside with real compositor input. Then the process is killed.
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
