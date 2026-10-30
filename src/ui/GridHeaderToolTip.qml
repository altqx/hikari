import QtQuick
import QtQuick.Controls
import Hikari.Ui

ToolTip {
    id: tip
    required property HikariGrid grid
    parent: grid
    objectName: grid.objectName + "HeaderToolTip"
    visible: grid.headerToolTip.length > 0
    text: grid.headerToolTip
    x: Math.max(0, Math.min(grid.headerToolTipCell.x, grid.width - width))
    y: grid.headerToolTipCell.y + grid.headerToolTipCell.height + 2
}
