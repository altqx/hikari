// O2: legacy HkeysDialog ("Hotkey mapping") and the questions around it.
// capture() shows the window for an action; a key press becomes its binding
// as HkeysDialog::OnKeyPress reads it (a refused one shows the legacy message
// and the window stays; plain Escape closes it, the dialog's escape key).
// With the window choice (not for scripts or mapped buttons) the binding is
// for the window chosen. ask() is the HikariMessageDialog "Warning" that
// follows when the keys are taken: Switch hotkeys (OK), Delete hotkey (Yes),
// Set anyway (No) and Cancel, as the conflict offers them.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Window {
    color: Theme.panel // K2: a Window draws white unless told
    id: mapping
    objectName: "hotkeyMapping"
    required property var hotkeys
    title: qsTr("Hotkey mapping")
    flags: Qt.Dialog
    modality: Qt.ApplicationModal
    width: 380
    height: mappingColumn.implicitHeight + 24
    property string name: ""
    property bool offerWindows: true
    property int type: 0
    property var done: null

    function capture(actionName, window, windows, callback) {
        name = actionName
        type = window
        offerWindows = windows
        done = callback
        windowChoice.currentIndex = window
        show()
        requestActivate()
        keysArea.forceActiveFocus()
    }
    // The HikariMessageDialog question; callback(answer): "switch",
    // "delete", "anyway" or "cancel".
    function ask(conflict, callback) {
        question.conflict = conflict
        question.done = callback
        question.show()
        question.requestActivate()
    }
    function finish(accel, window) {
        const callback = done
        done = null
        close()
        if (callback)
            callback(accel, window)
    }

    ColumnLayout {
        id: mappingColumn
        anchors.fill: parent
        anchors.margins: 12
        ComboBox {
            id: windowChoice
            objectName: "hotkeyWindowChoice"
            Layout.fillWidth: true
            visible: mapping.offerWindows
            // Legacy forwards key presses on the choice to the mapping.
            focusPolicy: Qt.NoFocus
            model: [qsTr("Global hotkey"), qsTr("Subtitle grid hotkey"), qsTr("Edit box hotkey"),
                    qsTr("Video hotkey"), qsTr("Audio hotkey")]
            Accessible.name: qsTr("Window")
        }
        Label {
            objectName: "hotkeyMappingText"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Please enter a hotkey for \"%1\".").arg(mapping.name)
        }
        Item {
            id: keysArea
            objectName: "hotkeyMappingKeys"
            Layout.fillWidth: true
            Layout.preferredHeight: 8
            focus: true
            // Every key is the mapping's, the application's shortcuts included.
            Keys.onShortcutOverride: event => event.accepted = true
            Keys.onPressed: event => {
                event.accepted = true
                const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
                if (event.key === Qt.Key_Escape && mods === 0) {
                    mapping.done = null
                    mapping.close()
                    return
                }
                const window = mapping.offerWindows ? windowChoice.currentIndex : mapping.type
                const r = mapping.hotkeys.capture(event.key, event.modifiers, window)
                if (r.result === "refused") {
                    refusal.text = r.message
                    refusal.show()
                    refusal.requestActivate()
                } else if (r.result === "accepted") {
                    mapping.finish(r.accel, window)
                }
            }
        }
    }
    onClosing: done = null
    // A modal window gives the activation back to its parent when it goes.
    function giveBack() {
        if (!question.visible && !mapping.visible && transientParent)
            transientParent.requestActivate()
    }
    onVisibleChanged: if (!visible) giveBack()

    // HikariMessageBox(message): OK only, the mapping window stays.
    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: refusal
        objectName: "hotkeyRefusal"
        property alias text: refusalText.text
        flags: Qt.Dialog
        modality: Qt.ApplicationModal
        width: 380
        height: refusalColumn.implicitHeight + 24
        ColumnLayout {
            id: refusalColumn
            anchors.fill: parent
            anchors.margins: 12
            Label { id: refusalText; objectName: "hotkeyRefusalText"; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Button {
                objectName: "hotkeyRefusalOk"
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("OK")
                onClicked: {
                    refusal.close()
                    mapping.requestActivate()
                    keysArea.forceActiveFocus()
                }
            }
        }
    }

    Window {
        color: Theme.panel // K2: a Window draws white unless told
        id: question
        objectName: "hotkeyQuestion"
        title: qsTr("Warning")
        flags: Qt.Dialog
        modality: Qt.ApplicationModal
        width: 460
        height: questionColumn.implicitHeight + 24
        property var conflict: ({})
        property var done: null
        function answer(a) {
            const callback = done
            done = null
            close()
            if (callback)
                callback(a)
        }
        onClosing: {
            if (done)
                answer("cancel")
        }
        onVisibleChanged: if (!visible) mapping.giveBack()
        ColumnLayout {
            id: questionColumn
            anchors.fill: parent
            anchors.margins: 12
            Label {
                objectName: "hotkeyQuestionText"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: question.conflict.message ?? ""
            }
            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Button {
                    objectName: "hotkeySwitch"
                    text: qsTr("Switch hotkeys")
                    visible: question.conflict.canSwitch === true
                    onClicked: question.answer("switch")
                }
                Button {
                    objectName: "hotkeyDelete"
                    text: qsTr("Delete hotkey")
                    onClicked: question.answer("delete")
                }
                Button {
                    objectName: "hotkeySetAnyway"
                    text: qsTr("Set anyway")
                    visible: question.conflict.canSetAnyway === true
                    onClicked: question.answer("anyway")
                }
                Button {
                    objectName: "hotkeyCancel"
                    text: qsTr("Cancel")
                    onClicked: question.answer("cancel")
                }
            }
        }
        Shortcut {
            sequence: "Escape"
            enabled: question.active
            onActivated: question.answer("cancel")
        }
    }
}
