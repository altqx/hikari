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
    detail: qsTr("Each translated line keeps its Translated text; its Original text is deleted.")
    choices: [{ id: "turnOff", text: qsTr("Turn off"), role: "destructive" },
              { id: "keepOn", text: qsTr("Keep it on"), role: "reject", isDefault: true }]
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
