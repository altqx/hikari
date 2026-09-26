"""Throwaway #27: compare TableView and a painted viewport on identical ASS rows."""
import argparse
import json
import math
import platform
import re
import sys
import time
from pathlib import Path

import PySide6
from PySide6.QtCore import QAbstractTableModel, QModelIndex, QObject, Property, QTimer, Qt, QUrl, Signal, Slot, qVersion
from PySide6.QtGui import QColor, QFont, QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine, qmlRegisterType
from PySide6.QtQuick import QQuickPaintedItem
from PySide6.QtQuickControls2 import QQuickStyle

HERE = Path(__file__).resolve().parent
HEADERS = ['ID', 'Start', 'End', 'Style', 'State', 'Subtitle text']
WIDTHS = [72, 112, 112, 112, 110]
STATES = ['Dialogue', 'Comment', 'Warning', 'Karaoke', 'RTL']
COLORS = ['#182330', '#283044', '#3b2928', '#1d3437', '#302b42']


def stamp(ms):
    return f'{ms // 3600000}:{ms // 60000 % 60:02}:{ms // 1000 % 60:02}.{ms // 10 % 100:02}'


class Rows(QAbstractTableModel):
    changed = Signal()
    visible = Signal(int)
    Display, Selected, Current, Background, SourceId = range(256, 261)

    def __init__(self, count):
        super().__init__()
        templates = [r'{\i1}The train leaves at midnight.{\i0}', r'{\pos(640,80)\bord2}STATION EXIT',
                     r'{\k25}A{\k30} quiet {\k40}promise', r'{\an7}مرحبا بالعالم — 2026',
                     r'{\c&H00FFFF&}明日の朝、ここで会いましょう。', r'{\i1}A line\Nwith a second sentence.{\i0}']
        self.rows = []
        for i in range(count):
            text = templates[i % len(templates)] + f'  [{i + 1}]'
            if i % 41 == 0:
                text += r' {\t(0,500,\fscx120)\blur0.8}A longer typesetting cue.' * 12
            state = STATES[(i % 97 == 0) * 2 or (4 if i % 6 == 3 else 3 if i % 6 == 2 else 1 if i % 19 == 0 else 0)]
            plain = re.sub(r'\{[^}]*\}', '', text).replace(r'\N', ' ↵ ')
            style = ['Default', 'Sign', 'Song'][i % 3]
            self.rows.append((str(i + 1), stamp(i * 1250), stamp(i * 1250 + 1750), style, state, text, plain,
                              (text + plain + state + style).casefold()))
        self.order = list(range(count))
        self.positions = {i: i for i in self.order}
        self.selected = {0}
        self.current = 0
        self.anchor = 0
        self.hidden = False
        self.query = ''
        self.sort_mode = 0
        self.data_calls = 0
        self.last_operation = 'Ready'

    def roleNames(self):
        return {self.Display: b'display', self.Selected: b'chosen', self.Current: b'currentLine',
                self.Background: b'rowColor', self.SourceId: b'sourceId'}

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.order)

    def columnCount(self, parent=QModelIndex()):
        return 6

    def data(self, index, role=Qt.DisplayRole):
        self.data_calls += 1
        if not index.isValid() or index.row() >= len(self.order):
            return None
        ident = self.order[index.row()]
        row = self.rows[ident]
        if role in (self.Display, Qt.DisplayRole):
            return row[6 if index.column() == 5 and self.hidden else index.column()]
        if role == self.Selected:
            return ident in self.selected
        if role == self.Current:
            return ident == self.current
        if role == self.Background:
            return COLORS[STATES.index(row[4])]
        if role == self.SourceId:
            return ident + 1

    @Property(int, notify=changed)
    def count(self):
        return len(self.order)

    @Property(int, constant=True)
    def total(self):
        return len(self.rows)

    @Property(str, notify=changed)
    def summary(self):
        shown = sum(i in self.positions for i in self.selected)
        return f'{len(self.order):,} / {len(self.rows):,} rows · current ID {self.current + 1} · {len(self.selected)} selected ({shown} visible) · {self.last_operation}'

    @Property(str, notify=changed)
    def currentText(self):
        r = self.rows[self.current]
        return f'ID {self.current + 1} | {r[1]} → {r[2]} | {r[3]} | {r[4]}\n{r[5]}'

    def refresh(self):
        if self.order:
            self.dataChanged.emit(self.index(0, 0), self.index(len(self.order) - 1, 5), [])
        self.changed.emit()

    @Slot(int, int)
    def select(self, row, modifiers=0):
        if not 0 <= row < len(self.order):
            return
        ident = self.order[row]
        if modifiers & Qt.ShiftModifier.value:
            anchor = self.positions.get(self.anchor, row)
            picked = set(self.order[min(anchor, row):max(anchor, row) + 1])
            self.selected = self.selected | picked if modifiers & Qt.ControlModifier.value else picked
        elif modifiers & Qt.ControlModifier.value:
            self.selected.symmetric_difference_update({ident})
            self.anchor = ident
        else:
            self.selected = {ident}
            self.anchor = ident
        self.current = ident
        self.last_operation = f'select visible row {row + 1}'
        self.refresh()

    @Slot(int, int)
    def move(self, delta, modifiers=0):
        row = max(0, min(len(self.order) - 1, self.positions.get(self.current, 0) + delta))
        self.select(row, modifiers)
        self.visible.emit(row)

    @Slot(bool, int)
    def boundary(self, end, modifiers=0):
        if not self.order:
            return
        row = len(self.order) - 1 if end else 0
        self.select(row, modifiers)
        self.visible.emit(row)

    @Slot(int)
    def jump(self, ident):
        if ident - 1 in self.positions:
            row = self.positions[ident - 1]
            self.select(row)
            self.visible.emit(row)

    @Slot(str, int)
    def arrange(self, query, mode):
        started = time.perf_counter()
        self.query, self.sort_mode = query.casefold(), mode
        self.beginResetModel()
        self.order = [i for i, r in enumerate(self.rows) if self.query in r[7] or self.query == r[0]]
        if mode == 1:
            self.order.reverse()
        elif mode == 2:
            self.order.sort(key=lambda i: (self.rows[i][3], i))
        self.positions = {i: row for row, i in enumerate(self.order)}
        self.endResetModel()
        self.last_operation = f'filter/sort {(time.perf_counter() - started) * 1000:.1f} ms'
        self.changed.emit()

    @Slot(bool)
    def hideTags(self, hidden):
        self.hidden = hidden
        self.last_operation = 'tags hidden' if hidden else 'raw ASS tags'
        self.refresh()


class PaintedGrid(QQuickPaintedItem):
    model = None
    timings = []

    def __init__(self, parent=None):
        super().__init__(parent)
        self._offset = 0.0
        self.setOpaquePainting(True)
        self.setAntialiasing(False)
        self.model.changed.connect(self.update)

    def setOffset(self, value):
        self._offset = value
        self.update()

    offset = Property(float, lambda self: self._offset, setOffset)

    def paint(self, painter):
        started = time.perf_counter()
        painter.fillRect(self.boundingRect(), QColor('#111923'))
        painter.setFont(QFont('Segoe UI', 10))
        widths = WIDTHS + [max(300, int(self.width()) - sum(WIDTHS))]
        first = int(self._offset // 30)
        for n in range(first, min(self.model.count, first + math.ceil(self.height() / 30) + 1)):
            ident = self.model.order[n]
            row = self.model.rows[ident]
            y = n * 30 - self._offset
            painter.fillRect(0, int(y), int(self.width()), 30,
                             QColor('#245571' if ident in self.model.selected else COLORS[STATES.index(row[4])]))
            x = 0
            for col, width in enumerate(widths):
                value = row[6 if col == 5 and self.model.hidden else col]
                painter.setPen(QColor('#e3e9f1'))
                rect = self.boundingRect().adjusted(x + 9, y, -(self.width() - x - width + 9), -(self.height() - y - 30))
                painter.drawText(rect, Qt.AlignVCenter | Qt.AlignLeft | Qt.TextSingleLine,
                                 painter.fontMetrics().elidedText(value, Qt.ElideRight, width - 18))
                painter.setPen(QColor('#35414d'))
                painter.drawLine(x + width - 1, int(y), x + width - 1, int(y + 30))
                x += width
            if ident == self.model.current:
                painter.setPen(QColor('#8ed4eb'))
                painter.drawRect(1, int(y + 1), int(self.width() - 2), 28)
        self.timings.append((time.perf_counter() - started) * 1000)


class Probe(QObject):
    changed = Signal()
    scroll = Signal(float)
    finished = Signal()

    def __init__(self, model):
        super().__init__()
        self.model = model
        self.message = 'No measurement yet. Scroll probe runs for 5 seconds.'
        self.running = False
        self.frames = []
        self.results = []
        self.created = self.reused = self.live = self.peak_live = 0
        self.timer = QTimer(self)
        self.timer.setInterval(16)
        self.timer.timeout.connect(self.tick)

    @Property(str, notify=changed)
    def status(self):
        return self.message

    @Property(bool, notify=changed)
    def busy(self):
        return self.running

    @Slot(int)
    def delegate(self, event):
        if event == 1:
            self.created += 1
            self.live += 1
            self.peak_live = max(self.peak_live, self.live)
        elif event == -1:
            self.live -= 1
        else:
            self.reused += 1

    def frame(self):
        if self.running:
            self.frames.append(time.perf_counter())

    @Slot(str)
    def start(self, variant):
        if self.running:
            return
        self.variant = variant
        self.started = time.perf_counter()
        self.frames = []
        self.steps = 0
        self.start_calls = self.model.data_calls
        self.start_reused = self.reused
        self.start_created = self.created
        PaintedGrid.timings.clear()
        self.running = True
        self.message = f'Measuring {variant}: 256 logical px per 16 ms requested tick…'
        self.changed.emit()
        self.timer.start()

    def tick(self):
        self.steps += 1
        self.scroll.emit(float((self.steps * 256) % max(1, self.model.count * 30 - 800)))
        if time.perf_counter() - self.started < 5:
            return
        self.timer.stop()
        self.running = False
        intervals = sorted((b - a) * 1000 for a, b in zip(self.frames, self.frames[1:]))
        percentile = lambda values, p: round(values[min(len(values) - 1, int((len(values) - 1) * p))], 2) if values else None
        result = dict(variant=self.variant, rows=self.model.count, tags_hidden=self.model.hidden,
                      seconds=round(time.perf_counter() - self.started, 3), scroll_steps=self.steps,
                      delivered_frames=len(self.frames), interval_p50_ms=percentile(intervals, .5),
                      interval_p95_ms=percentile(intervals, .95), interval_max_ms=percentile(intervals, 1),
                      model_data_calls=self.model.data_calls - self.start_calls,
                      delegates_created=self.created - self.start_created, delegates_reused=self.reused - self.start_reused,
                      live_delegates=self.live, peak_live_delegates=self.peak_live,
                      paint_p95_ms=percentile(sorted(PaintedGrid.timings), .95))
        self.results.append(result)
        self.message = f'{self.variant}: {len(self.frames)} frame signals; interval p50 {result["interval_p50_ms"]} / p95 {result["interval_p95_ms"]} / max {result["interval_max_ms"]} ms. Delivery cadence, not GPU time.'
        print(json.dumps(result), flush=True)
        self.changed.emit()
        self.finished.emit()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--rows', type=int, default=50000)
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--output', type=Path, default=HERE / 'local-results')
    parser.add_argument('--export-ass', type=Path)
    args = parser.parse_args()
    QQuickStyle.setStyle('Fusion')
    app = QGuiApplication(sys.argv)
    model = Rows(args.rows)
    if args.export_ass:
        args.export_ass.parent.mkdir(parents=True, exist_ok=True)
        header = '[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n'
        header += ''.join(f'Style: {s},Arial,36,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,0,2,20,20,20,1\n' for s in ['Default', 'Sign', 'Song'])
        header += '[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n'
        args.export_ass.write_text(header + ''.join(f'{"Comment" if r[4] == "Comment" else "Dialogue"}: 0,{r[1]},{r[2]},{r[3]},,0,0,0,,{r[5]}\n' for r in model.rows), encoding='utf-8')
    probe = Probe(model)
    PaintedGrid.model = model
    qmlRegisterType(PaintedGrid, 'HikariPrototype', 1, 0, 'PaintedGrid')
    engine = QQmlApplicationEngine()
    engine.rootContext().setContextProperty('gridModel', model)
    engine.rootContext().setContextProperty('probe', probe)
    engine.load(QUrl.fromLocalFile(str(HERE / 'Main.qml')))
    if not engine.rootObjects():
        return 1
    window = engine.rootObjects()[0]
    window.frameSwapped.connect(probe.frame)
    if args.smoke:
        args.output.mkdir(parents=True, exist_ok=True)
        observations = {}
        captures = []

        def capture(name, done):
            grab = window.contentItem().grabToImage()
            captures.append(grab)
            def ready():
                grab.saveToFile(str(args.output / name))
                done()
            grab.ready.connect(ready)

        def begin():
            from PySide6.QtTest import QTest
            window.findChild(QObject, 'gridFocus').forceActiveFocus()
            QTest.keyClick(window, Qt.Key_Down)
            observations['keyboard_down_selects_next_row'] = model.current == 1
            model.select(7)
            ident = model.current
            model.arrange('RTL', 1)
            observations['selection_survives_hidden_filter'] = model.selected == {ident} and ident not in model.positions
            model.arrange('', 1)
            observations['selection_survives_sort'] = model.current == ident and model.selected == {ident}
            model.move(1, Qt.ShiftModifier.value)
            observations['shift_extends_selection'] = len(model.selected) == 2
            model.arrange('', 0)
            model.hideTags(True)
            observations['hidden_tags_remove_override_blocks'] = '{' not in model.data(model.index(0, 5), Rows.Display)
            model.hideTags(False)
            model.jump(1)
            QTimer.singleShot(200, lambda: capture('tableview.png', lambda: probe.start('TableView')))

        def complete():
            if len(probe.results) == 1:
                window.setProperty('variant', 1)
                QTimer.singleShot(400, lambda: probe.start('Painted'))
            else:
                report = dict(runtime=dict(python=platform.python_version(), pyside=PySide6.__version__, qt=qVersion(),
                                           platform=platform.platform(), backend=str(window.rendererInterface().graphicsApi()),
                                           width=window.width(), height=window.height(), dpr=window.devicePixelRatio()),
                              smoke_observations=observations, measurements=probe.results)
                (args.output / 'measurements.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
                capture('painted.png', app.quit)

        probe.finished.connect(complete)
        QTimer.singleShot(800, begin)
    return app.exec()


if __name__ == '__main__':
    sys.exit(main())
