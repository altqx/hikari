TEST_RUNS = [
    ("hikari_ui_shell_tests", [
        "f6AndShortcutsReachAFloatingPanel", "panelsFloatDockHideAndKeepTheirState",
        "placementWindowMovesTabsAndResizes", "floatF6AndShowActivateTheFloatingPanelsWindow",
        "placementWindowShowsItsDefaultsAndKeyboardChanges", "fileDropAreaLeavesPanelDragsToTheDockingEngine",
        "dockingControlsAndTheGridAreAccessible", "floatingPanelsOffEveryScreenComeBack",
        "panelMenuMovesTabsWithinTheirGroup",
        "panelMenuHoldsMoveUndockAndClose", "keyboardReachesThePanelHeaderAndItsMenu",
        "doubleClickOnAHeaderFloatsAndDocks", "bottomPanelsKeepATabAndTheTrayToolbar",
        "panelsKeepTheirMinimumSizes"]),
    ("hikari_ui_docking_qualification_tests", []),
    ("hikari_ui_workspace_layout_tests", []),
    ("hikari_ui_tab_layouts_tests", []),
]

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


FAILING_TEST_SKIPS = ("(no window manager)",)


def unexpected_skips(skips, known=KNOWN_TEST_SKIPS):
    return [s for s in skips if not any(reason in s for reason in known)]


# openbox lagged one 44-pixel step behind the last motion of a window-system move.
MAX_DRAG_STEP = 8


def drag_steps(dx, dy, minimum=30, max_step=MAX_DRAG_STEP):
    longest = max(abs(dx), abs(dy))
    return max(minimum, -(-longest // max_step))


def drag_points(sx, sy, ex, ey, steps):
    return [(sx + (ex - sx) * i // steps, sy + (ey - sy) * i // steps) for i in range(1, steps + 1)]


def find_frame(raw, iw, ih, fw, fh, scale=1, x_from=0):
    """Top-left (x, y) of a floating panel's FW x FH frame in RGB screenshot RAW (IW x IH), or None."""

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


# videofs leaves the window on sway's scale-2 output; after outputs sway's virtual pointer maps over the old layout.
LAST_STEPS = ("videofs", "outputs")


def default_order(steps, sweep_steps):
    return ([n for n in steps if n not in LAST_STEPS] + [n for n in sweep_steps if n not in LAST_STEPS]
            + [n for n in LAST_STEPS if n in steps or n in sweep_steps])


def hikarisub_application(toolkits):
    """Index of Qt's HikariSub on the bus: on GNOME, GTK's ATK bridge registers the process a second time."""
    if "Qt" in toolkits:
        return toolkits.index("Qt")
    return 0 if toolkits else None
