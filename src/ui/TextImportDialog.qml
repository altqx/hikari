pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Documents"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "textImportDialog"
    dialogId: "textImport"
    required property var controller
    signal chooseAnother()
    title: qsTr("Import text")
    modal: true
    defaultButton: importButton

    GridLayout {
        columns: 2
        columnSpacing: 12
        rowSpacing: 4
        Layout.fillWidth: true
        Layout.preferredWidth: 600
        Label { text: qsTr("File") }
        Label {
            objectName: "textImportFile"
            text: dialog.controller.fileName
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        }
        Label { text: qsTr("Actor separator") }
        TextField {
            objectName: "textImportActorSeparator"
            text: dialog.controller.actorSeparator
            maximumLength: 16
            placeholderText: qsTr("None")
            Layout.preferredWidth: 120
            Accessible.name: qsTr("Actor separator")
            onTextEdited: dialog.controller.actorSeparator = text
        }
        Label { text: qsTr("Comment starter") }
        TextField {
            objectName: "textImportCommentStarter"
            text: dialog.controller.commentStarter
            maximumLength: 16
            placeholderText: qsTr("None")
            Layout.preferredWidth: 120
            Accessible.name: qsTr("Comment starter")
            onTextEdited: dialog.controller.commentStarter = text
        }
    }
    CheckBox {
        objectName: "textImportKeepBlank"
        text: qsTr("Keep blank lines as comment lines")
        checked: dialog.controller.keepBlankLines
        onToggled: dialog.controller.keepBlankLines = checked
    }
    CheckBox {
        objectName: "textImportCarryOver"
        text: qsTr("A line without an actor keeps the last one")
        checked: dialog.controller.namesCarryOver
        onToggled: dialog.controller.namesCarryOver = checked
    }

    FieldError {
        objectName: "textImportProblem"
        text: dialog.controller.problem
        Layout.fillWidth: true
    }

    MenuHeading {
        text: qsTr("Preview")
        visible: dialog.controller.canImport
        leftPadding: 0
    }
    Repeater {
        model: dialog.controller.preview
        delegate: RowLayout {
            id: previewRow
            required property var modelData
            objectName: "textImportPreviewRow"
            spacing: 12
            Layout.fillWidth: true
            Label {
                text: previewRow.modelData.name
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: Theme.muted
                Layout.preferredWidth: 120
            }
            Label {
                text: previewRow.modelData.comment ? qsTr("Comment: %1").arg(previewRow.modelData.text)
                                                   : previewRow.modelData.text
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: previewRow.modelData.comment ? Theme.muted : Theme.text
                Layout.fillWidth: true
            }
        }
    }
    Label {
        objectName: "textImportMore"
        visible: dialog.controller.canImport && dialog.controller.lineCount > dialog.controller.preview.length
        text: qsTr("and %n more lines", "", dialog.controller.lineCount - dialog.controller.preview.length)
        color: Theme.muted
    }

    MenuHeading {
        text: qsTr("Add the lines to")
        leftPadding: 0
    }
    RadioButton {
        objectName: "textImportIntoOpen"
        text: qsTr("After the last line of the open file")
        enabled: dialog.controller.openFileAvailable
        checked: dialog.controller.intoOpenFile
        onToggled: dialog.controller.intoOpenFile = checked
    }
    RadioButton {
        objectName: "textImportIntoNew"
        text: qsTr("A new file")
        checked: !dialog.controller.intoOpenFile
        onToggled: dialog.controller.intoOpenFile = !checked
    }
    RowLayout {
        visible: dialog.controller.intoOpenFile
        spacing: 8
        Layout.leftMargin: 24
        Label { text: qsTr("Style") }
        ComboBox {
            objectName: "textImportStyle"
            model: dialog.controller.styles
            currentIndex: dialog.controller.styles.indexOf(dialog.controller.style)
            onActivated: index => dialog.controller.style = dialog.controller.styles[index]
            Layout.preferredWidth: 220
        }
    }

    leadingActions: [
        Button {
            objectName: "textImportChooseAnother"
            text: qsTr("Choose another file…")
            onClicked: {
                dialog.reject()
                dialog.chooseAnother()
            }
        }
    ]
    buttons: [
        PrimaryButton {
            id: importButton
            objectName: "textImportImport"
            text: qsTr("Import")
            enabled: dialog.controller.canImport
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: if (dialog.controller.import()) dialog.accept()
        },
        Button {
            objectName: "textImportCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.reject()
        }
    ]
}
