pragma Translator: "HikariSub.Audio"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Window {
    id: calibration
    objectName: "tapCalibrationWindow"
    required property var audio
    property bool edited: false
    color: Theme.panel // a Window draws white unless told
    title: qsTr("Calibrate tap latency")
    flags: Qt.Dialog
    modality: Qt.ApplicationModal
    width: 480
    height: content.implicitHeight + 36
    minimumWidth: 400
    minimumHeight: height
    Component.onCompleted: IconTheme.setWindowIcon(calibration, "tap-calibrate")

    function openWindow() {
        latency.value = audio.tapLatency
        edited = false
        show()
        raise()
        requestActivate()
        keys.forceActiveFocus()
        start()
    }
    function start() {
        edited = false
        keys.forceActiveFocus()
        audio.startCalibration()
    }
    function apply() {
        audio.stopCalibration()
        // SpinBox.value lags the typed text until Enter commits it.
        const typed = latency.valueFromText(latency.contentItem.text, latency.locale)
        audio.setTapLatency(Math.max(latency.from, Math.min(latency.to, typed)))
        close()
    }
    onClosing: audio.stopCalibration()
    Connections {
        target: calibration.audio
        function onCalibrationChanged() {
            if (calibration.audio.calibrationEnough && !calibration.edited)
                latency.value = calibration.audio.calibrationLatency
        }
    }

    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: calibration.close()
    }
    Item {
        id: keys
        anchors.fill: parent
        focus: true
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Space) {
                if (!event.isAutoRepeat)
                    calibration.audio.calibrationPress()
                event.accepted = true
            } else if (event.key === Qt.Key_Escape) {
                calibration.close()
                event.accepted = true
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                calibration.apply()
                event.accepted = true
            }
        }

        ColumnLayout {
            id: content
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18 }
            spacing: 10
            ColumnLayout {
                spacing: 2
                KeycapSentence {
                    spacing: 5
                    //: %1 the Space key
                    text: qsTr("Press %1 on each beep.")
                    keys: [qsTr("Space")]
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("16 beeps at 100 BPM; the first four are practice.")
                }
            }
            Row {
                objectName: "tapCalibrationBeats"
                spacing: 6
                Accessible.role: Accessible.ProgressBar
                //: The latency calibration's progress; %1 the beeps heard
                Accessible.name: qsTr("%1 of 16 beeps").arg(calibration.audio.calibrationBeat)
                Repeater {
                    model: 16
                    delegate: Canvas {
                        required property int index
                        readonly property bool heard: index < calibration.audio.calibrationBeat
                        readonly property bool coming: calibration.audio.calibrating && index === calibration.audio.calibrationBeat
                        readonly property color ink: heard || coming ? Theme.accent : Theme.control
                        objectName: "tapCalibrationBeat" + index
                        width: 16
                        height: 16
                        onHeardChanged: requestPaint()
                        onComingChanged: requestPaint()
                        onInkChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.reset()
                            ctx.beginPath()
                            const line = coming ? 3 : 1
                            ctx.arc(8, 8, 8 - line / 2, 0, 2 * Math.PI)
                            if (heard) {
                                ctx.fillStyle = ink
                                ctx.fill()
                            } else {
                                ctx.lineWidth = line
                                ctx.strokeStyle = ink
                                if (index < 4 && !coming)
                                    ctx.setLineDash([2, 2])
                                ctx.stroke()
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                spacing: 2
                Label {
                    text: qsTr("Your presses, in ms after each beep")
                    color: Theme.muted
                    font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85)
                }
                Canvas {
                    id: plot
                    objectName: "tapCalibrationPlot"
                    readonly property var delays: calibration.audio.calibrationDelays
                    readonly property int median: calibration.audio.calibrationLatency
                    readonly property bool counted: calibration.audio.calibrationCounted > 0
                    readonly property var colours: [Theme.line, Theme.muted, Theme.accent, Theme.text]
                    Layout.preferredWidth: 320
                    Layout.preferredHeight: 44
                    onDelaysChanged: requestPaint()
                    onMedianChanged: requestPaint()
                    onColoursChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const left = 10, w = 300
                        const at = ms => left + Math.max(0, Math.min(200, ms)) / 200 * w
                        ctx.strokeStyle = Theme.line
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        ctx.moveTo(left, 30.5)
                        ctx.lineTo(left + w, 30.5)
                        ctx.stroke()
                        ctx.strokeStyle = Theme.muted
                        ctx.fillStyle = Theme.muted
                        ctx.font = Math.round(Qt.application.font.pixelSize * 0.75) + "px sans-serif"
                        ctx.textAlign = "center"
                        for (const v of [0, 50, 100, 150, 200]) {
                            ctx.beginPath()
                            ctx.moveTo(Math.round(at(v)) + 0.5, 30)
                            ctx.lineTo(Math.round(at(v)) + 0.5, 34)
                            ctx.stroke()
                            ctx.fillText(String(v), at(v), 43)
                        }
                        ctx.fillStyle = Theme.accent
                        for (let i = 0; i < delays.length; i++) {
                            ctx.beginPath()
                            ctx.arc(at(delays[i]), 12 + (i % 3) * 5, 3, 0, 2 * Math.PI)
                            ctx.fill()
                        }
                        if (counted) {
                            ctx.strokeStyle = Theme.text
                            ctx.lineWidth = 1.5
                            ctx.beginPath()
                            ctx.moveTo(at(median), 4)
                            ctx.lineTo(at(median), 30)
                            ctx.stroke()
                        }
                    }
                }
            }
            RowLayout {
                spacing: 10
                Label {
                    text: qsTr("Latency")
                    font.bold: true
                }
                SpinBox {
                    id: latency
                    objectName: "tapCalibrationLatency"
                    from: -200
                    to: 500
                    editable: true
                    enabled: !calibration.audio.calibrating
                    Accessible.name: qsTr("Latency")
                    //: The latency spin box's value; %1 is in milliseconds
                    textFromValue: (value, locale) => qsTr("%1 ms").arg(value)
                    valueFromText: (text, locale) => parseInt(text) || 0
                    onValueModified: calibration.edited = true
                }
                Label {
                    objectName: "tapCalibrationResult"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.muted
                    font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85)
                    //: The latency calibration's result; %1 the spread in ms, %2 the presses counted (of 12)
                    text: (calibration.audio.calibrating ? qsTr("Spread ±%1 ms · %2 of 12 counted beeps so far")
                                                         : qsTr("Spread ±%1 ms · %2 of 12 counted beeps"))
                          .arg(calibration.audio.calibrationSpread, calibration.audio.calibrationCounted)
                }
            }
            RowLayout {
                Layout.topMargin: 6
                spacing: 8
                Button {
                    objectName: "tapCalibrationAgain"
                    text: qsTr("Start again")
                    focusPolicy: Qt.NoFocus
                    onClicked: calibration.start()
                }
                Item { Layout.fillWidth: true }
                WindowFooter {
                    Layout.fillWidth: false
                    Button {
                        objectName: "tapCalibrationCancel"
                        text: qsTr("Cancel")
                        focusPolicy: Qt.NoFocus
                        DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                        onClicked: calibration.close()
                    }
                    PrimaryButton {
                        objectName: "tapCalibrationApply"
                        text: qsTr("Apply")
                        focusPolicy: Qt.NoFocus
                        DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                        onClicked: calibration.apply()
                    }
                }
            }
        }
    }
}
