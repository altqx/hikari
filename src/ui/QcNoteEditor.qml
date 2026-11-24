pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.QcNotes"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Pane {
    id: editor
    required property var qcNotes
    property bool adding: true
    property int videoMs: -1
    property int lineMs: -1
    readonly property int ms: qcNotes.parseTime(time.text)
    readonly property bool canSave: ms >= 0 && note.text.trim().length > 0
    readonly property font labelFont: TypeRoles.sized(fontSource.font, 12, Font.Normal)
    readonly property font timeFont: {
        const f = TypeRoles.sized(fontSource.font, 12, Font.Normal)
        f.features = { "tnum": 1 }
        return f
    }
    signal save(int ms, string section, string text, bool done)
    signal cancel()
    signal remove()

    function load(ms, section, text, done) {
        videoMs = qcNotes.videoTime()
        lineMs = qcNotes.lineStart()
        time.text = qcNotes.timeText(ms)
        const names = qcNotes.sections.map(s => s.name)
        if (names.indexOf(section) < 0)
            names.push(section)
        sectionBox.model = names
        sectionBox.currentIndex = names.indexOf(section)
        sectionBox.editText = section
        note.text = text
        doneBox.checked = done
        note.forceActiveFocus()
    }
    function trySave() {
        if (canSave)
            save(ms, sectionBox.editText, note.text, doneBox.checked)
    }

    padding: 8
    background: Rectangle { color: Theme.raised; border.color: Theme.line; radius: Theme.radius }
    Keys.onPressed: event => {
        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
            editor.trySave()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            editor.cancel()
            event.accepted = true
        }
    }

    Label {
        id: fontSource
        visible: false
    }
    TextMetrics {
        id: timeMetrics
        font: editor.timeFont
        text: "0:00:00.00"
    }

    GridLayout {
        anchors.fill: parent
        columns: 2
        columnSpacing: 8
        rowSpacing: 8
        Label {
            text: qsTr("Time")
            font: editor.labelFont
            color: Theme.muted
            Layout.alignment: Qt.AlignVCenter
        }
        RowLayout {
            spacing: 8
            TextField {
                id: time
                objectName: "qcNoteTime"
                font: editor.timeFont
                implicitHeight: Theme.controlHeight
                topPadding: 0
                bottomPadding: 0
                Accessible.name: qsTr("Time")
                color: editor.ms >= 0 ? Theme.text : Theme.danger
                Layout.preferredWidth: Math.ceil(timeMetrics.advanceWidth) + leftPadding + rightPadding + 2
                Keys.forwardTo: [editor]
            }
            LinkButton {
                objectName: "qcNoteVideoTime"
                iconRole: "video-seek"
                text: qsTr("Video time")
                enabled: editor.videoMs >= 0
                onClicked: time.text = editor.qcNotes.timeText(editor.qcNotes.videoTime())
            }
            LinkButton {
                objectName: "qcNoteLineStart"
                iconRole: "set-start-time"
                text: qsTr("Line start")
                enabled: editor.lineMs >= 0
                onClicked: time.text = editor.qcNotes.timeText(editor.qcNotes.lineStart())
            }
        }
        Label {
            text: qsTr("Section")
            font: editor.labelFont
            color: Theme.muted
            Layout.alignment: Qt.AlignVCenter
        }
        ComboBox {
            id: sectionBox
            objectName: "qcNoteSection"
            editable: true
            font: fontSource.font
            implicitHeight: Theme.controlHeight
            Accessible.name: qsTr("Section")
            Layout.fillWidth: true
            Keys.forwardTo: [editor]
        }
        Label {
            text: qsTr("Note")
            font: editor.labelFont
            color: Theme.muted
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: note.topPadding
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(Math.max(note.implicitHeight, 56), 140)
            TextArea {
                id: note
                objectName: "qcNoteText"
                wrapMode: TextEdit.Wrap
                placeholderText: qsTr("What needs fixing")
                Accessible.name: qsTr("Note")
                Keys.forwardTo: [editor]
            }
        }
        Item { implicitWidth: 1 }
        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            CheckBox {
                id: doneBox
                objectName: "qcNoteDoneBox"
                visible: !editor.adding
                text: qsTr("Done")
                font: fontSource.font
                implicitHeight: Theme.controlHeight
                topPadding: 0
                bottomPadding: 0
            }
            DangerButton {
                objectName: "qcNoteDelete"
                visible: !editor.adding
                text: qsTr("Delete")
                onClicked: editor.remove()
            }
            Label {
                visible: editor.adding
                text: qsTr("Ctrl+Enter saves · Esc cancels")
                font: editor.labelFont
                color: Theme.muted
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Item { Layout.fillWidth: !editor.adding }
            Button {
                objectName: "qcNoteCancel"
                text: qsTr("Cancel")
                onClicked: editor.cancel()
            }
            PrimaryButton {
                objectName: "qcNoteSave"
                text: editor.adding ? qsTr("Add note") : qsTr("Save")
                enabled: editor.canSave
                onClicked: editor.trySave()
            }
        }
    }
}
