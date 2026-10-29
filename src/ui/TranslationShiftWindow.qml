pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Translation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: shift
    objectName: "translationShiftWindow"
    required property var app
    dialogId: "translationShift"
    title: qsTr("Translation matching options")
    defaultButton: closeButton

    Rectangle {
        objectName: "translationShiftHelp"
        color: Theme.raised
        border.color: Theme.line
        implicitHeight: help.implicitHeight + 16
        Layout.fillWidth: true
        Layout.preferredWidth: 512
        ColumnLayout {
            id: help
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4
            Label {
                text: qsTr("<b>Original</b>: the text with the right timing. The pasted dialogue is matched to it, and it is deleted later.")
                textFormat: Text.StyledText
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("<b>Translation</b>: the text pasted in, moved line by line until each one sits beside its original.")
                textFormat: Text.StyledText
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }
    GridLayout {
        columns: 2
        columnSpacing: 20
        rowSpacing: 12
        uniformCellWidths: true
        Layout.fillWidth: true
        Repeater {
            model: [qsTr("Original"), qsTr("Translation")]
            Label {
                required property string modelData
                text: modelData
                color: Theme.muted
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
        }
        Repeater {
            // MUST KILL: mode is a magic int 0-5 for Application::shiftTranslation, listed out of order for the two columns
            model: [
                { mode: 2, name: qsTr("Add line"), hint: qsTr("A blank line before the selected one; the original moves down. Time the new line.") },
                { mode: 3, name: qsTr("Add line"), hint: qsTr("A blank line before the selected one; the translation moves down.") },
                { mode: 4, name: qsTr("Join lines"), hint: qsTr("Joins the selected line with the next one; the original moves up.") },
                { mode: 1, name: qsTr("Join lines"), hint: qsTr("Joins the selected line with the next one; the translation moves up.") },
                { mode: 5, name: qsTr("Delete line"), hint: qsTr("Deletes the selected line; the original moves up.") },
                { mode: 0, name: qsTr("Delete line"), hint: qsTr("Deletes the selected line; the translation moves up.") }
            ]
            ColumnLayout {
                id: cell
                required property var modelData
                spacing: 4
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Button {
                    objectName: "translationMove" + cell.modelData.mode
                    text: cell.modelData.name
                    Accessible.description: cell.modelData.hint
                    Layout.minimumWidth: 96
                    onClicked: shift.app.shiftTranslation(cell.modelData.mode)
                }
                Label {
                    text: cell.modelData.hint
                    color: Theme.muted
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
        }
    }

    buttons: [
        PrimaryButton {
            id: closeButton
            objectName: "translationShiftClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: shift.close()
        }
    ]
}
