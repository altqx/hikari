pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// WS3 (#217): Manage layouts…: the saved layouts, in the order they were
// saved, renamed or deleted at once (every tab can use them; a tab whose
// arrangement started from one keeps the arrangement under the new name, or
// its preset's once the layout is deleted). A layout this version cannot use
// says why and can be deleted. The list's keys: the arrows choose, F2 goes to
// the name, Delete deletes.
Dialog {
    id: dialog
    objectName: "manageLayoutsDialog"
    required property var layouts // WorkspaceLayoutController
    title: qsTr("Manage layouts")
    header: IconDialogHeader { iconRole: "layout-manage"; text: dialog.title }
    modal: true
    anchors.centerIn: parent
    readonly property var rows: layouts.savedLayouts
    readonly property var current: list.currentIndex >= 0 && list.currentIndex < rows.length ? rows[list.currentIndex] : null
    readonly property string problem: current ? layouts.layoutNameProblem(nameField.text, current.name) : ""
    readonly property bool renamable: current !== null && problem.length === 0 && nameField.text.trim() !== current.name

    function show() {
        list.currentIndex = rows.length > 0 ? Math.max(0, rows.findIndex(r => r.name === layouts.layout)) : -1
        nameField.text = current ? current.name : ""
        open()
        list.forceActiveFocus()
    }
    function rename() {
        if (!renamable)
            return
        const name = nameField.text.trim()
        if (layouts.renameSavedLayout(current.name, name))
            list.currentIndex = rows.findIndex(r => r.name === name)
    }
    function remove() {
        if (!current)
            return
        const at = list.currentIndex
        layouts.deleteSavedLayout(current.name)
        list.currentIndex = Math.min(at, rows.length - 1)
        if (rows.length === 0)
            dialog.close()
    }
    onCurrentChanged: nameField.text = current ? current.name : ""

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        Frame {
            padding: 1
            Layout.fillWidth: true
            Layout.preferredWidth: 320
            Layout.preferredHeight: 200
            ListView {
                id: list
                objectName: "manageLayoutsList"
                anchors.fill: parent
                clip: true
                model: dialog.rows
                keyNavigationEnabled: true
                activeFocusOnTab: true
                highlightMoveDuration: 0
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Saved layouts")
                // Both are global hotkeys' keys too (F2: Select line at
                // current video position): the list takes them first.
                Keys.onShortcutOverride: event => event.accepted = event.modifiers === Qt.NoModifier
                                                                   && (event.key === Qt.Key_Delete || event.key === Qt.Key_F2)
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Delete) {
                        dialog.remove()
                        event.accepted = true
                    } else if (event.key === Qt.Key_F2) {
                        nameField.forceActiveFocus()
                        nameField.selectAll()
                        event.accepted = true
                    }
                }
                delegate: ItemDelegate {
                    id: row
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    highlighted: ListView.isCurrentItem
                    text: modelData.name
                    focusPolicy: Qt.NoFocus // the list holds the focus
                    onClicked: list.currentIndex = index
                    Accessible.role: Accessible.ListItem
                    Accessible.name: modelData.usable ? modelData.name
                                                      : qsTr("%1, cannot be used").arg(modelData.name)
                    Accessible.description: modelData.problem
                    Accessible.selected: highlighted
                    contentItem: ColumnLayout {
                        spacing: 0
                        Label {
                            text: row.modelData.name
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            color: row.highlighted ? row.palette.highlightedText : row.palette.windowText
                        }
                        Label {
                            visible: !row.modelData.usable
                            text: qsTr("Cannot be used: %1").arg(row.modelData.problem)
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            color: row.highlighted ? row.palette.highlightedText : Theme.muted
                        }
                    }
                }
            }
        }
        Label {
            text: qsTr("Name:")
        }
        RowLayout {
            spacing: 6
            Layout.fillWidth: true
            TextField {
                id: nameField
                objectName: "manageLayoutsName"
                enabled: dialog.current !== null
                maximumLength: 64 // WorkspaceLayoutController::kMaximumNameLength
                Layout.fillWidth: true
                Accessible.name: qsTr("Name:")
                Accessible.description: dialog.problem
                onAccepted: dialog.rename()
            }
            Button {
                objectName: "manageLayoutsRename"
                text: qsTr("Rename")
                enabled: dialog.renamable
                onClicked: dialog.rename()
            }
            Button {
                objectName: "manageLayoutsDelete"
                text: qsTr("Delete")
                enabled: dialog.current !== null
                onClicked: dialog.remove()
            }
        }
        Label {
            objectName: "manageLayoutsProblem"
            text: dialog.problem
            visible: text.length > 0
            color: Theme.danger
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Accessible.role: Accessible.AlertMessage
        }
    }
    footer: DialogButtonBox {
        Button {
            objectName: "manageLayoutsClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
