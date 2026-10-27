// A script's dialog (N8, AD1 #310): fixed controls placed on the script's
// grid cells, on DG1's frame. Values go back through the controller, which
// types them per control; a script gets Aegisub's values and button results
// (docs/qt/automation.md). Closing the window answers with no button.
//
// AD1 (owner, 2026-10-22, the mockup on #310):
// - a resizable DG1 window titled with the macro, its size and place kept per
//   script and dialog (controller.dialogId); never larger than the screen's
//   free area: past that the grid scrolls and the hint strip and the
//   buttons stay. Columns holding an edit, a textbox, a number, a dropdown
//   or a colour take the extra width, rows holding a textbox the extra height;
// - the hint strip above the buttons shows the hovered control's hint, else
//   the focused one's (with a number's range), at one height while open;
//   the hint is the control's tooltip and accessible description;
// - numbers take any typing and clamp when committed; the arrows, the wheel,
//   Up / Down and Page Up / Page Down step them (a floatedit by its step,
//   AD1-float-step; without one it is a plain number field);
// - a colour is a swatch, which opens the shared colour popover, and a field
//   taking #RRGGBB, RRGGBB or ASS &HBBGGRR& (with alpha for coloralpha);
// - a dropdown of more than 10 items filters as you type;
// - keys (AD1-keyboard), one set on every platform: Tab follows the grid,
//   row by row, then the buttons in the script's order; Enter presses the
//   focused button, else the Enter button (controller.enterButton), and
//   Ctrl+Enter does so from a textbox; Esc ends the dialog with no button
//   from anywhere (an open list or popover takes it first); Space ticks a
//   check box and presses a button; Alt with a label's `&` letter presses
//   the button, ticks the check box or (a label) moves to the control after
//   it, the letters underlined while Alt is held.
//
// AD2 (owner, 2026-10-22, the mockup on #311): the ⋯ button at the leading
// end of the footer (its menu: Last used, Presets with Save as preset… and
// Delete preset, Reset to the script's values) only changes the values shown
// before a button is pressed. A control that differs from the script's value
// has a 2 px accent bar at its leading edge: hovering it says the script's
// value, clicking it puts that one back. A repeated run's dialog that is
// shown says why in an inline bar (controller.notice).
pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Automation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: window
    required property AutomationDialogController controller
    // SY2: the colour controls' popover.
    required property ColourPickerController colourPicker
    // AD2: asks before a preset is deleted (Main's MessageHost).
    property var messages: null

    title: controller.title
    dialogId: controller.dialogId
    guideId: "automation" // DX1: one page for every script's window
    resizable: true
    modal: true

    // Alt is held: the `&` letters are underlined.
    property bool altHeld: false
    // The controls in Tab order (the grid's rows, then its columns), each
    // {spec, index} with its place in the script's list.
    readonly property var cellsInOrder: {
        const list = controller.controls.map((spec, index) => ({ spec: spec, index: index }))
        list.sort((a, b) => a.spec.y - b.spec.y || a.spec.x - b.spec.x || a.index - b.index)
        return list
    }
    // The columns that take the extra width: those of an edit, a textbox, a
    // number, a dropdown or a colour one column wide, and the columns of a wider one
    // when none of its columns does already.
    readonly property var growingColumns: {
        const grows = {}
        const growing = controller.controls.filter(c => ["edit", "alpha", "textbox", "intedit", "floatedit",
                                                         "dropdown", "color", "coloralpha"].indexOf(c.kind) >= 0)
        for (const c of growing)
            if (c.width === 1)
                grows[c.x] = true
        for (const c of growing) {
            let any = false
            for (let x = c.x; x < c.x + c.width; ++x)
                any = any || grows[x] === true
            if (!any)
                for (let x = c.x; x < c.x + c.width; ++x)
                    grows[x] = true
        }
        return grows
    }
    readonly property bool hasTextbox: controller.controls.some(c => c.kind === "textbox")
    property Item hoveredCell: null
    property Item focusedCell: null
    readonly property string hintText: hoveredCell && hoveredCell.stripText !== "" ? hoveredCell.stripText
                                       : focusedCell ? focusedCell.stripText : ""
    // Every hint (and number range) the strip can show; none: no strip.
    readonly property var stripTexts: {
        const out = []
        for (let i = 0; i < cells.count; ++i) {
            const cell = cells.itemAt(i)
            if (cell && cell.longestStripText !== "")
                out.push(cell.longestStripText)
        }
        return out
    }

    function values() {
        const out = []
        for (let i = 0; i < cells.count; ++i) {
            const cell = cells.itemAt(i)
            out[cell.controlIndex] = cell.item ? cell.item.readValue() : undefined
        }
        return out
    }
    function press(index) { controller.finish(index, values()) }

    // AD2: the controls `list` holds a value for (one per control, as the
    // controls show it; undefined leaves one as it is) take it.
    function fill(list) {
        for (let i = 0; i < cells.count; ++i) {
            const cell = cells.itemAt(i)
            const v = list[cell.controlIndex]
            if (v !== undefined && v !== null && cell.item)
                cell.item.writeValue(v)
        }
    }
    function resetControl(cell) {
        const v = controller.scriptValues()[cell.controlIndex]
        if (v !== undefined && v !== null && cell.item)
            cell.item.writeValue(v)
    }
    // A control's hint, and that it was changed (the marker).
    function descriptionOf(cell) {
        const hint = cell && cell.spec ? cell.spec.hint : ""
        if (!cell || !cell.differs)
            return hint
        const changed = qsTr("Changed from the script’s %1.").arg(controller.scriptValueText(cell.controlIndex))
        return hint !== "" ? hint + " " + changed : changed
    }
    function askDelete(name) {
        if (!messages) {
            controller.deletePreset(name)
            return
        }
        messages.ask({
            title: qsTr("Delete preset"),
            text: qsTr("Delete the preset “%1”?").arg(name),
            plainText: true,
            severity: "warning",
            choices: [{ id: "delete", text: qsTr("Delete"), role: "destructive" },
                      { id: "cancel", text: qsTr("Cancel"), role: "reject", isDefault: true }]
        }, id => {
            if (id === "delete" && controller.open)
                controller.deletePreset(name)
        })
    }
    function pressEnterButton() {
        const b = madeButtons[controller.enterButton]
        if (b)
            b.click()
    }

    // The grid cell holding `item`, or null.
    function cellOf(item) {
        for (let p = item; p; p = p.parent)
            if (p.hasOwnProperty("controlIndex") && p.hasOwnProperty("stripText"))
                return p
        return null
    }
    onActiveFocusItemChanged: {
        // The footer's box is a focus scope of its own, but no control: on
        // the window's activation it can keep the keyboard its button had.
        if (activeFocusItem && (activeFocusItem === buttonBox || activeFocusItem === buttonBox.contentItem)
                && defaultButton && defaultButton.Window.window === window) {
            defaultButton.forceActiveFocus(Qt.TabFocusReason)
            return
        }
        const cell = cellOf(activeFocusItem)
        if (cell)
            focusedCell = cell
        else if (activeFocusItem && madeButtons.indexOf(activeFocusItem) >= 0)
            focusedCell = null
        if (cell && cell.item)
            grid.reveal(activeFocusItem)
    }
    onActiveChanged: if (!active) altHeld = false
    function trackAlt(event, down) {
        if (event.key === Qt.Key_Alt)
            altHeld = down
        event.accepted = false
    }

    // The label's `&` letter, the label without its `&`s, and the label
    // with that letter underlined (`&&` is one `&`).
    function mnemonic(label) {
        const m = /&([^&])/.exec(String(label).replace(/&&/g, "\u0001\u0001"))
        return m ? m[1] : ""
    }
    function plain(label) { return String(label).replace(/&(.)/g, "$1") }
    function underlined(label) {
        let out = ""
        let marked = false
        const s = String(label)
        for (let i = 0; i < s.length; ++i) {
            let ch = s[i]
            if (ch === "&" && i + 1 < s.length) {
                ch = s[++i]
                const esc = ch === "<" ? "&lt;" : ch === ">" ? "&gt;" : ch === "&" ? "&amp;" : ch
                out += !marked && ch !== "&" ? "<u>" + esc + "</u>" : esc
                marked = marked || ch !== "&"
                continue
            }
            out += ch === "<" ? "&lt;" : ch === ">" ? "&gt;" : ch === "&" ? "&amp;" : ch
        }
        return out
    }
    function shownLabel(label) { return altHeld ? underlined(label) : underlined(plain(label).replace(/&/g, "&&")) }

    // A label's `&` letter: the control after it in Tab order.
    function focusAfter(cell) {
        for (let i = 0; i < cells.count; ++i) {
            const c = cells.itemAt(i)
            if (c.index > cell.index && c.item && c.item.focusTargets && c.item.focusTargets.length > 0) {
                c.item.focusTargets[0].forceActiveFocus(Qt.ShortcutFocusReason)
                return
            }
        }
    }
    // The name a control is read with: the label just before it in its row.
    function nameFor(spec) {
        let best = null
        for (const c of controller.controls)
            if (c.kind === "label" && c.y === spec.y && c.x + c.width <= spec.x && (!best || c.x > best.x))
                best = c
        const label = best ? plain(best.label).trim().replace(/:$/, "") : ""
        return label !== "" ? label : spec.kind === "checkbox" ? plain(spec.label) : spec.name
    }

    function show() {
        makeButtons()
        // The footer's list places the new buttons on its next layout: now,
        // so the Enter button is in the window when open() gives it the keys.
        if (buttonBox.contentItem && typeof buttonBox.contentItem.forceLayout === "function")
            buttonBox.contentItem.forceLayout()
        // The box keeps its list at the width of the buttons it had partway
        // through (Qt 6.11) when its own width does not change, so a dialog
        // with the same buttons as the last one had them past the window's
        // edge: setting the alignment fits the list to them again.
        buttonBox.alignment = 0
        buttonBox.alignment = Qt.AlignRight
        if (visible) {
            raise()
            if (defaultButton)
                defaultButton.forceActiveFocus(Qt.TabFocusReason)
        } else {
            open()
        }
    }
    Connections {
        target: window.controller
        function onChanged() {
            if (window.controller.open)
                Qt.callLater(window.show)
            else if (window.visible)
                window.close()
        }
    }
    onRejected: if (controller.open) controller.finish(-1, values())

    // AD2: the ⋯ button and its menu, for a script's dialog.
    leadingActions: Button {
        id: moreButton
        objectName: "automationDialogMore"
        visible: window.controller.remembers
        text: qsTr("Answers and presets")
        display: AbstractButton.IconOnly
        implicitWidth: implicitHeight
        down: pressed || moreMenu.visible
        contentItem: Item {
            implicitWidth: 16
            implicitHeight: 16
            Icon {
                anchors.centerIn: parent
                iconRole: "panel-menu"
            }
        }
        Accessible.name: text
        ToolTip.visible: hovered && !moreMenu.visible
        ToolTip.text: text
        ToolTip.delay: 600
        onClicked: moreMenu.popup(moreButton, 0, -moreMenu.implicitHeight - 4)
        Keys.onPressed: event => window.trackAlt(event, true)
        Keys.onReleased: event => window.trackAlt(event, false)
    }
    ShellMenu {
        id: moreMenu
        objectName: "automationDialogMenu"
        ShellMenuItem {
            objectName: "dialogLastUsed"
            iconRole: "undo"
            text: qsTr("Last used")
            enabled: window.controller.hasLastUsed
            onTriggered: window.fill(window.controller.lastUsedValues())
        }
        ShellMenu {
            id: presetsMenu
            objectName: "dialogPresetsMenu"
            title: qsTr("Presets")
            iconRole: "save-as"
            Instantiator {
                model: window.controller.presets
                delegate: ShellMenuItem {
                    required property string modelData
                    objectName: "dialogPreset_" + modelData
                    text: modelData
                    onTriggered: window.fill(window.controller.presetValues(modelData))
                }
                onObjectAdded: (index, object) => presetsMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => presetsMenu.removeItem(object)
            }
            MenuSeparator {
                visible: window.controller.presets.length > 0
                height: visible ? implicitHeight : 0
            }
            ShellMenuItem {
                objectName: "dialogSavePreset"
                iconRole: "save-as"
                text: qsTr("Save as preset…")
                onTriggered: savePopover.openFor()
            }
            ShellMenu {
                id: deleteMenu
                objectName: "dialogDeletePresetMenu"
                title: qsTr("Delete preset")
                iconRole: "delete"
                enabled: window.controller.presets.length > 0
                Instantiator {
                    model: window.controller.presets
                    delegate: ShellMenuItem {
                        required property string modelData
                        objectName: "dialogDeletePreset_" + modelData
                        text: modelData
                        onTriggered: window.askDelete(modelData)
                    }
                    onObjectAdded: (index, object) => deleteMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => deleteMenu.removeItem(object)
                }
            }
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: "dialogReset"
            iconRole: "refresh"
            text: qsTr("Reset to the script’s values")
            onTriggered: window.fill(window.controller.scriptValues())
        }
    }
    // Save as preset…: a name over ⋯; Enter or Save saves (Replace for a
    // name in use), Esc, Cancel or a press outside closes it only.
    Popover {
        id: savePopover
        objectName: "savePresetPopover"
        title: qsTr("Save as preset")
        showTitle: true
        initialFocus: presetName
        property bool saving: false
        readonly property string name: presetName.text.trim()
        readonly property bool taken: window.controller.presets.indexOf(name) >= 0
        function openFor() {
            presetName.text = ""
            saving = false
            openAt(moreButton, window.contentItem)
        }
        function save() {
            if (name === "")
                return
            saving = true
            keep()
        }
        onKept: if (saving && name !== "") window.controller.savePreset(name, window.values())
        TextField {
            id: presetName
            objectName: "presetName"
            Layout.preferredWidth: 260
            placeholderText: qsTr("Preset name")
            Accessible.name: qsTr("Preset name")
            selectByMouse: true
            onAccepted: savePopover.save()
        }
        Label {
            objectName: "presetTaken"
            visible: savePopover.taken
            Layout.maximumWidth: 260
            text: qsTr("A preset named “%1” exists. Saving replaces it.").arg(savePopover.name)
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.muted
        }
        RowLayout {
            spacing: 8
            Layout.alignment: Qt.AlignRight
            PrimaryButton {
                objectName: "presetSave"
                text: savePopover.taken ? qsTr("Replace") : qsTr("Save")
                enabled: savePopover.name !== ""
                onClicked: savePopover.save()
            }
            Button {
                objectName: "presetCancel"
                text: qsTr("Cancel")
                onClicked: savePopover.revert()
            }
        }
    }

    // The script's buttons in its order at the trailing edge (all one role,
    // so the footer keeps the order), the Enter button in the accent.
    property var madeButtons: []
    function makeButtons() {
        for (const b of madeButtons) {
            buttonBox.removeItem(b)
            b.destroy()
        }
        const list = []
        controller.buttons.forEach((label, i) => {
            const kind = i === controller.enterButton ? primaryButton : plainButton
            const b = kind.createObject(null, { label: label, index: i, objectName: "dialogButton" + i })
            buttonBox.addItem(b)
            list.push(b)
        })
        madeButtons = list
        defaultButton = list[controller.enterButton] || null
    }
    Component {
        id: primaryButton
        PrimaryButton {
            id: b
            required property string label
            required property int index
            text: window.plain(label)
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            contentItem: Label {
                text: window.shownLabel(b.label)
                textFormat: Text.StyledText
                color: b.enabled ? Theme.onAccent : Theme.disabled
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: window.press(index)
            Keys.onPressed: event => window.trackAlt(event, true)
            Keys.onReleased: event => window.trackAlt(event, false)
        }
    }
    Component {
        id: plainButton
        Button {
            id: b
            required property string label
            required property int index
            text: window.plain(label)
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            contentItem: Label {
                text: window.shownLabel(b.label)
                textFormat: Text.StyledText
                color: b.enabled ? b.palette.buttonText : Theme.disabled
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: window.press(index)
            Keys.onPressed: event => window.trackAlt(event, true)
            Keys.onReleased: event => window.trackAlt(event, false)
        }
    }

    Item {
        id: keys
        objectName: "automationDialogContent"
        Layout.fillWidth: true
        Layout.fillHeight: true
        implicitWidth: scroll.implicitWidth
        implicitHeight: scroll.implicitHeight + (strip.visible ? strip.implicitHeight + 12 : 0)
                        + (noticeBar.visible ? noticeBar.implicitHeight + 12 : 0)
        // Alt held: the letters show (no control takes Alt; the buttons,
        // outside this item, track it too). Tab needs no help: the cells are
        // made in Tab order, then come the buttons.
        Keys.onPressed: event => window.trackAlt(event, true)
        Keys.onReleased: event => window.trackAlt(event, false)

        // The buttons' `&` letters.
        Repeater {
            model: window.controller.buttons
            delegate: Item {
                id: key
                required property string modelData
                required property int index
                Shortcut {
                    sequence: window.mnemonic(key.modelData) !== "" ? "Alt+" + window.mnemonic(key.modelData) : ""
                    enabled: sequence !== "" && window.visible
                    onActivated: {
                        const b = window.madeButtons[key.index]
                        if (b)
                            b.click()
                    }
                }
            }
        }

        // AD2-repeat-last-run: why a repeated run shows this dialog.
        InlineBar {
            id: noticeBar
            objectName: "automationDialogNotice"
            visible: window.controller.notice !== ""
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            text: window.controller.notice
            closable: false
        }

        Flickable {
            id: scroll
            objectName: "automationDialogScroll"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: noticeBar.visible ? noticeBar.bottom : parent.top
            anchors.topMargin: noticeBar.visible ? 12 : 0
            anchors.bottom: strip.visible ? strip.top : parent.bottom
            anchors.bottomMargin: strip.visible ? 12 : 0
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            readonly property bool overflows: contentHeight > height + 0.5
            readonly property int barWidth: 6
            // AD2: room for the first column's markers.
            readonly property int markerRoom: 6
            implicitWidth: markerRoom + grid.implicitWidth + barWidth + 4
            implicitHeight: grid.implicitHeight
            contentWidth: Math.max(markerRoom + grid.implicitWidth, width - barWidth - 4)
            contentHeight: window.hasTextbox ? Math.max(grid.implicitHeight, height) : grid.implicitHeight
            ScrollBar.vertical: ScrollBar {
                objectName: "automationDialogScrollBar"
                parent: scroll
                x: scroll.width - width
                y: 0
                height: scroll.height
                width: scroll.barWidth
                padding: 0
                policy: scroll.overflows ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                focusPolicy: Qt.NoFocus
                background: Rectangle {
                    implicitWidth: scroll.barWidth
                    radius: scroll.barWidth / 2
                    color: Theme.field
                    border.color: Theme.line
                }
                contentItem: Rectangle {
                    implicitWidth: scroll.barWidth
                    implicitHeight: 12
                    radius: scroll.barWidth / 2
                    color: parent.pressed ? Theme.text : Theme.muted
                    opacity: parent.pressed || parent.hovered ? 0.9 : 0.6
                }
            }
            ScrollBar.horizontal: ScrollBar {
                policy: scroll.contentWidth > scroll.width - scroll.barWidth - 4 + 0.5 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                focusPolicy: Qt.NoFocus
            }

            GridLayout {
                id: grid
                objectName: "automationDialogGrid"
                x: scroll.markerRoom
                width: scroll.contentWidth - scroll.markerRoom
                height: scroll.contentHeight
                columns: Math.max(1, window.controller.columns)
                rowSpacing: 6
                columnSpacing: 8

                // Brings the focused control into view.
                function reveal(item) {
                    const p = item.mapToItem(scroll.contentItem, 0, 0)
                    const across = scroll.width - scroll.barWidth - 4
                    if (p.x < scroll.contentX)
                        scroll.contentX = Math.max(0, p.x - 4)
                    else if (p.x + item.width > scroll.contentX + across)
                        scroll.contentX = Math.min(scroll.contentWidth - across, p.x + item.width - across + 4)
                    if (p.y < scroll.contentY)
                        scroll.contentY = Math.max(0, p.y - 4)
                    else if (p.y + item.height > scroll.contentY + scroll.height)
                        scroll.contentY = Math.min(scroll.contentHeight - scroll.height, p.y + item.height - scroll.height + 4)
                }

                Repeater {
                    id: cells
                    model: window.cellsInOrder
                    delegate: Loader {
                        id: cell
                        required property var modelData
                        required property int index
                        readonly property var spec: modelData.spec
                        readonly property int controlIndex: modelData.index
                        readonly property bool numeric: spec.kind === "intedit" || spec.kind === "floatedit"
                        // What the strip shows for it: its hint, and a number's range.
                        readonly property string stripText: {
                            const parts = []
                            if (spec.hint)
                                parts.push(spec.hint)
                            if (numeric && item && item.rangeText)
                                parts.push(item.rangeText + ".")
                            if (numeric && item && item.overflowText)
                                parts.push(item.overflowText)
                            return parts.join(" ")
                        }
                        // Its longest: a number's with the out-of-range sentence.
                        readonly property string longestStripText: {
                            const parts = []
                            if (spec.hint)
                                parts.push(spec.hint)
                            if (numeric && item && item.rangeText)
                                parts.push(item.rangeText + ".")
                            if (numeric && item)
                                parts.push(item.overflowLongest)
                            return parts.join(" ")
                        }
                        // AD2: the value shown differs from the script's.
                        readonly property bool differs: item !== null && item.current !== undefined
                                                        && window.controller.differs(controlIndex, item.current)
                        objectName: "control_" + controlIndex
                        Layout.row: spec.y
                        Layout.column: spec.x
                        Layout.rowSpan: spec.height
                        Layout.columnSpan: spec.width
                        Layout.fillWidth: {
                            if (spec.kind === "label" || spec.kind === "checkbox")
                                return false
                            for (let x = spec.x; x < spec.x + spec.width; ++x)
                                if (window.growingColumns[x])
                                    return true
                            return false
                        }
                        Layout.fillHeight: spec.kind === "textbox"
                        Layout.alignment: spec.kind === "label" ? Qt.AlignLeft | Qt.AlignVCenter
                                          : spec.kind === "checkbox" ? Qt.AlignLeft | Qt.AlignVCenter : 0
                        sourceComponent: {
                            switch (spec.kind) {
                            case "label": return labelControl
                            case "textbox": return textboxControl
                            case "intedit":
                            case "floatedit": return numberControl
                            case "dropdown": return dropdownControl
                            case "checkbox": return checkControl
                            case "color":
                            case "coloralpha": return colourControl
                            default: return editControl // edit, alpha
                            }
                        }
                        HoverHandler {
                            onHoveredChanged: {
                                if (hovered)
                                    window.hoveredCell = cell
                                else if (window.hoveredCell === cell)
                                    window.hoveredCell = null
                            }
                        }
                        // The marker in the column gap at the leading edge:
                        // 2 px of accent; hovering says the script's value
                        // and a click puts it back.
                        Item {
                            id: marker
                            objectName: "marker_" + cell.controlIndex
                            visible: cell.differs
                            x: -6
                            width: 6
                            height: cell.height
                            Rectangle {
                                x: 2
                                width: 2
                                height: parent.height
                                color: Theme.accent
                            }
                            HoverHandler {
                                id: markerHover
                                cursorShape: Qt.PointingHandCursor
                            }
                            TapHandler {
                                onTapped: window.resetControl(cell)
                            }
                            ToolTip.visible: markerHover.hovered
                            ToolTip.text: qsTr("Changed from the script’s %1. Click the bar to put it back.")
                                .arg(window.controller.scriptValueText(cell.controlIndex))
                        }
                    }
                }
            }
        }

        // The hint strip: one height while open, the tallest text it holds
        // up to three lines; no strip when nothing has a hint or a range.
        Item {
            id: strip
            objectName: "automationHints"
            visible: window.stripTexts.length > 0
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            implicitWidth: 0
            implicitHeight: 9 + Math.min(3 * lineHeight.height, tallest())
            function tallest() {
                let h = lineHeight.height
                for (let i = 0; i < measures.count; ++i)
                    h = Math.max(h, measures.itemAt(i).implicitHeight)
                return h
            }
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.line
            }
            Label {
                id: hintLabel
                objectName: "automationHintText"
                y: 9
                width: parent.width
                text: window.hintText
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                color: Theme.muted
            }
            Label { id: lineHeight; visible: false; text: "X" }
            Repeater {
                id: measures
                model: window.stripTexts
                delegate: Label {
                    required property string modelData
                    visible: false
                    width: strip.width
                    text: modelData
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Component {
        id: labelControl
        Label {
            id: label
            readonly property var spec: parent ? parent.spec : ({})
            readonly property var focusTargets: []
            text: window.shownLabel(spec.label)
            textFormat: Text.StyledText
            Accessible.description: window.descriptionOf(parent)
            ToolTip.text: spec.hint
            ToolTip.visible: labelHover.hovered && spec.hint !== ""
            HoverHandler { id: labelHover }
            function readValue() { return undefined }
            readonly property var current: undefined
            function writeValue(v) {}
            Shortcut {
                sequence: window.mnemonic(label.spec.label) !== "" ? "Alt+" + window.mnemonic(label.spec.label) : ""
                enabled: sequence !== "" && window.visible
                onActivated: window.focusAfter(label.parent)
            }
        }
    }
    Component {
        id: editControl
        TextField {
            id: edit
            readonly property var spec: parent ? parent.spec : ({})
            readonly property var focusTargets: [edit]
            text: spec.text
            selectByMouse: true
            font.family: spec.kind === "alpha" ? "monospace" : font.family
            Accessible.name: window.nameFor(spec)
            Accessible.description: window.descriptionOf(parent)
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            function readValue() { return text }
            readonly property var current: text
            function writeValue(v) { text = v }
        }
    }
    Component {
        id: textboxControl
        TextArea {
            id: textbox
            readonly property var spec: parent ? parent.spec : ({})
            readonly property var focusTargets: [textbox]
            text: spec.text
            selectByMouse: true
            wrapMode: TextEdit.NoWrap
            // At least two lines (Aegisub: 30 pixels).
            implicitHeight: Math.max(2 * font.pixelSize * 1.5 + topPadding + bottomPadding,
                                     contentHeight + topPadding + bottomPadding)
            Accessible.name: window.nameFor(spec)
            Accessible.description: window.descriptionOf(parent)
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            // Enter is a new line here; Ctrl+Enter presses the Enter button.
            // Tab moves on (a script's textbox takes no tabs, as in Aegisub).
            Keys.onPressed: event => {
                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                        && (event.modifiers & Qt.ControlModifier)) {
                    event.accepted = true
                    window.pressEnterButton()
                } else if ((event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
                           && !(event.modifiers & Qt.ControlModifier)) {
                    event.accepted = true
                    const back = event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier) !== 0
                    textbox.nextItemInFocusChain(!back).forceActiveFocus(back ? Qt.BacktabFocusReason : Qt.TabFocusReason)
                }
            }
            function readValue() { return text }
            readonly property var current: text
            function writeValue(v) { text = v }
        }
    }
    Component {
        id: numberControl
        // AD1-number-commit: any text can be typed; the value is clamped to
        // the range when the field is committed (Enter, leaving it, a button).
        // The arrows, the wheel, Up / Down and Page Up / Page Down step it by
        // 1 / 10 steps: an intedit by 1, a floatedit by its step
        // (AD1-float-step); a floatedit without one is a plain number field.
        // An intedit keeps legacy NumCtrl's right-button drag: 1 per 8 pixels
        // up or down, 10 per 10 pixels sideways.
        TextField {
            id: num
            objectName: "numberField"
            readonly property var spec: parent ? parent.spec : ({})
            readonly property var focusTargets: [num]
            readonly property bool isInt: spec.kind === "intedit"
            readonly property real lo: isInt ? spec.intMin : spec.numberMin
            readonly property real hi: isInt ? spec.intMax : spec.numberMax
            readonly property real step: isInt ? 1 : spec.step
            readonly property bool steps: step > 0
            readonly property bool hasLo: isInt ? lo > -2147483648 : lo > -1.7976931348623157e308
            readonly property bool hasHi: isInt ? hi < 2147483647 : hi < 1.7976931348623157e308
            // The committed value.
            property real value: isInt ? spec.intValue : spec.number
            readonly property real typed: parse(text)
            readonly property bool outOfRange: !isNaN(typed) && (typed < lo || typed > hi)
            // Text that is no number: it goes back to the value when committed.
            readonly property bool notANumber: isNaN(typed) && text.trim() !== ""
            readonly property bool bad: outOfRange || notANumber
            // An intedit's decimal, which the field cuts to a whole number.
            readonly property bool truncated: isInt && !isNaN(typed) && /[.,eE]/.test(text)
            readonly property string rangeText: {
                if (hasLo && hasHi)
                    return steps && !isInt ? qsTr("%1 to %2, in steps of %3").arg(shown(lo)).arg(shown(hi)).arg(shown(step))
                                           : qsTr("%1 to %2").arg(shown(lo)).arg(shown(hi))
                if (hasLo)
                    return qsTr("At least %1").arg(shown(lo))
                if (hasHi)
                    return qsTr("At most %1").arg(shown(hi))
                return ""
            }
            readonly property string overflowText: bad || truncated
                ? qsTr("%1 becomes %2 when you leave the field.").arg(text)
                      .arg(shown(outOfRange ? clamp(typed) : notANumber ? value : typed))
                : ""
            readonly property string overflowLongest: qsTr("%1 becomes %2 when you leave the field.")
                .arg(shown(hasHi ? hi : hasLo ? lo : value)).arg(shown(hasHi ? hi : hasLo ? lo : value))
            property bool holding: false
            property real wheelRest: 0
            property real oldX: 0
            property real oldY: 0

            // Decimal text only (a comma for the point, an exponent), as the
            // controller reads it: not 0x10 or Infinity. An intedit cuts a
            // decimal toward zero (legacy NumCtrl::GetInt).
            function parse(t) {
                const s = String(t).trim().replace(",", ".")
                if (!/^[-+]?([0-9]+\.?[0-9]*|\.[0-9]+)([eE][-+]?[0-9]+)?$/.test(s))
                    return NaN
                const n = Number(s)
                return !isFinite(n) ? NaN : isInt ? Math.trunc(n) : n
            }
            // The digits after the point a number is written with (-1: in
            // exponent form).
            function decimals(n) {
                const s = String(n)
                return s.indexOf("e") >= 0 ? -1 : (s.split(".")[1] || "").length
            }
            function clamp(n) { return Math.min(Math.max(n, lo), hi) }
            // The shortest text that reads back as the same number.
            function shown(n) { return isInt ? String(Math.trunc(n)) : String(Number(n)) }
            function commit() {
                const n = parse(text)
                value = isNaN(n) ? value : clamp(n)
                text = shown(value)
            }
            function stepBy(n) {
                const base = parse(text)
                const from = isNaN(base) ? value : clamp(base)
                let next = from + n * step
                // The sum to the digits its terms are written with: no float
                // noise (1.3000000000000003), nothing more dropped.
                const places = Math.max(decimals(from), decimals(step))
                if (!isInt && decimals(from) >= 0 && decimals(step) >= 0 && places <= 100)
                    next = Number(next.toFixed(places))
                value = clamp(isInt ? Math.round(next) : next)
                text = shown(value)
            }
            function readValue() {
                const n = parse(text)
                return shown(isNaN(n) ? value : clamp(n))
            }
            readonly property var current: readValue()
            function writeValue(v) {
                value = clamp(Number(v))
                text = shown(value)
            }

            Component.onCompleted: text = shown(value)
            selectByMouse: true
            rightPadding: steps ? arrows.width + 4 : 8
            placeholderText: rangeText.replace(/ to /, " – ")
            placeholderTextColor: Theme.muted
            color: bad ? Theme.danger : palette.text
            Accessible.name: window.nameFor(spec)
            Accessible.description: window.descriptionOf(parent)
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            onEditingFinished: commit()
            onActiveFocusChanged: if (!activeFocus) commit()
            Keys.onPressed: event => {
                if (!steps)
                    return
                const n = event.key === Qt.Key_Up ? 1 : event.key === Qt.Key_Down ? -1
                        : event.key === Qt.Key_PageUp ? 10 : event.key === Qt.Key_PageDown ? -10 : 0
                if (n !== 0) {
                    event.accepted = true
                    stepBy(n)
                }
            }
            implicitWidth: Math.max(96, contentWidth + leftPadding + rightPadding)
            Rectangle {
                objectName: "numberOutOfRange"
                anchors.fill: parent
                visible: num.bad
                color: "transparent"
                border.width: 2
                border.color: Theme.danger
                radius: 1
            }
            Column {
                id: arrows
                visible: num.steps
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.margins: 1
                width: 18
                Repeater {
                    model: [1, -1]
                    delegate: Rectangle {
                        id: arrow
                        required property int modelData
                        objectName: modelData > 0 ? "numberUp" : "numberDown"
                        width: arrows.width
                        height: arrows.height / 2
                        color: press.pressed ? Theme.select : press.containsMouse ? Theme.raised : Theme.panel
                        border.color: Theme.line
                        border.width: 0
                        Rectangle { width: 1; height: parent.height; color: Theme.line }
                        Rectangle { visible: arrow.modelData < 0; width: parent.width; height: 1; color: Theme.line }
                        Icon {
                            anchors.centerIn: parent
                            iconRole: "chevron-down"
                            size: 12
                            rotation: arrow.modelData > 0 ? 180 : 0
                        }
                        MouseArea {
                            id: press
                            anchors.fill: parent
                            hoverEnabled: true
                            onPressed: {
                                num.forceActiveFocus(Qt.MouseFocusReason)
                                num.stepBy(arrow.modelData)
                            }
                            onPressAndHold: repeat.start()
                            onReleased: repeat.stop()
                            onCanceled: repeat.stop()
                            Timer {
                                id: repeat
                                interval: 60
                                repeat: true
                                onTriggered: num.stepBy(arrow.modelData)
                            }
                        }
                    }
                }
            }
            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: num.steps ? arrows.width : 0
                acceptedButtons: num.isInt ? Qt.RightButton : Qt.NoButton
                preventStealing: true
                cursorShape: Qt.IBeamCursor
                onPressed: mouse => {
                    num.holding = true
                    num.oldX = mouse.x
                    num.oldY = mouse.y
                    num.forceActiveFocus(Qt.MouseFocusReason)
                }
                onReleased: num.holding = false
                onPositionChanged: mouse => {
                    if (!num.holding)
                        return
                    const absy = Math.abs(Math.trunc(mouse.y) - Math.trunc(num.oldY))
                    const absx = Math.abs(Math.trunc(mouse.x) - Math.trunc(num.oldX))
                    if (absy >= absx && absy >= 8) {
                        num.stepBy(num.oldY < mouse.y ? -1 : 1)
                        num.oldY = mouse.y
                        num.oldX = mouse.x
                    } else if (absx > absy && absx >= 10) {
                        num.stepBy(num.oldX > mouse.x ? -10 : 10)
                        num.oldX = mouse.x
                        num.oldY = mouse.y
                    }
                }
                onWheel: wheel => {
                    if (!num.steps) {
                        wheel.accepted = false
                        return
                    }
                    // Precision wheels and touchpads send parts of a notch.
                    num.wheelRest += wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.pixelDelta.y * 8
                    const notches = Math.trunc(num.wheelRest / 120)
                    num.wheelRest -= notches * 120
                    if (notches !== 0)
                        num.stepBy(notches)
                }
            }
        }
    }
    Component {
        id: dropdownControl
        // Ten items or fewer: a plain list (a letter jumps to the next item
        // starting with it), the keyboard staying on the dropdown. More:
        // typing (or Space) opens the list with a filter field, which takes
        // the keys and an input method's text; the matches keep the
        // script's order, the first is current, Enter picks it, Esc closes
        // the list only. A value that is none of the items shows blank and
        // is returned untouched (legacy).
        ComboBox {
            id: dropdown
            readonly property var spec: parent ? parent.spec : ({})
            readonly property var focusTargets: [dropdown]
            readonly property bool filters: spec.items.length > 10
            property string filter: ""
            readonly property var matches: {
                const out = []
                const f = filter.toLowerCase()
                spec.items.forEach((item, i) => {
                    if (f === "" || item.toLowerCase().indexOf(f) >= 0)
                        out.push(i)
                })
                return out
            }
            model: spec.items
            currentIndex: spec.items.indexOf(spec.text)
            Accessible.name: window.nameFor(spec)
            Accessible.description: window.descriptionOf(parent)
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== "" && !popup.visible
            // The script's value when it is none of the items (kept as it is).
            property string unlisted: spec.text
            function readValue() { return currentIndex >= 0 ? currentText : unlisted }
            readonly property var current: currentIndex >= 0 ? spec.items[currentIndex] : unlisted
            function writeValue(v) {
                unlisted = v
                currentIndex = spec.items.indexOf(v)
            }
            // The list opens below the dropdown, or above it when there is
            // more room there, at most as tall as that room (it scrolls) and
            // no wider than the dialog.
            function fit() {
                const top = dropdown.mapToItem(null, 0, 0).y
                const below = window.height - (top + dropdown.height + 2) - 4
                const above = top - 2 - 4
                list.above = above > below && list.wanted > below
                list.room = Math.max(60, list.above ? above : below)
                list.roomRight = window.width - dropdown.mapToItem(null, 0, 0).x - 8
            }
            onPressedChanged: if (pressed && !popup.visible) fit()
            Keys.onPressed: event => {
                if (popup.visible) {
                    listKey(event)
                    return
                }
                fit()
                if (filters && event.text.length === 1 && event.text > " "
                        && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier))) {
                    event.accepted = true
                    openFiltered(event.text)
                }
            }
            // A key while the list is open: it moves the current row, picks
            // it (Enter, Space without a filter), closes the list (Esc) or,
            // in a short list, jumps to an item.
            function listKey(event) {
                const view = popup.contentItem.view
                event.accepted = true
                if (event.key === Qt.Key_Down || event.key === Qt.Key_Up)
                    view.currentIndex = Math.max(0, Math.min(view.count - 1,
                                        view.currentIndex + (event.key === Qt.Key_Down ? 1 : -1)))
                else if (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp)
                    view.currentIndex = Math.max(0, Math.min(view.count - 1,
                                        view.currentIndex + (event.key === Qt.Key_PageDown ? 8 : -8)))
                else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                         || (event.key === Qt.Key_Space && filter === ""))
                    view.pick(view.currentIndex)
                else if (event.key === Qt.Key_Escape)
                    popup.close()
                else if (!filters && event.text.length === 1 && event.text > " "
                         && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier)))
                    typeAhead(event.text.toLowerCase())
                else {
                    // Tab closes the list and moves on; the rest are the dropdown's.
                    if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
                        popup.close()
                    event.accepted = false
                    return
                }
                if (popup.visible)
                    view.positionViewAtIndex(view.currentIndex, ListView.Contain)
            }
            // A letter in a short open list: the next row starting with it.
            function typeAhead(letter) {
                const view = popup.contentItem.view
                const n = matches.length
                for (let k = 1; k <= n; ++k) {
                    const row = (Math.max(0, view.currentIndex) + k) % n
                    if (spec.items[matches[row]].toLowerCase().startsWith(letter)) {
                        view.currentIndex = row
                        return
                    }
                }
            }
            function setFilter(text) {
                filter = text
                popup.contentItem.view.currentIndex = 0
            }
            function openFiltered(text) {
                filter = text
                popup.open()
            }
            popup: Popup {
                id: list
                // Set by fit() before each opening.
                property bool above: false
                property real room: 320
                property real roomRight: 180
                readonly property real wanted: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 320)
                y: above ? -height - 2 : dropdown.height + 2
                width: Math.max(60, Math.min(Math.max(dropdown.width, 180), roomRight))
                implicitHeight: Math.min(wanted, room)
                padding: 4
                // In the dialog: a list window would close on losing the keys.
                popupType: Popup.Item
                focus: false // the keys stay on the dropdown, or go to the filter
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                onAboutToShow: {
                    if (!dropdown.filters)
                        dropdown.filter = ""
                    view.currentIndex = dropdown.filters && dropdown.filter !== "" ? 0
                                        : Math.max(0, dropdown.matches.indexOf(dropdown.currentIndex))
                    if (dropdown.filters) {
                        filterField.text = dropdown.filter
                        filterField.forceActiveFocus(Qt.PopupFocusReason)
                    }
                }
                onAboutToHide: if (filterField.activeFocus) dropdown.forceActiveFocus(Qt.PopupFocusReason)
                onOpened: view.positionViewAtIndex(view.currentIndex, ListView.Contain)
                onClosed: dropdown.filter = ""
                background: PopoverBackground {}
                contentItem: ColumnLayout {
                    readonly property alias view: view
                    spacing: 4
                    // The filter; the list's keys go on to the dropdown.
                    TextField {
                        id: filterField
                        objectName: "dropdownFilter"
                        visible: dropdown.filters
                        Layout.fillWidth: true
                        focusPolicy: Qt.ClickFocus
                        placeholderText: qsTr("Type to filter")
                        onTextEdited: dropdown.setFilter(text)
                        Keys.onPressed: event => {
                            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                                event.accepted = true
                                const back = event.key === Qt.Key_Backtab || (event.modifiers & Qt.ShiftModifier) !== 0
                                list.close()
                                dropdown.nextItemInFocusChain(!back).forceActiveFocus(back ? Qt.BacktabFocusReason
                                                                                           : Qt.TabFocusReason)
                            } else if ([Qt.Key_Up, Qt.Key_Down, Qt.Key_PageUp, Qt.Key_PageDown, Qt.Key_Return,
                                        Qt.Key_Enter, Qt.Key_Escape].indexOf(event.key) >= 0) {
                                dropdown.listKey(event)
                            }
                        }
                    }
                    ListView {
                        id: view
                        objectName: "dropdownList"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, 260)
                        Layout.fillHeight: true
                        implicitHeight: contentHeight
                        clip: true
                        model: dropdown.matches
                        highlightMoveDuration: 0
                        function pick(row) {
                            if (row >= 0 && row < dropdown.matches.length) {
                                dropdown.currentIndex = dropdown.matches[row]
                                dropdown.activated(dropdown.currentIndex)
                            }
                            list.close()
                        }
                        delegate: ItemDelegate {
                            required property int modelData
                            required property int index
                            width: ListView.view.width
                            text: dropdown.spec.items[modelData]
                            highlighted: ListView.isCurrentItem
                            focusPolicy: Qt.NoFocus
                            onClicked: view.pick(index)
                        }
                        ScrollBar.vertical: ScrollBar {}
                    }
                }
            }
        }
    }
    Component {
        id: checkControl
        CheckBox {
            id: check
            readonly property var spec: parent ? parent.spec : ({})
            readonly property var focusTargets: [check]
            text: window.plain(spec.label)
            checked: spec.checked
            Accessible.name: window.plain(spec.label)
            Accessible.description: window.descriptionOf(parent)
            ToolTip.text: spec.hint
            ToolTip.visible: hovered && spec.hint !== ""
            contentItem: Label {
                leftPadding: check.indicator.width + check.spacing
                text: window.shownLabel(check.spec.label)
                textFormat: Text.StyledText
                verticalAlignment: Text.AlignVCenter
                color: check.enabled ? check.palette.windowText : Theme.disabled
            }
            function readValue() { return checked }
            readonly property var current: checked
            function writeValue(v) { checked = v }
            Shortcut {
                sequence: window.mnemonic(check.spec.label) !== "" ? "Alt+" + window.mnemonic(check.spec.label) : ""
                enabled: sequence !== "" && window.visible
                onActivated: {
                    check.forceActiveFocus(Qt.ShortcutFocusReason)
                    check.toggle()
                }
            }
        }
    }
    Component {
        id: colourControl
        // A swatch, which opens the colour popover, and a field taking
        // #RRGGBB, RRGGBB or ASS &HBBGGRR& (and the alpha: #RRGGBBAA or
        // &HAABBGGRR& for coloralpha; AA is the ASS transparency, 0 opaque).
        // The script gets the value as before (the controller's legacy hex).
        RowLayout {
            id: colourRow
            readonly property var spec: parent ? parent.spec : ({})
            readonly property var focusTargets: [pickButton, hexField]
            property color value: spec.color
            readonly property bool alpha: spec.kind === "coloralpha"
            property bool editing: false
            spacing: 6
            function readValue() { return value }
            readonly property var current: value
            function writeValue(v) {
                editing = false
                value = v
            }
            function hex2(n) { return ("0" + Math.round(n).toString(16).toUpperCase()).slice(-2) }
            function shownText(c) {
                return "#" + hex2(c.r * 255) + hex2(c.g * 255) + hex2(c.b * 255)
                       + (alpha ? hex2(255 - c.a * 255) : "")
            }
            // A typed colour, or null.
            function parsed(text) {
                const t = String(text).trim()
                let m = /^#?([0-9A-Fa-f]{6})([0-9A-Fa-f]{2})?$/.exec(t)
                if (m && (!m[2] || alpha)) {
                    const v = parseInt(m[1], 16)
                    const tr = m[2] ? parseInt(m[2], 16) : 0
                    return Qt.rgba(((v >> 16) & 255) / 255, ((v >> 8) & 255) / 255, (v & 255) / 255, (255 - tr) / 255)
                }
                m = /^&[Hh]([0-9A-Fa-f]{2})?([0-9A-Fa-f]{6})&?$/.exec(t)
                if (m && (!m[1] || alpha)) {
                    const v = parseInt(m[2], 16)
                    const tr = m[1] ? parseInt(m[1], 16) : 0
                    return Qt.rgba((v & 255) / 255, ((v >> 8) & 255) / 255, ((v >> 16) & 255) / 255, (255 - tr) / 255)
                }
                return null
            }
            onValueChanged: if (!editing) hexField.text = shownText(value)
            Component.onCompleted: hexField.text = shownText(value)
            // SY2: the colour popover at the value; its alpha is the ASS
            // transparency (255 - the colour's own alpha).
            function pick() {
                const row = colourRow
                let before = null
                colourPopover.openFor(pickButton, {
                    changeColour: c => row.value = Qt.rgba(c.r / 255, c.g / 255, c.b / 255, row.alpha ? (255 - c.a) / 255 : 1),
                    endDialog: accepted => { if (!accepted) row.value = before }
                }, () => {
                    // A copy, once the colour open before has ended: `value` reads back live.
                    before = Qt.rgba(row.value.r, row.value.g, row.value.b, row.value.a)
                    window.colourPicker.opened("automation")
                    return { r: Math.round(before.r * 255), g: Math.round(before.g * 255), b: Math.round(before.b * 255),
                             a: row.alpha ? 255 - Math.round(before.a * 255) : 0 }
                }, { title: window.nameFor(spec), alpha: alpha, focus: pickButton })
            }
            Button {
                id: pickButton
                objectName: "colorPick"
                implicitWidth: 40
                Accessible.name: window.nameFor(colourRow.spec)
                Accessible.description: window.descriptionOf(colourRow.parent)
                ToolTip.text: colourRow.spec.hint
                ToolTip.visible: hovered && colourRow.spec.hint !== ""
                onClicked: colourRow.pick()
                contentItem: Item {
                    implicitWidth: 28
                    implicitHeight: 16
                    Canvas {
                        id: checkerboard
                        anchors.centerIn: parent
                        width: 28
                        height: 16
                        visible: colourRow.alpha
                        onPaint: {
                            const ctx = getContext("2d")
                            for (let y = 0; y < height; y += 4)
                                for (let x = 0; x < width; x += 4) {
                                    ctx.fillStyle = ((x + y) / 4) % 2 ? "#bbbbbb" : "#ffffff"
                                    ctx.fillRect(x, y, 4, 4)
                                }
                        }
                    }
                    Rectangle {
                        objectName: "colorSwatch"
                        anchors.centerIn: parent
                        width: 28
                        height: 16
                        color: colourRow.value
                        border.color: Theme.control
                    }
                }
            }
            TextField {
                id: hexField
                objectName: "colorText"
                Layout.fillWidth: true
                Layout.minimumWidth: 96
                font.family: "monospace"
                selectByMouse: true
                readonly property bool bad: colourRow.editing && colourRow.parsed(text) === null
                color: bad ? Theme.danger : palette.text
                Accessible.name: window.nameFor(colourRow.spec)
                Accessible.description: window.descriptionOf(colourRow.parent)
                ToolTip.text: colourRow.spec.hint
                ToolTip.visible: hovered && colourRow.spec.hint !== ""
                onTextEdited: {
                    colourRow.editing = true
                    const c = colourRow.parsed(text)
                    if (c)
                        colourRow.value = c
                }
                Rectangle {
                    anchors.fill: parent
                    visible: hexField.bad
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.danger
                    radius: 1
                }
                onEditingFinished: done()
                onActiveFocusChanged: if (!activeFocus) done()
                function done() {
                    colourRow.editing = false
                    text = colourRow.shownText(colourRow.value)
                }
            }
        }
    }
    ColourPopover {
        id: colourPopover
        objectName: "automationColourPopover"
        picker: window.colourPicker
    }
}
