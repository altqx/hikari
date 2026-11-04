pragma Translator: "HikariSub.Reference"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: tray
    required property var app
    required property var shell
    required property var hotkeys
    property var shellRoot: null
    property Item editingGrid: null
    readonly property alias grid: referenceGrid

    function clipboardKey(event) {
        const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
        return mods === Qt.ControlModifier && (event.key === Qt.Key_C || event.key === Qt.Key_X || event.key === Qt.Key_V)
    }

    readonly property Item toolbar: referenceBar
    RowLayout {
        id: referenceBar
        objectName: "referenceBar"
        // MUST KILL: referenceBar shows only once the dock header reparents it
        visible: parent !== tray
        spacing: 2
        IconToolButton {
            objectName: "referenceLinked"
            iconRole: "link"
            text: qsTr("Follow the editing line")
            checkable: true
            checked: tray.shell.referenceLinked
            onToggled: tray.app.setReferenceLinked(checked)
            Accessible.checkable: true
            Accessible.checked: checked
        }
        Label {
            objectName: "referenceMatchStatus"
            Layout.fillWidth: true
            elide: Text.ElideRight
            text: !tray.shell.referenceLinked ? qsTr("Independent navigation")
                : tray.shell.referenceNoMatch ? qsTr("No matching line")
                : tray.shell.referenceMatchCount > 0
                  ? qsTr("Match %1 of %2").arg(tray.shell.referenceMatchIndex + 1, tray.shell.referenceMatchCount)
                : ""
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
        IconToolButton {
            objectName: "referenceNearest"
            iconRole: "go-to-selection"
            visible: tray.shell.referenceHasNearest
            text: qsTr("Show nearest line")
            onClicked: tray.app.showNearestReferenceLine()
        }
        IconToolButton {
            objectName: "referencePreviousMatch"
            iconRole: "match-previous"
            text: qsTr("Previous match")
            enabled: tray.shell.referenceLinked && tray.shell.referenceMatchIndex > 0
            onClicked: tray.app.stepReferenceMatch(-1)
        }
        IconToolButton {
            objectName: "referenceNextMatch"
            iconRole: "match-next"
            text: qsTr("Next match")
            enabled: tray.shell.referenceLinked && tray.shell.referenceMatchIndex + 1 < tray.shell.referenceMatchCount
            onClicked: tray.app.stepReferenceMatch(1)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        HikariGrid {
            id: referenceGrid
            objectName: "referenceGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            Accessible.role: Accessible.Table
            accessibleName: qsTr("Reference lines (protected, read-only)")
            model: tray.shell.referenceLines
            columnWidths: tray.shell.gridColumnWidths
            onColumnWidthsEdited: tray.shell.gridColumnWidths = columnWidths
            GridHeaderToolTip { grid: referenceGrid }
            GridScrollBars { grid: referenceGrid }
            onActiveLineRequested: id => tray.app.selectReferenceLine(id)
            onExtendRequested: rows => tray.app.extendReferenceSelection(rows)
            onLineClicked: (id, modifiers) => tray.app.clickReferenceLine(id, modifiers)
            onLineDragged: id => tray.app.dragReferenceSelection(id)
            onSelectAllRequested: tray.app.selectAllReferenceLines()
            onContextMenuRequested: (x, y) => {
                occurrences.model = []
                occurrences.model = tray.app.referenceOccurrences()
                occurrenceMenu.popup(referenceGrid, x, y)
            }
            MenuKey.onRequested: referenceGrid.requestMenu()
            Keys.onShortcutOverride: event => {
                event.accepted = tray.clipboardKey(event) || tray.hotkeys.actionFor(1, event.key, event.modifiers) !== ""
            }
            Keys.onPressed: event => {
                if (tray.clipboardKey(event)) {
                    if (event.key === Qt.Key_C)
                        tray.app.copyReferenceLines()
                    else if (event.key === Qt.Key_V)
                        tray.app.refuseReferenceChange()
                    else
                        tray.app.cutLines()
                    event.accepted = true
                    return
                }
                const action = tray.hotkeys.actionFor(1, event.key, event.modifiers)
                if (action === "" || !tray.shellRoot)
                    return
                tray.shellRoot.runGridHotkey(action)
                event.accepted = true
            }
        }
    }

    Connections {
        target: tray.shell
        function onReferenceLineShown(id) { referenceGrid.makeLineVisible(id) }
    }
    Connections {
        target: tray.editingGrid
        enabled: tray.editingGrid !== null && tray.shell.referenceLinked
        function onContentYChanged() {
            const row = tray.app.comparedReferenceRow(Math.floor(tray.editingGrid.contentY / tray.editingGrid.rowHeight))
            if (row >= 0)
                referenceGrid.scrollToTopRow(row)
        }
    }

    ShellMenu {
        id: occurrenceMenu
        objectName: "referenceOccurrenceMenu"
        readonly property bool found: occurrences.count > 0
        ShellMenuItem {
            objectName: "referenceCopy"
            iconRole: "edit-copy"
            text: qsTr("Copy")
            shortcutText: ShellMenus.keyText("Ctrl+C")
            onTriggered: tray.app.copyReferenceLines()
        }
        MenuSeparator {}
        MenuHeading {
            objectName: "referenceOccurrencesHeading"
            text: qsTr("Also in")
            visible: occurrenceMenu.found
            height: occurrenceMenu.found ? implicitHeight : 0
        }
        Instantiator {
            id: occurrences
            model: []
            delegate: MenuRadioItem {
                required property var modelData
                required property int index
                objectName: "referenceOccurrence" + index
                text: modelData.text
                checked: modelData.checked
                onTriggered: tray.app.chooseReferenceOccurrence(index)
            }
            // MUST KILL: 3 counts Copy, the separator and the heading before the occurrence rows
            onObjectAdded: (index, object) => occurrenceMenu.insertItem(3 + index, object)
            onObjectRemoved: (index, object) => occurrenceMenu.removeItem(object)
        }
        ShellMenuItem {
            objectName: "referenceNoOccurrences"
            text: qsTr("No other occurrences")
            enabled: false
            visible: !occurrenceMenu.found
            height: occurrenceMenu.found ? 0 : implicitHeight
        }
    }
}
