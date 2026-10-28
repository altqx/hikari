// E4: the "Text position" choice (legacy EditBox's Ban, EditBox.cpp:156-206):
// the nine \an positions. It shows the shown Line's position
// (EditBox::SetAlignment) and a choice puts \an<n> into the text's first
// block (EditBox::OnAnChoice); only ASS has it (EditBox::HideControls).
//
// LE4 (#229): legacy's dropdown is one icon button showing the position
// (the mixed icon when the selected Lines differ) that opens a 3 x 3 grid in
// numpad layout: on hover after a short delay, on a click, or from the
// keyboard (Enter or Space on the button; then the arrows and Enter or Space,
// or 1-9 on either keyboard row or the numpad; Esc closes). A grid the
// pointer opened takes no focus, so typing stays where it was, and closes
// when the pointer has left it and the button for a moment.
pragma Translator: "HikariSub.Editor"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

IconToolButton {
    id: alignment
    objectName: "alignmentChoice"
    required property LineEditorController editor
    // After a choice the text field takes the focus (PutTagInText's SetFocus).
    signal chosen()

    // Legacy's choice texts, by \an number.
    readonly property var names: ["", qsTr("Bottom-left") + " (an1)", qsTr("Bottom-center") + " (an2)",
                                  qsTr("Bottom-right") + " (an3)", qsTr("Middle-left") + " (an4)",
                                  qsTr("Center") + " (an5)", qsTr("Middle-right") + " (an6)",
                                  qsTr("Top-left") + " (an7)", qsTr("Top-center") + " (an8)",
                                  qsTr("Top-right") + " (an9)"]
    // The shown \an (1-9), or 0 for none or a mixed selection.
    readonly property int current: editor.alignmentMixed || editor.alignmentIndex < 0 ? 0 : editor.alignmentIndex + 1
    readonly property alias gridOpen: grid.opened

    // The set's roles by \an number, 0 the mixed icon (named here as literals
    // for the icon manifest's check).
    iconRole: ["alignment-mixed", "alignment-1", "alignment-2", "alignment-3", "alignment-4", "alignment-5", "alignment-6", "alignment-7", "alignment-8", "alignment-9"][current]
    function roleFor(n) { return "alignment-" + n }
    text: qsTr("Text position")
    tip: text + ": " + (editor.alignmentMixed ? qsTr("mixed") : current > 0 ? names[current] : "")
    Accessible.description: editor.alignmentMixed ? qsTr("mixed") : current > 0 ? names[current] : ""
    enabled: editor.editable && editor.assFields
    focusPolicy: Qt.TabFocus
    down: pressed || grid.visible
    // The tooltip would cover the grid.
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
    // Space clicks the button (AbstractButton), which opens the grid with keys.

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
        // A choice sends the focus to the text (chosen()); Esc gives it back
        // to the button.
        property bool chose: false
        y: alignment.height
        x: Math.round((alignment.width - width) / 2)
        margins: 0
        padding: 4
        // A grid the pointer opened leaves the keyboard focus where it was.
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
            // Numpad order: 7 8 9 / 4 5 6 / 1 2 3.
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
                    // The shown Line's position; the keyboard's place is ringed.
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
            // The grid's keys are its own, not the shortcuts bound to them
            // (the arrows step video frames elsewhere).
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
