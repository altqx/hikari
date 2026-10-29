pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// VT1: one of the active tool's values in the inspector bar, its label in
// the muted colour. An editable value is a number field in tabular figures
// with its unit inside (%, °, ms): typed and applied by Enter, or scrubbed
// by dragging its label sideways (the value's step per pixel, a tenth of it
// with Shift), the video showing each sample. Enter, and each drag's
// release, is one history step; Esc during a drag drops it (the tool's
// handles go back), Esc while typing puts the value back. A read-only value
// (the crosshair's position) is its number, shown while it has one; its
// place stays in the bar without one, so the bar does not re-wrap.
//
// HY1: a colour's first value has its swatch before it, and the value a
// drag on the video changes is marked (◆); a press on a value marks it.
RowLayout {
    id: field
    required property VisualToolsController tools
    required property string name
    // The tool's value (name, label, text, editable, group, unit, step),
    // looked up by name so the field is not rebuilt as the value changes.
    required property var value
    // VT1: an unseen copy the bar measures (no names).
    property bool measuring: false
    // VB1: named, unless unseen (measured, or folded off the bar's line).
    property bool named: !measuring
    // Assistive technology's name: the tool, the group and the label.
    property string accessibleName: value.label
    readonly property bool editable: value.editable
    readonly property bool scrubbing: scrub.active
    readonly property bool shown: editable || value.text.length > 0
    spacing: 3

    // The fields fit a coordinate ("-00000.00") and the unit, not the panel.
    TextMetrics {
        id: numberWidth
        font: Qt.application.font
        text: "-00000.00"
    }
    // A read-only value is the crosshair's whole pixels.
    TextMetrics {
        id: readoutWidth
        font: Qt.application.font
        text: "-00000"
    }

    Rectangle {
        objectName: !field.named ? "" : "visualValueSwatch_" + field.name
        visible: (field.value.swatch ?? "").length > 0
        Layout.preferredWidth: 22
        Layout.preferredHeight: 22
        Layout.alignment: Qt.AlignVCenter
        Layout.rightMargin: 3
        radius: 2
        color: visible ? field.value.swatch : "transparent"
        border.color: Theme.control
        Accessible.ignored: true
    }
    Label {
        objectName: !field.named ? "" : "visualValueMark_" + field.name
        // Its place stays for every value of a tool that marks one, so the
        // mark moving does not move the bar.
        visible: field.value.marked !== undefined && field.editable
        opacity: field.value.marked ? 1 : 0
        text: "\u25C6"
        color: Theme.accent
        font.pixelSize: 9
        Layout.alignment: Qt.AlignVCenter
        Accessible.ignored: true
    }
    Label {
        id: label
        objectName: !field.named ? "" : "visualValueLabel_" + field.name
        text: field.value.label
        opacity: field.shown ? 1 : 0
        Accessible.ignored: !field.shown
        color: scrub.active ? Theme.accent : Theme.muted
        Layout.alignment: Qt.AlignVCenter
        // The drag handle: a dotted underline says it can be dragged.
        Rectangle {
            visible: field.editable && input.enabled
            anchors { left: parent.left; right: parent.right; top: parent.bottom }
            height: 1
            color: "transparent"
            Row {
                anchors.fill: parent
                spacing: 1
                Repeater {
                    model: Math.ceil(label.width / 2)
                    Rectangle { width: 1; height: 1; color: scrub.active ? Theme.accent : Theme.control }
                }
            }
        }
        MouseArea {
            id: scrub
            objectName: !field.named ? "" : "visualValueScrub_" + field.name
            anchors.fill: parent
            anchors.margins: -3
            enabled: field.editable && input.enabled
            cursorShape: enabled ? Qt.SizeHorCursor : Qt.ArrowCursor
            preventStealing: true
            property bool active: false
            property bool cancelled: false
            property real lastX: 0
            property real current: 0
            property string sent: ""
            onPressed: mouse => {
                field.tools.markValue(field.name)
                const v = Number(field.value.text)
                current = isFinite(v) ? v : 0
                lastX = mapToItem(null, mouse.x, mouse.y).x
                active = false
                cancelled = false
                sent = ""
                input.forceActiveFocus(Qt.MouseFocusReason) // Esc reaches the field
            }
            onPositionChanged: mouse => {
                if (cancelled)
                    return
                const x = mapToItem(null, mouse.x, mouse.y).x
                const dx = x - lastX
                if (!active && Math.abs(dx) < 3)
                    return
                lastX = x
                active = true
                const step = field.value.step * ((mouse.modifiers & Qt.ShiftModifier) ? 0.1 : 1)
                const next = current + Math.round(dx) * step
                const text = field.format(next)
                if (text === sent)
                    return
                // A sample the tool refuses (Scale below zero) is not taken:
                // the field and the release keep the last one it showed.
                if (!field.tools.scrubValue(field.name, text))
                    return
                current = next
                sent = text
                input.text = text
            }
            // A scrub whose gesture the controller ended (the active Line or
            // the Document changed under it: refresh() escapes) is over: its
            // later moves and its release write nothing (Codex on #293).
            Connections {
                target: field.tools
                function onChanged() {
                    if (scrub.active && !scrub.cancelled && scrub.sent.length > 0 && !field.tools.gestureActive)
                        scrub.cancelled = true
                }
            }
            onReleased: {
                if (active && !cancelled && sent.length > 0)
                    field.tools.setValue(field.name, sent)
                field.finish()
            }
            onCanceled: {
                if (active && !cancelled)
                    field.tools.escape()
                field.finish()
            }
        }
    }
    TextField {
        id: input
        objectName: field.editable && field.named ? "visualValue_" + field.name : ""
        visible: field.editable
        enabled: field.tools.railEnabled
        // Typed text stays until Enter or Esc: the tool's refreshes (the
        // pointer over the video, a frame) do not put the value back.
        property bool editing: false
        selectByMouse: true
        implicitHeight: 24
        topPadding: 0
        bottomPadding: 0
        rightPadding: (unit.text.length > 0 ? unit.implicitWidth + 4 : 0) + 6
        implicitWidth: Math.ceil(numberWidth.width) + leftPadding + rightPadding
        horizontalAlignment: TextInput.AlignRight
        font.features: { "tnum": 1 }
        Accessible.name: field.accessibleName
        Accessible.description: field.value.unit
        onTextEdited: editing = true
        onAccepted: {
            field.tools.setValue(field.name, text)
            field.restore()
        }
        // The field's keys come before the window's shortcuts (the digits
        // and the arrows are bound elsewhere): its typing, its editing keys
        // and Esc, and the standard copy, paste and undo.
        Keys.onShortcutOverride: event => {
            const plain = (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) === 0
            // Esc is the field's while it has something to put back, else the
            // tool's (a pending point or step) when it has one (Codex on #293).
            if (event.key === Qt.Key_Escape) {
                event.accepted = plain && (input.editing || scrub.active || field.tools.escapable)
                return
            }
            event.accepted = (plain && ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Home, Qt.Key_End,
                                         Qt.Key_Backspace, Qt.Key_Delete, Qt.Key_Return, Qt.Key_Enter].includes(event.key)
                                        || (event.text.length > 0 && event.text >= " ")))
                || [StandardKey.Copy, StandardKey.Cut, StandardKey.Paste, StandardKey.Undo, StandardKey.Redo,
                    StandardKey.SelectAll].some(k => event.matches(k))
        }
        Keys.onEscapePressed: event => {
            if (scrub.active || input.editing) {
                if (scrub.active && !scrub.cancelled) {
                    scrub.cancelled = true
                    field.tools.escape()
                }
                field.restore()
                event.accepted = true
                return
            }
            // Nothing typed or scrubbed: the tool's Esc, as over the video.
            event.accepted = field.tools.escape()
        }
        onActiveFocusChanged: {
            if (!activeFocus)
                field.restore()
            else if (!field.measuring)
                field.tools.markValue(field.name)
        }
        Label {
            id: unit
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: field.value.unit
            color: Theme.muted
        }
    }
    // A read-only value keeps a whole coordinate's width, right-aligned, so
    // the groups after it do not move as the pointer crosses the video.
    Label {
        objectName: field.editable || !field.named ? "" : "visualValue_" + field.name
        visible: !field.editable
        text: field.value.text
        font.features: { "tnum": 1 }
        horizontalAlignment: Text.AlignRight
        Layout.preferredWidth: Math.max(implicitWidth, Math.ceil(readoutWidth.width))
        Layout.rightMargin: 4
        Accessible.name: field.accessibleName
        Accessible.ignored: !field.shown
    }

    // The scrub's text: up to three decimals, as the tags hold them.
    function format(v) {
        return String(Number(v.toFixed(3)))
    }
    // The field shows the tool's value again (after Enter, Esc or a drag),
    // and follows it while it is not typed in or scrubbed.
    function restore() {
        input.editing = false
        input.text = field.value.text
    }
    onValueChanged: if (!input.editing && !scrub.active) input.text = value.text
    // Typed text belongs to what it was typed for: another Document, active
    // Line or picked Lines put the value back (Codex on #293).
    readonly property int editTarget: tools.editTarget
    onEditTargetChanged: if (input.editing) restore()
    Component.onCompleted: input.text = value.text
    function finish() {
        scrub.active = false
        scrub.cancelled = false
        scrub.sent = ""
        restore()
    }
}
