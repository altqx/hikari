#!/bin/bash
# Builds the two small input injectors into /tmp (inside the container).
set -e
cd "$(dirname "$0")"
gcc -O1 -Wall -o /tmp/eiinject eiinject.c $(pkg-config --cflags --libs libei-1.0)
x=/usr/share/wlr-protocols/unstable/wlr-virtual-pointer-unstable-v1.xml
wayland-scanner client-header $x /tmp/wlr-virtual-pointer-unstable-v1-client-protocol.h
wayland-scanner private-code $x /tmp/wlr-virtual-pointer-unstable-v1-protocol.c
gcc -O1 -Wall -I/tmp -o /tmp/vpointer vpointer.c /tmp/wlr-virtual-pointer-unstable-v1-protocol.c $(pkg-config --cflags --libs wayland-client)
