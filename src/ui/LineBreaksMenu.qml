pragma Translator: "HikariSub.Text"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: menu
    required property var shellRoot
    required property string prefix
    property bool wraps: false
    objectName: prefix + "LineBreaksMenu"
    iconRole: "line-break-insert"
    title: qsTr("Line breaks")
    onAboutToShow: wraps = shellRoot.app.selectionWrapsAtBreaksOnly()
    function run(symbol, command) {
        if (!menu.shellRoot.hotkeyGesture(symbol))
            menu.shellRoot.app.textCommand(command)
    }
    ShellMenuItem {
        command: "GLOBAL_TEXT_INSERT_BALANCED_BREAK"
        iconRole: "line-break-insert"
        objectName: menu.prefix + "InsertBalancedBreak"
        text: qsTr("Insert balanced break")
        onTriggered: menu.run(command, "balancedBreak")
    }
    ShellMenuItem {
        command: "GLOBAL_TEXT_MOVE_BREAK_LEFT"
        iconRole: "line-break-left"
        objectName: menu.prefix + "MoveBreakLeft"
        text: qsTr("Move break left")
        onTriggered: menu.run(command, "breakLeft")
    }
    ShellMenuItem {
        command: "GLOBAL_TEXT_MOVE_BREAK_RIGHT"
        iconRole: "line-break-right"
        objectName: menu.prefix + "MoveBreakRight"
        text: qsTr("Move break right")
        onTriggered: menu.run(command, "breakRight")
    }
    ShellMenuItem {
        command: "GLOBAL_TEXT_REMOVE_BREAKS"
        iconRole: "line-break-remove"
        objectName: menu.prefix + "RemoveBreaks"
        text: qsTr("Remove breaks")
        onTriggered: menu.run(command, "removeBreaks")
    }
    MenuSeparator {}
    ShellMenuItem {
        command: "GLOBAL_TEXT_WRAP_AT_BREAKS_ONLY"
        objectName: menu.prefix + "WrapAtBreaksOnly"
        text: qsTr("Wrap only at %1 (%2)").arg("\\N", "\\q2")
        checkable: true
        checked: menu.wraps
        onTriggered: menu.run(command, "wrapAtBreaksOnly")
    }
}
