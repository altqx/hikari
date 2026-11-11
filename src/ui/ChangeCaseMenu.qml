pragma Translator: "HikariSub.Text"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: menu
    required property var shellRoot
    required property string prefix
    objectName: prefix + "ChangeCaseMenu"
    iconRole: "change-case"
    title: qsTr("Change case")
    function run(symbol, command) {
        if (!menu.shellRoot.hotkeyGesture(symbol))
            menu.shellRoot.app.textCommand(command)
    }
    ShellMenuItem {
        command: "GLOBAL_TEXT_LOWERCASE"
        iconRole: "case-lower"
        objectName: menu.prefix + "Lowercase"
        text: qsTr("lowercase")
        onTriggered: menu.run(command, "lowercase")
    }
    ShellMenuItem {
        command: "GLOBAL_TEXT_UPPERCASE"
        iconRole: "case-upper"
        objectName: menu.prefix + "Uppercase"
        text: qsTr("UPPERCASE")
        onTriggered: menu.run(command, "uppercase")
    }
    ShellMenuItem {
        command: "GLOBAL_TEXT_TITLE_CASE"
        iconRole: "case-title"
        objectName: menu.prefix + "TitleCase"
        text: qsTr("Title Case")
        onTriggered: menu.run(command, "titleCase")
    }
    ShellMenuItem {
        command: "GLOBAL_TEXT_SENTENCE_CASE"
        iconRole: "case-sentence"
        objectName: menu.prefix + "SentenceCase"
        text: qsTr("Sentence case")
        onTriggered: menu.run(command, "sentenceCase")
    }
    MenuSeparator {}
    ShellMenuItem {
        command: "GLOBAL_TEXT_FULL_WIDTH"
        iconRole: "fullwidth"
        objectName: menu.prefix + "FullWidth"
        text: qsTr("Full-width")
        onTriggered: menu.run(command, "fullWidth")
    }
    ShellMenuItem {
        command: "GLOBAL_TEXT_HALF_WIDTH"
        iconRole: "halfwidth"
        objectName: menu.prefix + "HalfWidth"
        text: qsTr("Half-width")
        onTriggered: menu.run(command, "halfWidth")
    }
}
