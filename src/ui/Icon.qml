import QtQuick
import Hikari.Ui

TintedSvg {
    id: icon
    property int size: 16
    implicitWidth: size
    implicitHeight: size
    color: enabled ? IconTheme.normal : IconTheme.disabled
    accentColor: enabled ? IconTheme.accent : IconTheme.disabled
    mirrored: LayoutMirroring.enabled && IconTheme.mirrors(iconRole)
    Accessible.ignored: true
}
