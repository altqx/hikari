// K1: a push button with an icon of the set beside its text (legacy
// buttons with a bitmap beside the label, e.g. the Style manager's
// transfer buttons, stylestore.cpp:86-88), in the appearance's colours
// (disabled while disabled; hover and press shade the button). The
// style draws the button; the icon is its image source from IconTheme's
// image provider. Directional icons mirror in right-to-left layouts.
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Button {
    id: button
    property string iconRole
    property string tip
    icon.source: iconRole.length === 0 ? ""
        : IconTheme.image(iconRole, enabled ? IconTheme.normal : IconTheme.disabled,
                          enabled ? IconTheme.accent : IconTheme.disabled,
                          mirrored && IconTheme.mirrors(iconRole))
    icon.color: "transparent"
    icon.width: 16
    icon.height: 16
    ToolTip.visible: hovered && tip.length > 0
    ToolTip.text: tip
}
