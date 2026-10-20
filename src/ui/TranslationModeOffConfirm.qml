// E5: turning Translation mode off asks first, as SubsGrid::SetTlMode
// (SubsGridBase.cpp:1339-1342) does, with No as the default answer. TM1
// (#232): asked by Subtitles > Translation mode (GLOBAL_TRANSLATION_MODE),
// which replaced the Line editor's "Translator mode" check box.
pragma Translator: "HikariSub.Translation"
import QtQuick
import QtQuick.Controls

Dialog {
    id: dialog
    required property var app
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
    onAccepted: dialog.app.turnOffTranslationMode()
}
