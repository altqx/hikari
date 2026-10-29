pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ToolButton {
    id: switcher
    objectName: "layoutSwitcher"
    required property var layouts
    signal presetChosen(string name)
    signal savedLayoutChosen(string name)
    signal saveRequested()
    signal resetRequested()
    signal manageRequested()
    property font labelFont: font
    property bool opensUpward: false
    property bool keyboardCurrent: false
    // A Menu's height is known only once laid out, so its place is a binding, not popup()'s.
    function openMenu() {
        layoutMenu.parent = switcher
        layoutMenu.x = Qt.binding(() => switcher.width - layoutMenu.width)
        layoutMenu.y = Qt.binding(() => switcher.opensUpward ? -layoutMenu.height : switcher.height)
        layoutMenu.open()
    }

    text: layouts.layoutLabel
    focusPolicy: Qt.NoFocus
    checked: layoutMenu.visible
    padding: 2
    leftPadding: 6
    rightPadding: 4
    implicitHeight: Math.max(24, implicitContentHeight + topPadding + bottomPadding)
    background: Rectangle {
        radius: 1
        readonly property bool lit: switcher.checked || switcher.down || switcher.hovered
        color: lit ? Theme.select : Theme.raised
        border.width: 1
        border.color: switcher.keyboardCurrent ? Theme.focus : lit ? Theme.accent : Theme.line
    }
    contentItem: RowLayout {
        spacing: 4
        Icon {
            iconRole: "layout"
            Layout.alignment: Qt.AlignVCenter
        }
        Label {
            objectName: "layoutSwitcherLabel"
            text: switcher.text
            font: switcher.labelFont
            elide: Text.ElideRight
            maximumLineCount: 1
            Layout.maximumWidth: 200
            Layout.alignment: Qt.AlignVCenter
        }
        Icon {
            iconRole: "menu-more"
            size: 12
            Layout.alignment: Qt.AlignVCenter
        }
    }
    Accessible.role: Accessible.ButtonMenu
    Accessible.name: qsTr("Layout: %1").arg(text)
    ToolTip.visible: hovered && !layoutMenu.visible
    ToolTip.text: qsTr("Layout of this tab")
    onClicked: layoutMenu.visible ? layoutMenu.close() : openMenu()

    LayoutMenu {
        id: layoutMenu
        objectName: "layoutSwitcherMenu"
        prefix: "switcher"
        layouts: switcher.layouts
        onPresetChosen: name => switcher.presetChosen(name)
        onSavedLayoutChosen: name => switcher.savedLayoutChosen(name)
        onSaveRequested: switcher.saveRequested()
        onResetRequested: switcher.resetRequested()
        onManageRequested: switcher.manageRequested()
    }
}
