pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

MenuBar {
    id: menus
    objectName: "appMenus"
    property bool showMnemonics: false
    property Item keyboardTarget: null
    property bool windowActive: true
    spacing: 0
    padding: 0
    // MenuBar's implicit width also counts the titles MainTitleBar folds away.
    Layout.preferredWidth: {
        let shown = 0
        for (let i = 0; i < count; ++i) {
            const item = itemAt(i)
            if (item && item.visible)
                shown += item.implicitWidth
        }
        return shown
    }
    background: null
    delegate: TitleBarMenuItem {}
    Accessible.name: qsTr("Application menu")
}
