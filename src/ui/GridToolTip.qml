import QtQuick
import QtQuick.Controls
import Hikari.Ui

ToolTip {
    id: tip
    required property HikariGrid grid
    parent: grid
    objectName: grid.objectName + "ToolTip"
    visible: grid.hoverToolTip.length > 0
    text: grid.hoverToolTip
    x: Math.max(0, Math.min(grid.hoverToolTipCell.x, grid.width - width))
    y: grid.hoverToolTipCell.y + grid.hoverToolTipCell.height + 2
}
