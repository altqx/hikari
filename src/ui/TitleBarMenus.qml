pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

MenuBar {
    id: menus
    objectName: "appMenus"
    property bool showMnemonics: false
    property Item keyboardTarget: null
    property bool windowActive: true
    spacing: 0
    padding: 0
    background: null
    delegate: TitleBarMenuItem {}
    Accessible.name: qsTr("Application menu")
}
