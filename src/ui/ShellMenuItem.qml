import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Hikari.Ui

MenuItem {
    id: control
    property bool keepsMenuOpen: checkable
    property bool holdsMenu: false
    // MUST KILL: holdMenu cuts the menu's own close on first highlight, so onTriggered must dismiss it
    function holdMenu() {
        if (keepsMenuOpen && !subMenu && !holdsMenu)
            holdsMenu = ShellMenus.keepOpen(control)
    }
    function keepsOpenNow() {
        return holdsMenu && keepsMenuOpen && !ShellMenus.shiftHeld()
    }
    onMenuChanged: holdsMenu = false
    onHighlightedChanged: if (highlighted) holdMenu()
    onPressed: holdMenu()
    property string iconRole
    property string command
    property string shortcutText
    readonly property int tabAt: text.indexOf("\t")
    readonly property string labelText: tabAt >= 0 ? text.slice(0, tabAt) : text
    // Qt reads "<" or "&lt;" as rich text; an <img> would be fetched and crashes IconLabel.
    readonly property bool plainLabel: labelText.indexOf("<") >= 0 || labelText.indexOf("&lt;") >= 0
    readonly property string keysText: (tabAt >= 0 ? text.slice(tabAt + 1) : shortcutText)
        .replace(/(Ctrl|Shift|Alt|Meta|Win|Cmd)-/g, "$1+")
    property string help
    readonly property string shownHelp: help.length > 0 ? help
        : subMenu && typeof subMenu.help === "string" ? subMenu.help
        : action && typeof action.help === "string" ? action.help : ""
    onHoveredChanged: hovered ? StatusHelp.show(control, shownHelp) : StatusHelp.hide(control)
    onReleased: if (!subMenu && !keepsOpenNow()) StatusHelp.activating()
    onTriggered: {
        if (subMenu || keepsOpenNow())
            return
        StatusHelp.activated()
        if (holdsMenu && menu)
            menu.dismiss()
    }
    Component.onDestruction: StatusHelp.hide(control)
    readonly property string shownIconRole: iconRole.length > 0 ? iconRole
        : subMenu && typeof subMenu.iconRole === "string" ? subMenu.iconRole : ""
    readonly property bool windowsStyle: ControlsStyle.name === "Windows"
    // The Windows style keeps the text colour under its faint tint, so the icon keeps its colours there.
    readonly property bool iconHighlighted: enabled && (highlighted || down) && !windowsStyle
    icon.source: shownIconRole.length === 0 ? ""
        : IconTheme.image(shownIconRole, !enabled ? IconTheme.disabled : iconHighlighted ? palette.highlightedText : IconTheme.normal,
                          !enabled ? IconTheme.disabled : iconHighlighted ? palette.highlightedText : IconTheme.accent,
                          mirrored && IconTheme.mirrors(shownIconRole))
    icon.color: "transparent" // transparent turns off Qt's icon tint
    icon.width: 16
    icon.height: 16

    contentItem: Item {
        readonly property real arrowPadding: control.subMenu && control.arrow ? control.arrow.width + control.spacing : 0
        readonly property real checkGutter: control.indicator ? control.indicator.width + control.spacing : 0
        readonly property real iconGutter: control.icon.source.toString().length > 0 ? 0 : control.icon.width + control.spacing
        readonly property color textColour: control.enabled && (control.down || control.highlighted)
            ? control.palette.highlightedText : control.palette.text
        readonly property real iconRoom: control.icon.source.toString().length > 0 ? control.icon.width + control.spacing : 0
        readonly property real leadPadding: control.mirrored ? arrowPadding : checkGutter + iconGutter
        readonly property real trailPadding: control.mirrored ? checkGutter + iconGutter : arrowPadding
        readonly property real textStart: leadPadding + (control.mirrored ? 0 : iconRoom)
        readonly property real textEnd: trailPadding + (control.mirrored ? iconRoom : 0)
        implicitWidth: (control.plainLabel ? textStart + plainText.implicitWidth + textEnd : label.implicitWidth)
            + (keys.text.length > 0 ? keys.implicitWidth + 24 : 0)
        implicitHeight: Math.max(label.implicitHeight, plainText.implicitHeight, keys.implicitHeight)
        IconLabel {
            id: label
            width: parent.width - (keys.text.length > 0 ? keys.implicitWidth + 24 : 0)
            height: parent.height
            x: control.mirrored ? parent.width - width : 0
            leftPadding: parent.leadPadding
            rightPadding: parent.trailPadding
            spacing: control.spacing
            mirrored: control.mirrored
            display: control.display
            alignment: Qt.AlignLeft
            icon: control.icon
            text: control.plainLabel ? "" : control.labelText
            font: control.font
            color: parent.textColour
        }
        Text {
            id: plainText
            objectName: "menuItemPlainText"
            visible: control.plainLabel
            text: control.plainLabel ? control.labelText : ""
            textFormat: Text.PlainText
            x: label.x + parent.textStart
            width: Math.max(0, label.width - parent.textStart - parent.textEnd)
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: control.mirrored ? Text.AlignRight : Text.AlignLeft
            elide: Text.ElideRight
            font: control.font
            color: parent.textColour
        }
        Text {
            id: keys
            objectName: "menuItemKeys"
            text: control.keysText
            visible: text.length > 0
            x: control.mirrored ? parent.arrowPadding : parent.width - width - parent.arrowPadding
            anchors.verticalCenter: parent.verticalCenter
            font: control.font
            color: control.enabled && (control.down || control.highlighted) ? control.palette.highlightedText : Theme.muted
        }
    }

    // Qt's Windows style tints only a hovered or pressed item; give the keyboard's highlight that tint.
    Binding {
        when: control.windowsStyle && control.highlighted && !control.down && !control.hovered
        target: control.background
        property: "color"
        value: Color.rgba(0, 0, 0, 0.0373)
    }
}
