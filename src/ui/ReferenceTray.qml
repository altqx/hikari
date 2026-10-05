import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// R2: the reference tray (docs/qt/ux/translation-comparison.md), standing in
// for legacy's in-Grid subtitles preview (SubsGridPreview at 20d647c4). It
// shows the protected reference's Lines, navigated on their own: clicks and
// keys move the reference's selection, never its content. "Follow the
// editing Line" links it one way to the editing target's active Line, with
// the candidates counted, Previous/Next match and an empty no-match state
// (legacy's nearest Line only on request). The close mark is legacy's X.
// The menu lists every occurrence of the editing Line in the other
// Documents, as the preview's context menu did.
FocusScope {
    id: tray
    required property var app
    required property var shell
    required property var hotkeys
    // Main's root: the Grid's key routing (runGridHotkey).
    property var shellRoot: null
    // The editing Grid: while comparing, the linked reference scrolls with it.
    property Item editingGrid: null
    readonly property alias grid: referenceGrid

    // The Grid's fixed clipboard keys (TabPanel::SetAccels, SubsGridPreview's
    // own Ctrl+C and Ctrl+V).
    function clipboardKey(event) {
        const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
        return mods === Qt.ControlModifier && (event.key === Qt.Key_C || event.key === Qt.Key_X || event.key === Qt.Key_V)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            objectName: "referenceBar"
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.rightMargin: 4
            spacing: 2
            IconToolButton {
                objectName: "referenceLinked"
                iconRole: "link"
                text: qsTr("Follow the editing Line")
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
                    : tray.shell.referenceNoMatch ? qsTr("No matching Line")
                    : tray.shell.referenceMatchCount > 0
                      ? qsTr("Match %1 of %2").arg(tray.shell.referenceMatchIndex + 1).arg(tray.shell.referenceMatchCount)
                    : ""
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
            IconToolButton {
                objectName: "referenceNearest"
                iconRole: "go-to-selection"
                visible: tray.shell.referenceHasNearest
                text: qsTr("Show nearest Line")
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
            IconToolButton {
                objectName: "referenceClose"
                iconRole: "tab-close"
                text: qsTr("Close reference")
                onClicked: tray.app.closeReference()
            }
        }
        HikariGrid {
            id: referenceGrid
            objectName: "referenceGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            Accessible.role: Accessible.Table // in the accessibility tree, as the Grid
            accessibleName: qsTr("Reference Lines (protected, read-only)")
            model: tray.shell.referenceLines
            // The tray's own navigation: every gesture is a request for the
            // reference's selection (G1), never its content.
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
            // Ctrl+C copies the reference's selected Lines (PREVIEW_COPY);
            // Ctrl+V pasted into the previewed grid (PREVIEW_PASTE) and is
            // refused, as the reference is protected. Ctrl+X and the Grid's
            // bindings reached the grid the preview was drawn on (its
            // accelerator table, TabPanel::SetAccels, TabPanel.cpp:100-200):
            // they act on the editing target, never on the reference.
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

    // A seek made a reference Line active: bring it into view (MakeVisible).
    Connections {
        target: tray.shell
        function onReferenceLineShown(id) { referenceGrid.makeLineVisible(id) }
    }
    // Comparing with the linked reference: its first row follows the editing
    // Grid's (ShowSecondComparedLine with setViaScroll).
    Connections {
        target: tray.editingGrid
        enabled: tray.editingGrid !== null && tray.shell.referenceLinked
        function onContentYChanged() {
            const row = tray.app.comparedReferenceRow(Math.floor(tray.editingGrid.contentY / tray.editingGrid.rowHeight))
            if (row >= 0)
                referenceGrid.scrollToTopRow(row)
        }
    }

    // SubsGridPreview::ContextMenu: "SubsName (lineRangeStart lineRangeLen)"
    // for each occurrence, the one shown checked.
    ShellMenu {
        id: occurrenceMenu
        objectName: "referenceOccurrenceMenu"
        Instantiator {
            id: occurrences
            model: []
            delegate: ShellMenuItem {
                required property var modelData
                required property int index
                objectName: "referenceOccurrence" + index
                text: modelData.text
                checkable: true
                checked: modelData.checked
                onTriggered: tray.app.chooseReferenceOccurrence(index)
            }
            onObjectAdded: (index, object) => occurrenceMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => occurrenceMenu.removeItem(object)
        }
    }
}
