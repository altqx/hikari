pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// WS3 (#217): Save layout as…: the current Document tab's arrangement under
// a name, a saved layout every tab can use. The name starts as the tab's
// saved layout's, so saving again replaces it; a name in use (in any case)
// says so and the button reads Replace. A preset's name, an empty name or one
// too long is refused with the reason under the field.
Dialog {
    id: dialog
    objectName: "saveLayoutDialog"
    required property var layouts // WorkspaceLayoutController
    title: qsTr("Save layout")
    header: IconDialogHeader { iconRole: "layout-save"; text: dialog.title }
    modal: true
    anchors.centerIn: parent
    // Both read the saved layouts too: re-evaluated when they change, not
    // only when the name does (the dialog opens again on the same name).
    readonly property string problem: layouts.savedLayouts && layouts.layoutNameProblem(nameField.text)
    readonly property string existing: layouts.savedLayouts && layouts.savedLayoutNamed(nameField.text)

    function show() {
        nameField.text = layouts.layout
        open()
        nameField.selectAll()
        nameField.forceActiveFocus()
    }
    onAccepted: layouts.saveLayoutAs(nameField.text)

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        Label {
            text: qsTr("Name:")
        }
        TextField {
            id: nameField
            objectName: "saveLayoutName"
            Layout.fillWidth: true
            Layout.preferredWidth: 280
            maximumLength: 64 // WorkspaceLayoutController::kMaximumNameLength
            Accessible.name: qsTr("Name:")
            Accessible.description: dialog.problem
            onAccepted: if (dialog.problem.length === 0) dialog.accept()
        }
        Label {
            objectName: "saveLayoutNote"
            // Nothing to say while the field is empty: the button is off.
            text: nameField.text.trim().length === 0 ? ""
                : dialog.problem.length > 0 ? dialog.problem
                : dialog.existing.length > 0 ? qsTr("Saving replaces the layout %1.").arg(dialog.existing) : ""
            color: dialog.problem.length > 0 ? Theme.danger : palette.windowText
            visible: text.length > 0
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.maximumWidth: nameField.width
            Accessible.role: Accessible.AlertMessage
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "saveLayoutSave"
            text: dialog.existing.length > 0 ? qsTr("Replace") : qsTr("Save")
            enabled: dialog.problem.length === 0
            highlighted: true
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            objectName: "saveLayoutCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
