import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Hikari.Ui

// A shell menu item. Qt 6.11's Windows style tints an item only under the
// pointer (windows/MenuItem.qml: the background's alpha is 0 unless the item
// is down or hovered), so the item the keyboard highlighted showed nothing
// (D1 Windows gate). There it gets the style's hover tint. Since K2 the
// application runs the HikariStyle controls style (Fusion, src/ui/style) on
// every platform, whose items show the keyboard's highlight themselves; the
// Windows branch applies only when QT_QUICK_CONTROLS_STYLE=Windows asks for
// the native style.
//
// K1: with an iconRole, the item shows that icon of the set (legacy menus'
// bitmaps, HikariSubFrame.cpp's AppendTool). The style draws the item; its
// icon is the set's SVG drawn at the device's pixels by IconTheme's image
// provider, in the theme's colours, retinted live: on the highlight while
// highlighted or pressed, the whole icon in the palette's highlighted text
// colour (the accent is the highlight itself); the disabled colour while
// disabled, accent included. The Windows style keeps the item's text colour
// under its faint tint, so there the icon keeps its normal colours too.
// Directional icons mirror in right-to-left layouts. A submenu's item shows
// the submenu's iconRole (ShellMenu) the same way.
//
// UI polish: every item keeps the check gutter and the icon gutter, with or
// without a check mark or an icon, so the labels of a menu start at one x;
// the item's key binding (shortcutText, or the part of the text after a tab,
// legacy SetAccMenu's form) stands right-aligned in the muted colour, in one
// notation ("Ctrl+Shift+O").
// P10: `help` is legacy's menu help (Menu::Append's help text). While the
// pointer is over the item the status bar's first field shows it (a
// submenu's item shows the submenu's help; an item made for a menu's Action,
// the Action's `help`), and the field empties when the pointer leaves it or
// the menu closes, whatever the field holds then, and before the chosen
// command runs (MenuDialog::OnMouseEvent, SendEvent and HideMenus,
// Menu.cpp:600-646, 673-682 and 818-822; the keyboard's highlight did not
// show it).
MenuItem {
    id: control
    property string iconRole
    // The binding shown at the right (Main.applyMenuShortcuts fills the menu
    // bar's); a text with a tab carries its own.
    property string shortcutText
    readonly property int tabAt: text.indexOf("\t")
    readonly property string labelText: tabAt >= 0 ? text.slice(0, tabAt) : text
    readonly property string keysText: (tabAt >= 0 ? text.slice(tabAt + 1) : shortcutText)
        .replace(/(Ctrl|Shift|Alt|Meta|Win|Cmd)-/g, "$1+")
    property string help
    readonly property string shownHelp: help.length > 0 ? help
        : subMenu && typeof subMenu.help === "string" ? subMenu.help
        : action && typeof action.help === "string" ? action.help : ""
    onHoveredChanged: hovered ? StatusHelp.show(control, shownHelp) : StatusHelp.hide(control)
    // Legacy emptied the field before the chosen command ran (StatusHelp).
    onReleased: if (!subMenu) StatusHelp.activating()
    onTriggered: if (!subMenu) StatusHelp.activated()
    Component.onDestruction: StatusHelp.hide(control)
    readonly property string shownIconRole: iconRole.length > 0 ? iconRole
        : subMenu && typeof subMenu.iconRole === "string" ? subMenu.iconRole : ""
    readonly property bool windowsStyle: ControlsStyle.name === "Windows"
    readonly property bool iconHighlighted: enabled && (highlighted || down) && !windowsStyle
    icon.source: shownIconRole.length === 0 ? ""
        : IconTheme.image(shownIconRole, !enabled ? IconTheme.disabled : iconHighlighted ? palette.highlightedText : IconTheme.normal,
                          !enabled ? IconTheme.disabled : iconHighlighted ? palette.highlightedText : IconTheme.accent,
                          mirrored && IconTheme.mirrors(shownIconRole))
    icon.color: "transparent" // drawn in its own colours, not the style's tint
    icon.width: 16
    icon.height: 16

    contentItem: Item {
        readonly property real arrowPadding: control.subMenu && control.arrow ? control.arrow.width + control.spacing : 0
        readonly property real checkGutter: control.indicator ? control.indicator.width + control.spacing : 0
        readonly property real iconGutter: control.icon.source.toString().length > 0 ? 0 : control.icon.width + control.spacing
        readonly property color textColour: control.enabled && (control.down || control.highlighted)
            ? control.palette.highlightedText : control.palette.text
        implicitWidth: label.implicitWidth + (keys.text.length > 0 ? keys.implicitWidth + 24 : 0)
        implicitHeight: Math.max(label.implicitHeight, keys.implicitHeight)
        IconLabel {
            id: label
            width: parent.width - (keys.text.length > 0 ? keys.implicitWidth + 24 : 0)
            height: parent.height
            x: control.mirrored ? parent.width - width : 0
            leftPadding: control.mirrored ? parent.arrowPadding : parent.checkGutter + parent.iconGutter
            rightPadding: control.mirrored ? parent.checkGutter + parent.iconGutter : parent.arrowPadding
            spacing: control.spacing
            mirrored: control.mirrored
            display: control.display
            alignment: Qt.AlignLeft
            icon: control.icon
            text: control.labelText
            font: control.font
            color: parent.textColour
        }
        Text {
            id: keys
            objectName: "menuItemKeys"
            text: control.keysText
            visible: text.length > 0 && !control.subMenu
            x: control.mirrored ? parent.arrowPadding : parent.width - width - parent.arrowPadding
            anchors.verticalCenter: parent.verticalCenter
            font: control.font
            color: control.enabled && (control.down || control.highlighted) ? control.palette.highlightedText : Theme.muted
        }
    }

    Binding {
        when: control.windowsStyle && control.highlighted && !control.down && !control.hovered
        target: control.background
        property: "color"
        value: Qt.rgba(0, 0, 0, 0.0373)
    }
}
