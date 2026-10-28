// A Grid's hover text for its abbreviated headings ("Layer" over "L.",
// "Vertical margin" over "Vert."), below the heading under the pointer. The
// Grid paints its header itself, so it reports the name and the heading's
// cell (LineGrid::headerToolTip); a heading that is a word has none.
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
    delay: 500
    x: Math.max(0, Math.min(grid.headerToolTipCell.x, grid.width - width))
    y: grid.headerToolTipCell.y + grid.headerToolTipCell.height + 2
}
