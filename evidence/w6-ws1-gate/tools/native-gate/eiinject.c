// Real compositor input for mutter and KWin through libei (the protocol both
// compositors expose for remote input). The EIS connection is handed in as
// an inherited file descriptor (eiinject <fd>); commands come on stdin, one
// per line:
//   move X Y        absolute pointer motion, compositor logical coordinates
//   rel DX DY       relative pointer motion
//   down / up       left button press / release (right: rdown / rup)
//   key CODE        press and release an evdev key code (linux/input-event-codes.h)
//   keydown CODE / keyup CODE
//   wait MS
//   regions         print the absolute device's regions
// It prints "ready" on stderr once the devices resumed.
#include <libei.h>

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111

static struct ei *ei;
static struct ei_device *absdev, *reldev, *kbddev;
static uint32_t seq = 1;

static void handle_events(int timeout_ms)
{
    struct pollfd p = {.fd = ei_get_fd(ei), .events = POLLIN};
    if (poll(&p, 1, timeout_ms) > 0)
        ei_dispatch(ei);
    struct ei_event *e;
    while ((e = ei_get_event(ei))) {
        struct ei_device *d = ei_event_get_device(e);
        switch (ei_event_get_type(e)) {
        case EI_EVENT_SEAT_ADDED:
            ei_seat_bind_capabilities(ei_event_get_seat(e), EI_DEVICE_CAP_POINTER, EI_DEVICE_CAP_POINTER_ABSOLUTE,
                                      EI_DEVICE_CAP_BUTTON, EI_DEVICE_CAP_KEYBOARD, EI_DEVICE_CAP_SCROLL, NULL);
            break;
        case EI_EVENT_DEVICE_ADDED:
            fprintf(stderr, "device %s:%s%s%s%s\n", ei_device_get_name(d),
                    ei_device_has_capability(d, EI_DEVICE_CAP_POINTER_ABSOLUTE) ? " abs" : "",
                    ei_device_has_capability(d, EI_DEVICE_CAP_POINTER) ? " rel" : "",
                    ei_device_has_capability(d, EI_DEVICE_CAP_BUTTON) ? " button" : "",
                    ei_device_has_capability(d, EI_DEVICE_CAP_KEYBOARD) ? " keyboard" : "");
            // A compositor may replace a device (mutter does when its monitors
            // change): the newest one is used.
            if (ei_device_has_capability(d, EI_DEVICE_CAP_POINTER_ABSOLUTE))
                absdev = ei_device_ref(d);
            if (ei_device_has_capability(d, EI_DEVICE_CAP_POINTER))
                reldev = ei_device_ref(d);
            if (ei_device_has_capability(d, EI_DEVICE_CAP_KEYBOARD))
                kbddev = ei_device_ref(d);
            break;
        case EI_EVENT_DEVICE_REMOVED:
            fprintf(stderr, "removed %s\n", ei_device_get_name(d));
            if (d == absdev)
                absdev = NULL;
            if (d == reldev)
                reldev = NULL;
            if (d == kbddev)
                kbddev = NULL;
            break;
        case EI_EVENT_DEVICE_RESUMED:
            ei_device_start_emulating(d, seq++);
            fprintf(stderr, "resumed %s\n", ei_device_get_name(d));
            break;
        case EI_EVENT_DISCONNECT:
            fprintf(stderr, "disconnected\n");
            exit(3);
        default:
            break;
        }
        ei_event_unref(e);
    }
}

static struct ei_device *buttondev(void)
{
    if (absdev && ei_device_has_capability(absdev, EI_DEVICE_CAP_BUTTON))
        return absdev;
    return reldev;
}

static void frame(struct ei_device *d)
{
    ei_device_frame(d, ei_now(ei));
    handle_events(0);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: eiinject <fd>\n");
        return 2;
    }
    ei = ei_new_sender(NULL);
    ei_configure_name(ei, "hikari-native-gate");
    int rc = ei_setup_backend_fd(ei, atoi(argv[1]));
    if (rc != 0) {
        fprintf(stderr, "ei_setup_backend_fd: %s\n", strerror(-rc));
        return 2;
    }
    for (int i = 0; i < 100 && !((absdev || reldev) && kbddev); ++i)
        handle_events(50);
    for (int i = 0; i < 10; ++i)
        handle_events(30);
    fprintf(stderr, "ready abs=%d rel=%d kbd=%d\n", absdev != NULL, reldev != NULL, kbddev != NULL);

    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        double x, y;
        int code;
        handle_events(0);
        if (!strncmp(line, "down", 4) || !strncmp(line, "up", 2) || !strncmp(line, "rdown", 5) ||
            !strncmp(line, "rup", 3)) {
            if (!buttondev()) {
                fprintf(stderr, "no button device\n");
                continue;
            }
        }
        if (sscanf(line, "move %lf %lf", &x, &y) == 2 && absdev) {
            ei_device_pointer_motion_absolute(absdev, x, y);
            frame(absdev);
        } else if (sscanf(line, "rel %lf %lf", &x, &y) == 2 && reldev) {
            ei_device_pointer_motion(reldev, x, y);
            frame(reldev);
        } else if (strncmp(line, "down", 4) == 0 || strncmp(line, "up", 2) == 0) {
            struct ei_device *d = buttondev();
            ei_device_button_button(d, BTN_LEFT, line[0] == 'd');
            frame(d);
        } else if (strncmp(line, "rdown", 5) == 0 || strncmp(line, "rup", 3) == 0) {
            struct ei_device *d = buttondev();
            ei_device_button_button(d, BTN_RIGHT, line[1] == 'd');
            frame(d);
        } else if (sscanf(line, "keydown %d", &code) == 1 && kbddev) {
            ei_device_keyboard_key(kbddev, code, true);
            frame(kbddev);
        } else if (sscanf(line, "keyup %d", &code) == 1 && kbddev) {
            ei_device_keyboard_key(kbddev, code, false);
            frame(kbddev);
        } else if (sscanf(line, "key %d", &code) == 1 && kbddev) {
            ei_device_keyboard_key(kbddev, code, true);
            frame(kbddev);
            ei_device_keyboard_key(kbddev, code, false);
            frame(kbddev);
        } else if (sscanf(line, "wait %d", &code) == 1) {
            struct timespec ts = {code / 1000, (code % 1000) * 1000000L};
            nanosleep(&ts, NULL);
        } else if (strncmp(line, "regions", 7) == 0 && absdev) {
            struct ei_region *r;
            for (size_t i = 0; (r = ei_device_get_region(absdev, i)); ++i)
                fprintf(stderr, "region %u,%u %ux%u scale %.2f\n", ei_region_get_x(r), ei_region_get_y(r),
                        ei_region_get_width(r), ei_region_get_height(r), ei_region_get_physical_scale(r));
        } else {
            fprintf(stderr, "ignored: %s", line);
        }
        handle_events(10);
    }
    for (int i = 0; i < 5; ++i)
        handle_events(20);
    ei_unref(ei);
    return 0;
}
