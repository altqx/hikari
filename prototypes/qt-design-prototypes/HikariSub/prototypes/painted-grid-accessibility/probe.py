"""Throwaway #45: exact PySide accessibility boundary, not a production grid adapter."""
import argparse
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
from pathlib import Path

import PySide6
from PySide6 import QtGui
from PySide6.QtCore import QPointF, QRect, Qt, QTimer, Signal, Slot
from PySide6.QtGui import QAccessible, QAccessibleEvent, QAccessibleObject, QColor, QFont, QGuiApplication, QKeyEvent
from PySide6.QtQml import QQmlApplicationEngine, qmlRegisterType
from PySide6.QtQuick import QQuickPaintedItem

HERE = Path(__file__).resolve().parent
FACTORY_CALLS = []
INTERFACES = []


class PaintedStudy(QQuickPaintedItem):
    changed = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName('painted-accessibility-boundary')
        self.setAcceptedMouseButtons(Qt.MouseButton.LeftButton)
        self.order = list(range(1, 65))
        self.selected = {1}
        self.current = 1
        self.anchor = 1
        self.top = 0
        self.hidden = False
        self.reversed = False
        self.query = ''

    def caption(self):
        return f'Painted subtitle study. {len(self.order)} displayed rows. Current source ID {self.current}. Selected IDs {sorted(self.selected)}. No native table interface.'

    def notify(self):
        if self.current in self.order:
            index = self.order.index(self.current)
            self.top = min(max(0, index - 6), max(0, len(self.order) - 10))
        self.update()
        self.changed.emit()
        QAccessible.updateAccessibility(QAccessibleEvent(self, QAccessible.Event.NameChanged))

    @Slot(result=str)
    def summary(self):
        return self.caption()

    @Slot(str)
    def filterRows(self, query):
        self.query = query
        self.order = [i for i in range(1, 65) if not query or (query.lower() == 'rtl' and i % 3 == 0)]
        if self.reversed:
            self.order.reverse()
        self.top = 0
        self.notify()

    @Slot()
    def reverseOrder(self):
        self.reversed = not self.reversed
        self.order.reverse()
        self.notify()

    @Slot()
    def toggleTags(self):
        self.hidden = not self.hidden
        self.notify()

    def raw(self, source_id):
        return (r'{\i1}مرحبا — 2026 12:34' if source_id % 3 == 0 else r'{\bord2}明日の朝 / Harbor subtitle') + f' [{source_id}]'

    def paint(self, painter):
        painter.fillRect(self.boundingRect(), QColor('#1c232c'))
        painter.setFont(QFont('Segoe UI', 11))
        painter.setPen(QColor('#a5b1bd'))
        painter.drawText(12, 23, 'Source ID     Selection / current                  Text — painted only')
        for viewport_row, source_id in enumerate(self.order[self.top:self.top + 10]):
            y = 38 + viewport_row * 28
            if source_id in self.selected:
                painter.fillRect(0, y - 18, self.width(), 27, QColor('#304c47'))
            painter.setPen(QColor('#f9d784' if source_id == self.current else '#e8edf2'))
            text = self.raw(source_id)
            if self.hidden:
                text = re.sub(r'\{[^}]*\}', '', text)
            painter.drawText(12, y, str(source_id))
            painter.drawText(105, y, ('selected ' if source_id in self.selected else '') + ('current' if source_id == self.current else ''))
            painter.drawText(290, y, text)

    def keyPressEvent(self, event):
        if not self.order:
            event.accept()
            return
        pos = self.order.index(self.current) if self.current in self.order else 0
        targets = {Qt.Key.Key_Home: 0, Qt.Key.Key_End: len(self.order) - 1,
                   Qt.Key.Key_Up: max(0, pos - 1), Qt.Key.Key_Down: min(len(self.order) - 1, pos + 1)}
        if event.key() not in targets:
            event.ignore()
            return
        target = self.order[targets[event.key()]]
        if event.modifiers() & Qt.KeyboardModifier.ShiftModifier:
            anchor = self.order.index(self.anchor) if self.anchor in self.order else targets[event.key()]
            lo, hi = sorted((anchor, targets[event.key()]))
            self.selected = set(self.order[lo:hi + 1])
        elif not event.modifiers() & Qt.KeyboardModifier.ControlModifier:
            self.selected = {target}
            self.anchor = target
        self.current = target
        self.notify()
        event.accept()

    def mousePressEvent(self, event):
        self.forceActiveFocus()
        index = self.top + int((event.position().y() - 20) // 28)
        if 0 <= index < len(self.order):
            self.current = self.order[index]
            self.anchor = self.current
            self.selected = {self.current}
            self.notify()
        event.accept()


class PaintedAccessible(QAccessibleObject):
    """Aggregate named table role only. Deliberately does not fake a C++ TableInterface."""
    def __init__(self, item):
        super().__init__(item)
        self.item = item

    def role(self):
        return QAccessible.Role.Table

    def text(self, kind):
        if kind == QAccessible.Text.Name:
            return self.item.caption()
        if kind == QAccessible.Text.Description:
            return 'Boundary probe: painted content has no row/cell or table-pattern provider.'
        return ''

    def setText(self, kind, value):
        pass

    def state(self):
        state = QAccessible.State()
        state.focusable = True
        state.focused = self.item.hasActiveFocus()
        state.readOnly = True
        return state

    def rect(self):
        p = self.item.mapToGlobal(QPointF(0, 0))
        return QRect(round(p.x()), round(p.y()), round(self.item.width()), round(self.item.height()))

    def parent(self):
        return QAccessible.queryAccessibleInterface(self.item.window())

    def childCount(self):
        return 0

    def child(self, index):
        return None

    def indexOfChild(self, child):
        return -1

    def childAt(self, x, y):
        return None


def factory(class_name, obj):
    if isinstance(obj, PaintedStudy):
        FACTORY_CALLS.append(class_name)
        interface = PaintedAccessible(obj)
        INTERFACES.append(interface)
        return interface
    return None


QML = '''
import QtQuick
import QtQuick.Controls
import Study 1.0
ApplicationWindow {
    width: 990; height: 545; visible: true; title: "HikariSub #45 accessibility boundary study"
    color: "#171b20"
    Column {
        anchors.fill: parent; anchors.margins: 16; spacing: 10
        Label { text: "Painted direction accepted. This probe cannot implement the native table contract."; color: "#f2ca86" }
        Row {
            spacing: 8
            Button { text: "Reverse order"; onClicked: { study.reverseOrder(); study.forceActiveFocus() } }
            Button { text: "All rows"; onClicked: { study.filterRows(""); study.forceActiveFocus() } }
            Button { text: "RTL rows"; onClicked: { study.filterRows("RTL"); study.forceActiveFocus() } }
            Button { text: "Empty result"; onClicked: { study.filterRows("missing"); study.forceActiveFocus() } }
            Button { text: "Hide / show tags"; onClicked: { study.toggleTags(); study.forceActiveFocus() } }
        }
        PaintedStudy {
            id: study; width: parent.width; height: 335; focus: true
            Accessible.role: Accessible.Table
            Accessible.name: "Painted boundary probe"
            Component.onCompleted: forceActiveFocus()
        }
        Label { id: summary; text: study.summary(); color: "#e8edf2"; wrapMode: Text.Wrap; width: parent.width }
        Label { text: "Home/End/Up/Down · Shift extends · Ctrl moves current without replacing selection. No persistence."; color: "#a5b1bd" }
        Connections { target: study; function onChanged() { summary.text = study.summary() } }
    }
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--probe', action='store_true', help='Record bounded Qt + Windows UIA observations, then exit.')
    parser.add_argument('--skip-uia', action='store_true', help='Do not launch the optional Windows observer, e.g. where scripts are disabled.')
    args = parser.parse_args()
    output = HERE / 'local-results'
    output.mkdir(exist_ok=True)
    app = QGuiApplication(sys.argv[:1])
    QAccessible.installFactory(factory)
    QAccessible.setActive(True)
    qmlRegisterType(PaintedStudy, 'Study', 1, 0, 'PaintedStudy')
    engine = QQmlApplicationEngine()
    engine.loadData(QML.encode('utf-8'))
    if not engine.rootObjects():
        return 2
    window = engine.rootObjects()[0]
    item = window.findChild(PaintedStudy)
    record = {'platform': platform.platform(), 'python': sys.version, 'pyside': PySide6.__version__,
              'qt_platform': app.platformName(), 'explicit_setActive': True,
              'uia_skipped_by_request': args.skip_uia,
              'has_QAccessibleTableInterface': hasattr(QtGui, 'QAccessibleTableInterface'),
              'has_tableInterface_accessor': hasattr(QtGui.QAccessibleInterface, 'tableInterface'),
              'interface_cast_signature': 'QtGui.pyi: interface_cast(InterfaceType) -> int',
              'factory_calls': FACTORY_CALLS, 'keyboard_observations': [],
              'limitations': ['No C++ table adapter', 'No virtual row/cell semantics', 'No NVDA/Orca run',
                              '64 synthetic logical rows only', 'No performance, CPU/GPU, memory or reference-host acceptance']}
    fixture_bytes = '\n'.join(f'{i}\t{item.raw(i)}' for i in range(1, 65)).encode('utf-8')
    record['fixture'] = {'source': 'probe.py deterministic 64-row generator; tab-separated ID/raw ASS, LF, UTF-8, no final LF',
                         'bytes': len(fixture_bytes), 'sha256': hashlib.sha256(fixture_bytes).hexdigest(),
                         'probe_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    observer = None

    def key(key_code, modifiers=Qt.KeyboardModifier.NoModifier):
        item.forceActiveFocus()
        for event_type in [QKeyEvent.Type.KeyPress, QKeyEvent.Type.KeyRelease]:
            app.sendEvent(window, QKeyEvent(event_type, key_code, modifiers))

    def observe(label, expected_current, expected_selection):
        record['keyboard_observations'].append({'case': label, 'displayed': len(item.order), 'current_id': item.current,
            'selected_ids': sorted(item.selected), 'active_focus': item.hasActiveFocus(),
            'expected_current': expected_current, 'expected_selection': sorted(expected_selection),
            'matched': item.current == expected_current and item.selected == set(expected_selection)})

    def run_probe():
        nonlocal observer
        interface = QAccessible.queryAccessibleInterface(item)
        record['qt_interface'] = {'python_type': type(interface).__name__, 'role': interface.role().name,
            'name': interface.text(QAccessible.Text.Name), 'child_count': interface.childCount(),
            'table_interface_pointer': interface.interface_cast(QAccessible.InterfaceType.TableInterface)}
        item.reverseOrder()
        item.filterRows('RTL')
        key(Qt.Key.Key_Home)
        observe('Reversed RTL Home chooses first displayed ID', 63, {63})
        key(Qt.Key.Key_End, Qt.KeyboardModifier.ControlModifier)
        observe('Ctrl End moves current while retaining selected ID', 3, {63})
        key(Qt.Key.Key_Home, Qt.KeyboardModifier.ShiftModifier)
        observe('Shift Home extends using stable source-ID anchor', 63, {63})
        key(Qt.Key.Key_Down, Qt.KeyboardModifier.ShiftModifier)
        observe('Shift Down extends in displayed order', 60, {63, 60})
        item.filterRows('missing')
        key(Qt.Key.Key_End)
        observe('Empty result does not discard current or selection', 60, {63, 60})
        item.filterRows('')
        item.toggleTags()
        record['tag_hidden'] = item.hidden
        record['restored_displayed_rows'] = len(item.order)
        if sys.platform == 'win32' and not args.skip_uia:
            observer = subprocess.Popen(['powershell.exe', '-NoProfile', '-File',
                str(HERE / 'uia-observe.ps1'), '-TargetProcessId', str(os.getpid()), '-OutputPath', str(output / 'uia.json')],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
        QTimer.singleShot(500, finish_probe)

    attempts = 0

    def finish_probe():
        nonlocal attempts
        attempts += 1
        if observer and observer.poll() is None and attempts < 30:
            QTimer.singleShot(500, finish_probe)
            return
        if observer:
            if observer.poll() is None:
                observer.terminate()
                record['uia_observer'] = 'Timed out; unavailable, not passed'
            else:
                stdout, stderr = observer.communicate()
                record['uia_exit_code'] = observer.returncode
                record['uia_stderr'] = stderr.decode(errors='replace')
                if observer.returncode == 0:
                    record['uia'] = json.loads((output / 'uia.json').read_text(encoding='utf-8-sig'))
        (output / 'observed.json').write_text(json.dumps(record, indent=2, ensure_ascii=False), encoding='utf-8')
        print(json.dumps(record, indent=2, ensure_ascii=True))
        app.quit()

    if args.probe:
        QTimer.singleShot(800, run_probe)
    result = app.exec()
    return result


if __name__ == '__main__':
    raise SystemExit(main())
