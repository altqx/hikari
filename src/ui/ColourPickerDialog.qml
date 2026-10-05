// E1: the editor's colour picker (legacy DialogColorPicker "Choose color"):
// a spectrum with a hue strip, RGB and alpha values, ASS and HTML text and
// the recent colours. Each change is applied to the edited text at once, as
// legacy's COLOR_CHANGED does; Cancel takes every change back, OK adds the
// colour to the recent ones.
//
// Y7: the HSL and HSV values, the screen dropper and the "swap shortcuts"
// option (ColorPicker.cpp:474-694 at 20d647c4). The values follow legacy's
// UpdateFrom* (ColorPicker.cpp:770-907) through its integer colour spaces
// (colorspace.cpp): the spectrum is saturation across and value down from
// black at the top (MakeSVSpectrum), the hue strip hue down, all 0-255.
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
    // hsl_input and hsv_input (0-255 each).
    property int hslHue: 0
    property int hslSaturation: 0
    property int lightness: 0
    property int hsvHue: 0
    property int hsvSaturation: 0
    property int hsvValue: 0
    // The screen dropper holds the pointer (legacy screen_dropper_icon's capture).
    property bool dropping: false
    property bool portalAsked: false
    // The portal's last failure, shown under the dropper until the next ask.
    property string portalNote: ""
    readonly property color shown: Qt.rgba(red / 255, green / 255, blue / 255, 1)
    readonly property ScreenSampler sampler: picker.sampler

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
    // SetColor without a change event (the colour the dialog opens with,
    // COLOR_TYPE_CHANGED).
    function setColour(c) {
        loading = true
        red = c.r
        green = c.g
        blue = c.b
        alpha = c.a
        fromRgb()
        loading = false
    }
    function fromRgb() {
        const hsl = picker.rgbToHsl(red, green, blue)
        const hsv = picker.rgbToHsv(red, green, blue)
        hslHue = hsl[0]; hslSaturation = hsl[1]; lightness = hsl[2]
        hsvHue = hsv[0]; hsvSaturation = hsv[1]; hsvValue = hsv[2]
    }
    // UpdateFromRGB (also the ASS and HTML fields, UpdateFromASS/HTML).
    function setRgb(r, g, b) {
        red = r
        green = g
        blue = b
        fromRgb()
        changed()
    }
    // UpdateFromHSL: the HSV values from hsl_to_hsv, not from the RGB.
    function setHsl(h, s, l) {
        hslHue = h; hslSaturation = s; lightness = l
        const rgb = picker.hslToRgb(h, s, l)
        const hsv = picker.hslToHsv(h, s, l)
        red = rgb[0]; green = rgb[1]; blue = rgb[2]
        hsvHue = hsv[0]; hsvSaturation = hsv[1]; hsvValue = hsv[2]
        changed()
    }
    // UpdateFromHSV (the spectrum and the hue strip too).
    function setHsv(h, s, v) {
        hsvHue = h; hsvSaturation = s; hsvValue = v
        const rgb = picker.hsvToRgb(h, s, v)
        const hsl = picker.hsvToHsl(h, s, v)
        red = rgb[0]; green = rgb[1]; blue = rgb[2]
        hslHue = hsl[0]; hslSaturation = hsl[1]; lightness = hsl[2]
        changed()
    }
    // OnRecentSelect, shared by the recent colours and the dropper: the
    // colour without its alpha (SetColor(color, 0, true, false)).
    function pickColour(c) {
        setRgb(c.r, c.g, c.b)
    }
    function changed() {
        if (!loading)
            changeTimer.restart()
    }
    // OnDropperMouse (ColorPicker.cpp:1142-1172): a left press on the icon
    // takes the pointer; on the portal route the desktop picks instead.
    function startDropper() {
        if (sampler.route === "portal") {
            portalAsked = true
            portalNote = ""
            sampler.pickFromPortal()
            return
        }
        if (sampler.route !== "grab" || dropping)
            return
        dropping = true
        sampler.startTracking(dialog.contentItem.Window.window, false)
    }
    function stopDropper() {
        if (!dropping)
            return
        dropping = false
        sampler.stopTracking()
    }
    Timer {
        id: changeTimer
        interval: 50
        onTriggered: dialog.editor.changeColour(dialog.colour())
    }
    Connections {
        target: dialog.sampler
        enabled: dialog.dropping
        // While captured, the motion and the left press, left release and
        // right release refresh the capture (DropFromScreenXY); a right
        // release picks its centre and lets go, a left press lets go.
        function onPointerEvent(kind, x, y, button, buttons, inside) {
            const left = button === Qt.LeftButton, right = button === Qt.RightButton
            if (!(kind === 0 || (kind === 1 && left) || (kind === 2 && (left || right))))
                return
            const cells = dialog.sampler.sample(x, y)
            if (cells.length)
                screenDropper.cells = cells
            if (kind === 2 && right) {
                dialog.pickColour(screenDropper.centre())
                dialog.stopDropper()
            } else if (kind === 1 && left) {
                dialog.stopDropper()
            }
        }
        function onTrackingLost() { dialog.stopDropper() }
    }
    Connections {
        target: dialog.sampler
        enabled: dialog.portalAsked
        function onPortalPicked(colour) {
            dialog.portalAsked = false
            if (dialog.visible)
                dialog.pickColour(colour)
        }
        function onPortalFailed(message) {
            dialog.portalAsked = false
            dialog.portalNote = message
        }
        // The answer comes while portalBusy is still true; a cancel only
        // lets it fall.
        function onPortalBusyChanged() {
            if (!dialog.sampler.portalBusy)
                dialog.portalAsked = false
        }
    }
    onClosed: {
        stopDropper()
        portalAsked = false
        portalNote = ""
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
                    // Legacy COLOR_TYPE_CHANGED: GetColor() puts the colour
                    // into the recent ones (ColorPicker.cpp:554-561), then the
                    // picker shows the new colour.
                    changeTimer.stop()
                    dialog.picker.addRecent(dialog.colour())
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
                        readonly property var hueColour: dialog.picker.hsvToRgb(dialog.hsvHue, 255, 255)
                        Rectangle {
                            anchors.fill: parent
                            color: Qt.rgba(spectrum.hueColour[0] / 255, spectrum.hueColour[1] / 255,
                                           spectrum.hueColour[2] / 255, 1)
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
                                GradientStop { position: 0; color: "black" }
                                GradientStop { position: 1; color: "transparent" }
                            }
                        }
                        Rectangle {
                            x: dialog.hsvSaturation - 5
                            y: dialog.hsvValue - 5
                            width: 10; height: 10; radius: 5
                            color: "transparent"
                            border.color: dialog.hsvValue > 127 ? "black" : "white"
                        }
                        MouseArea {
                            anchors.fill: parent
                            function pick(mouse) {
                                dialog.setHsv(dialog.hsvHue, Math.max(0, Math.min(255, Math.floor(mouse.x))),
                                              Math.max(0, Math.min(255, Math.floor(mouse.y))))
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
                            y: dialog.hsvHue - 1
                            width: hueStrip.width; height: 3
                            color: "transparent"
                            border.color: Theme.text
                        }
                        MouseArea {
                            anchors.fill: parent
                            function pick(mouse) {
                                dialog.setHsv(Math.max(0, Math.min(255, Math.floor(mouse.y))), dialog.hsvSaturation,
                                              dialog.hsvValue)
                            }
                            onPressed: (mouse) => pick(mouse)
                            onPositionChanged: (mouse) => pick(mouse)
                        }
                    }
                }
            }
            // COLORPICKER_SWITCH_CLICKS, saved at each click.
            CheckBox {
                objectName: "switchClicks"
                text: qsTr("Swap shortcuts between the color picker\nand the color selection window")
                checked: dialog.picker.switchClicks
                onToggled: dialog.picker.switchClicks = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Checking this option opens the color picker on left-click,\nand the color selection window on right-click.")
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
                        // GetAss(false, false): the alpha has its own field.
                        text: dialog.picker.assText(dialog.colour(), false)
                        onEditingFinished: {
                            const c = dialog.picker.parse(text)
                            dialog.setRgb(c.r, c.g, c.b)
                        }
                    }
                    Label { text: qsTr("HTML:") }
                    TextField {
                        objectName: "htmlText"
                        Accessible.name: qsTr("HTML colour")
                        text: dialog.picker.htmlText(dialog.colour())
                        onEditingFinished: {
                            const c = dialog.picker.htmlColour(text)
                            dialog.setRgb(c.r, c.g, c.b)
                        }
                    }
                }
            }
            RowLayout {
                GroupBox {
                    title: qsTr("HSL color")
                    GridLayout {
                        columns: 2
                        Label { text: qsTr("Hue:") }
                        SpinBox {
                            objectName: "hslHue"; from: 0; to: 255; editable: true; value: dialog.hslHue
                            Accessible.name: qsTr("HSL hue")
                            onValueModified: dialog.setHsl(value, dialog.hslSaturation, dialog.lightness)
                        }
                        Label { text: qsTr("Saturation:") }
                        SpinBox {
                            objectName: "hslSaturation"; from: 0; to: 255; editable: true; value: dialog.hslSaturation
                            Accessible.name: qsTr("HSL saturation")
                            onValueModified: dialog.setHsl(dialog.hslHue, value, dialog.lightness)
                        }
                        Label { text: qsTr("Lightness:") }
                        SpinBox {
                            objectName: "lightness"; from: 0; to: 255; editable: true; value: dialog.lightness
                            Accessible.name: qsTr("Lightness")
                            onValueModified: dialog.setHsl(dialog.hslHue, dialog.hslSaturation, value)
                        }
                    }
                }
                GroupBox {
                    title: qsTr("HSV color")
                    GridLayout {
                        columns: 2
                        Label { text: qsTr("Hue:") }
                        SpinBox {
                            objectName: "hsvHue"; from: 0; to: 255; editable: true; value: dialog.hsvHue
                            Accessible.name: qsTr("HSV hue")
                            onValueModified: dialog.setHsv(value, dialog.hsvSaturation, dialog.hsvValue)
                        }
                        Label { text: qsTr("Saturation:") }
                        SpinBox {
                            objectName: "hsvSaturation"; from: 0; to: 255; editable: true; value: dialog.hsvSaturation
                            Accessible.name: qsTr("HSV saturation")
                            onValueModified: dialog.setHsv(dialog.hsvHue, value, dialog.hsvValue)
                        }
                        Label { text: qsTr("Value:") }
                        SpinBox {
                            objectName: "hsvValue"; from: 0; to: 255; editable: true; value: dialog.hsvValue
                            Accessible.name: qsTr("Value")
                            onValueModified: dialog.setHsv(dialog.hsvHue, dialog.hsvSaturation, value)
                        }
                    }
                }
            }
            // The eyedropper icon and the screen dropper beside the recent
            // colours (picker_sizer).
            RowLayout {
                spacing: 10
                Rectangle {
                    id: eyedropper
                    objectName: "eyedropper"
                    implicitWidth: 32
                    implicitHeight: 32
                    color: "transparent"
                    border.color: Theme.line
                    enabled: dialog.sampler.available && !dialog.sampler.portalBusy
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Pick a colour from the screen")
                    Accessible.description: dialog.sampler.unavailableReason
                    Icon {
                        anchors.centerIn: parent
                        iconRole: "eyedropper"
                        size: 24
                        // Legacy clears the bitmap while the dropper holds the pointer.
                        visible: !dialog.dropping
                        hovered: dropperArea.containsMouse
                    }
                    MouseArea {
                        id: dropperArea
                        objectName: "eyedropperArea"
                        anchors.fill: parent
                        hoverEnabled: true
                        onPressed: (mouse) => {
                            // The press is not kept: the dropper takes every
                            // event from here on.
                            mouse.accepted = false
                            dialog.startDropper()
                        }
                    }
                    ToolTip.visible: dropperArea.containsMouse
                    ToolTip.text: dialog.sampler.available ? Accessible.name : dialog.sampler.unavailableReason
                }
                ScreenDropperView {
                    id: screenDropper
                    objectName: "screenDropper"
                    onPicked: (colour) => dialog.pickColour(colour)
                }
            }
            Label {
                id: dropperNote
                objectName: "dropperUnavailable"
                Layout.maximumWidth: 320
                wrapMode: Text.WordWrap
                visible: text.length > 0
                text: dialog.sampler.available ? dialog.portalNote : dialog.sampler.unavailableReason
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
                                onTapped: dialog.pickColour(parent.modelData)
                            }
                        }
                    }
                }
            }
        }
    }
}
