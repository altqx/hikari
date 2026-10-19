// SY2: a grid of colour swatches (the colour popover's palette and recent
// colours). One tab stop: the arrow keys move the current swatch, Space
// picks it, as a click does, and Enter picks it and goes on to the
// container (the popover keeps the colour). Each colour is {r, g, b, a} with a the ASS
// alpha; `names` gives each its accessible name.
pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Colours"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Item {
    id: grid
    property var colours: []
    property var names: []
    property int columns: 8
    property int cell: 20
    property int spacing: 4
    property int current: 0
    signal picked(var colour)

    readonly property int rows: Math.ceil(colours.length / columns)
    implicitWidth: columns * cell + (columns - 1) * spacing
    implicitHeight: rows > 0 ? rows * cell + (rows - 1) * spacing : 0
    activeFocusOnTab: colours.length > 0
    Accessible.role: Accessible.List
    Accessible.description: current < names.length ? names[current] : ""

    function pick(i) {
        if (i < 0 || i >= colours.length)
            return
        current = i
        picked(colours[i])
    }
    function move(step) {
        const next = current + step
        if (next >= 0 && next < colours.length)
            current = next
    }
    // Before the application's arrow shortcuts (frame steps).
    Keys.onShortcutOverride: event => event.accepted = [Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_Space,
                                                        Qt.Key_Return, Qt.Key_Enter].includes(event.key)
    Keys.onLeftPressed: move(-1)
    Keys.onRightPressed: move(1)
    Keys.onUpPressed: move(-columns)
    Keys.onDownPressed: move(columns)
    Keys.onSpacePressed: pick(current)
    Keys.onReturnPressed: event => { pick(current); event.accepted = false }
    Keys.onEnterPressed: event => { pick(current); event.accepted = false }
    onColoursChanged: current = Math.min(current, Math.max(0, colours.length - 1))

    Repeater {
        model: grid.colours
        Rectangle {
            id: swatch
            required property var modelData
            required property int index
            objectName: grid.objectName + index
            x: (index % grid.columns) * (grid.cell + grid.spacing)
            y: Math.floor(index / grid.columns) * (grid.cell + grid.spacing)
            width: grid.cell
            height: grid.cell
            radius: 2
            color: Qt.rgba(modelData.r / 255, modelData.g / 255, modelData.b / 255, 1)
            border.color: Theme.line
            Accessible.role: Accessible.Button
            Accessible.name: swatch.index < grid.names.length ? grid.names[swatch.index] : ""
            Accessible.focusable: true
            Accessible.focused: grid.activeFocus && grid.current === swatch.index
            Accessible.onPressAction: grid.pick(swatch.index)
            // The current swatch while the grid has the keyboard: K2's ring,
            // 1 wide just outside it in the focus role (FocusRing.qml).
            Rectangle {
                anchors.fill: parent
                anchors.margins: -1
                radius: 3
                color: "transparent"
                border.color: Theme.focus
                visible: grid.activeFocus && grid.current === swatch.index
            }
            HoverHandler { id: hover }
            TapHandler {
                onTapped: grid.pick(swatch.index)
            }
            ToolTip.visible: hover.hovered && swatch.Accessible.name.length > 0
            ToolTip.text: swatch.Accessible.name
            ToolTip.delay: 500
        }
    }
}
