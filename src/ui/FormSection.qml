// TS1: a titled section of a dock form (MuseScore 4's ExpandableBlank,
// docs/research/musescore-shell.md §5): a disclosure arrow and the title in
// bold, the content 8 below, sections 12 apart with a 1-pixel `line` rule
// between them. The header is a button: a click, Space or Enter collapses
// or expands the content. The arrow is drawn here, as Qt draws a list's
// arrows (outside the icon set, docs/ux/icons.md), and points to the
// trailing side while collapsed.
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: section
    property string title
    // CR1 (#452): a muted line after the title, what the section holds.
    property string summary
    property bool expanded: true
    // The rule above the section (off for the form's first section).
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
        Layout.bottomMargin: 4 // 12 from the section above with the spacing
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
        // Qt Quick's Accessible has no expanded state: a toggle, on while open.
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
