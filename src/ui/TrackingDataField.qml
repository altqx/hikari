pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Tracking"
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: field
    required property var app
    required property var host
    property string title
    property alias text: area.text
    property alias area: area
    property int boxHeight: 236
    property string statusKind: "idle"
    property string statusText
    property string dataObjectName
    property string statusObjectName
    property string pasteObjectName
    property string openObjectName
    property string loadError: ""
    spacing: 6
    Layout.preferredWidth: 400

    function load(url) {
        const read = app.readTrackingFile(url)
        if (read.error !== undefined) {
            loadError = qsTr("Could not read the file: %1").arg(read.error)
            return
        }
        area.text = read.text
        loadError = ""
    }

    RowLayout {
        spacing: 2
        Label {
            text: field.title
            font: field.host.fontAt(12, Font.DemiBold)
            Layout.fillWidth: true
        }
        IconButton {
            objectName: field.pasteObjectName
            iconRole: "edit-paste"
            text: qsTr("Paste")
            padding: 3
            onClicked: {
                area.clear()
                area.paste()
                area.forceActiveFocus()
            }
        }
        IconButton {
            objectName: field.openObjectName
            iconRole: "folder-open"
            text: qsTr("Open file…")
            padding: 3
            onClicked: picker.open()
        }
    }
    Item {
        implicitWidth: 400
        implicitHeight: field.boxHeight
        Layout.fillWidth: true
        ScrollView {
            anchors.fill: parent
            TextArea {
                id: area
                objectName: field.dataObjectName
                textFormat: TextEdit.PlainText
                selectByMouse: true
                font.family: "monospace"
                Accessible.name: field.title
                Accessible.description: statusLabel.text
                background: Rectangle {
                    color: Theme.field
                    border.width: 1
                    border.color: parent.activeFocus ? Theme.focus : Theme.line
                }
                onTextChanged: field.loadError = ""
            }
        }
    }
    RowLayout {
        id: statusRow
        spacing: 6
        Layout.fillWidth: true
        Layout.minimumHeight: 34
        readonly property string kind: field.loadError.length > 0 ? "error" : field.statusKind
        readonly property color tint: kind === "error" ? Theme.danger : kind === "warning" ? Theme.warning
                                      : kind === "idle" ? Theme.muted : Theme.text
        Icon {
            iconRole: ({ ok: "run-applied", warning: "notice-warning", error: "notice-error" })[statusRow.kind] || "notice-info"
            size: 14
            color: statusRow.tint
            accentColor: statusRow.kind === "ok" ? Theme.accent : statusRow.tint
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
        }
        Label {
            id: statusLabel
            objectName: field.statusObjectName
            text: field.loadError.length > 0 ? field.loadError : field.statusText
            color: statusRow.tint
            wrapMode: Text.Wrap
            font: field.host.fontAt(12)
            Layout.fillWidth: true
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }
    FileChooser {
        id: picker
        title: qsTr("Open tracking data")
        nameFilters: [qsTr("Tracking data (*.txt)"), qsTr("All files (*)")]
        onAccepted: field.load(selectedFile)
    }
}
