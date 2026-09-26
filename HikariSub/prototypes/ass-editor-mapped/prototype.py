"""Throwaway native editable projection. Raw ASS is authoritative; all state is memory-only."""
import argparse
import json
import os
from pathlib import Path
import re
import sys

from PySide6.QtCore import QObject, Property, QTimer, QUrl, Signal, Slot, QCoreApplication, QMetaObject, Qt, QEvent, Q_ARG
from PySide6.QtGui import QColor, QFont, QFontDatabase, QGuiApplication, QInputMethodEvent, QSyntaxHighlighter, QTextCharFormat, QTextCursor
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuickControls2 import QQuickStyle
from mapping import cp_index, mapped_replace, project, slice16, u16
from fixtures import run_fixtures

SAMPLES = [
    ("Boundary / spelling", r"A {\i1}teh subtitle{\i0} ends.\NNext line"),
    ("Karaoke", r"{\k20}Ka{\kf30}ra{\ko15}oke 🎵"),
    ("Escapes + Unicode", "{\\b1}A😀e\u0301 👩🏽‍💻{\\b0}\\hHard\\nsoft\\Nbreak"),
    ("Mixed RTL", r"{\an7}مرحبا بالعالم — Hello {\\i1}שלום{\\i0} 123"),
    ("CJK / IME", r"{\an2}日本語 / 中文 / 한국어: "),
    ("Drawing / malformed", r"{\p1}m 0 0 l 20 30{\p0}Text {\i1"),
]
SAMPLES[3] = (SAMPLES[3][0], SAMPLES[3][1].replace("\\\\", "\\"))

def document_text(doc):
    # toPlainText normalizes NBSP to a space; keep it distinct for ASS \h.
    return doc.toRawText().replace("\u2029", "\n")

def replace_document(doc, value):
    """Minimal counterpart patch; never reset the active TextArea per keystroke."""
    old = document_text(doc)
    if old == value:
        return
    prefix = 0
    while prefix < min(len(old), len(value)) and old[prefix] == value[prefix]:
        prefix += 1
    suffix = 0
    while suffix < len(old)-prefix and suffix < len(value)-prefix and old[-1-suffix] == value[-1-suffix]:
        suffix += 1
    cursor = QTextCursor(doc)
    cursor.beginEditBlock()
    cursor.setPosition(u16(old[:prefix]))
    cursor.setPosition(u16(old[:len(old)-suffix]), QTextCursor.KeepAnchor)
    cursor.insertText(value[prefix:len(value)-suffix if suffix else len(value)])
    cursor.endEditBlock()

class Highlighter(QSyntaxHighlighter):
    def __init__(self, document, raw):
        super().__init__(document)
        self.raw = raw

    def highlightBlock(self, text):
        if self.raw:
            for match in re.finditer(r"\{[^}]*\}?|\\[Nnh]", text):
                style = QTextCharFormat()
                style.setForeground(QColor("#b9a4eb"))
                self.setFormat(u16(text[:match.start()]), u16(match.group()), style)
        for match in re.finditer(r"\bteh\b", text):
            style = QTextCharFormat()
            style.setForeground(QColor("#ffaaaa"))
            style.setUnderlineColor(QColor("#ffaaaa"))
            style.setUnderlineStyle(QTextCharFormat.SingleUnderline)
            self.setFormat(u16(text[:match.start()]), u16(match.group()), style)

class Bridge(QObject):
    changed = Signal()

    def __init__(self, sample=0, affinity="after", crossing="retain"):
        super().__init__()
        self.raw = SAMPLES[sample][1]
        self.affinity, self.crossing = affinity, crossing
        self.lines = [self.raw, r"{\an2}Second synthetic Line. Enter commits and advances."]
        self.line_index = 0
        self.undo_stack, self.redo_stack, self.commits = [], [], []
        self.docs, self.quick_documents, self.items, self.highlighters = {}, [], {}, []
        self.guard = False
        self.composing = False
        self.preedit = ""
        self.pending = False
        self.rejected = ""
        self.last_delta = {}
        self.notice = "Editable hidden-tag view; choose the tag-boundary policies for review."
        self.revision = 0
        self.input_hint = None
        self.ime_selection = None
        self.canceling_view = None

    @Property("QVariantList", constant=True)
    def samples(self):
        return [sample[0] for sample in SAMPLES]

    @Property("QVariantMap", notify=changed)
    def state(self):
        p = project(self.raw)
        return {
            "raw": self.raw, "clean": p.text, "map": p.records(), "mapJson": json.dumps(p.records(), ensure_ascii=False, indent=2),
            "notice": self.notice, "warnings": " · ".join(p.warnings), "affinity": self.affinity, "crossing": self.crossing,
            "revision": self.revision, "undo": len(self.undo_stack), "redo": len(self.redo_stack),
            "commits": len(self.commits), "line": self.line_index+1, "lineCount": len(self.lines),
            "composing": self.composing, "preedit": self.preedit, "pending": self.pending, "rejected": self.rejected,
            "compositionSelection": self.ime_selection,
            "rawUtf16": u16(self.raw), "cleanUtf16": u16(p.text), "lastDelta": self.last_delta,
            "summary": json.dumps({"lastDelta": self.last_delta, "lines": self.lines, "draftRaw": self.raw,
                                  "undo": self.undo_stack, "redo": self.redo_stack, "commits": self.commits}, ensure_ascii=False, indent=2)
        }

    def message(self, text):
        self.notice = text
        self.changed.emit()

    @Slot(QObject, QObject, QObject, QObject)
    def attach(self, raw_quick, clean_quick, raw_item, clean_item):
        self.quick_documents = [raw_quick, clean_quick]  # retain wrapper ownership
        self.docs = {"raw": raw_quick.textDocument(), "clean": clean_quick.textDocument()}
        self.items = {"raw": raw_item, "clean": clean_item}
        clean_item.installEventFilter(self)
        raw_item.installEventFilter(self)
        self.sync()
        for name, doc in self.docs.items():
            # This probe owns raw-snapshot undo to avoid independent histories drifting.
            doc.setUndoRedoEnabled(False)
            doc.contentsChange.connect(lambda pos, removed, added, name=name: self.native_change(name, pos, removed, added))
            self.highlighters.append(Highlighter(doc, name=="raw"))
        self.message("Native text documents attached. Probe undo owns raw snapshots; native IME owns preedit.")

    def eventFilter(self, watched, event):
        # Observe the native event's exact replacement range; never consume or synthesize preedit.
        if watched in self.items.values() and event.type() == QEvent.InputMethod:
            view = "clean" if watched is self.items.get("clean") else "raw"
            self.input_hint = None
            start = int(watched.property("selectionStart"))
            end = int(watched.property("selectionEnd"))
            if event.preeditString() and not event.commitString() and not event.replacementLength():
                if start != end and self.ime_selection is None:
                    self.ime_selection = {"start": start, "end": end, "raw": self.raw, "view": view}
            elif not event.preeditString() and not event.commitString() and not event.replacementLength() and self.ime_selection:
                # Qt removes a selected range at preedit start. Cancel must restore it.
                selected = self.ime_selection
                self.ime_selection = None
                self.canceling_view = view
                def restore_cancelled_selection():
                    self.sync()
                    self.canceling_view = None
                    QMetaObject.invokeMethod(watched, "select", Qt.DirectConnection, Q_ARG(int, selected["start"]), Q_ARG(int, selected["end"]))
                    self.message("Cancelled native composition restored its selected source unchanged.")
                QTimer.singleShot(0, restore_cancelled_selection)
            if event.commitString() or event.replacementLength():
                cursor = int(watched.property("cursorPosition"))
                if self.ime_selection:
                    if event.replacementStart() or event.replacementLength():
                        start = end = -1  # combined selection + surrounding replacement is unqualified
                    else:
                        start, end = self.ime_selection["start"], self.ime_selection["end"]
                elif start == end:
                    start = cursor + event.replacementStart()
                    end = start + event.replacementLength()
                elif event.replacementStart() or event.replacementLength():
                    # Combined selection + surrounding-text replacement needs its own fixture.
                    start = end = -1
                self.input_hint = {"start": start, "end": end, "text": event.commitString(), "view": view}
                captured_hint = self.input_hint
                def clear_hint():
                    if self.input_hint is captured_hint:
                        self.input_hint = None
                QTimer.singleShot(0, clear_hint)
        return False

    def sync(self):
        if not self.docs:
            return
        self.guard = True
        try:
            replace_document(self.docs["raw"], self.raw)
            replace_document(self.docs["clean"], project(self.raw).text)
        finally:
            self.guard = False

    def adopt(self, value, origin):
        if value == self.raw:
            return
        self.undo_stack.append({"before": self.raw, "after": value, "origin": origin})
        self.redo_stack.clear()
        self.raw = value
        self.revision += 1
        self.sync()
        self.message("Accepted "+origin+" edit; original hidden tokens retained for mapped edits.")

    def native_change(self, name, position, removed, added):
        if self.guard or self.canceling_view == name:
            return
        current = document_text(self.docs[name])
        previous = self.raw if name=="raw" else project(self.raw).text
        if current == previous:
            return
        if self.pending:
            self.rejected = current
            self.changed.emit()
            return
        if self.ime_selection and self.ime_selection["view"] == name and self.input_hint is None:
            # Native preedit can remove a selection from its document before commit.
            # Hold that change in the native input transaction, not authoritative raw.
            self.message("Native composition owns the selected range; raw source waits for commit or cancellation.")
            return
        self.last_delta = {"view": name, "positionUtf16": position, "removedUtf16": removed, "addedUtf16": added}
        if name == "raw":
            self.input_hint = None
            self.ime_selection = None
            self.adopt(current, "raw")
            return
        try:
            if self.input_hint is not None:
                hint = self.input_hint
                self.input_hint = None
                self.ime_selection = None
                position, removed, insert = hint["start"], hint["end"]-hint["start"], hint["text"]
                self.last_delta["inputMethodReplacement"] = hint
            else:
                # Qt may include its final paragraph sentinel in a whole-block change.
                removed = min(removed, max(0, u16(previous)-position))
                added = min(added, max(0, u16(current)-position))
                insert = slice16(current, position, position+added)
            expected = slice16(previous, 0, position)+insert+slice16(previous, position+removed, u16(previous))
            if expected != current:
                raise ValueError("Native edit delta is not a simple mapped replacement; no guessed diff was applied.")
            value = mapped_replace(self.raw, position, position+removed, insert, self.affinity, self.crossing)
            self.adopt(value, "hidden-view")
        except (ValueError, UnicodeError) as error:
            self.pending = True
            self.rejected = current
            self.notice = str(error)+" Attempted display retained below."
            self.changed.emit()
            if not self.composing:
                QTimer.singleShot(0, self.rollback_projection)

    def rollback_projection(self):
        if not self.pending or self.composing:
            return
        self.pending = False
        self.sync()
        self.changed.emit()

    @Slot(bool, str)
    def composition(self, active, preedit):
        self.composing, self.preedit = active, preedit
        self.changed.emit()
        if not active and self.pending:
            QTimer.singleShot(0, self.rollback_projection)

    @Slot(int)
    def fixture(self, index):
        if self.composing or self.pending:
            self.message("Finish native composition / pending recovery before changing fixtures.")
            return
        self.raw = SAMPLES[index][1]
        self.lines = [self.raw, r"{\an2}Second synthetic Line. Enter commits and advances."]
        self.line_index = 0
        self.undo_stack.clear(); self.redo_stack.clear(); self.commits.clear()
        self.rejected = ""; self.last_delta = {}; self.revision += 1
        self.sync()
        self.message("Fixture loaded in memory; mappings use UTF-16 offsets and Qt grapheme boundaries.")

    @Slot(str, str)
    def policy(self, affinity, crossing):
        if self.composing or self.pending:
            return
        self.affinity, self.crossing = affinity, crossing
        self.message("Proposal selected: "+affinity+" boundary tags; "+crossing+" cross-tag edits. No product policy approved.")

    @Slot()
    def undo(self):
        if self.composing or self.pending or not self.undo_stack:
            return
        record = self.undo_stack.pop()
        self.redo_stack.append(record)
        self.raw = record["before"]; self.revision += 1
        self.sync(); self.message("Raw snapshot restored exactly; mapped view regenerated. Probe undo grouping remains a proposal.")

    @Slot()
    def redo(self):
        if self.composing or self.pending or not self.redo_stack:
            return
        record = self.redo_stack.pop()
        self.undo_stack.append(record)
        self.raw = record["after"]; self.revision += 1
        self.sync(); self.message("Raw snapshot redone exactly.")

    @Slot()
    def commitNext(self):
        if self.composing or self.pending:
            self.message("Enter/commit guarded: native composition or rejected edit is still pending.")
            return
        self.commits.append({"line": self.line_index, "before": self.lines[self.line_index], "after": self.raw})
        self.lines[self.line_index] = self.raw
        self.line_index = (self.line_index+1) % len(self.lines)
        self.raw = self.lines[self.line_index]
        self.undo_stack.clear(); self.redo_stack.clear(); self.revision += 1
        self.sync(); self.message("Committed Line and advanced in the two-Line memory document. Draft/document undo split is a proposal.")

    @Slot()
    def undoCommit(self):
        if self.composing or self.pending or not self.commits:
            return
        record = self.commits.pop()
        self.line_index = record["line"]
        self.lines[self.line_index] = record["before"]
        self.raw = record["before"]
        self.undo_stack.clear(); self.redo_stack.clear(); self.revision += 1
        self.sync(); self.message("Previous committed Line restored in this memory-only document.")

    @Slot(int, int, str)
    def mappedInsert(self, start, end, value):
        if self.composing or self.pending:
            self.message("External mutation deferred while native composition is active.")
            return
        cursor = QTextCursor(self.docs["clean"])
        cursor.beginEditBlock()
        cursor.setPosition(start)
        cursor.setPosition(end, QTextCursor.KeepAnchor)
        cursor.insertText(value)
        cursor.endEditBlock()

    @Slot()
    def spell(self):
        p = project(self.raw)
        found = re.search(r"\bteh\b", p.text)
        if found:
            self.mappedInsert(u16(p.text[:found.start()]), u16(p.text[:found.end()]), "the")
        else:
            self.message("No sample 'teh' in the projected text. No Hunspell service is loaded.")

    @Slot()
    def boundaryDemo(self):
        if self.composing or self.pending:
            return
        self.raw = r"A{\i1}B{\i0}C"; self.undo_stack.clear(); self.redo_stack.clear()
        self.sync()
        self.mappedInsert(1, 1, "X")

    @Slot()
    def crossingDemo(self):
        if self.composing or self.pending:
            return
        self.raw = r"A{\i1}B{\i0}C"; self.undo_stack.clear(); self.redo_stack.clear()
        self.sync()
        self.mappedInsert(0, 3, "X")

def native_checks(bridge, window):
    """Offscreen QObject/QInputMethodEvent probes, NOT actual platform IME acceptance."""
    records = []
    def load(raw):
        bridge.raw = raw; bridge.undo_stack.clear(); bridge.redo_stack.clear(); bridge.pending=False
        bridge.sync(); QCoreApplication.processEvents()
    baseline = r"A{\i1}B{\i0}C"
    load(baseline)
    bridge.affinity, bridge.crossing = "after", "retain"
    bridge.mappedInsert(1,1,"X")
    assert bridge.raw == r"A{\i1}XB{\i0}C", bridge.raw
    bridge.undo(); assert bridge.raw == baseline
    bridge.redo(); assert bridge.raw == r"A{\i1}XB{\i0}C"
    records.append("Native QTextCursor hidden-view insertion + exact raw undo/redo passed")
    load(r"A\hB\nC\ND")
    bridge.mappedInsert(0,1,"X")
    assert bridge.raw == r"X\hB\nC\ND"
    assert "\u00a0" in document_text(bridge.docs["clean"])
    records.append("QTextDocument raw-text retrieval retained NBSP / untouched escape tokens")
    load(baseline)
    bridge.mappedInsert(0,3,"X")
    assert bridge.raw == r"X{\i1}{\i0}"
    bridge.undo(); assert bridge.raw == baseline
    records.append("Native selection replacement retained intervening tags; one probe undo restored exact source text")
    load(baseline)
    bridge.mappedInsert(0,1,r"{\b1}")
    QCoreApplication.processEvents()
    assert bridge.raw == baseline and document_text(bridge.docs["clean"]) == "ABC"
    assert bridge.rejected
    records.append("Ambiguous ASS paste rejected; raw unchanged and attempted text retained")
    clean = bridge.items["clean"]
    load(r"{\an2}A")
    QMetaObject.invokeMethod(clean, "forceActiveFocus")
    clean.setProperty("cursorPosition", 1)
    QCoreApplication.processEvents()
    preedit = QInputMethodEvent("にほん", [])
    QCoreApplication.sendEvent(clean, preedit)
    QCoreApplication.processEvents()
    assert clean.property("inputMethodComposing"), "Synthetic preedit did not reach native TextArea"
    assert bridge.raw == r"{\an2}A"
    before = (bridge.line_index, len(bridge.commits), bridge.raw)
    bridge.commitNext()
    assert before == (bridge.line_index, len(bridge.commits), bridge.raw)
    commit = QInputMethodEvent("", [])
    commit.setCommitString("日本")
    QCoreApplication.sendEvent(clean, commit)
    QCoreApplication.processEvents()
    assert bridge.raw == r"{\an2}A日本", json.dumps({"raw":bridge.raw,"native":document_text(bridge.docs["clean"]),"delta":bridge.last_delta,"notice":bridge.notice,"composing":bridge.composing,"pending":bridge.pending},ensure_ascii=True)
    assert not clean.property("inputMethodComposing")
    replacement = QInputMethodEvent("", [])
    replacement.setCommitString("語", -1, 1)
    QCoreApplication.sendEvent(clean, replacement)
    QCoreApplication.processEvents()
    assert bridge.raw == r"{\an2}A日語", bridge.raw
    records.append("Synthetic QInputMethodEvent preedit/commit/replacement passed; guarded commit did not advance")
    from PySide6.QtTest import QTest
    load(r"A\nB\hC{\i1}D{\i0}")
    clean.setProperty("cursorPosition", u16(project(bridge.raw).text))
    replacement = QInputMethodEvent("", [])
    replacement.setCommitString("字", -1, 1)
    QCoreApplication.sendEvent(clean, replacement)
    QCoreApplication.processEvents()
    assert bridge.raw == r"A\nB\hC{\i1}字{\i0}", bridge.raw
    records.append("Synthetic IME replacement retained preceding soft/hard-space escapes and adjacent overrides exactly")
    load(baseline)
    QMetaObject.invokeMethod(clean, "select", Qt.DirectConnection, Q_ARG(int, 0), Q_ARG(int, 3))
    QCoreApplication.sendEvent(clean, QInputMethodEvent("に", []))
    QCoreApplication.processEvents()
    assert bridge.raw == baseline, (bridge.raw, "selection preedit changed source")
    selected_commit = QInputMethodEvent("", [])
    selected_commit.setCommitString("日本")
    QCoreApplication.sendEvent(clean, selected_commit)
    QCoreApplication.processEvents()
    assert bridge.raw == r"日本{\i1}{\i0}", (bridge.raw, bridge.notice)
    bridge.undo()
    assert bridge.raw == baseline
    records.append("Synthetic IME selection replacement across hidden tags retained tags and exact-source undo")
    QMetaObject.invokeMethod(clean, "select", Qt.DirectConnection, Q_ARG(int, 0), Q_ARG(int, 3))
    QCoreApplication.sendEvent(clean, QInputMethodEvent("に", []))
    QCoreApplication.processEvents()
    QCoreApplication.sendEvent(clean, QInputMethodEvent("", []))
    QCoreApplication.processEvents()
    assert bridge.raw == baseline and document_text(bridge.docs["clean"]) == "ABC", (bridge.raw, bridge.notice)
    records.append("Synthetic selected-range preedit cancellation restored source and visible selection text")
    raw_item = bridge.items["raw"]
    load(baseline)
    QMetaObject.invokeMethod(raw_item, "forceActiveFocus")
    QMetaObject.invokeMethod(raw_item, "select", Qt.DirectConnection, Q_ARG(int, 0), Q_ARG(int, 1))
    QCoreApplication.sendEvent(raw_item, QInputMethodEvent("に", []))
    QCoreApplication.processEvents()
    assert bridge.raw == baseline
    QCoreApplication.sendEvent(raw_item, QInputMethodEvent("", []))
    QCoreApplication.processEvents()
    assert bridge.raw == baseline and document_text(bridge.docs["raw"]) == baseline
    QCoreApplication.sendEvent(raw_item, QInputMethodEvent("に", []))
    raw_commit = QInputMethodEvent("", [])
    raw_commit.setCommitString("日本")
    QCoreApplication.sendEvent(raw_item, raw_commit)
    QCoreApplication.processEvents()
    assert bridge.raw == r"日本{\i1}B{\i0}C", bridge.raw
    bridge.undo(); assert bridge.raw == baseline
    records.append("Raw-view synthetic selected preedit cancellation / commit retained untouched tags with exact-source undo")
    QMetaObject.invokeMethod(clean, "forceActiveFocus")
    load(r"{\an2}A")
    clean.setProperty("cursorPosition", 1)
    QTest.keyClick(window, Qt.Key_X)
    QCoreApplication.processEvents()
    assert bridge.raw == r"{\an2}Ax", bridge.raw
    QTest.keyClick(window, Qt.Key_Z, Qt.ControlModifier)
    QCoreApplication.processEvents()
    assert bridge.raw == r"{\an2}A", bridge.raw
    records.append("Offscreen native key typing and Ctrl+Z used source-preserving probe history")
    before_line = bridge.line_index
    QTest.keyClick(window, Qt.Key_Return)
    QCoreApplication.processEvents()
    assert bridge.line_index != before_line, "Plain Enter did not advance"
    records.append("Offscreen native key event Enter committed and advanced outside composition")
    return records

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--offscreen", action="store_true")
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--self-check", action="store_true")
    parser.add_argument("--sample", type=int, default=0, choices=range(len(SAMPLES)))
    parser.add_argument("--affinity", choices=("before","after"), default="after")
    parser.add_argument("--crossing", choices=("retain","block"), default="retain")
    args = parser.parse_args()
    if args.offscreen:
        os.environ["QT_QPA_PLATFORM"] = "offscreen"
        os.environ["QT_QUICK_BACKEND"] = "software"
    app = QGuiApplication(sys.argv[:1])
    if args.offscreen and sys.platform == "win32":
        directory = Path(os.environ.get("WINDIR","C:/Windows"))/"Fonts"
        for name in ("segoeui.ttf","segoeuib.ttf","seguisym.ttf","seguiemj.ttf","msyh.ttc","malgun.ttf"):
            if (directory/name).exists():
                QFontDatabase.addApplicationFont(str(directory/name))
    app.setFont(QFont("Segoe UI",10))
    QQuickStyle.setStyle("Basic")
    bridge = Bridge(args.sample,args.affinity,args.crossing)
    engine = QQmlApplicationEngine()
    engine.rootContext().setContextProperty("bridge",bridge)
    engine.setInitialProperties({"initialSample":args.sample})
    engine.load(QUrl.fromLocalFile(str(Path(__file__).with_name("Editor.qml"))))
    if not engine.rootObjects():
        return 1
    if args.self_check or args.capture:
        def exercise():
            try:
                report = {"runtime":"PySide6 6.11.2 / offscreen software" if args.offscreen else "native platform",
                          "fixtures":len(run_fixtures()),"nativeChecks":native_checks(bridge,engine.rootObjects()[0]) if args.self_check else [],
                          "realIME":False,"screenReader":False,"linux":False}
                bridge.fixture(args.sample)
                bridge.policy(args.affinity,args.crossing)
                def capture():
                    if args.capture:
                        args.capture.parent.mkdir(parents=True,exist_ok=True)
                        report["capture"] = str(args.capture)
                        report["captureSaved"] = engine.rootObjects()[0].grabWindow().save(str(args.capture))
                    print(json.dumps(report,ensure_ascii=True,indent=2))
                    app.exit(0 if report.get("captureSaved",True) else 2)
                QTimer.singleShot(300,capture)
            except Exception:
                import traceback
                traceback.print_exc()
                app.exit(2)
        QTimer.singleShot(500,exercise)
    return app.exec()

if __name__=="__main__":
    raise SystemExit(main())
