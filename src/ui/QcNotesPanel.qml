pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.QcNotes"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: panel
    objectName: "qcNotesTool"
    required property var qcNotes
    property var messages: null
    signal escaped()

    readonly property size minimumBodySize: Qt.size(300, 220)
    readonly property var notes: qcNotes.notes
    readonly property int noEditor: -2
    readonly property int newNote: -1
    property int editing: noEditor
    property int chosen: -1
    readonly property int chosenRow: notes.findIndex(n => n.index === chosen)
    property string message: ""
    property bool exporting: false
    property string importName: ""

    function say(text) {
        message = text
        messageTimer.restart()
    }
    function choose(index) {
        chosen = index
    }
    function goTo(index) {
        chosen = index
        qcNotes.goTo(index)
    }
    function step(forward) {
        if (forward ? qcNotes.next() : qcNotes.previous()) {
            chosen = qcNotes.current
            return true
        }
        say(qsTr("No more notes"))
        return false
    }
    function startAdding(ms) {
        exporting = false
        editing = newNote
        editor.load(ms === undefined || ms < 0 ? qcNotes.addTime() : ms, qcNotes.lastSection, "", false)
    }
    function startEditing(index) {
        const note = notes.find(n => n.index === index)
        if (!note || !qcNotes.editable)
            return
        exporting = false
        chosen = index
        editing = index
        editor.load(note.time, note.section, note.text, note.done)
    }
    function stopEditing() {
        editing = noEditor
        list.forceActiveFocus()
    }
    function remove(index) {
        if (qcNotes.remove(index)) {
            if (editing === index)
                editing = noEditor
            list.forceActiveFocus()
        }
    }
    function toggleDone(index) {
        const note = notes.find(n => n.index === index)
        if (note)
            qcNotes.setDone(index, !note.done)
    }
    function importText(text, name) {
        const preview = qcNotes.preview(text)
        if (preview.count === 0) {
            say(name.length > 0 ? qsTr("%1 has no notes. A note is a line that starts with a time, like 3:24 or 0:03:24.").arg(name)
                                : qsTr("The clipboard has no notes. A note is a line that starts with a time, like 3:24 or 0:03:24."))
            return
        }
        const run = replace => {
            const result = qcNotes.importNotes(text, replace)
            if (result.added === undefined)
                return
            let said = qsTr("Imported %n note(s).", "", result.added)
            if (result.skipped > 0)
                said += " " + qsTr("%n were already in the file.", "", result.skipped)
            const lineless = qcNotes.notes.filter(n => n.row === 0).length
            if (lineless > 0)
                said += " " + qsTr("%n note(s) have no line within 15 seconds of their time.", "", lineless)
            say(said)
        }
        if (preview.existing === 0 || !messages) {
            run(false)
            return
        }
        const source = name.length > 0 ? name : qsTr("The clipboard")
        messages.ask({
            title: qsTr("Import QC notes"),
            text: qsTr("%1 has %n note(s)", "", preview.count).arg(source) + " "
                  + qsTr("in %n section(s).", "", preview.sections) + " "
                  + qsTr("This file already has %n note(s).", "", preview.existing),
            detail: preview.duplicates > 0 ? qsTr("Adding skips %n note(s) that are already here.", "", preview.duplicates) : "",
            choices: [{ id: "add", text: qsTr("Add %n note(s)", "", preview.count - preview.duplicates), isDefault: true },
                      { id: "replace", text: qsTr("Replace the notes in this file"), role: "destructive" },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject" }]
        }, id => {
            if (id === "add" || id === "replace")
                run(id === "replace")
        })
    }
    function importFile(url) {
        const read = qcNotes.readFile(url)
        const name = decodeURIComponent(url.toString().split("/").pop())
        if (read.error !== undefined) {
            say(qsTr("%1 could not be read: %2").arg(name, read.error))
            return
        }
        importText(read.text, name)
    }
    function openImport() {
        importPicker.open()
    }
    function importClipboard() {
        importText(qcNotes.clipboardText(), "")
    }
    function openExport() {
        editing = noEditor
        exporting = true
    }
    function removeAll() {
        if (!messages) {
            qcNotes.removeAll()
            return
        }
        messages.ask({
            title: qsTr("Remove all notes"),
            text: qsTr("Remove all %n QC note(s) from this file?", "", qcNotes.total),
            detail: qsTr("Undo brings them back."),
            severity: "warning",
            choices: [{ id: "remove", text: qsTr("Remove all"), role: "destructive" },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
        }, id => {
            if (id === "remove")
                qcNotes.removeAll()
        })
    }

    Timer {
        id: messageTimer
        interval: 8000
        onTriggered: panel.message = ""
    }
    Connections {
        target: panel.qcNotes
        function onChanged() {
            if (panel.editing >= 0 && !panel.notes.some(n => n.index === panel.editing))
                panel.editing = noEditor
        }
    }

    FileChooser {
        id: importPicker
        objectName: "qcNotesImportPicker"
        title: qsTr("Import notes file")
        nameFilters: [qsTr("QC notes (*.txt)"), qsTr("All files (*)")]
        onAccepted: panel.importFile(selectedFile)
    }
    FileChooser {
        id: exportPicker
        objectName: "qcNotesExportPicker"
        fileMode: Dialogs.FileDialog.SaveFile
        title: qsTr("Export QC notes")
        defaultSuffix: "txt"
        nameFilters: [qsTr("QC notes (*.txt)")]
        onAccepted: {
            const text = panel.qcNotes.exportText(includeDone.checked, onlySection.checked ? panel.qcNotes.section : "")
            if (panel.qcNotes.writeFile(selectedFile, text)) {
                panel.exporting = false
                panel.say(qsTr("Exported %n note(s).", "", exportCount.count))
            } else {
                panel.say(qsTr("The notes could not be written."))
            }
        }
    }

    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        enabled: panel.qcNotes.editable
        onDropped: drop => {
            if (drop.hasUrls && drop.urls.length > 0) {
                drop.accept(Qt.CopyAction)
                panel.importFile(drop.urls[0])
            }
        }
        Rectangle {
            anchors.fill: parent
            visible: parent.containsDrag
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: panel.qcNotes.hasDocument
            Label {
                objectName: "qcNotesCount"
                text: qsTr("In this file") + " · " + qsTr("%n note(s)", "", panel.qcNotes.total)
                color: Theme.muted
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            IconTextButton {
                objectName: "qcNotesAdd"
                iconRole: "add"
                text: qsTr("Add note")
                enabled: panel.qcNotes.editable && panel.editing !== newNote
                onClicked: panel.startAdding()
            }
        }

        Pane {
            id: exportBar
            objectName: "qcNotesExportBar"
            visible: panel.exporting
            Layout.fillWidth: true
            padding: 8
            background: Rectangle { color: Theme.raised; border.color: Theme.line; radius: 4 }
            ColumnLayout {
                anchors.fill: parent
                spacing: 4
                Label { text: qsTr("Export QC notes"); font.bold: true }
                CheckBox {
                    id: includeDone
                    objectName: "qcNotesExportDone"
                    text: qsTr("Include done notes")
                }
                CheckBox {
                    id: onlySection
                    objectName: "qcNotesExportSection"
                    enabled: panel.qcNotes.section.length > 0
                    checked: enabled
                    text: enabled ? qsTr("Only the chosen section (%1)").arg(panel.qcNotes.section) : qsTr("Only the chosen section")
                }
                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    Button {
                        text: qsTr("Cancel")
                        onClicked: panel.exporting = false
                    }
                    PrimaryButton {
                        id: exportCount
                        objectName: "qcNotesExportRun"
                        readonly property int count: panel.qcNotes.sections
                            .filter(s => !onlySection.checked || s.name === panel.qcNotes.section)
                            .reduce((sum, s) => sum + (includeDone.checked ? s.count : s.open), 0)
                        enabled: count > 0
                        text: qsTr("Export %n note(s)…", "", count)
                        onClicked: exportPicker.open()
                    }
                }
            }
        }

        QcNoteEditor {
            id: editor
            objectName: "qcNotesAddEditor"
            qcNotes: panel.qcNotes
            visible: panel.editing === newNote
            adding: true
            Layout.fillWidth: true
            onSave: (ms, section, text, done) => {
                if (panel.qcNotes.add(ms, section, text)) {
                    panel.chosen = panel.qcNotes.current
                    panel.stopEditing()
                }
            }
            onCancel: panel.stopEditing()
        }

        Flow {
            objectName: "qcNotesSections"
            Layout.fillWidth: true
            spacing: 6
            visible: panel.qcNotes.total > 0
            FilterChip {
                objectName: "qcNotesSectionAll"
                text: qsTr("All") + "  " + panel.qcNotes.total
                on: panel.qcNotes.section.length === 0
                onClicked: panel.qcNotes.section = ""
            }
            Repeater {
                model: panel.qcNotes.sections
                FilterChip {
                    required property var modelData
                    objectName: "qcNotesSection_" + modelData.name
                    text: modelData.name + "  " + (panel.qcNotes.hideDone && modelData.open !== modelData.count
                                                   ? qsTr("%1 of %2").arg(modelData.open, modelData.count) : modelData.count)
                    on: panel.qcNotes.section === modelData.name
                    onClicked: panel.qcNotes.section = on ? "" : modelData.name
                }
            }
        }

        ListView {
            id: list
            objectName: "qcNotesList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            visible: panel.qcNotes.total > 0
            model: panel.notes
            currentIndex: panel.chosenRow
            keyNavigationEnabled: false
            spacing: 2
            ScrollBar.vertical: ScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: qsTr("QC notes")
            Keys.onPressed: event => {
                const row = panel.chosenRow
                const at = r => panel.notes[Math.max(0, Math.min(panel.notes.length - 1, r))]
                if (panel.notes.length === 0)
                    return
                if (event.key === Qt.Key_Down || event.key === Qt.Key_Up) {
                    panel.choose(at(row < 0 ? 0 : row + (event.key === Qt.Key_Down ? 1 : -1)).index)
                } else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && row >= 0) {
                    panel.goTo(panel.chosen)
                } else if (event.key === Qt.Key_Space && row >= 0) {
                    panel.toggleDone(panel.chosen)
                } else if (event.key === Qt.Key_F2 && row >= 0) {
                    panel.startEditing(panel.chosen)
                } else if (event.key === Qt.Key_Delete && row >= 0) {
                    panel.remove(panel.chosen)
                } else if (event.key === Qt.Key_Escape) {
                    panel.escaped()
                } else {
                    return
                }
                event.accepted = true
            }
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool isEditing: panel.editing === modelData.index
                readonly property bool isCurrent: panel.qcNotes.current === modelData.index
                width: ListView.view.width
                implicitHeight: isEditing ? rowEditor.implicitHeight : noteRow.implicitHeight + 8
                objectName: "qcNote_" + index
                Accessible.role: Accessible.ListItem
                Accessible.name: [modelData.timeText, modelData.section,
                                  modelData.row > 0 ? qsTr("line %1").arg(modelData.row) : qsTr("no line"),
                                  modelData.text, modelData.done ? qsTr("done") : qsTr("not done")].join(", ")
                Rectangle {
                    anchors.fill: parent
                    visible: !row.isEditing
                    radius: 3
                    color: row.ListView.isCurrentItem ? Theme.select : mouse.containsMouse ? Theme.raised : "transparent"
                    border.width: row.isCurrent ? 1 : 0
                    border.color: Theme.accent
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    visible: !row.isEditing
                    hoverEnabled: true
                    onClicked: {
                        list.forceActiveFocus()
                        panel.goTo(row.modelData.index)
                    }
                    onDoubleClicked: panel.startEditing(row.modelData.index)
                }
                RowLayout {
                    id: noteRow
                    visible: !row.isEditing
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 8
                    opacity: row.modelData.done ? 0.55 : 1
                    CheckBox {
                        objectName: "qcNoteDone"
                        checked: row.modelData.done
                        enabled: panel.qcNotes.editable
                        focusPolicy: Qt.NoFocus
                        Accessible.name: qsTr("Done")
                        onClicked: panel.qcNotes.setDone(row.modelData.index, checked)
                        Layout.alignment: Qt.AlignTop
                    }
                    Label {
                        text: row.modelData.timeText
                        font.family: "monospace"
                        Layout.alignment: Qt.AlignTop
                    }
                    Label {
                        text: row.modelData.section
                        visible: panel.qcNotes.section.length === 0
                        color: Theme.muted
                        Layout.alignment: Qt.AlignTop
                    }
                    Label {
                        text: row.modelData.text
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        font.strikeout: row.modelData.done
                        Layout.fillWidth: true
                    }
                    Label {
                        text: row.modelData.row > 0 ? "#" + row.modelData.row : qsTr("No line")
                        color: Theme.muted
                        Layout.alignment: Qt.AlignTop
                    }
                }
                QcNoteEditor {
                    id: rowEditor
                    visible: row.isEditing
                    width: parent.width
                    qcNotes: panel.qcNotes
                    adding: false
                    onSave: (ms, section, text, done) => {
                        if (panel.qcNotes.edit(row.modelData.index, ms, section, text, done)) {
                            panel.chosen = panel.qcNotes.current
                            panel.stopEditing()
                        }
                    }
                    onCancel: panel.stopEditing()
                    onRemove: panel.remove(row.modelData.index)
                    Component.onCompleted: if (row.isEditing) load(row.modelData.time, row.modelData.section,
                                                                    row.modelData.text, row.modelData.done)
                }
                onIsEditingChanged: if (isEditing) rowEditor.load(modelData.time, modelData.section, modelData.text, modelData.done)
            }
        }

        ColumnLayout {
            objectName: "qcNotesEmpty"
            visible: panel.qcNotes.total === 0 && panel.editing !== newNote
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            Item { Layout.fillHeight: true }
            Label {
                text: panel.qcNotes.hasDocument ? qsTr("Drop a notes file here") : qsTr("Open a subtitle file to keep QC notes in it.")
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            RowLayout {
                visible: panel.qcNotes.hasDocument
                Layout.alignment: Qt.AlignHCenter
                Button {
                    objectName: "qcNotesEmptyImport"
                    text: qsTr("Import notes file…")
                    enabled: panel.qcNotes.editable
                    onClicked: panel.openImport()
                }
                Button {
                    objectName: "qcNotesEmptyPaste"
                    text: qsTr("Paste notes")
                    enabled: panel.qcNotes.editable && panel.qcNotes.clipboardHasNotes
                    onClicked: panel.importClipboard()
                }
            }
            Label {
                visible: panel.qcNotes.hasDocument
                text: qsTr("Notes start with a time: 3:24 - …\n[Section] lines group them.")
                color: Theme.muted
                horizontalAlignment: Text.AlignHCenter
                Layout.alignment: Qt.AlignHCenter
            }
            Item { Layout.fillHeight: true }
        }

        Label {
            objectName: "qcNotesMessage"
            visible: panel.message.length > 0
            text: panel.message
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: panel.qcNotes.total > 0
            IconToolButton {
                objectName: "qcNotesPrevious"
                iconRole: "chevron-left"
                text: qsTr("Previous")
                onClicked: panel.step(false)
            }
            IconToolButton {
                objectName: "qcNotesNext"
                iconRole: "chevron-right"
                text: qsTr("Next")
                onClicked: panel.step(true)
            }
            CheckBox {
                objectName: "qcNotesHideDone"
                text: qsTr("Hide done")
                checked: panel.qcNotes.hideDone
                onToggled: panel.qcNotes.hideDone = checked
            }
            Item { Layout.fillWidth: true }
            Label {
                objectName: "qcNotesPosition"
                color: Theme.muted
                text: panel.chosenRow >= 0 ? qsTr("%1 of %2").arg(panel.chosenRow + 1, panel.notes.length)
                                           : qsTr("%n note(s)", "", panel.notes.length)
            }
        }
    }
}
