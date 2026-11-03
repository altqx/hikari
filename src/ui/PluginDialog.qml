pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "pluginDialog_" + dialogState.key
    required property var ui
    required property var dialogState
    property ColourPickerController colourPicker: null
    title: dialogState.title
    dialogId: dialogState.key
    modal: dialogState.modal
    resizable: true
    defaultButton: null

    readonly property var host: ({
        send: event => dialog.ui.dialogEvent(dialog.dialogState.key, event),
        packageRoot: dialogState.packageRoot.toString(),
        colourPicker: dialog.colourPicker,
        colourPopover: popover.item
    })

    function finish() {
        dialog.accepting = true
        dialog.close()
        dialog.accepting = false
    }
    onRejected: ui.dismiss(dialogState.key)

    property var made: []
    function makeButtons() {
        for (const button of made) {
            dialog.buttonBox.removeItem(button)
            button.destroy()
        }
        const next = []
        dialog.defaultButton = null
        for (const spec of dialogState.buttons) {
            const kind = spec.style === "primary" ? primary : spec.style === "danger" ? danger : normal
            const button = kind.createObject(null, { spec: spec })
            dialog.buttonBox.addItem(button)
            if (spec.default === true)
                dialog.defaultButton = button
            next.push(button)
        }
        made = next
    }
    Connections {
        target: dialog.dialogState
        function onChanged() { dialog.makeButtons() }
    }
    Component.onCompleted: makeButtons()

    component Press: QtObject {
        id: press
        required property var spec
        required property Item button
        readonly property Connections clicks: Connections {
            target: press.button
            function onClicked() { dialog.ui.press(dialog.dialogState.key, press.spec.id) }
        }
    }
    Component {
        id: normal
        IconTextButton {
            id: button
            required property var spec
            objectName: "pluginDialogButton_" + spec.id
            text: spec.text
            DialogButtonBox.buttonRole: spec.cancel === true ? DialogButtonBox.RejectRole : DialogButtonBox.AcceptRole
            readonly property Press press: Press { spec: button.spec; button: button }
        }
    }
    Component {
        id: primary
        PrimaryButton {
            id: button
            required property var spec
            objectName: "pluginDialogButton_" + spec.id
            text: spec.text
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            readonly property Press press: Press { spec: button.spec; button: button }
        }
    }
    Component {
        id: danger
        DangerButton {
            id: button
            required property var spec
            objectName: "pluginDialogButton_" + spec.id
            text: spec.text
            DialogButtonBox.buttonRole: spec.cancel === true ? DialogButtonBox.RejectRole : DialogButtonBox.AcceptRole
            readonly property Press press: Press { spec: button.spec; button: button }
        }
    }

    Loader {
        id: popover
        active: dialog.colourPicker !== null
        sourceComponent: ColourPopover { picker: dialog.colourPicker }
    }

    Item {
        implicitWidth: Math.max(dialog.dialogState.width, Math.min(body.implicitWidth, 640), 280)
        implicitHeight: dialog.dialogState.height > 0 ? dialog.dialogState.height : body.implicitHeight
        width: parent ? parent.width : implicitWidth
        height: parent ? parent.height : implicitHeight
        PluginWidget {
            id: body
            objectName: "pluginDialogBody"
            anchors.fill: parent
            node: dialog.dialogState.body
            host: dialog.host
        }
    }
}
