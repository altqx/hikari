pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Hotkeys"
// PK1 (#301): the hotkey capture, one for the two copies (legacy
// HkeysDialog "Hotkey mapping" and the HikariMessageDialog question after
// it; Main.qml's automation copy had no caller left after AM1). By the
// small forms rule a popover under the control that asked for it (the
// Hotkeys page's row), else a compact window (Shift+click on a command).
//
// capture(name, window, windows, done, options): waits for keys as
// HkeysDialog::OnKeyPress reads them (hotkeys.capture): a refused key says
// why on the form and it keeps waiting; plain Esc cancels, so Esc alone is
// never a hotkey. With `windows` the window the hotkey works in is chosen
// ("In"). done(accel, window) takes the keys; options.anchor places it,
// options.clear (a function) adds Clear.
// ask(conflict, done), from inside done: the keys are taken, and the
// choice is made on the form, the buttons named for it (legacy's "Switch
// hotkeys / Delete hotkey / Set anyway" warning): Swap ("switch"), Take it
// ("delete"), Keep both ("anyway"), or Cancel ("cancel"). Other keys start
// again.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: capture
    required property var hotkeys
    property SettingsStore settings: null
    property string name: ""
    property bool offerWindows: true
    property int type: 0
    property int window: 0
    property var done: null
    property var clear: null
    // The keys shown, and why the last ones were refused.
    property string keys: ""
    property string refusal: ""
    // The question when the keys are taken ({message, canSwitch,
    // canSetAnyway, commands}) and its answer's callback.
    property var conflict: null
    property var answerDone: null
    property bool asking: false
    readonly property bool opened: popover.visible || compact.visible
    readonly property alias compactWindow: compact
    readonly property string title: qsTr("Keys for \"%1\"").arg(name)
    readonly property var windowNames: [qsTr("Global hotkey"), qsTr("Subtitle grid hotkey"), qsTr("Edit box hotkey"),
                                        qsTr("Video hotkey"), qsTr("Audio hotkey")]
    visible: false

    function capture(actionName, win, windows, callback, options) {
        const o = options || {}
        name = actionName
        type = win
        window = win
        offerWindows = windows
        done = callback
        clear = o.clear || null
        keys = ""
        refusal = ""
        conflict = null
        answerDone = null
        if (o.anchor && o.anchor.visible) {
            if (compact.visible)
                compact.close()
            popover.openAt(o.anchor)
            popoverForm.focusKeys()
        } else {
            compact.open()
            compactForm.focusKeys()
        }
    }
    function ask(c, callback) {
        conflict = c
        answerDone = callback
        asking = true
    }
    function finish(accel, win) {
        const callback = done
        done = null
        asking = false
        conflict = null
        answerDone = null
        if (callback)
            callback(accel, win)
        // The callback asked (the keys are taken): the form stays, with the question.
        if (asking)
            done = callback
        else
            end()
    }
    function answer(a) {
        const callback = answerDone
        answerDone = null
        conflict = null
        asking = false
        done = null
        end()
        if (callback)
            callback(a)
    }
    // `closing`: the compact window is closing already.
    function cancel(closing) {
        const callback = answerDone
        answerDone = null
        conflict = null
        asking = false
        done = null
        end(closing)
        if (callback)
            callback("cancel")
    }
    function end(closing) {
        if (popover.visible)
            popover.keep()
        if (compact.visible && closing !== true)
            compact.close()
    }
    function press(event) {
        event.accepted = true
        const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
        if (event.key === Qt.Key_Escape && mods === 0) {
            cancel()
            return
        }
        // Other keys while the question shows start again.
        answerDone = null
        conflict = null
        const r = hotkeys.capture(event.key, event.modifiers, window)
        if (r.result === "refused") {
            refusal = r.message
            keys = ""
        } else if (r.result === "accepted") {
            refusal = ""
            keys = r.keys || r.accel
            finish(r.accel, offerWindows ? window : type)
        }
    }

    Popover {
        id: popover
        objectName: "hotkeyCapturePopover"
        title: capture.title
        showTitle: true
        onReverted: capture.cancel()
        // A press outside cancels too.
        onKept: if (capture.done !== null || capture.answerDone !== null) capture.cancel()
        HotkeyForm { id: popoverForm; host: capture; Layout.preferredWidth: 288 }
    }
    DialogWindow {
        id: compact
        objectName: "hotkeyCaptureWindow"
        dialogId: "hotkeyCapture"
        settings: capture.settings
        modal: true
        title: capture.title
        onRejected: capture.cancel(true)
        HotkeyForm { id: compactForm; host: capture; Layout.preferredWidth: 340 }
    }

    component HotkeyForm: ColumnLayout {
        id: form
        required property Item host
        spacing: 10
        function focusKeys() { keysArea.forceActiveFocus() }
        Rectangle {
            id: keysArea
            objectName: "hotkeyMappingKeys"
            color: Theme.field
            border.width: activeFocus ? 2 : 1
            border.color: activeFocus ? Theme.focus : Theme.line
            implicitHeight: 48
            Layout.fillWidth: true
            focus: true
            activeFocusOnTab: true
            Accessible.role: Accessible.EditableText
            Accessible.name: form.host.title
            Accessible.description: form.host.keys.length > 0 ? form.host.keys : qsTr("Press the keys")
            // Every key is the capture's, the application's shortcuts included.
            Keys.onShortcutOverride: event => event.accepted = true
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                    event.accepted = false
                    return
                }
                form.host.press(event)
            }
            Label {
                objectName: "hotkeyMappingText"
                anchors.centerIn: parent
                visible: form.host.keys.length === 0
                text: qsTr("Press the keys…")
                color: Theme.muted
            }
            Row {
                anchors.centerIn: parent
                spacing: 6
                visible: form.host.keys.length > 0
                Repeater {
                    model: form.host.keys.length > 0 ? form.host.keys.split("+") : []
                    Row {
                        id: cap
                        required property string modelData
                        required property int index
                        spacing: 6
                        Label {
                            visible: cap.index > 0
                            text: "+"
                            color: Theme.muted
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Rectangle {
                            color: Theme.raised
                            border.color: Theme.control
                            radius: 3
                            implicitWidth: keyLabel.implicitWidth + 20
                            implicitHeight: 28
                            Label {
                                id: keyLabel
                                anchors.centerIn: parent
                                text: cap.modelData
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }
            }
        }
        RowLayout {
            visible: form.host.offerWindows
            spacing: 8
            Label { text: qsTr("In") }
            ComboBox {
                objectName: "hotkeyWindowChoice"
                Layout.fillWidth: true
                // Choosing keeps the keyboard on the keys (legacy forwards key
                // presses on the choice to the mapping).
                focusPolicy: Qt.NoFocus
                model: form.host.windowNames
                currentIndex: form.host.window
                Accessible.name: qsTr("Window")
                // Set by assistive technology too, without activated().
                onCurrentIndexChanged: form.host.window = currentIndex
                onActivated: form.focusKeys()
            }
        }
        RowLayout {
            visible: form.host.refusal.length > 0
            spacing: 8
            Layout.fillWidth: true
            Icon { iconRole: "notice-warning"; color: Theme.warning; accentColor: Theme.warning; Layout.alignment: Qt.AlignTop }
            Label {
                objectName: "hotkeyRefusalText"
                text: form.host.refusal
                wrapMode: Text.Wrap
                Accessible.role: Accessible.AlertMessage
                Layout.fillWidth: true
            }
        }
        RowLayout {
            visible: form.host.conflict !== null
            spacing: 8
            Layout.fillWidth: true
            Icon { iconRole: "notice-warning"; color: Theme.warning; accentColor: Theme.warning; Layout.alignment: Qt.AlignTop }
            Label {
                objectName: "hotkeyQuestionText"
                readonly property var commands: form.host.conflict ? (form.host.conflict.commands || []) : []
                text: commands.length > 0
                      ? qsTr("%1 is already used by %2.").arg(form.host.keys).arg(commands.join(", "))
                      : form.host.conflict ? form.host.conflict.message : ""
                wrapMode: Text.Wrap
                Accessible.role: Accessible.AlertMessage
                Layout.fillWidth: true
            }
        }
        Flow {
            visible: form.host.conflict !== null
            spacing: 6
            Layout.fillWidth: true
            readonly property string holder: {
                const c = form.host.conflict ? (form.host.conflict.commands || []) : []
                return c.length === 1 ? c[0] : ""
            }
            PrimaryButton {
                objectName: "hotkeySwitch"
                visible: form.host.conflict !== null && form.host.conflict.canSwitch === true
                text: parent.holder.length > 0 ? qsTr("Swap with %1").arg(parent.holder) : qsTr("Swap")
                onClicked: form.host.answer("switch")
            }
            Button {
                objectName: "hotkeyDelete"
                text: parent.holder.length > 0 ? qsTr("Take it from %1").arg(parent.holder) : qsTr("Take it")
                onClicked: form.host.answer("delete")
            }
            Button {
                objectName: "hotkeySetAnyway"
                visible: form.host.conflict !== null && form.host.conflict.canSetAnyway === true
                text: qsTr("Keep both")
                onClicked: form.host.answer("anyway")
            }
        }
        RowLayout {
            spacing: 6
            Layout.fillWidth: true
            Button {
                objectName: "hotkeyClear"
                visible: form.host.clear !== null
                text: qsTr("Clear")
                onClicked: {
                    const c = form.host.clear
                    form.host.done = null
                    form.host.end()
                    c()
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "hotkeyCancel"
                text: qsTr("Cancel")
                onClicked: form.host.cancel()
            }
        }
    }
}
