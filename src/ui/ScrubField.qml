pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.NodeEffects"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

FocusScope {
    id: scrub
    property real value: 0
    property string placeholder: ""
    readonly property bool blank: placeholder !== ""
    property string unit: ""
    property var minimum: undefined
    property var maximum: undefined
    property real step: 1
    property real fineStep: 0.1
    property int decimals: 2
    property bool refused: false
    property font valueFont: Qt.application.font
    readonly property alias editing: input.activeFocus
    readonly property real pixelsPerStep: 4
    signal edited(real value)
    signal typed(string text)
    signal finished()

    implicitWidth: 96
    implicitHeight: Theme.controlHeight
    activeFocusOnTab: true
    Accessible.role: Accessible.SpinBox
    property string stateText: ""
    Accessible.description: [input.text, stateText].filter(t => t !== "").join(", ")

    function clamped(v) {
        if (scrub.minimum !== undefined && v < scrub.minimum)
            v = scrub.minimum
        if (scrub.maximum !== undefined && v > scrub.maximum)
            v = scrub.maximum
        return v
    }
    function shown(v) {
        return String(Number(v.toFixed(scrub.decimals)))
    }
    function display() {
        input.text = scrub.blank && !input.activeFocus ? scrub.placeholder : scrub.shown(scrub.value)
    }
    function stepBy(count, fine) {
        const base = scrub.blank ? (scrub.minimum ?? 0) : scrub.value
        const next = scrub.clamped(base + count * (fine ? scrub.fineStep : scrub.step))
        const rounded = Number(next.toFixed(scrub.decimals))
        if (rounded !== scrub.value || scrub.blank)
            scrub.edited(rounded)
    }

    onValueChanged: display()
    onPlaceholderChanged: display()
    Component.onCompleted: display()

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: Theme.field
        border.width: 1
        border.color: scrub.refused ? Theme.danger
                      : !scrub.enabled ? Theme.disabled
                      : input.activeFocus ? Theme.accent
                      : drag.containsMouse ? Theme.text
                      : Theme.control
        Rectangle {
            objectName: "focusRing"
            anchors.fill: parent
            anchors.margins: -1
            radius: parent.radius + 1
            color: "transparent"
            border.width: 1
            border.color: Theme.focus
            visible: input.activeFocus && !input.mouseFocus
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 1
        anchors.rightMargin: 1
        spacing: 0

        component Stepper: AbstractButton {
            id: stepper
            required property int direction
            Layout.fillHeight: true
            implicitWidth: 16
            focusPolicy: Qt.NoFocus
            enabled: scrub.enabled
            autoRepeat: true
            Accessible.name: direction < 0 ? qsTr("Step down") : qsTr("Step up")
            contentItem: Text {
                text: stepper.direction < 0 ? "‹" : "›"
                color: stepper.enabled ? Theme.muted : Theme.disabled
                font: scrub.valueFont
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.radius
                color: stepper.pressed ? Theme.pressed : stepper.hovered && stepper.enabled ? Theme.hover : "transparent"
            }
            onClicked: scrub.stepBy(direction, false)
        }

        Stepper {
            objectName: scrub.objectName + "Down"
            direction: -1
        }
        TextInput {
            id: input
            objectName: scrub.objectName + "Input"
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            clip: true
            enabled: scrub.enabled
            font: scrub.valueFont
            color: scrub.blank && !activeFocus ? Theme.muted : scrub.enabled ? Theme.text : Theme.muted
            selectionColor: Theme.select
            selectedTextColor: Theme.text
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            selectByMouse: true
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            onActiveFocusChanged: {
                if (activeFocus) {
                    text = scrub.blank ? "" : scrub.shown(scrub.value)
                    selectAll()
                } else {
                    mouseFocus = false
                    scrub.display()
                }
            }
            property bool retyped: false
            property bool mouseFocus: false
            onTextEdited: retyped = true
            onEditingFinished: {
                if (retyped) {
                    retyped = false
                    scrub.typed(text)
                }
                scrub.finished()
            }
            Keys.onUpPressed: event => scrub.stepBy(1, event.modifiers & Qt.ShiftModifier)
            Keys.onDownPressed: event => scrub.stepBy(-1, event.modifiers & Qt.ShiftModifier)
            Keys.onEscapePressed: event => {
                event.accepted = retyped
                retyped = false
                text = scrub.blank ? "" : scrub.shown(scrub.value)
            }

            MouseArea {
                id: drag
                objectName: scrub.objectName + "Drag"
                property real pressX: 0
                property real startValue: 0
                property bool scrubbing: false
                anchors.fill: parent
                enabled: !input.activeFocus && scrub.enabled
                hoverEnabled: true
                cursorShape: Qt.SizeHorCursor
                preventStealing: true
                onPressed: mouse => {
                    pressX = mouse.x
                    startValue = scrub.blank ? (scrub.minimum ?? 0) : scrub.value
                    scrubbing = false
                }
                onPositionChanged: mouse => {
                    if (!pressed)
                        return
                    const dx = mouse.x - pressX
                    if (!scrubbing && Math.abs(dx) < Application.styleHints.startDragDistance)
                        return
                    scrubbing = true
                    const fine = mouse.modifiers & Qt.ShiftModifier
                    const steps = Math.round(dx / scrub.pixelsPerStep)
                    const next = Number(scrub.clamped(startValue + steps * (fine ? scrub.fineStep : scrub.step)).toFixed(scrub.decimals))
                    if (next !== scrub.value || scrub.blank)
                        scrub.edited(next)
                }
                onReleased: {
                    if (scrubbing)
                        scrub.finished()
                    else {
                        input.mouseFocus = true
                        input.forceActiveFocus(Qt.MouseFocusReason)
                    }
                    scrubbing = false
                }
            }
        }
        Label {
            visible: scrub.unit !== ""
            text: scrub.unit
            color: Theme.muted
            font: TypeRoles.sized(scrub.valueFont, 12, Font.Normal)
            Layout.rightMargin: 2
        }
        Stepper {
            objectName: scrub.objectName + "Up"
            direction: 1
        }
    }
}
