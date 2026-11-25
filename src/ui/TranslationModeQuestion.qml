pragma Translator: "HikariSub.Translation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Popover {
    id: question
    objectName: "translationModeQuestion"
    required property var app
    title: qsTr("Turn on Translation mode for this file?")
    showTitle: true
    implicitWidth: 360
    closePolicy: Popup.NoAutoClose
    initialFocus: turnOn
    property var askedFor: null
    function editingTarget() {
        const tab = question.app.tabs[question.app.currentTab]
        return tab ? tab.id : null
    }
    function ask(switcher) {
        askedFor = editingTarget()
        openAt(switcher, switcher)
    }
    function accept() { keep() }
    function reject() { revert() }
    function drop() {
        active = false
        close()
    }
    onKept: question.app.turnOnTranslationMode()
    onReverted: question.app.declineTranslationMode()
    Connections {
        target: question.app
        function onTabsChanged() {
            if (question.visible && question.editingTarget() !== question.askedFor)
                question.drop()
        }
    }

    Label {
        objectName: "translationModeQuestionDetail"
        text: qsTr("Each line keeps its text as the Original text and gets a Translated text beside it. The file saves both until you turn the mode off.")
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        color: Theme.muted
        Layout.fillWidth: true
    }
    RowLayout {
        spacing: 8
        Layout.alignment: Qt.AlignRight
        Layout.topMargin: 4
        Button {
            objectName: "translationModeNotNow"
            text: qsTr("Not now")
            flat: true
            onClicked: question.reject()
        }
        PrimaryButton {
            id: turnOn
            objectName: "translationModeTurnOn"
            text: qsTr("Turn on")
            onClicked: question.accept()
        }
    }
}
