pragma Translator: "HikariSub.Position"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: menu
    required property var shellRoot
    required property string prefix
    objectName: prefix + "PhantomMenu"
    iconRole: "phantom"
    title: qsTr("Phantom")
    ShellMenuItem {
        command: "GLOBAL_PHANTOM_KEEP_START"
        objectName: menu.prefix + "PhantomKeepStart"
        text: qsTr("Keep the start aligned")
        onTriggered: if (!menu.shellRoot.hotkeyGesture(command)) menu.shellRoot.app.phantom(false)
    }
    ShellMenuItem {
        command: "GLOBAL_PHANTOM_KEEP_END"
        objectName: menu.prefix + "PhantomKeepEnd"
        text: qsTr("Keep the end aligned")
        onTriggered: if (!menu.shellRoot.hotkeyGesture(command)) menu.shellRoot.app.phantom(true)
    }
}
