// SH1 (#220): the menus in the title bar (MainTitleBar.qml), MS1's menu bar
// with no panel of its own: the title bar is its background. Its titles are
// TitleBarMenuItems, which read the state below.
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

MenuBar {
    id: menus
    objectName: "appMenus"
    // The titles underline their letters (Alt held, or the keyboard is on
    // the title bar).
    property bool showMnemonics: false
    // The title the keyboard is on (MainTitleBar's keyboard mode), or null.
    property Item keyboardTarget: null
    // The titles take the disabled role while the window is not active.
    property bool windowActive: true
    spacing: 0
    padding: 0
    background: null
    delegate: TitleBarMenuItem {}
    Accessible.name: qsTr("Application menu")
}
