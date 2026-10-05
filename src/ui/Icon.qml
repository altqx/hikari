// K1: a UI icon of the in-house set (docs/qt/ux/icons.md, roles in
// icons/manifest.json). The icon takes the current appearance's icon colours
// from IconTheme: normal with its accent layer, the hover/pressed colour
// while `hovered` or `pressed`, the disabled colour while not enabled (an
// item is disabled with its parent). Colours and appearance change live.
// Directional icons mirror in right-to-left layouts.
import QtQuick
import Hikari.Ui

TintedSvg {
    id: icon
    property int size: 16
    property bool hovered: false
    property bool pressed: false
    implicitWidth: size
    implicitHeight: size
    color: !enabled ? IconTheme.disabled : (hovered || pressed) ? IconTheme.active : IconTheme.normal
    // The accent never carries meaning alone: a disabled or active icon is one colour.
    accentColor: !enabled ? IconTheme.disabled : (hovered || pressed) ? IconTheme.active : IconTheme.accent
    mirrored: LayoutMirroring.enabled && IconTheme.mirrors(iconRole)
    Accessible.ignored: true // the control it sits in carries the name
}
