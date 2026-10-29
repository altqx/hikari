import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: section
    property string title
    property string summary
    property bool expanded: true
    property bool ruled: true
    default property alias content: body.data
    signal toggled(bool expanded)

    spacing: 8
    Layout.fillWidth: true

    Rectangle {
        visible: section.ruled
        color: Theme.line
        implicitHeight: 1
        Layout.fillWidth: true
        Layout.bottomMargin: 4
    }
    T.AbstractButton {
        id: header
        objectName: section.objectName + "Header"
        text: section.title
        focusPolicy: Qt.StrongFocus
        Layout.fillWidth: true
        implicitHeight: Math.max(20, title.implicitHeight + 2)
        implicitWidth: arrow.width + 4 + title.implicitWidth + (summaryLabel.visible ? 8 + summaryLabel.implicitWidth : 0)
        Accessible.role: Accessible.Button
        Accessible.name: text
        // Qt Quick's Accessible has no expanded state, so the header is a checkable toggle.
        Accessible.checkable: true
        Accessible.checked: section.expanded
        onClicked: section.toggled(!section.expanded)
        Keys.onReturnPressed: section.toggled(!section.expanded)
        Keys.onEnterPressed: section.toggled(!section.expanded)
        background: Item {
            FocusRing { control: header }
        }
        contentItem: RowLayout {
            spacing: 4
            Canvas {
                id: arrow
                width: 16
                height: 16
                readonly property color ink: Theme.text
                rotation: section.expanded ? 0 : (header.mirrored ? 90 : -90)
                onInkChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = ink
                    ctx.lineWidth = 1.2
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    ctx.moveTo(4.5, 6.5)
                    ctx.lineTo(8, 10)
                    ctx.lineTo(11.5, 6.5)
                    ctx.stroke()
                }
            }
            Label {
                id: title
                text: section.title
                font.bold: true
                color: Theme.text
                elide: Text.ElideRight
                Layout.fillWidth: !summaryLabel.visible
            }
            Label {
                id: summaryLabel
                visible: section.summary.length > 0
                text: section.summary
                color: Theme.muted
                elide: Text.ElideRight
                Layout.leftMargin: 4
                Layout.fillWidth: true
            }
        }
    }
    ColumnLayout {
        id: body
        visible: section.expanded
        spacing: 8
        Layout.fillWidth: true
    }
}
