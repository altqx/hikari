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
    // The answer is for the tab it was asked in: another editing target
    // (Ctrl+PgUp, Ctrl+PgDn, a click on a tab) closes the dialog unanswered.
    property var askedFor: null
    function editingTarget() {
        const tab = dialog.app.tabs[dialog.app.currentTab]
        return tab ? tab.id : null
    }
    Connections {
        target: dialog.app
        function onTabsChanged() {
            if (dialog.opened && dialog.editingTarget() !== dialog.askedFor)
                dialog.close()
        }
    }
    onAboutToShow: askedFor = editingTarget()
    onOpened: standardButton(Dialog.No).forceActiveFocus()
    onAccepted: dialog.app.turnOffTranslationMode()
}
