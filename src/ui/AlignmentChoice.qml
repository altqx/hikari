pragma Translator: "HikariSub.Editor"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

IconToolButton {
    id: alignment
    objectName: "alignmentChoice"
    required property LineEditorController editor
    signal chosen()

    readonly property var names: ["", qsTr("Bottom-left") + " (an1)", qsTr("Bottom-center") + " (an2)",
                                  qsTr("Bottom-right") + " (an3)", qsTr("Middle-left") + " (an4)",
                                  qsTr("Center") + " (an5)", qsTr("Middle-right") + " (an6)",
                                  qsTr("Top-left") + " (an7)", qsTr("Top-center") + " (an8)",
                                  qsTr("Top-right") + " (an9)"]
    readonly property int current: editor.alignmentMixed || editor.alignmentIndex < 0 ? 0 : editor.alignmentIndex + 1
    readonly property alias gridOpen: grid.opened

    // MUST KILL: literal role list only so the icon manifest check sees the names
    iconRole: ["alignment-mixed", "alignment-1", "alignment-2", "alignment-3", "alignment-4", "alignment-5", "alignment-6", "alignment-7", "alignment-8", "alignment-9"][current]
    function roleFor(n) { return "alignment-" + n }
    text: qsTr("Text position")
    tip: text + ": " + (editor.alignmentMixed ? qsTr("mixed") : current > 0 ? names[current] : "")
    Accessible.description: editor.alignmentMixed ? qsTr("mixed") : current > 0 ? names[current] : ""
    enabled: editor.editable && editor.assFields
    focusPolicy: Qt.TabFocus
    down: pressed || grid.visible
    ToolTip.visible: hovered && tip.length > 0 && !grid.visible

    function openGrid(withKeys) {
        if (!enabled)
            return
        grid.highlighted = current > 0 ? current : 5
        grid.withKeys = withKeys
        grid.chose = false
        grid.open()
    }
    function choose(n) {
        grid.chose = true
        grid.close()
        editor.chooseAlignment(n - 1)
        alignment.chosen()
    }

    onClicked: grid.visible ? grid.close() : openGrid(activeFocus)
    Keys.onReturnPressed: openGrid(true)
    Keys.onEnterPressed: openGrid(true)

    HoverHandler { id: buttonHover }
    Timer {
        id: hoverOpen
        interval: 400
        running: buttonHover.hovered && !grid.visible && alignment.enabled && !alignment.pressed
        onTriggered: alignment.openGrid(false)
    }
    Timer {
        id: hoverClose
        interval: 300
        running: grid.visible && !grid.withKeys && !buttonHover.hovered && !gridHover.hovered
        onTriggered: grid.close()
    }

    Popup {
        id: grid
        objectName: "alignmentGrid"
        property int highlighted: 5
        property bool withKeys: false
        property bool chose: false
        y: alignment.height
        x: Math.round((alignment.width - width) / 2)
        margins: 0
        padding: 4
        focus: withKeys
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        onClosed: if (withKeys && !chose && alignment.enabled) alignment.forceActiveFocus()
        HoverHandler { id: gridHover }
        contentItem: Grid {
            id: cells
            columns: 3
            spacing: 2
            focus: true
            Accessible.role: Accessible.Table
            Accessible.name: alignment.text
            readonly property var order: [7, 8, 9, 4, 5, 6, 1, 2, 3]
            Repeater {
                model: cells.order
                IconToolButton {
                    id: cell
                    required property int modelData
                    objectName: "alignmentCell" + modelData
                    iconRole: alignment.roleFor(modelData)
                    text: alignment.names[modelData]
                    focusPolicy: Qt.NoFocus
                    checked: modelData === alignment.current
                    readonly property bool keyed: grid.withKeys && modelData === grid.highlighted
                    Accessible.role: Accessible.Cell
                    Accessible.selected: checked
                    ToolTip.delay: 300
                    onClicked: alignment.choose(modelData)
                    FocusRing {
                        control: cell
                        visible: cell.keyed
                    }
                }
            }
            Keys.onShortcutOverride: event => {
                event.accepted = event.modifiers === Qt.NoModifier || event.modifiers === Qt.KeypadModifier
                    ? [Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_Return, Qt.Key_Enter, Qt.Key_Space,
                       Qt.Key_Escape].includes(event.key) || (event.key >= Qt.Key_1 && event.key <= Qt.Key_9)
                    : false
            }
            Keys.onPressed: event => {
                const h = grid.highlighted
                const row = h >= 7 ? 0 : h >= 4 ? 1 : 2
                const column = (h - 1) % 3
                let next = 0
                switch (event.key) {
                case Qt.Key_Left: next = column > 0 ? h - 1 : h; break
                case Qt.Key_Right: next = column < 2 ? h + 1 : h; break
                case Qt.Key_Up: next = row > 0 ? h + 3 : h; break
                case Qt.Key_Down: next = row < 2 ? h - 3 : h; break
                case Qt.Key_Return:
                case Qt.Key_Enter:
                case Qt.Key_Space:
                    alignment.choose(h)
                    event.accepted = true
                    return
                default:
                    if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9) {
                        alignment.choose(event.key - Qt.Key_0)
                        event.accepted = true
                    }
                    return
                }
                grid.highlighted = next
                event.accepted = true
            }
        }
    }
}
