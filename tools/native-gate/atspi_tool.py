#!/usr/bin/env python3
"""AT-SPI views of HikariSub for the D1 native gate.

  atspi_tool.py dump [--app hikarisub] [--depth N]     the accessible tree
  atspi_tool.py focus                                  the focused object and its path
  atspi_tool.py windows                                top-level frames/windows with states
  atspi_tool.py find ROLE NAME                         objects with that role and name
  atspi_tool.py do ROLE NAME [ACTION]                  run an action (default the first)
  atspi_tool.py listen SECONDS                         focus and window events, one per line
  atspi_tool.py text ROLE NAME                         the text of that object (Text interface)
  atspi_tool.py json                                   frames, panels, focus and texts as JSON
  atspi_tool.py press-showing NAME                     press every showing button with that name
  atspi_tool.py fsjson                                 V5: each showing frame, whether it holds the
                                                       full screen video, its buttons, times and focus

Every line names the role, the accessible name, the states that matter for
the gate (focused, active, showing, visible) and the screen extents.
"""
import sys
import time
import warnings

warnings.filterwarnings("ignore", category=DeprecationWarning)

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib  # noqa: E402

INTERESTING = [
    Atspi.StateType.FOCUSED,
    Atspi.StateType.ACTIVE,
    Atspi.StateType.SHOWING,
    Atspi.StateType.VISIBLE,
    Atspi.StateType.SELECTED,
    Atspi.StateType.FOCUSABLE,
]


def app(name="hikarisub"):
    desktop = Atspi.get_desktop(0)
    for i in range(desktop.get_child_count()):
        a = desktop.get_child_at_index(i)
        if a and (a.get_name() or "").lower().startswith(name.lower()):
            return a
    return None


def states(o):
    try:
        s = o.get_state_set()
    except GLib.Error:
        return "?"
    return ",".join(t.value_nick for t in INTERESTING if s.contains(t))


def extents(o):
    try:
        e = o.get_extents(Atspi.CoordType.SCREEN)
        return f"{e.x},{e.y} {e.width}x{e.height}"
    except Exception:
        return "-"


def describe(o):
    try:
        role = o.get_role_name()
        name = o.get_name()
    except GLib.Error as e:
        return f"<gone: {e.message}>"
    desc = ""
    try:
        desc = o.get_description()
    except GLib.Error:
        pass
    actions = ""
    try:
        act = o.get_action_iface()
        if act:
            actions = " actions=" + "|".join(act.get_action_name(i) for i in range(act.get_n_actions()))
    except Exception:
        pass
    return f"[{role}] {name!r}" + (f" desc={desc!r}" if desc else "") + f" <{states(o)}> @{extents(o)}{actions}"


def frame_id(name):
    """The main window is titled "<document> - HikariSub" once a Document is open."""
    return "HikariSub" if name == "HikariSub" or name.endswith(" - HikariSub") else name


def panel_id(name):
    """Panels are named after their role, followed by ": <document>" while one is
    open; the Grid's panel carries the editing-target label instead."""
    if name.startswith("Editing:") or name == "No document open":
        return "Grid"
    return name.split(":")[0]


def text_of(o):
    try:
        return Atspi.Text.get_text(o, 0, Atspi.Text.get_character_count(o))
    except Exception:
        return None


def walk(o, depth=0, limit=40, out=None):
    out = out if out is not None else []
    out.append("  " * depth + describe(o))
    if depth >= limit:
        return out
    try:
        n = o.get_child_count()
    except GLib.Error:
        return out
    for i in range(n):
        try:
            c = o.get_child_at_index(i)
        except GLib.Error:
            continue
        if c is not None:
            walk(c, depth + 1, limit, out)
    return out


def all_objects(o, acc=None):
    acc = acc if acc is not None else []
    acc.append(o)
    try:
        for i in range(o.get_child_count()):
            c = o.get_child_at_index(i)
            if c is not None:
                all_objects(c, acc)
    except GLib.Error:
        pass
    return acc


def path_of(o):
    parts = []
    while o is not None:
        try:
            parts.append(f"{o.get_role_name()}:{o.get_name()!r}")
            o = o.get_parent()
        except GLib.Error:
            break
        if parts and parts[-1].startswith("desktop frame"):
            break
    return " > ".join(reversed(parts))


def focused(a):
    for o in all_objects(a):
        try:
            if o.get_state_set().contains(Atspi.StateType.FOCUSED):
                return o
        except GLib.Error:
            pass
    return None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    if cmd == "listen":
        seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 10
        t0 = time.monotonic()

        def on(ev):
            src = ev.source
            print(f"{time.monotonic() - t0:7.3f} {ev.type} detail={ev.detail1} {describe(src)}", flush=True)

        listener = Atspi.EventListener.new(on)
        for t in ("object:state-changed:focused", "focus:", "window:activate", "window:deactivate",
                  "window:create", "window:destroy", "object:state-changed:active", "object:children-changed"):
            listener.register(t)
        loop = GLib.MainLoop()
        GLib.timeout_add(int(seconds * 1000), loop.quit)
        loop.run()
        return 0
    a = app()
    if a is None:
        print("hikarisub is not on the accessibility bus")
        return 1
    if cmd == "dump":
        depth = int(sys.argv[2]) if len(sys.argv) > 2 else 40
        print("\n".join(walk(a, limit=depth)))
    elif cmd == "windows":
        for i in range(a.get_child_count()):
            print(describe(a.get_child_at_index(i)))
    elif cmd == "focus":
        f = focused(a)
        print(describe(f) if f else "no focused object")
        if f:
            print("path:", path_of(f))
    elif cmd == "text":
        role, name = sys.argv[2], sys.argv[3]
        for o in all_objects(a):
            if o.get_role_name() == role and (o.get_name() or "") == name:
                t = text_of(o)
                print(t if t is not None else "<no text interface>")
                break
    elif cmd == "json":
        import json
        out = {"frames": [], "panels": [], "focus": None, "focusPath": None, "texts": {}, "labels": []}
        for i in range(a.get_child_count()):
            f = a.get_child_at_index(i)
            st = f.get_state_set()
            if not st.contains(Atspi.StateType.SHOWING):
                continue
            e = f.get_extents(Atspi.CoordType.SCREEN)
            out["frames"].append({"name": f.get_name(), "id": frame_id(f.get_name()),
                                  "active": st.contains(Atspi.StateType.ACTIVE),
                                  "x": e.x, "y": e.y, "w": e.width, "h": e.height})
            for o in all_objects(f):
                try:
                    role = o.get_role_name()
                    ost = o.get_state_set()
                except GLib.Error:
                    continue
                if not ost.contains(Atspi.StateType.SHOWING):
                    continue
                if role == "panel" and o.get_name():
                    pe = o.get_extents(Atspi.CoordType.WINDOW)
                    out["panels"].append({"name": o.get_name(), "id": panel_id(o.get_name()),
                                          "frame": frame_id(f.get_name()),
                                          "x": pe.x, "y": pe.y, "w": pe.width, "h": pe.height})
                if role == "text" and o.get_name() in ("Line text", "Start", "End"):
                    out["texts"][o.get_name()] = text_of(o)
                if role == "label" and o.get_name() and (o.get_name().startswith("Editing") or
                                                          o.get_name().startswith("No editing") or
                                                          o.get_name() == "Video times"):
                    out["labels"].append(text_of(o) or o.get_name())
        fo = focused(a)
        if fo:
            out["focus"] = describe(fo)
            out["focusPath"] = path_of(fo)
        print(json.dumps(out, indent=1))
    elif cmd == "fsjson":
        # V5 (#184): the fullscreen video window carries the main window's
        # title, so frames are told apart by the pane named "Full screen video".
        import json
        out = {"frames": [], "focusPath": None}
        for i in range(a.get_child_count()):
            f = a.get_child_at_index(i)
            st = f.get_state_set()
            if not st.contains(Atspi.StateType.SHOWING):
                continue
            e = f.get_extents(Atspi.CoordType.SCREEN)
            fr = {"name": f.get_name(), "active": st.contains(Atspi.StateType.ACTIVE),
                  "x": e.x, "y": e.y, "w": e.width, "h": e.height, "fullscreen": False,
                  "buttons": [], "times": None, "position": None, "panels": []}
            for o in all_objects(f):
                try:
                    role = o.get_role_name()
                    ost = o.get_state_set()
                    name = o.get_name() or ""
                except GLib.Error:
                    continue
                if not ost.contains(Atspi.StateType.SHOWING):
                    continue
                if role == "panel" and name == "Full screen video":
                    fr["fullscreen"] = True
                elif role == "panel" and name:
                    fr["panels"].append(panel_id(name))
                elif role in ("button", "push button", "toggle button", "check box") and name:
                    fr["buttons"].append(name)
                elif role == "slider" and name == "Video position":
                    try:
                        fr["position"] = Atspi.Value.get_current_value(o)
                    except Exception:
                        pass
                elif role == "label" and name == "Video times":
                    fr["times"] = text_of(o) or name
            out["frames"].append(fr)
        fo = focused(a)
        if fo:
            out["focusPath"] = path_of(fo)
        print(json.dumps(out, indent=1))
    elif cmd == "press-showing":
        for o in all_objects(a):
            try:
                if o.get_role_name() == "button" and o.get_name() == sys.argv[2] and \
                        o.get_state_set().contains(Atspi.StateType.SHOWING):
                    print(describe(o), "->", o.get_action_iface().do_action(0))
            except GLib.Error:
                pass
    elif cmd in ("find", "do"):
        role, name = sys.argv[2], sys.argv[3]
        hits = [o for o in all_objects(a) if o.get_role_name() == role and (o.get_name() or "") == name]
        for o in hits:
            print(describe(o), "| path:", path_of(o))
        if cmd == "do":
            if not hits:
                return 1
            act = hits[0].get_action_iface()
            wanted = sys.argv[4] if len(sys.argv) > 4 else None
            idx = 0
            if wanted:
                names = [act.get_action_name(i) for i in range(act.get_n_actions())]
                idx = names.index(wanted)
            print("action", act.get_action_name(idx), "->", act.do_action(idx))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
