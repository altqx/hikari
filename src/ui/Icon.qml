// K1: a UI icon of the in-house set (docs/ux/icons.md, roles in
// icons/manifest.json). The icon takes the current appearance's icon colours
// from IconTheme: normal with its accent layer, the disabled colour while not
// enabled (an item is disabled with its parent). Hover and press keep these
// colours: the button's panel shows them (K1-hover-one-colour). Colours and
// appearance change live. Directional icons mirror in right-to-left layouts.
import QtQuick
import Hikari.Ui

TintedSvg {
    id: icon
    property int size: 16
    implicitWidth: size
    implicitHeight: size
    color: enabled ? IconTheme.normal : IconTheme.disabled
    // The accent never carries meaning alone: a disabled icon is one colour.
    accentColor: enabled ? IconTheme.accent : IconTheme.disabled
    mirrored: LayoutMirroring.enabled && IconTheme.mirrors(iconRole)
    Accessible.ignored: true // the control it sits in carries the name
}
