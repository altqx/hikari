// Real pointer input for wlroots compositors (sway) through
// zwlr_virtual_pointer_v1: the compositor routes these events like a
// physical mouse's, so window moves, drags and drops go through its own
// pointer handling. Commands on stdin, one per line:
//   move X Y      absolute position in layout coordinates (needs `extent W H` first)
//   extent W H    the layout's bounding box (default 3200x1000)
//   rel DX DY     relative motion
//   down / up     left button; rdown / rup right button
//   wait MS
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>

static struct wl_seat *seat;
static struct zwlr_virtual_pointer_manager_v1 *manager;

static void global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version)
{
    (void)data;
    if (!strcmp(iface, wl_seat_interface.name) && !seat)
        seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
    else if (!strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name))
        manager = wl_registry_bind(r, name, &zwlr_virtual_pointer_manager_v1_interface, version < 2 ? version : 2);
}
static void global_remove(void *data, struct wl_registry *r, uint32_t name) { (void)data; (void)r; (void)name; }
static const struct wl_registry_listener listener = {global, global_remove};

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "no wayland display\n");
        return 2;
    }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &listener, NULL);
    wl_display_roundtrip(display);
    if (!manager) {
        fprintf(stderr, "no zwlr_virtual_pointer_manager_v1\n");
        return 2;
    }
    struct zwlr_virtual_pointer_v1 *p = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager, seat);
    wl_display_roundtrip(display);
    fprintf(stderr, "ready\n");
    unsigned w = 3200, h = 1000;
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        double x, y;
        int ms;
        if (sscanf(line, "extent %u %u", &w, &h) == 2) {
        } else if (sscanf(line, "move %lf %lf", &x, &y) == 2) {
            zwlr_virtual_pointer_v1_motion_absolute(p, now_ms(), (uint32_t)x, (uint32_t)y, w, h);
            zwlr_virtual_pointer_v1_frame(p);
        } else if (sscanf(line, "rel %lf %lf", &x, &y) == 2) {
            zwlr_virtual_pointer_v1_motion(p, now_ms(), wl_fixed_from_double(x), wl_fixed_from_double(y));
            zwlr_virtual_pointer_v1_frame(p);
        } else if (!strncmp(line, "down", 4) || !strncmp(line, "up", 2)) {
            zwlr_virtual_pointer_v1_button(p, now_ms(), BTN_LEFT,
                                           line[0] == 'd' ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
            zwlr_virtual_pointer_v1_frame(p);
        } else if (!strncmp(line, "rdown", 5) || !strncmp(line, "rup", 3)) {
            zwlr_virtual_pointer_v1_button(p, now_ms(), BTN_RIGHT,
                                           line[1] == 'd' ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
            zwlr_virtual_pointer_v1_frame(p);
        } else if (sscanf(line, "wait %d", &ms) == 1) {
            wl_display_flush(display);
            struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
            nanosleep(&ts, NULL);
        } else {
            fprintf(stderr, "ignored: %s", line);
        }
        wl_display_roundtrip(display);
    }
    zwlr_virtual_pointer_v1_destroy(p);
    wl_display_roundtrip(display);
    return 0;
}
