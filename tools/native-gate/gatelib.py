"""The parts of gate.py that need no session: finding a floating frame in a
screenshot, the steps of a pointer drag, the test skips the gate accepts and
the order of the default run. test_gatelib.py tests them."""

# The UI test executables step "tests" runs, with the test functions it
# runs of each (none: all of them).
TEST_RUNS = [
    ("hikari_ui_shell_tests", [
        "f6AndShortcutsReachAFloatingPanel", "panelsFloatDockHideAndKeepTheirState",
        "placementWindowMovesTabsAndResizes", "floatF6AndShowActivateTheFloatingPanelsWindow",
        "placementWindowShowsItsDefaultsAndKeyboardChanges", "fileDropAreaLeavesPanelDragsToTheDockingEngine",
        "dockingControlsAndTheGridAreAccessible", "floatingPanelsOffEveryScreenComeBack",
        "panelMenuMovesTabsWithinTheirGroup",
        # D3
        "panelMenuHoldsMoveUndockAndClose", "keyboardReachesThePanelHeaderAndItsMenu",
        "doubleClickOnAHeaderFloatsAndDocks", "bottomPanelsKeepATabAndTheTrayToolbar",
        "panelsKeepTheirMinimumSizes"]),
    ("hikari_ui_docking_qualification_tests", []),
    ("hikari_ui_workspace_layout_tests", []),
    ("hikari_ui_tab_layouts_tests", []),  # WS1
]

# Skips the tests declare as platform limits, each observed for real by
# another gate item: Wayland compositors place windows (outputs); a
# synthetic drag between windows needs the offscreen platform (pointer); a
# window manager's focus-stealing policy (the F6 items; on sway, the
# sway-activate pass runs these tests again). Since D3's split header and
# the scale settle: a live scale change (outputs' mixed DPI), a declined
# system move (floating's moves) and the X11-only engine move (the x11
# session's floating-move). Since the D3 screen-loss fix: a screen removed
# with a floating panel on it (outputs' monitor removal).
KNOWN_TEST_SKIPS = (
    "the compositor places windows",
    "a synthetic drag between windows needs the offscreen platform",
    "refused the shell's activation request",
    "did not activate the main window",
    "did not activate the floating panel's window",
    "did not give the new floating window the focus",
    "the screen's scale changes through the offscreen platform's configuration",
    "the window follows a declined system move only where the platform declines it",
    "X11 only (the .xvfb run)",
    "screens come and go through the offscreen platform's configuration",
)


# Skips a gate session must not see: every session runs a window manager.
FAILING_TEST_SKIPS = ("(no window manager)",)


def unexpected_skips(skips, known=KNOWN_TEST_SKIPS):
    """The SKIP lines that carry none of the known reasons."""
    return [s for s in skips if not any(reason in s for reason in known)]


# A window-system move follows the pointer in steps of at most this many
# pixels: openbox lagged one 44-pixel step behind the last motion.
MAX_DRAG_STEP = 8


def drag_steps(dx, dy, minimum=30, max_step=MAX_DRAG_STEP):
    """How many motions a drag by (dx, dy) takes so that none is longer than
    MAX_STEP pixels on either axis (at least MINIMUM)."""
    longest = max(abs(dx), abs(dy))
    return max(minimum, -(-longest // max_step))


def drag_points(sx, sy, ex, ey, steps):
    """The pointer positions after each of STEPS motions from (sx, sy) to
    (ex, ey)."""
    return [(sx + (ex - sx) * i // steps, sy + (ey - sy) * i // steps) for i in range(1, steps + 1)]


def find_frame(raw, iw, ih, fw, fh, scale=1, x_from=0):
    """The top-left corner (x, y) of a floating panel's frame in an RGB
    screenshot RAW of IW x IH pixels, or None. The frame is FW x FH image
    pixels (the window less its shadow, at SCALE): its top edge is a run of
    one colour as wide as the frame, less up to 10 SCALE pixels of rounded
    corners, from image column X_FROM on. It is a line SCALE pixels thick
    (a stretch of flat background is not), its left side has that colour
    down the frame, and its bottom edge lies where FH puts it (a box of
    another height, such as the Line editor's text box, has the same top
    run but no bottom there)."""

    def px(x, y):
        i = (y * iw + x) * 3
        return raw[i:i + 3]

    for y in range(ih - fh + 1):
        x = x_from
        while x < iw:
            c = px(x, y)
            e = x + 1
            while e < iw and px(e, y) == c:
                e += 1
            run = e - x
            if fw - 10 * scale <= run <= fw - 2:
                left = x - (fw - run) // 2
                mid = (x + e) // 2
                ok = 0 < y < ih - scale and px(mid, y - 1) != c and px(mid, y + scale) != c
                ok = ok and 0 <= left < iw
                for k in range(1, 9):
                    yy = y + k * fh // 9
                    if not ok or yy >= ih:
                        ok = False
                        break
                    ok = px(left, yy) == c
                by = y + fh - 1
                if ok and by < ih:
                    ok = all(px(left + k * (fw - 1) // 8, by) == c
                             for k in range(1, 8) if left + k * (fw - 1) // 8 < iw)
                if ok:
                    return left, y
            x = e
    return None


# The steps that must run after every other one of the default run:
# "videofs" leaves the main window on sway's scale-2 output, half the size,
# where the main window overflows it; "outputs" removes an output, after
# which sway's virtual pointer still maps absolute positions over the old
# layout.
LAST_STEPS = ("videofs", "outputs")


def default_order(steps, sweep_steps):
    """The default run: every step, the sweep's included, LAST_STEPS last."""
    return ([n for n in steps if n not in LAST_STEPS] + [n for n in sweep_steps if n not in LAST_STEPS]
            + [n for n in LAST_STEPS if n in steps or n in sweep_steps])
