pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: field
    required property VisualToolsController tools
    required property string name
    required property var value
    property bool measuring: false
    property bool named: !measuring
    property string accessibleName: value.label
    readonly property bool editable: value.editable
    readonly property bool scrubbing: scrub.active
    readonly property bool shown: editable || value.text.length > 0
    spacing: 3

    TextMetrics {
        id: numberWidth
        font: Qt.application.font
        text: "-00000.00"
    }
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
                input.forceActiveFocus(Qt.MouseFocusReason)
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
                if (!field.tools.scrubValue(field.name, text))
                    return
                current = next
                sent = text
                input.text = text
            }
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
        Keys.onShortcutOverride: event => {
            const plain = (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) === 0
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

    function format(v) {
        return String(Number(v.toFixed(3)))
    }
    function restore() {
        input.editing = false
        input.text = field.value.text
    }
    onValueChanged: if (!input.editing && !scrub.active) input.text = value.text
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
