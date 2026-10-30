pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Conversion"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "fpsWindow"
    dialogId: "setNewFps"
    required property var app
    title: qsTr("Set new FPS")
    modal: true
    defaultButton: changeButton
    readonly property var presets: ["23.976", "24", "25", "29.97", "30", "60"]
    readonly property real oldValue: rate(oldFps.editText)
    readonly property real newValue: rate(newFps.editText)
    property var openedFor: 0

    function rate(text) {
        const t = text.trim()
        if (!/^(\d+\.?\d*|\.\d+)$/.test(t))
            return NaN
        const v = Number(t)
        return v > 0 ? v : NaN
    }
    function show() {
        oldFps.currentIndex = 0
        oldFps.editText = presets[0]
        newFps.currentIndex = 2
        newFps.editText = presets[2]
        openedFor = app.editingTargetId()
        open()
    }
    function change() {
        if (dialog.app.editingTargetId() !== openedFor)
            dialog.reject()
        else if (dialog.app.setNewFps(oldFps.editText, newFps.editText))
            dialog.accept()
    }
    Connections {
        target: dialog.app
        function onTabsChanged() {
            if (dialog.visible && dialog.app.editingTargetId() !== dialog.openedFor)
                dialog.reject()
        }
    }

    GridLayout {
        columns: 2
        columnSpacing: 12
        rowSpacing: 12
        Label { text: qsTr("Subtitles FPS"); Layout.alignment: Qt.AlignTop; Layout.topMargin: 6 }
        ColumnLayout {
            spacing: 6
            ComboBox {
                id: oldFps
                objectName: "oldFps"
                editable: true
                model: dialog.presets
                Layout.preferredWidth: 112
                validator: RegularExpressionValidator { regularExpression: /[0-9.]*/ }
                Accessible.name: qsTr("Subtitles FPS")
                Accessible.description: isNaN(dialog.oldValue) ? oldProblem.text : ""
            }
            Label {
                id: oldProblem
                objectName: "oldFpsProblem"
                visible: isNaN(dialog.oldValue)
                text: qsTr("Type a frame rate, such as 25.")
                color: Theme.danger
                Accessible.role: Accessible.AlertMessage
            }
        }
        Label { text: qsTr("New FPS"); Layout.alignment: Qt.AlignTop; Layout.topMargin: 6 }
        ColumnLayout {
            spacing: 6
            ComboBox {
                id: newFps
                objectName: "newFps"
                editable: true
                model: dialog.presets
                Layout.preferredWidth: 112
                validator: RegularExpressionValidator { regularExpression: /[0-9.]*/ }
                Accessible.name: qsTr("New FPS")
                Accessible.description: isNaN(dialog.newValue) ? newProblem.text : scaleNote.text
            }
            Label {
                id: newProblem
                objectName: "newFpsProblem"
                visible: isNaN(dialog.newValue)
                text: qsTr("Type a frame rate, such as 25.")
                color: Theme.danger
                Accessible.role: Accessible.AlertMessage
            }
            Label {
                id: scaleNote
                objectName: "fpsScale"
                visible: !isNaN(dialog.oldValue) && !isNaN(dialog.newValue)
                text: qsTr("Every start and end time × %1").arg((dialog.oldValue / dialog.newValue).toFixed(3))
                color: Theme.muted
            }
        }
    }
    buttons: [
        PrimaryButton {
            id: changeButton
            objectName: "fpsOk"
            text: qsTr("Change FPS")
            enabled: !isNaN(dialog.oldValue) && !isNaN(dialog.newValue)
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.change()
        },
        Button {
            objectName: "fpsCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.reject()
        }
    ]
}
