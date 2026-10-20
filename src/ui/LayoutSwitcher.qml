pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Workspace"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// WS3 (#217): the layout switcher, MuseScore 4's Workspaces button (MS1's
// research, musescore-shell.md section 7, the SH1 row): the layout icon, the
// current Document tab's layout (a saved layout's name, else its preset's)
// and the drop-down mark, opening the LayoutMenu. It is self-contained: the
// status bar holds it until SH1 (#220) moves it into the title bar, and the
// shell answers its signals as it answers View > Layout's.
ToolButton {
    id: switcher
    objectName: "layoutSwitcher"
    required property var layouts // WorkspaceLayoutController
    signal presetChosen(string name)
    signal savedLayoutChosen(string name)
    signal saveRequested()
    signal resetRequested()
    signal manageRequested()
    // The text size beside it (the status bar's status text role).
    property font labelFont: font
    // The menu opens above the button (the status bar), or below it.
    property bool opensUpward: true
    // The menu's height is known only once its items are laid out, so its
    // place is a binding rather than popup()'s one-off position.
    function openMenu() {
        layoutMenu.parent = switcher
        layoutMenu.x = Qt.binding(() => switcher.width - layoutMenu.width)
        layoutMenu.y = Qt.binding(() => switcher.opensUpward ? -layoutMenu.height : switcher.height)
        layoutMenu.open()
    }

    text: layouts.layoutLabel
    // The status bar takes no focus (legacy's AcceptsFocus false, P10): the
    // keyboard reaches the same menu as View > Layout.
    focusPolicy: Qt.NoFocus
    checked: layoutMenu.visible
    padding: 2
    leftPadding: 6
    rightPadding: 4
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
            iconRole: "menu-more" // a list that drops open (docking.md, Chrome)
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
