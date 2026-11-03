import QtQuick
import Hikari.Ui
import com.kdab.dockwidgets 2.0 as KDDW

Rectangle {
    id: root
    objectName: "dockSeparator"
    anchors.fill: parent
    color: Theme.line

    readonly property KDDW.SeparatorView kddwSeparator: parent
    readonly property Item layout: kddwSeparator ? kddwSeparator.parent : null
    readonly property bool vertical: kddwSeparator ? kddwSeparator.isVertical : false
    readonly property int grabMargin: 5

    Component.onCompleted: if (kddwSeparator) kddwSeparator.z = 1

    function separatorsNear(point) {
        if (!layout)
            return []
        const near = []
        let horizontal = false
        let vertical = false
        for (const item of layout.children) {
            if (!item.visible || typeof item.isVertical !== "boolean" || typeof item.onMouseMoved !== "function")
                continue
            if (point.x < item.x - grabMargin || point.x > item.x + item.width + grabMargin
                    || point.y < item.y - grabMargin || point.y > item.y + item.height + grabMargin)
                continue
            near.push({ separator: item, x: item.x, y: item.y })
            if (item.isVertical)
                vertical = true
            else
                horizontal = true
        }
        return horizontal && vertical ? near : []
    }

    function moveJunction(junction, from, to) {
        for (const end of junction) {
            const target = end.separator.mapFromItem(layout, end.x + to.x - from.x, end.y + to.y - from.y)
            end.separator.onMousePressed()
            end.separator.onMouseMoved(target)
            end.separator.onMouseReleased()
        }
    }

    MouseArea {
        id: handle
        objectName: "dockSeparatorHandle"
        anchors.fill: parent
        anchors.leftMargin: root.vertical ? 0 : -root.grabMargin
        anchors.rightMargin: root.vertical ? 0 : -root.grabMargin
        anchors.topMargin: root.vertical ? -root.grabMargin : 0
        anchors.bottomMargin: root.vertical ? -root.grabMargin : 0
        hoverEnabled: true

        property var junction: []
        property point pressedAt
        readonly property bool atJunction: junction.length > 0

        function layoutPoint(mouse) { return mapToItem(root.layout, mouse.x, mouse.y) }

        cursorShape: atJunction ? Qt.SizeAllCursor : root.vertical ? Qt.SizeVerCursor : Qt.SizeHorCursor
        onPositionChanged: mouse => {
            if (!pressed)
                junction = root.separatorsNear(layoutPoint(mouse))
            else if (atJunction)
                root.moveJunction(junction, pressedAt, layoutPoint(mouse))
            else
                root.kddwSeparator.onMouseMoved(mapToItem(root, mouse.x, mouse.y))
        }
        onExited: if (!pressed) junction = []
        onPressed: mouse => {
            pressedAt = layoutPoint(mouse)
            junction = root.separatorsNear(pressedAt)
            if (!atJunction)
                root.kddwSeparator.onMousePressed()
        }
        onReleased: {
            if (!atJunction)
                root.kddwSeparator.onMouseReleased()
            junction = containsMouse ? junction : []
        }
        onDoubleClicked: if (!atJunction) root.kddwSeparator.onMouseDoubleClicked()
    }
}
