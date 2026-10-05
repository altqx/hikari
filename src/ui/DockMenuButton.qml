import QtQuick
import QtQuick.Controls
import Hikari.Ui

// D3: a panel header's "⋯" button (MuseScore's MenuButton,
// docs/research/musescore-docking.md §1): 20 × 20, the K1 set's menu-more
// icon, named "<panel> options" for assistive technology and in its
// tooltip; a button (Qt Quick gives a menu button no press action). It shows as pressed while its menu is open. Space, Enter and a
// click open the menu (DockTabBar.qml asks Main.qml for it).
ToolButton {
    id: button
    property string panelTitle
    property bool menuOpen: false
    objectName: "dockMenuButton"
    implicitWidth: 20
    implicitHeight: 20
    padding: 2
    display: AbstractButton.IconOnly
    checked: menuOpen
    text: qsTr("%1 options").arg(panelTitle)
    contentItem: Icon {
        iconRole: "menu-more"
        size: 16
        hovered: button.hovered
        pressed: button.down || button.menuOpen
    }
    Accessible.role: Accessible.Button
    Accessible.name: text
    ToolTip.visible: hovered && !menuOpen
    ToolTip.text: text
    ToolTip.delay: 600
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
}
