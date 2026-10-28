# gdb script (gdb -x trace_drop.py --args hikarisub): during a drag, logs what
# KDDockWidgets' DragController finds under the cursor: the top-level view
# (qtTopLevelUnderCursor, a std::shared_ptr returned through memory) and the
# drop area (dropAreaUnderCursor, a pointer). Release build, no debug info:
# values are read from registers at the callers' return.
import gdb

gdb.execute("set pagination off")
gdb.execute("handle SIGPIPE nostop noprint")
counts = {"top": 0, "area": 0}


class Ret(gdb.FinishBreakpoint):
    def __init__(self, frame, kind):
        super().__init__(frame, internal=True)
        self.kind = kind

    def stop(self):
        rax = int(gdb.parse_and_eval("$rax"))
        if self.kind == "top":
            val = int(gdb.parse_and_eval(f"*(void**){rax}")) if rax else 0
        else:
            val = rax
        counts[self.kind] += 1
        if counts[self.kind] <= 40:
            print(f"TRACE {self.kind} -> {val:#x}", flush=True)
        return False

    def out_of_scope(self):
        pass


class Entry(gdb.Breakpoint):
    def __init__(self, spec, kind):
        super().__init__(spec, internal=True)
        self.kind = kind

    def stop(self):
        Ret(gdb.newest_frame(), self.kind)
        return False


Entry("KDDockWidgets::Core::DragController::qtTopLevelUnderCursor", "top")
Entry("KDDockWidgets::Core::DragController::dropAreaUnderCursor", "area")
gdb.execute("run")
# Stopped by SIGINT (kill -INT): optionally hide the shell's file DropArea
# (objectName "dropArea") first, then let the app run on.
import os
if os.environ.get("HIDE_DROPAREA") == "1":
    w = int(gdb.parse_and_eval("((void*(*)(void))_ZN15QGuiApplication11focusWindowEv)()"))
    d = int(gdb.parse_and_eval(
        "((void*(*)(void*,const char*,unsigned long,void*,int))"
        "_Z20qt_qFindChild_helperPK7QObject14QAnyStringViewRK11QMetaObject6QFlagsIN2Qt15FindChildOptionEE)"
        f"({w}, \"dropArea\", 8, &_ZN7QObject16staticMetaObjectE, 1)"))
    print(f"TRACE hide dropArea {d:#x} in window {w:#x}", flush=True)
    if d:
        gdb.parse_and_eval(f"((void(*)(void*,int))_ZN10QQuickItem10setVisibleEb)({d}, 0)")
gdb.execute("continue")
