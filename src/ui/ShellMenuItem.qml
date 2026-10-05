import QtQuick
import QtQuick.Controls
import Hikari.Ui

// A shell menu item. Qt 6.11's Windows style tints an item only under the
// pointer (windows/MenuItem.qml: the background's alpha is 0 unless the item
// is down or hovered), so the item the keyboard highlighted showed nothing
// (D1 Windows gate). There it gets the style's hover tint.
MenuItem {
    id: control

    Binding {
        when: ControlsStyle.name === "Windows" && control.highlighted && !control.down && !control.hovered
        target: control.background
        property: "color"
        value: Qt.rgba(0, 0, 0, 0.0373)
    }
}
