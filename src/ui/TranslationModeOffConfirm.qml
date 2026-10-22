// E5: turning Translation mode off asks first, as SubsGrid::SetTlMode
// (SubsGridBase.cpp:1339-1342) does, with No as the default answer. TM1
// (#232): asked by Subtitles > Translation mode (GLOBAL_TRANSLATION_MODE),
// which replaced the Line editor's "Translator mode" check box. PU2 (#304):
// a PK1 message box with verb buttons, Keep it on the default
// (PU2-translation-mode-off).
pragma Translator: "HikariSub.Translation"
import QtQuick
import Hikari.Ui

MessageBox {
    id: dialog
    required property var app
    objectName: "translatorModeOffConfirm"
    settings: app.settings
    title: qsTr("Translation mode")
    severity: "warning"
    text: qsTr("Turn off Translation mode?")
    detail: qsTr("Each translated Line keeps its Translated text; its Original text is deleted.")
    choices: [{ id: "turnOff", text: qsTr("Turn off"), role: "destructive" },
              { id: "keepOn", text: qsTr("Keep it on"), role: "reject", isDefault: true }]
    // The answer is for the tab it was asked in: another editing target
    // (Ctrl+PgUp, Ctrl+PgDn, a click on a tab) closes it unanswered.
    property var askedFor: null
    function editingTarget() {
        const tab = dialog.app.tabs[dialog.app.currentTab]
        return tab ? tab.id : null
    }
    Connections {
        target: dialog.app
        function onTabsChanged() {
            if (dialog.visible && dialog.editingTarget() !== dialog.askedFor)
                dialog.close()
        }
    }
    onVisibleChanged: if (visible) askedFor = editingTarget()
    onAccepted: dialog.app.turnOffTranslationMode()
}
