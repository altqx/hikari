import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// V3: GLOBAL_OPEN_DUMMY_VIDEO's dialog (legacy DummyVideo "Dummy video
// options", DummyVideo.cpp:20-116): the resolution presets (1920x1080 chosen)
// and the width and height (300 to 8000, 200 to 4500), the colour and the
// checkerboard, the frames per second (typed or one of six, digits and dots
// only) and the duration, with legacy's "This gives %i frames" computed once
// for the default and never updated. OK hands the values to
// App.openDummyVideo, which logs "Invalid FPS value." for a rate it refuses.
// The colour is legacy's ButtonColorPicker value as ASS text (&HBBGGRR&),
// shown beside a swatch (V3-dummy-colour-text).
Dialog {
    id: dialog
    objectName: "dummyVideoDialog"
    required property var app
    title: qsTr("Dummy video options")
    // K1: the title with its icon, as the other dialogs show theirs.
    header: IconDialogHeader { objectName: "dummyVideoDialogTitle"; iconRole: "open-video"; text: dialog.title }
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    property var defaults: ({})
    // the text to log when the values are refused ("Invalid FPS value.")
    signal refused(string message)

    function show() {
        defaults = app.dummyVideoDefaults()
        resolution.model = defaults.resolutions
        resolution.currentIndex = defaults.resolution
        widthBox.value = defaults.width
        heightBox.value = defaults.height
        colour.text = defaults.colour
        pattern.checked = false
        fps.model = defaults.fpsChoices
        fps.editText = defaults.fps
        duration.text = defaults.duration
        frames.text = qsTr("This gives %1 frames").arg(defaults.frames)
        open()
    }
    // SubsTime from the TimeCtrl's "H:MM:SS.CC" (ms; -1 unreadable)
    function durationMs(text) {
        const m = /^(\d+):(\d{1,2}):(\d{1,2})(?:[.,](\d{1,2}))?$/.exec(text.trim())
        if (!m)
            return -1
        return ((+m[1] * 60 + +m[2]) * 60 + +m[3]) * 1000 + (m[4] ? +m[4].padEnd(2, "0") * 10 : 0)
    }
    // AssColor from "&HBBGGRR&"
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

    GridLayout {
        columns: 2
        anchors.fill: parent
        Label { text: qsTr("Video resolution:") }
        ColumnLayout {
            ComboBox {
                id: resolution
                objectName: "dummyResolution"
                Layout.fillWidth: true
                Accessible.name: qsTr("Video resolution:")
                // OnResolutionChoose: "<w>x<h> (...)" into the two fields
                onActivated: {
                    const m = /^(\d+)x(\d+)/.exec(currentText)
                    if (m) {
                        widthBox.value = +m[1]
                        heightBox.value = +m[2]
                    }
                }
            }
            RowLayout {
                SpinBox {
                    id: widthBox
                    objectName: "dummyWidth"
                    from: 300; to: 8000; editable: true
                    Accessible.name: qsTr("Width")
                }
                Label { text: "×" }
                SpinBox {
                    id: heightBox
                    objectName: "dummyHeight"
                    from: 200; to: 4500; editable: true
                    Accessible.name: qsTr("Height")
                }
            }
        }
        Label { text: qsTr("Color:") }
        RowLayout {
            Rectangle {
                objectName: "dummyColourSwatch"
                implicitWidth: 24
                implicitHeight: 24
                readonly property var c: dialog.rgb(colour.text)
                color: c ? Qt.rgba(c.red / 255, c.green / 255, c.blue / 255, 1) : "transparent"
                border.color: Theme.line
            }
            TextField {
                id: colour
                objectName: "dummyColour"
                Layout.fillWidth: true
                Accessible.name: qsTr("Color:")
            }
            CheckBox {
                id: pattern
                objectName: "dummyPattern"
                text: qsTr("Checkerboard pattern")
            }
        }
        Label { text: qsTr("Frames per second:") }
        ComboBox {
            id: fps
            objectName: "dummyFps"
            editable: true
            Layout.fillWidth: true
            validator: RegularExpressionValidator { regularExpression: /[0-9.]*/ }
            Accessible.name: qsTr("Frames per second:")
        }
        Label { text: qsTr("Duration:") }
        ColumnLayout {
            TextField {
                id: duration
                objectName: "dummyDuration"
                Layout.fillWidth: true
                Accessible.name: qsTr("Duration:")
            }
            Label {
                id: frames
                objectName: "dummyFrames"
            }
        }
    }
    onAccepted: {
        const problem = dialog.app.openDummyVideo(dialog.values())
        if (problem.length > 0)
            dialog.refused(problem)
    }
}
