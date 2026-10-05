import QtQuick
import QtQuick.Controls
import Hikari.Ui

// A shell menu item. Qt 6.11's Windows style tints an item only under the
// pointer (windows/MenuItem.qml: the background's alpha is 0 unless the item
// is down or hovered), so the item the keyboard highlighted showed nothing
// (D1 Windows gate). There it gets the style's hover tint.
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
// P10: `help` is legacy's menu help (Menu::Append's help text). While the
// pointer is over the item the status bar's first field shows it (a
// submenu's item shows the submenu's help; an item made for a menu's Action,
// the Action's `help`), and the field empties when the pointer leaves it or
// the menu closes (MenuDialog::OnMouseEvent and HideMenus, Menu.cpp:600-646
// and 818-822; the keyboard's highlight did not show it).
MenuItem {
    id: control
    property string iconRole
    property string help
    readonly property string shownHelp: help.length > 0 ? help
        : subMenu && typeof subMenu.help === "string" ? subMenu.help
        : action && typeof action.help === "string" ? action.help : ""
    onHoveredChanged: hovered ? StatusHelp.show(control, shownHelp) : StatusHelp.hide(control)
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

    Binding {
        when: control.windowsStyle && control.highlighted && !control.down && !control.hovered
        target: control.background
        property: "color"
        value: Qt.rgba(0, 0, 0, 0.0373)
    }
}
