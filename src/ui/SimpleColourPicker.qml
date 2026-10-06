// Y7: legacy SimpleColorPickerDialog "Color picker" (ColorPicker.cpp:1374-1533
// at 20d647c4), the colour buttons' right click (EditBox::AllColorClick,
// EditBox.cpp:903-918). It opens at the pointer and takes the pointer
// (OnShow); outside the window a held left or right button captures the 7x7
// pixels around it, a held right button picks their centre into the edited
// text at once (COLOR_CHANGED), and a release moves the window to the pointer
// when "Move the window to the color selection location" is checked
// (OnLeaveWindow). Over the window the pointer works its controls. OK adds
// the colour to the recent ones; Cancel, closing or losing the pointer takes
// every change back.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Window {
    id: window
    objectName: "simpleColourPicker"
    required property LineEditorController editor
    required property ColourPickerController picker
    readonly property ScreenSampler sampler: picker.sampler
    title: qsTr("Color picker")
    flags: Qt.Dialog
    modality: Qt.ApplicationModal
    width: content.implicitWidth + 16
    height: content.implicitHeight + 16
    color: Theme.panel // K2: a Window draws white unless told
    // AssColor color: r, g, b and the ASS alpha.
    property var colour: ({ r: 0, g: 0, b: 0, a: 0 })
    property bool picking: false
    property bool portalAsked: false
    // The portal's last failure, shown until the next ask.
    property string portalNote: ""
    readonly property bool tracking: picking && sampler.tracking

    function openFor(number, role, selectionStart, selectionEnd) {
        const c = editor.beginColour(number, role, selectionStart, selectionEnd)
        if (c.r === undefined)
            return false
        typeBox.currentIndex = number - 1
        moveWindow.checked = true
        colour = c
        hexColour.text = picker.assText(c, false)
        picking = true
        const p = sampler.cursorPosition()
        moveToPointer(p.x, p.y)
        show()
        requestActivate()
        // OnShow: CaptureMouse with the eyedropper cursor.
        if (sampler.route === "grab")
            sampler.startTracking(window, true)
        return true
    }
    // MoveToMousePosition (config.cpp:1379-1400), frame included.
    function moveToPointer(px, py) {
        sampler.moveToPointer(window, px, py)
    }
    // The dropper's DROPPER_SELECT (ColorPicker.cpp:1399-1406): the picked
    // red, green and blue (color.Copy keeps the alpha), at once in the text.
    function pick(c) {
        colour = { r: c.r, g: c.g, b: c.b, a: colour.a }
        hexColour.text = picker.assText(c, false)
        editor.changeColour(colour)
    }
    function finish(accepted, closing) {
        if (!picking)
            return
        picking = false
        portalAsked = false
        portalNote = ""
        sampler.stopTracking()
        // EditBox: scpd->AddRecent() after OK, DummyUndo otherwise.
        if (accepted)
            picker.addRecentFromSimplePicker(colour)
        editor.endDialog(accepted)
        if (!closing)
            close()
    }
    function accept() { finish(true) }
    function reject() { finish(false) }

    onClosing: (close) => finish(false, true)

    Connections {
        target: window.sampler
        enabled: window.tracking
        function onPointerEvent(kind, x, y, button, buttons, inside) {
            if (inside)
                return // released over the window: its controls take the event
            if (buttons & (Qt.LeftButton | Qt.RightButton)) {
                const cells = window.sampler.sample(x, y)
                if (cells.length)
                    dropper.cells = cells
                if (buttons & Qt.RightButton)
                    window.pick(dropper.centre())
            } else if (kind === 2 && (button === Qt.LeftButton || button === Qt.RightButton)) {
                if (moveWindow.checked)
                    window.moveToPointer(x, y)
            }
        }
        // wxEVT_MOUSE_CAPTURE_LOST: EndModal(0).
        function onTrackingLost() { window.finish(false) }
    }
    Connections {
        target: window.sampler
        enabled: window.portalAsked
        function onPortalPicked(colour) {
            window.portalAsked = false
            if (window.picking)
                window.pick(colour)
        }
        function onPortalFailed(message) {
            window.portalAsked = false
            window.portalNote = message
        }
        // The answer comes while portalBusy is still true; a cancel only
        // lets it fall.
        function onPortalBusyChanged() {
            if (!window.sampler.portalBusy)
                window.portalAsked = false
        }
    }

    Shortcut {
        sequence: "Escape"
        onActivated: window.reject()
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        ComboBox {
            id: typeBox
            objectName: "simpleColourType"
            Layout.fillWidth: true
            Accessible.name: qsTr("Colour")
            model: [qsTr("Primary color"), qsTr("Secondary color"), qsTr("Border color"), qsTr("Shadow color")]
            onActivated: (index) => {
                // AddRecent, then COLOR_TYPE_CHANGED: SetColor with that
                // colour, alpha included, and no change to the text.
                window.picker.addRecentFromSimplePicker(window.colour)
                const c = window.editor.simplePickerColour(index + 1)
                window.colour = c
                hexColour.text = window.picker.assText(c, false)
            }
        }
        // HexColor: the ASS text on the colour, in white when fewer than two
        // channels pass 127, black otherwise (Colorize). Typing in it changes
        // nothing, as in legacy.
        TextField {
            id: hexColour
            objectName: "hexColour"
            Layout.fillWidth: true
            Accessible.name: qsTr("ASS colour")
            readonly property int bright: (window.colour.r > 127) + (window.colour.g > 127) + (window.colour.b > 127)
            color: bright < 2 ? "white" : "black"
            background: Rectangle {
                color: Qt.rgba(window.colour.r / 255, window.colour.g / 255, window.colour.b / 255, 1)
                border.color: Theme.line
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            ScreenDropperView {
                id: dropper
                objectName: "simpleDropper"
                onPicked: (c) => window.pick(c)
            }
            // The portal route: the desktop picks (Wayland has no capture).
            IconButton {
                objectName: "simplePortalPick"
                visible: window.sampler.route === "portal"
                enabled: !window.sampler.portalBusy
                iconRole: "eyedropper"
                text: qsTr("Pick a colour from the screen")
                onClicked: {
                    window.portalAsked = true
                    window.portalNote = ""
                    window.sampler.pickFromPortal()
                }
            }
        }
        Label {
            id: note
            objectName: "simpleDropperUnavailable"
            Layout.maximumWidth: 240
            wrapMode: Text.WordWrap
            visible: text.length > 0
            text: window.sampler.available ? window.portalNote : window.sampler.unavailableReason
        }
        CheckBox {
            id: moveWindow
            objectName: "moveWindow"
            text: qsTr("Move the window\nto the color selection location").replace("\n", " ")
            Component.onCompleted: if (contentItem && contentItem.wrapMode !== undefined) contentItem.wrapMode = Text.Wrap
            Layout.fillWidth: true
            Layout.maximumWidth: 240
            Layout.preferredHeight: Math.max(implicitIndicatorHeight, contentItem.implicitHeight) + topPadding + bottomPadding
            checked: true
            // The portal reports no location.
            enabled: window.sampler.route === "grab"
        }
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Button {
                objectName: "simpleOk"
                text: qsTr("OK")
                onClicked: window.accept()
            }
            Button {
                objectName: "simpleCancel"
                text: qsTr("Cancel")
                onClicked: window.reject()
            }
        }
    }
}
