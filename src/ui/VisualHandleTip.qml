import QtQuick
import QtQuick.Controls
import Hikari.Ui

Item {
    id: tip
    objectName: "visualHandleTip"
    required property VisualToolsController tools
    property bool shown: true
    required property HoverHandler hover
    property int row: -1
    readonly property string name: row >= 0 ? (texts.handles(tools.activeFamily)[row]?.name ?? "") : ""

    function refresh() {
        const p = tip.hover.point.position
        row = shown && tip.hover.hovered && !tools.gestureActive ? tools.handleAt(p.x, p.y) : -1
    }

    VisualToolTexts {
        id: texts
    }
    Connections {
        target: tip.hover
        function onPointChanged() { tip.refresh() }
        function onHoveredChanged() { tip.refresh() }
    }
    Connections {
        target: tip.tools
        function onOverlayChanged() { tip.refresh() }
        function onChanged() { tip.refresh() }
    }
    onShownChanged: refresh()

    ToolTip {
        id: label
        objectName: "visualHandleTipLabel"
        parent: tip
        x: tip.hover.point.position.x + 14
        y: tip.hover.point.position.y + 18
        visible: tip.name.length > 0
        text: tip.name
        delay: 300
    }
}
