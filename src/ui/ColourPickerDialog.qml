// E1: the editor's colour picker (legacy DialogColorPicker "Choose color"):
// a spectrum with a hue strip, RGB and alpha values, ASS and HTML text and
// the recent colours. Each change is applied to the edited text at once, as
// legacy's COLOR_CHANGED does; Cancel takes every change back, OK adds the
// colour to the recent ones.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Dialog {
    id: dialog
    objectName: "colourDialog"
    required property LineEditorController editor
    required property ColourPickerController picker
    title: qsTr("Choose color")
    modal: true
    standardButtons: Dialog.Ok | Dialog.Cancel
    property bool loading: false
    property int red: 0
    property int green: 0
    property int blue: 0
    property int alpha: 0 // ASS alpha: 0 opaque
    property real hue: 0
    property real saturation: 0
    property real value: 0
    readonly property color shown: Qt.rgba(red / 255, green / 255, blue / 255, 1)

    function openFor(number, role, selectionStart, selectionEnd) {
        const c = editor.beginColour(number, role, selectionStart, selectionEnd)
        if (c.r === undefined)
            return false
        typeBox.currentIndex = number - 1
        setColour(c)
        open()
        return true
    }
    function colour() {
        return { r: red, g: green, b: blue, a: alpha }
    }
    function setColour(c) {
        loading = true
        red = c.r
        green = c.g
        blue = c.b
        alpha = c.a
        syncHsv()
        loading = false
    }
    function syncHsv() {
        const col = Qt.rgba(red / 255, green / 255, blue / 255, 1)
        if (col.hsvHue >= 0)
            hue = col.hsvHue
        saturation = col.hsvSaturation
        value = col.hsvValue
    }
    function setHsv(h, s, v) {
        hue = h
        saturation = s
        value = v
        const col = Qt.hsva(h, s, v, 1)
        red = Math.round(col.r * 255)
        green = Math.round(col.g * 255)
        blue = Math.round(col.b * 255)
        changed()
    }
    function setRgb(r, g, b) {
        red = r
        green = g
        blue = b
        syncHsv()
        changed()
    }
    function changed() {
        if (!loading)
            changeTimer.restart()
    }
    Timer {
        id: changeTimer
        interval: 50
        onTriggered: dialog.editor.changeColour(dialog.colour())
    }
    onAccepted: {
        if (changeTimer.running) {
            changeTimer.stop()
            editor.changeColour(colour())
        }
        picker.addRecent(colour())
        editor.endDialog(true)
    }
    onRejected: {
        changeTimer.stop()
        editor.endDialog(false)
    }

    RowLayout {
        anchors.fill: parent
        spacing: 12
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            ComboBox {
                id: typeBox
                objectName: "colourType"
                Layout.fillWidth: true
                Accessible.name: qsTr("Colour")
                model: [qsTr("Primary color"), qsTr("Secondary color"), qsTr("Border color"), qsTr("Shadow color")]
                onActivated: (index) => {
                    // Legacy COLOR_TYPE_CHANGED: the picker shows that colour.
                    changeTimer.stop()
                    dialog.setColour(dialog.editor.switchColour(index + 1))
                }
            }
            GroupBox {
                title: qsTr("Spectrum color")
                RowLayout {
                    Item {
                        id: spectrum
                        objectName: "spectrum"
                        Layout.preferredWidth: 256
                        Layout.preferredHeight: 256
                        Accessible.name: qsTr("Saturation and value")
                        Rectangle {
                            anchors.fill: parent
                            color: Qt.hsva(dialog.hue, 1, 1, 1)
                        }
                        Rectangle {
                            anchors.fill: parent
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: "white" }
                                GradientStop { position: 1; color: "transparent" }
                            }
                        }
                        Rectangle {
                            anchors.fill: parent
                            gradient: Gradient {
                                GradientStop { position: 0; color: "transparent" }
                                GradientStop { position: 1; color: "black" }
                            }
                        }
                        Rectangle {
                            x: dialog.saturation * spectrum.width - 5
                            y: (1 - dialog.value) * spectrum.height - 5
                            width: 10; height: 10; radius: 5
                            color: "transparent"
                            border.color: dialog.value > 0.5 ? "black" : "white"
                        }
                        MouseArea {
                            anchors.fill: parent
                            function pick(mouse) {
                                dialog.setHsv(dialog.hue, Math.max(0, Math.min(1, mouse.x / width)),
                                              1 - Math.max(0, Math.min(1, mouse.y / height)))
                            }
                            onPressed: (mouse) => pick(mouse)
                            onPositionChanged: (mouse) => pick(mouse)
                        }
                    }
                    Item {
                        id: hueStrip
                        objectName: "hueStrip"
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 256
                        Accessible.name: qsTr("Hue")
                        Rectangle {
                            anchors.fill: parent
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: Qt.hsva(0.0, 1, 1, 1) }
                                GradientStop { position: 1 / 6; color: Qt.hsva(1 / 6, 1, 1, 1) }
                                GradientStop { position: 2 / 6; color: Qt.hsva(2 / 6, 1, 1, 1) }
                                GradientStop { position: 3 / 6; color: Qt.hsva(3 / 6, 1, 1, 1) }
                                GradientStop { position: 4 / 6; color: Qt.hsva(4 / 6, 1, 1, 1) }
                                GradientStop { position: 5 / 6; color: Qt.hsva(5 / 6, 1, 1, 1) }
                                GradientStop { position: 1.0; color: Qt.hsva(0.999, 1, 1, 1) }
                            }
                        }
                        Rectangle {
                            y: dialog.hue * hueStrip.height - 1
                            width: hueStrip.width; height: 3
                            color: "transparent"
                            border.color: "black"
                        }
                        MouseArea {
                            anchors.fill: parent
                            function pick(mouse) {
                                dialog.setHsv(Math.max(0, Math.min(0.999, mouse.y / height)), dialog.saturation, dialog.value)
                            }
                            onPressed: (mouse) => pick(mouse)
                            onPositionChanged: (mouse) => pick(mouse)
                        }
                    }
                }
            }
        }
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            RowLayout {
                Label { text: qsTr("Selected color:") }
                Rectangle {
                    objectName: "selectedColour"
                    width: 48; height: 24
                    color: dialog.shown
                    border.color: palette.mid
                }
            }
            GroupBox {
                title: qsTr("RGB color")
                GridLayout {
                    columns: 2
                    Label { text: qsTr("Red:") }
                    SpinBox {
                        objectName: "red"; from: 0; to: 255; editable: true; value: dialog.red
                        Accessible.name: qsTr("Red")
                        onValueModified: dialog.setRgb(value, dialog.green, dialog.blue)
                    }
                    Label { text: qsTr("Green:") }
                    SpinBox {
                        objectName: "green"; from: 0; to: 255; editable: true; value: dialog.green
                        Accessible.name: qsTr("Green")
                        onValueModified: dialog.setRgb(dialog.red, value, dialog.blue)
                    }
                    Label { text: qsTr("Blue:") }
                    SpinBox {
                        objectName: "blue"; from: 0; to: 255; editable: true; value: dialog.blue
                        Accessible.name: qsTr("Blue")
                        onValueModified: dialog.setRgb(dialog.red, dialog.green, value)
                    }
                    Label { text: qsTr("Alpha:") }
                    SpinBox {
                        objectName: "alpha"; from: 0; to: 255; editable: true; value: dialog.alpha
                        Accessible.name: qsTr("Alpha")
                        onValueModified: {
                            dialog.alpha = value
                            dialog.changed()
                        }
                    }
                    Label { text: qsTr("ASS:") }
                    TextField {
                        objectName: "assText"
                        Accessible.name: qsTr("ASS colour")
                        text: dialog.picker.assText(dialog.colour(), true)
                        onEditingFinished: {
                            const c = dialog.picker.parse(text)
                            dialog.alpha = c.a
                            dialog.setRgb(c.r, c.g, c.b)
                        }
                    }
                    Label { text: qsTr("HTML:") }
                    TextField {
                        objectName: "htmlText"
                        Accessible.name: qsTr("HTML colour")
                        text: dialog.picker.htmlText(dialog.colour())
                        onEditingFinished: {
                            const c = dialog.picker.parse(text)
                            dialog.setRgb(c.r, c.g, c.b)
                        }
                    }
                }
            }
            GroupBox {
                title: qsTr("Recent colors")
                Grid {
                    columns: 8
                    spacing: 2
                    Repeater {
                        model: dialog.picker.recent
                        Rectangle {
                            required property var modelData
                            required property int index
                            objectName: "recent" + index
                            width: 16; height: 16
                            color: Qt.rgba(modelData.r / 255, modelData.g / 255, modelData.b / 255, 1)
                            border.color: palette.mid
                            Accessible.role: Accessible.Button
                            Accessible.name: dialog.picker.assText(modelData, true)
                            TapHandler {
                                onTapped: {
                                    dialog.alpha = parent.modelData.a
                                    dialog.setRgb(parent.modelData.r, parent.modelData.g, parent.modelData.b)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
