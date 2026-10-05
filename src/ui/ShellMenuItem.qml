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
MenuItem {
    id: control
    property string iconRole
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
