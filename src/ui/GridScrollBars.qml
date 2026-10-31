import QtQuick
import QtQuick.Controls
import Hikari.Ui

Item {
    id: bars
    required property HikariGrid grid
    parent: grid
    anchors.fill: parent

    component Bar: ScrollBar {
        id: bar
        readonly property int thickness: 6
        padding: 0
        policy: ScrollBar.AlwaysOn
        focusPolicy: Qt.NoFocus
        minimumSize: Math.min(1, 24 / Math.max(1, orientation === Qt.Vertical ? height : width))
        background: Rectangle {
            implicitWidth: bar.thickness
            implicitHeight: bar.thickness
            radius: bar.thickness / 2
            color: Theme.field
            border.color: Theme.line
        }
        contentItem: Rectangle {
            implicitWidth: bar.thickness
            implicitHeight: bar.thickness
            radius: bar.thickness / 2
            color: bar.pressed ? Theme.text : Theme.muted
            opacity: bar.pressed || bar.hovered ? 0.9 : 0.6
        }
    }

    Bar {
        id: vertical
        readonly property real range: bars.grid.contentHeight - bars.grid.headerHeight
        objectName: bars.grid.objectName + "VerticalScrollBar"
        orientation: Qt.Vertical
        visible: bars.grid.verticalScrollBarShown
        x: bars.grid.viewportWidth + (bars.grid.scrollBarRoom - thickness) / 2
        y: bars.grid.headerHeight + 2
        width: thickness
        height: Math.max(0, bars.grid.viewportHeight - y - 2)
        size: range > 0 ? Math.min(1, (bars.grid.viewportHeight - bars.grid.headerHeight) / range) : 1
        Binding on position {
            when: !vertical.pressed
            value: vertical.range > 0 ? bars.grid.contentY / vertical.range : 0
        }
        onPositionChanged: if (pressed) bars.grid.contentY = position * range
    }

    Bar {
        id: horizontal
        objectName: bars.grid.objectName + "HorizontalScrollBar"
        orientation: Qt.Horizontal
        visible: bars.grid.horizontalScrollBarShown
        x: 2
        y: bars.grid.viewportHeight + (bars.grid.scrollBarRoom - thickness) / 2
        width: Math.max(0, bars.grid.viewportWidth - 4)
        height: thickness
        size: bars.grid.contentWidth > 0 ? Math.min(1, bars.grid.viewportWidth / bars.grid.contentWidth) : 1
        Binding on position {
            when: !horizontal.pressed
            value: bars.grid.contentWidth > 0 ? bars.grid.contentX / bars.grid.contentWidth : 0
        }
        onPositionChanged: if (pressed) bars.grid.contentX = position * bars.grid.contentWidth
    }
}
