pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "findFadeDialog"
    dialogId: "findFade"
    required property var controller
    title: qsTr("Find fade")
    modal: true
    resizable: true
    defaultButton: applyButton
    initialFocus: still

    function timeText(ms) {
        const cs = Math.floor(ms / 10) % 100, s = Math.floor(ms / 1000) % 60
        const m = Math.floor(ms / 60000) % 60, h = Math.floor(ms / 3600000)
        return h + ":" + String(m).padStart(2, "0") + ":" + String(s).padStart(2, "0") + "." + String(cs).padStart(2, "0")
    }
    function problemText(problem) {
        switch (problem) {
        case "noVideo": return qsTr("Open a video first.")
        case "noLine": return qsTr("Select a line first.")
        case "outsideLine": return qsTr("Move the video to a frame of the line where the sign is fully shown.")
        case "decodeFailed": return qsTr("A frame of the video could not be read.")
        case "readOnly": return qsTr("The subtitles are read-only.")
        case "noTrack": return qsTr("The clipboard holds no After Effects tracking data.")
        case "trackLength": return qsTr("The tracking data doesn’t have one row for each frame of the line.")
        case "fadeInNotFound": return qsTr("No frame up to this one has the point’s colour, so the fade in can’t be found.")
        case "fadeOutNotFound": return qsTr("The point keeps its colour to the end of the line, so the fade out can’t be found.")
        }
        return ""
    }
    function openDialog() {
        if (!controller.open("fade"))
            return problemText(controller.problem)
        open()
        return ""
    }
    function apply() {
        if (controller.ready && !controller.reading && controller.result.length > 0)
            controller.apply()
    }

    onVisibleChanged: if (!visible) controller.close()

    Connections {
        target: dialog.controller
        function onApplied() { dialog.accept() }
    }

    Label {
        text: qsTr("Line %1, %2 to %3, %n frames. The point’s colour on this frame is the fully shown colour.", "",
                   dialog.controller.lineFrames)
            .arg(dialog.controller.lineNumber, dialog.timeText(dialog.controller.startMs), dialog.timeText(dialog.controller.endMs))
        color: Theme.muted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        Layout.preferredWidth: 584
    }
    VideoPointPicker {
        id: still
        controller: dialog.controller
        namePrefix: "findFade"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumWidth: 240
        Layout.minimumHeight: 135
        onAccepted: dialog.apply()
    }
    GridLayout {
        columns: 2
        columnSpacing: 8
        rowSpacing: 6
        enabled: dialog.controller.ready
        Label { text: qsTr("Point") }
        RowLayout {
            spacing: 8
            Label { text: "X"; Accessible.ignored: true }
            SpinBox {
                objectName: "findFadeX"
                from: 0
                to: Math.max(0, dialog.controller.frameWidth - 1)
                editable: true
                value: dialog.controller.pointX
                Accessible.name: qsTr("Point X")
                onValueModified: dialog.controller.pick(value, dialog.controller.pointY)
            }
            Label { text: "Y"; Accessible.ignored: true }
            SpinBox {
                objectName: "findFadeY"
                from: 0
                to: Math.max(0, dialog.controller.frameHeight - 1)
                editable: true
                value: dialog.controller.pointY
                Accessible.name: qsTr("Point Y")
                onValueModified: dialog.controller.pick(dialog.controller.pointX, value)
            }
            Label { text: qsTr("Colour") }
            Rectangle {
                objectName: "findFadeColour"
                implicitWidth: 22
                implicitHeight: 22
                radius: 3
                color: dialog.controller.colour
                border.color: Theme.line
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Colour %1").arg(String(dialog.controller.colour).toUpperCase())
            }
            CheckBox {
                objectName: "findFadeFollow"
                text: qsTr("Follow tracking data")
                checked: dialog.controller.follow
                onToggled: dialog.controller.follow = checked
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Moves the point with After Effects tracking data from the clipboard, one row for each frame of the line")
            }
        }
        Label { text: qsTr("Find") }
        RowLayout {
            spacing: 8
            CheckBox {
                objectName: "findFadeIn"
                text: qsTr("Fade in")
                checked: dialog.controller.fadeIn
                onToggled: dialog.controller.fadeIn = checked
            }
            CheckBox {
                objectName: "findFadeOut"
                text: qsTr("Fade out")
                checked: dialog.controller.fadeOut
                onToggled: dialog.controller.fadeOut = checked
            }
            Label { text: qsTr("Tolerance") }
            SpinBox {
                objectName: "findFadeTolerance"
                from: 0
                to: 442
                editable: true
                value: dialog.controller.tolerance
                Accessible.name: qsTr("Tolerance")
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("How far, in RGB, a frame’s colour may be from the point’s and still count as shown")
                onValueModified: dialog.controller.tolerance = value
            }
        }
    }
    ColumnLayout {
        spacing: 2
        Layout.fillWidth: true
        Label { text: qsTr("Distance from the colour on each frame"); font.bold: true }
        Canvas {
            id: chart
            objectName: "findFadeChart"
            Layout.fillWidth: true
            implicitHeight: 110
            Accessible.role: Accessible.Chart
            Accessible.name: qsTr("Distance from the colour on each frame")
            readonly property var values: dialog.controller.distances
            readonly property int inIndex: dialog.controller.fadeInIndex
            readonly property int outIndex: dialog.controller.fadeOutIndex
            readonly property real tolerance: dialog.controller.tolerance
            onValuesChanged: requestPaint()
            onInIndexChanged: requestPaint()
            onOutIndexChanged: requestPaint()
            onToleranceChanged: requestPaint()
            onWidthChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = Theme.field
                ctx.fillRect(0, 0, width, height)
                ctx.strokeStyle = Theme.line
                ctx.strokeRect(0.5, 0.5, width - 1, height - 1)
                const n = values.length
                if (n < 2)
                    return
                let top = tolerance * 4
                for (const v of values)
                    top = Math.max(top, v)
                const xAt = i => 4 + i * (width - 8) / (n - 1)
                const yAt = v => height - 6 - v * (height - 24) / Math.max(1, top)
                ctx.strokeStyle = Theme.warning
                ctx.setLineDash([5, 4])
                ctx.beginPath()
                ctx.moveTo(0, yAt(tolerance))
                ctx.lineTo(width, yAt(tolerance))
                ctx.stroke()
                ctx.setLineDash([])
                ctx.strokeStyle = Theme.accent
                ctx.lineWidth = 2
                ctx.beginPath()
                let pen = false
                for (let i = 0; i < n; ++i) {
                    if (values[i] < 0) {
                        pen = false
                        continue
                    }
                    if (pen)
                        ctx.lineTo(xAt(i), yAt(values[i]))
                    else
                        ctx.moveTo(xAt(i), yAt(values[i]))
                    pen = true
                }
                ctx.stroke()
                ctx.lineWidth = 1
                ctx.strokeStyle = Theme.text
                ctx.fillStyle = Theme.text
                ctx.font = "12px sans-serif"
                if (inIndex >= 0) {
                    ctx.beginPath()
                    ctx.moveTo(xAt(inIndex), 4)
                    ctx.lineTo(xAt(inIndex), height - 4)
                    ctx.stroke()
                    ctx.textAlign = "left"
                    ctx.fillText(qsTr("fade in ends, frame %1").arg(inIndex + 1), xAt(inIndex) + 6, 16)
                }
                if (outIndex >= 0) {
                    ctx.beginPath()
                    ctx.moveTo(xAt(outIndex), 4)
                    ctx.lineTo(xAt(outIndex), height - 4)
                    ctx.stroke()
                    ctx.textAlign = "right"
                    ctx.fillText(qsTr("fade out starts, frame %1").arg(outIndex + 1), xAt(outIndex) - 6, 16)
                }
            }
        }
    }
    RowLayout {
        spacing: 8
        Layout.fillWidth: true
        Label { text: qsTr("Result"); font.bold: true }
        TextField {
            objectName: "findFadeResult"
            Layout.fillWidth: true
            readOnly: true
            font.family: "monospace"
            text: dialog.controller.result
            Accessible.name: qsTr("Result")
        }
    }
    FieldError {
        objectName: "findFadeProblem"
        text: dialog.problemText(dialog.controller.problem)
        Layout.fillWidth: true
    }

    leadingActions: Label {
        objectName: "findFadeProgress"
        color: Theme.muted
        text: dialog.controller.reading
              ? qsTr("Reading frame %1 of %2…").arg(dialog.controller.framesRead + 1, dialog.controller.framesToRead)
              : qsTr("Read %1 of %2 frames").arg(dialog.controller.framesRead, dialog.controller.framesToRead)
    }
    buttons: [
        PrimaryButton {
            id: applyButton
            objectName: "findFadeApply"
            text: qsTr("Apply")
            enabled: dialog.controller.ready && !dialog.controller.reading && dialog.controller.result.length > 0
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.apply()
        },
        Button {
            objectName: "findFadeCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.reject()
        }
    ]
}
