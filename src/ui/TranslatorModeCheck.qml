// E5: the Line editor's "Translator mode" check box (legacy EditBox TlMode,
// EditBox.cpp:215-217, 1072-1078 and 1200). Turning it on is one "Turning on
// translator mode" step; turning it off asks first, as SubsGrid::SetTlMode
// (SubsGridBase.cpp:1339-1342) does, with No as the default answer.
import QtQuick
import QtQuick.Controls

CheckBox {
    id: check
    required property var app
    required property var editor
    objectName: "translatorMode"
    text: qsTr("Translator mode")
    focusPolicy: Qt.NoFocus
    enabled: app.translatorModeAvailable
    checked: editor.translationMode
    ToolTip.visible: hovered
    ToolTip.text: qsTr("Translator mode displays and saves both foreign text and translation text")
    function rebind() {
        checked = Qt.binding(() => check.editor.translationMode)
    }
    onToggled: {
        if (checked)
            app.turnOnTranslationMode()
        else
            turnOffConfirm.open()
        rebind()
    }

    Dialog {
        id: turnOffConfirm
        objectName: "translatorModeOffConfirm"
        parent: Overlay.overlay
        anchors.centerIn: parent
        title: qsTr("Confirmation")
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            text: qsTr("Are you sure you want to turn off translation mode?\nForeign language text will be deleted.")
        }
        // HikariMessageBox(..., wxYES_NO, ..., wxNO): No is the default.
        onOpened: standardButton(Dialog.No).forceActiveFocus()
        onAccepted: check.app.turnOffTranslationMode()
    }
}
