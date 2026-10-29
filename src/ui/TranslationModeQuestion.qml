pragma Translator: "HikariSub.Translation"
import QtQuick
import Hikari.Ui

MessageBox {
    id: dialog
    objectName: "translationModeQuestion"
    required property var app
    settings: app.settings
    title: qsTr("Translation mode")
    severity: "info"
    text: qsTr("Turn on Translation mode for this file?")
    detail: qsTr("Each Line keeps its text as the Original text and gets a Translated text beside it. The file saves both until you turn the mode off.")
    choices: [{ id: "turnOn", text: qsTr("Turn on"), role: "accept", isDefault: true },
              { id: "notNow", text: qsTr("Not now"), role: "reject" }]
    property var askedFor: null
    // MUST KILL: dropping marks a close() as unanswered so onRejected does not decline
    property bool dropping: false
    function editingTarget() {
        const tab = dialog.app.tabs[dialog.app.currentTab]
        return tab ? tab.id : null
    }
    Connections {
        target: dialog.app
        function onTabsChanged() {
            if (dialog.visible && dialog.editingTarget() !== dialog.askedFor) {
                dialog.dropping = true
                dialog.close()
                dialog.dropping = false
            }
        }
    }
    onVisibleChanged: if (visible) askedFor = editingTarget()
    onAccepted: dialog.app.turnOnTranslationMode()
    onRejected: if (!dropping) dialog.app.declineTranslationMode()
}
