// K1: a menu item with an icon of the set (legacy menus' bitmaps,
// HikariSubFrame.cpp's AppendTool). The style draws the item; its icon is
// the set's SVG drawn at the device's pixels by IconTheme's image provider,
// in the theme's colours, retinted live: on the highlight while highlighted
// or pressed, the whole icon in the palette's highlighted text colour (the
// accent is the highlight itself); the disabled colour while disabled,
// accent included. Directional icons mirror in right-to-left layouts.
import QtQuick
import QtQuick.Controls
import Hikari.Ui

MenuItem {
    id: item
    property string iconRole
    readonly property bool iconHighlighted: enabled && (highlighted || down)
    icon.source: iconRole.length === 0 ? ""
        : IconTheme.image(iconRole, !enabled ? IconTheme.disabled : iconHighlighted ? palette.highlightedText : IconTheme.normal,
                          !enabled ? IconTheme.disabled : iconHighlighted ? palette.highlightedText : IconTheme.accent,
                          mirrored && IconTheme.mirrors(iconRole))
    icon.color: "transparent" // drawn in its own colours, not the style's tint
    icon.width: 16
    icon.height: 16
}
