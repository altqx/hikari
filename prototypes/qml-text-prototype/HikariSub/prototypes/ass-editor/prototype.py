"""Throwaway native ASS editor probe for issue #28; all edits are in memory."""
import argparse
import json
import os
from pathlib import Path
import re
import sys

from PySide6.QtCore import QObject, Property, QTimer, QUrl, Signal, Slot
from PySide6.QtGui import QColor, QFont, QFontDatabase, QGuiApplication, QSyntaxHighlighter, QTextCharFormat, QTextCursor
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick import QQuickTextDocument
from PySide6.QtQuickControls2 import QQuickStyle


def u16(text):
    return len(text.encode("utf-16-le")) // 2


SAMPLES = [
    ("ASS + spelling", r"{\an8\bord2}This is teh {\i1}subtitel{\i0}.\N{\k25}Ka{\k30}ra{\k20}oke 🎵"),
    ("Mixed RTL", r"{\an7}مرحبا بالعالم — Hello 123 {\i1}שלום{\i0} (ABC)\Nالعربية 🎬"),
    ("CJK / IME", r"{\an2}日本語の字幕 / 中文 / 한국어\NPlace the caret here and use your IME: "),
    ("Emoji + combining", "{\\b1}Ame\u0301lie{\\b0} 👩🏽‍💻 says teh thing.\\hKeep\\nsoft\\Nhard"),
    ("Incomplete + drawing", r"{\p1}m 0 0 l 60 0 60 30{\p0}Text {\t(0,300,\bord4)} and {\i1"),
]


def project(raw):
    """Small lossless-offset experiment, not a production ASS parser."""
    clean, spans, tags, problems = [], [], [], []
    source = display = 0
    i = 0
    drawing = False
    while i < len(raw):
        if raw[i] == "{":
            end = raw.find("}", i + 1)
            if end < 0:
                end = len(raw) - 1
                problems.append("Unclosed override block: retained in raw; hidden in projection.")
            token = raw[i:end + 1]
            for p in re.finditer(r"\\p(\d+)", token):
                drawing = int(p.group(1)) != 0
            tags.append({"start": source, "end": source + u16(token), "text": token})
            spans.append({"rawStart": source, "rawEnd": source + u16(token),
                          "cleanStart": display, "cleanEnd": display, "kind": "hidden tag"})
            source += u16(token)
            i = end + 1
            continue
        if raw[i:i + 2] in (r"\N", r"\n", r"\h"):
            token = raw[i:i + 2]
            value = {r"\N": "\n", r"\n": " ", r"\h": "\u00a0"}[token]
            kind = {r"\N": "hard break", r"\n": "soft break → space", r"\h": "hard space"}[token]
            i += 2
        else:
            token = raw[i]
            value = token
            kind = "text"
            i += 1
        if drawing:
            value, kind = "", "hidden drawing"
        clean.append(value)
        spans.append({"rawStart": source, "rawEnd": source + u16(token),
                      "cleanStart": display, "cleanEnd": display + u16(value), "kind": kind})
        source += u16(token)
        display += u16(value)
    return "".join(clean), spans, tags, problems


class AssHighlighter(QSyntaxHighlighter):
    def __init__(self, document):
        super().__init__(document)
        self.underline = 0
        self.cursor = 0
        self.words = {"teh": "the", "subtitel": "subtitle"}

    def highlightBlock(self, text):
        _, spans, tags, _ = project(text)
        for tag in tags:
            fmt = QTextCharFormat()
            fmt.setForeground(QColor("#9174cf"))
            fmt.setBackground(QColor("#efe9f8"))
            self.setFormat(tag["start"], tag["end"] - tag["start"], fmt)
        for match in re.finditer(r"\\(?:[1-4]?[a-zA-Z]+)", text):
            fmt = QTextCharFormat()
            fmt.setForeground(QColor("#6c42ad"))
            fmt.setFontWeight(600)
            self.setFormat(u16(text[:match.start()]), u16(match.group()), fmt)
        for match in re.finditer(r"\b(teh|subtitel)\b", text):
            begin, end = u16(text[:match.start()]), u16(text[:match.end()])
            if any(s["kind"] != "text" and s["rawStart"] <= begin < s["rawEnd"] for s in spans):
                continue
            fmt = QTextCharFormat()
            fmt.setForeground(QColor("#b52840"))
            fmt.setUnderlineColor(QColor("#db334e"))
            fmt.setUnderlineStyle([QTextCharFormat.SingleUnderline,
                                   QTextCharFormat.WaveUnderline,
                                   QTextCharFormat.SpellCheckUnderline][self.underline])
            self.setFormat(begin, end - begin, fmt)
        # Cursor brace emphasis does not mutate text or add an undo command.
        local = self.cursor - self.currentBlock().position()
        for tag in tags:
            if tag["start"] <= local <= tag["end"]:
                fmt = QTextCharFormat()
                fmt.setForeground(QColor("#573092"))
                fmt.setBackground(QColor("#d8c5f3"))
                self.setFormat(tag["start"], 1, fmt)
                if tag["text"].endswith("}"):
                    self.setFormat(tag["end"] - 1, 1, fmt)


class Bridge(QObject):
    noticeChanged = Signal()

    def __init__(self):
        super().__init__()
        self.doc = self.highlighter = None
        self._state = {}
        self._notice = "Draft only · Ctrl+Enter commits this line in memory."
        self.history = []
        self.last_commit = ""

    @Property("QVariantList", constant=True)
    def samples(self):
        return [s[0] for s in SAMPLES]

    @Property(str, notify=noticeChanged)
    def notice(self):
        return self._notice

    def message(self, value):
        self._notice = value
        self.noticeChanged.emit()

    @Slot(int, result=str)
    def sample(self, index):
        self.last_commit = SAMPLES[index][1]
        self.history = []
        return self.last_commit

    @Slot(QObject)
    def attach(self, quick_document):
        self.quick_document = quick_document
        self.doc = quick_document.textDocument()
        self.highlighter = AssHighlighter(self.doc)
        self.message("Native QTextDocument + QSyntaxHighlighter attached · draft undo is local.")

    @Slot(str, int, int, int, bool, str, result="QVariantMap")
    def inspect(self, raw, cursor, start, end, composing, preedit):
        clean, spans, tags, problems = project(raw)
        suggestions = []
        for match in re.finditer(r"\b(teh|subtitel)\b", raw):
            begin, finish = u16(raw[:match.start()]), u16(raw[:match.end()])
            if any(s["kind"] != "text" and s["rawStart"] <= begin < s["rawEnd"] for s in spans):
                continue
            suggestions.append({"start": begin, "end": finish, "word": match.group(),
                                "replacement": {"teh": "the", "subtitel": "subtitle"}[match.group()]})
        self._state = {"raw": raw, "clean": clean, "cursor": cursor,
                       "selectionStart": start, "selectionEnd": end,
                       "composing": composing, "preedit": preedit,
                       "utf16Length": u16(raw), "codepoints": len(raw),
                       "tags": tags, "problems": problems, "problemsText": " · ".join(problems), "suggestions": suggestions,
                       "map": spans, "mapJson": json.dumps(spans, ensure_ascii=False, indent=2),
                       "commits": len(self.history), "lastCommit": self.last_commit}
        if self.highlighter:
            self.highlighter.cursor = cursor
            self.highlighter.rehighlight()
        return self._state

    @Slot(int)
    def underline(self, index):
        if self.highlighter:
            self.highlighter.underline = index
            self.highlighter.rehighlight()
        self.message(["SingleUnderline", "WaveUnderline", "SpellCheckUnderline"][index]
                     + " requested. Judge the rendered shape; the enum is not proof of a wave.")

    @Slot(int, int, str)
    def replace(self, start, end, value):
        if not self.doc or self._state.get("composing"):
            self.message("Suggestion deferred while input-method composition is active.")
            return
        cursor = QTextCursor(self.doc)
        cursor.beginEditBlock()
        cursor.setPosition(start)
        cursor.setPosition(end, QTextCursor.KeepAnchor)
        cursor.insertText(value)
        cursor.endEditBlock()
        self.message("Suggestion replaced one source span · Ctrl+Z should undo it in one step.")

    @Slot(str)
    def fixWord(self, word):
        suggestion = next((s for s in self._state.get("suggestions", []) if s["word"] == word), None)
        if suggestion:
            self.replace(suggestion["start"], suggestion["end"], suggestion["replacement"])
        else:
            self.message("No matching visible dialogue word. Tags and drawing data are excluded.")

    @Slot(str, bool)
    def commit(self, raw, composing):
        if composing:
            self.message("Commit blocked: finish or cancel the IME composition first.")
            return
        self.history.append(self.last_commit)
        self.last_commit = raw
        self.doc.clearUndoRedoStacks()
        self.message("Line committed in memory · local undo cleared; document undo restores the prior commit.")

    @Slot(result=str)
    def undoCommit(self):
        if self.history:
            self.last_commit = self.history.pop()
        self.message("Document undo restored the previous committed line; local typing history is separate.")
        return self.last_commit

    @Slot(int, int, result="QVariantMap")
    def locate(self, start, end):
        spans = self._state.get("map", [])
        visible = [s for s in spans if s["cleanEnd"] > s["cleanStart"]]
        picked = [s for s in visible if s["cleanStart"] < end and s["cleanEnd"] > start]
        if picked:
            result = {"start": picked[0]["rawStart"], "end": picked[-1]["rawEnd"]}
        else:
            right = next((s for s in visible if s["cleanEnd"] > start), None)
            pos = right["rawStart"] if right else self._state.get("utf16Length", 0)
            result = {"start": pos, "end": pos}
        self.message("Clean → raw mapping uses right affinity at hidden tags; spanning selections include intervening tags.")
        return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture", type=Path, help="Save a runtime screenshot and exit (review aid).")
    parser.add_argument("--offscreen", action="store_true")
    parser.add_argument("--sample", type=int, choices=range(len(SAMPLES)), default=0)
    parser.add_argument("--underline", choices=("single", "wave", "spellcheck"), default="single")
    parser.add_argument("--view", choices=("raw", "translation", "map"), default="raw")
    args = parser.parse_args()
    if args.offscreen:
        os.environ["QT_QPA_PLATFORM"] = "offscreen"
        os.environ["QT_QUICK_BACKEND"] = "software"
    app = QGuiApplication(sys.argv[:1])
    if args.offscreen and sys.platform == "win32":
        # The Windows offscreen plugin has no system font catalog here.
        fonts_dir = Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts"
        for name in ("segoeui.ttf", "segoeuib.ttf", "seguisym.ttf", "seguiemj.ttf", "msyh.ttc", "malgun.ttf"):
            if (fonts_dir / name).exists():
                QFontDatabase.addApplicationFont(str(fonts_dir / name))
    app.setFont(QFont("Segoe UI", 10))
    QQuickStyle.setStyle("Basic")
    engine = QQmlApplicationEngine()
    engine.setInitialProperties({"initialSample": args.sample,
                                 "initialUnderline": ("single", "wave", "spellcheck").index(args.underline),
                                 "initialView": ("raw", "translation", "map").index(args.view)})
    bridge = Bridge()
    engine.rootContext().setContextProperty("bridge", bridge)
    engine.load(QUrl.fromLocalFile(str(Path(__file__).with_name("Editor.qml"))))
    if not engine.rootObjects():
        return 1
    if args.capture:
        def capture():
            window = engine.rootObjects()[0]
            args.capture.parent.mkdir(parents=True, exist_ok=True)
            ok = window.grabWindow().save(str(args.capture))
            print(json.dumps({"capture": str(args.capture), "saved": ok,
                              "documentAttached": bridge.doc is not None,
                              "raw": bridge._state.get("raw"), "clean": bridge._state.get("clean"),
                              "tagCount": len(bridge._state.get("tags", [])),
                              "suggestions": bridge._state.get("suggestions")}, ensure_ascii=True))
            app.exit(0 if ok else 2)
        QTimer.singleShot(1200, capture)
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
