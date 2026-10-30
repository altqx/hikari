pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "dummyVideoDialog"
    dialogId: "dummyVideo"
    required property var app
    required property ColourPickerController colourPicker
    title: qsTr("Open dummy video")
    modal: true
    defaultButton: openButton
    property var defaults: ({})
    signal refused(string message)

    readonly property int frames: app.dummyVideoFrames(fps.editText, Math.max(0, durationMs(duration.text)))
    readonly property bool fpsValid: frames >= 0
    readonly property bool durationValid: durationMs(duration.text) >= 0

    function show() {
        defaults = app.dummyVideoDefaults()
        resolution.model = defaults.resolutions
        widthBox.value = defaults.width
        heightBox.value = defaults.height
        colour.text = defaults.colour
        pattern.checked = false
        fps.model = defaults.fpsChoices
        fps.editText = defaults.fps
        duration.text = defaults.duration
        syncPreset()
        open()
    }
    function durationMs(text) {
        const m = /^(\d+):(\d{1,2}):(\d{1,2})(?:[.,](\d{1,2}))?$/.exec(text.trim())
        if (!m)
            return -1
        return ((+m[1] * 60 + +m[2]) * 60 + +m[3]) * 1000 + (m[4] ? +m[4].padEnd(2, "0") * 10 : 0)
    }
    // ASS colours are &H[AA]BBGGRR&, blue first.
    function rgb(text) {
        const m = /^&H([0-9A-Fa-f]{2})?([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})([0-9A-Fa-f]{2})&?$/.exec(text.trim())
        if (!m)
            return null
        return { red: parseInt(m[4], 16), green: parseInt(m[3], 16), blue: parseInt(m[2], 16) }
    }
    function values() {
        const c = rgb(colour.text) || rgb(defaults.colour)
        return { fps: fps.editText, durationMs: Math.max(0, durationMs(duration.text)),
                 width: widthBox.value, height: heightBox.value,
                 red: c.red, green: c.green, blue: c.blue, pattern: pattern.checked }
    }
    function syncPreset() {
        const list = resolution.model || []
        resolution.currentIndex = list.findIndex(r => {
            const m = /^(\d+)x(\d+)/.exec(r)
            return m && +m[1] === widthBox.value && +m[2] === heightBox.value
        })
    }

    GridLayout {
        columns: 2
        columnSpacing: 12
        rowSpacing: 12
        Label { text: qsTr("Video resolution"); Layout.alignment: Qt.AlignTop; Layout.topMargin: 6 }
        ColumnLayout {
            spacing: 6
            ComboBox {
                id: resolution
                objectName: "dummyResolution"
                Layout.fillWidth: true
                displayText: currentIndex < 0 ? qsTr("Custom") : currentText
                Accessible.name: qsTr("Video resolution")
                onActivated: {
                    const m = /^(\d+)x(\d+)/.exec(currentText)
                    if (m) {
                        widthBox.value = +m[1]
                        heightBox.value = +m[2]
                    }
                }
            }
            RowLayout {
                spacing: 6
                SpinBox {
                    id: widthBox
                    objectName: "dummyWidth"
                    from: 300; to: 8000; editable: true
                    Accessible.name: qsTr("Width")
                    onValueChanged: dialog.syncPreset()
                }
                Label { text: "×"; color: Theme.muted }
                SpinBox {
                    id: heightBox
                    objectName: "dummyHeight"
                    from: 200; to: 4500; editable: true
                    Accessible.name: qsTr("Height")
                    onValueChanged: dialog.syncPreset()
                }
            }
        }
        Label { text: qsTr("Colour"); Layout.alignment: Qt.AlignTop; Layout.topMargin: 6 }
        ColumnLayout {
            spacing: 6
            RowLayout {
                spacing: 6
                Button {
                    id: swatch
                    objectName: "dummyColourSwatch"
                    implicitWidth: 32
                    implicitHeight: 24
                    readonly property var c: dialog.rgb(colour.text)
                    readonly property color color: c ? Qt.rgba(c.red / 255, c.green / 255, c.blue / 255, 1) : "transparent"
                    text: qsTr("Choose the colour")
                    Accessible.name: text
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    background: Rectangle {
                        color: swatch.color
                        radius: 2
                        border.color: swatch.visualFocus ? Theme.focus : Theme.control
                    }
                    contentItem: Item {}
                    onClicked: {
                        let before = colour.text
                        colourPopover.openFor(swatch, {
                            changeColour: v => colour.text = dialog.colourPicker.assText(v, false),
                            endDialog: accepted => { if (!accepted) colour.text = before }
                        }, () => {
                            before = colour.text
                            const c = dialog.rgb(before) || dialog.rgb(dialog.defaults.colour) || { red: 0, green: 0, blue: 0 }
                            dialog.colourPicker.opened("dummyVideo")
                            return { r: c.red, g: c.green, b: c.blue, a: 0 }
                        }, { title: qsTr("Colour"), alpha: false, focus: swatch })
                    }
                }
                TextField {
                    id: colour
                    objectName: "dummyColour"
                    font.family: "monospace"
                    Layout.preferredWidth: 112
                    Accessible.name: qsTr("Colour")
                }
            }
            CheckBox {
                id: pattern
                objectName: "dummyPattern"
                text: qsTr("Checkerboard pattern")
            }
        }
        Label { text: qsTr("Frames per second"); Layout.alignment: Qt.AlignTop; Layout.topMargin: 6 }
        ColumnLayout {
            spacing: 6
            ComboBox {
                id: fps
                objectName: "dummyFps"
                editable: true
                Layout.preferredWidth: 112
                validator: RegularExpressionValidator { regularExpression: /[0-9.]*/ }
                Accessible.name: qsTr("Frames per second")
                Accessible.description: dialog.fpsValid ? "" : fpsProblem.text
            }
            Label {
                id: fpsProblem
                objectName: "dummyFpsProblem"
                visible: !dialog.fpsValid
                text: qsTr("Must be from 15 to 120.")
                color: Theme.danger
                Accessible.role: Accessible.AlertMessage
            }
        }
        Label { text: qsTr("Duration"); Layout.alignment: Qt.AlignTop; Layout.topMargin: 6 }
        ColumnLayout {
            spacing: 6
            TextField {
                id: duration
                objectName: "dummyDuration"
                font.family: "monospace"
                Layout.preferredWidth: 112
                Accessible.name: qsTr("Duration")
                Accessible.description: dialog.durationValid ? frameCount.text : durationProblem.text
            }
            Label {
                id: durationProblem
                objectName: "dummyDurationProblem"
                visible: !dialog.durationValid
                text: qsTr("Type a time such as 0:25:00.00.")
                color: Theme.danger
                Accessible.role: Accessible.AlertMessage
            }
            Label {
                id: frameCount
                objectName: "dummyFrames"
                visible: dialog.fpsValid && dialog.durationValid
                text: qsTr("This gives %1 frames").arg(Math.max(0, dialog.frames))
                color: Theme.muted
            }
        }
    }
    buttons: [
        PrimaryButton {
            id: openButton
            objectName: "dummyVideoOpen"
            text: qsTr("Open")
            enabled: dialog.fpsValid && dialog.durationValid
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.accept()
        },
        Button {
            objectName: "dummyVideoCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.reject()
        }
    ]
    onAccepted: {
        const problem = dialog.app.openDummyVideo(dialog.values())
        if (problem.length > 0)
            dialog.refused(problem)
    }
    ColourPopover {
        id: colourPopover
        objectName: "dummyColourPopover"
        picker: dialog.colourPicker
    }
}
