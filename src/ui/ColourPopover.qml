pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Colours"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Popup {
    id: popover
    objectName: "colourPopover"
    required property ColourPickerController picker
    readonly property ScreenSampler sampler: picker.sampler
    property var target: null
    property Item anchor: null
    property string title: ""
    property bool hasAlpha: true
    property bool expanded: false
    // MUST KILL: mode is a magic int (0 RGB, 1 HSL, 2 HSV)
    property int mode: 0
    property bool active: false
    property bool loading: false
    property var original: ({ r: 0, g: 0, b: 0, a: 0 })
    property int red: 0
    property int green: 0
    property int blue: 0
    property int alpha: 0 // ASS alpha: 0 opaque
    property int hslHue: 0
    property int hslSaturation: 0
    property int lightness: 0
    property int hsvHue: 0
    property int hsvSaturation: 0
    property int hsvValue: 0
    property bool dropping: false
    property bool portalAsked: false
    property string portalNote: ""
    property Item returnFocus: null
    readonly property color shown: Qt.rgba(red / 255, green / 255, blue / 255, 1)
    readonly property int compactRecent: 16

    function openFor(anchorItem, owner, begin, options) {
        if (active)
            finish(true)
        // A Popup.Window lets the press that closed it through to the anchor; that click must not reopen it.
        if (anchorItem && anchorItem === pressedAnchor && Date.now() - pressedAt < 1000) {
            pressedAnchor = null
            return
        }
        pressedAnchor = null
        const c = begin()
        if (!c || c.r === undefined)
            return
        const o = options || {}
        target = owner
        title = o.title || ""
        hasAlpha = o.alpha !== false
        returnFocus = o.focus || null
        anchor = anchorItem && anchorItem.visible ? anchorItem : null
        parent = anchor ? anchor : (anchorItem && anchorItem.Window.window ? anchorItem.Window.window.contentItem : anchorItem)
        original = { r: c.r, g: c.g, b: c.b, a: hasAlpha ? c.a : 0 }
        setColour(original)
        portalNote = ""
        active = true
        const host = parent ? parent.Window.window : null
        popupType = host && (host.width < fullSize.width || host.height < fullSize.height) ? Popup.Window : Popup.Item
        open()
        place()
        if (o.dropper)
            startDropper()
    }
    function place() {
        if (!parent)
            return
        const win = parent.Window.window
        const h = implicitHeight
        if (!anchor) {
            x = Math.round((parent.width - implicitWidth) / 2)
            y = Math.round((parent.height - h) / 2)
            return
        }
        x = 0
        const own = popupType === Popup.Window && win && win.screen
        const top = own ? anchor.mapToGlobal(0, 0).y - win.screen.virtualY : anchor.mapToItem(null, 0, 0).y
        const room = own ? win.screen.desktopAvailableHeight : win ? win.height : top + anchor.height + 4 + h + 8
        const below = room - (top + anchor.height + 4) - 8
        y = below >= h || top - 4 - 8 < h ? anchor.height + 4 : -h - 4
    }
    property size fullSize: Qt.size(560, 540)
    function measure() {
        if (expanded && visible && implicitWidth > 0)
            fullSize = Qt.size(implicitWidth + 2 * margins, implicitHeight + 2 * margins)
    }
    onImplicitWidthChanged: measure()
    onImplicitHeightChanged: {
        measure()
        if (visible)
            place()
    }
    function colour() {
        return { r: red, g: green, b: blue, a: hasAlpha ? alpha : 0 }
    }
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
    function setRgb(r, g, b) {
        red = r
        green = g
        blue = b
        fromRgb()
        changed()
    }
    function setHsl(h, s, l) {
        hslHue = h; hslSaturation = s; lightness = l
        const rgb = picker.hslToRgb(h, s, l)
        const hsv = picker.hslToHsv(h, s, l)
        red = rgb[0]; green = rgb[1]; blue = rgb[2]
        hsvHue = hsv[0]; hsvSaturation = hsv[1]; hsvValue = hsv[2]
        changed()
    }
    function setHsv(h, s, v) {
        hsvHue = h; hsvSaturation = s; hsvValue = v
        const rgb = picker.hsvToRgb(h, s, v)
        const hsl = picker.hsvToHsl(h, s, v)
        red = rgb[0]; green = rgb[1]; blue = rgb[2]
        hslHue = hsl[0]; hslSaturation = hsl[1]; lightness = hsl[2]
        changed()
    }
    function setAlpha(a) {
        alpha = a
        changed()
    }
    function pickColour(c) {
        setRgb(c.r, c.g, c.b)
    }
    function changed() {
        if (!loading && active)
            changeTimer.restart()
    }
    function setHex(text) {
        const t = text.trim()
        if (/^#?([0-9a-f]{3}|[0-9a-f]{6})$/i.test(t)) {
            const c = picker.htmlColour(t.startsWith("#") ? t : "#" + t)
            setRgb(c.r, c.g, c.b)
            return true
        }
        if (/^&?h[0-9a-f]{1,8}&?$/i.test(t)) {
            const c = picker.parse(t)
            setRgb(c.r, c.g, c.b)
            return true
        }
        return false
    }
    function startDropper() {
        if (sampler.route === "portal") {
            if (sampler.portalBusy)
                return
            portalAsked = true
            portalNote = ""
            sampler.pickFromPortal()
            return
        }
        if (sampler.route !== "grab" || dropping)
            return
        dropping = true
        sampler.startTracking(popover.contentItem.Window.window, false)
    }
    function stopDropper() {
        if (!dropping)
            return
        dropping = false
        sampler.stopTracking()
    }
    function finish(accepted) {
        if (!active)
            return
        // An editable SpinBox commits its text only on focus loss, so take the focus first.
        if (accepted)
            editEnd.forceActiveFocus()
        active = false
        stopDropper()
        portalAsked = false
        if (changeTimer.running) {
            changeTimer.stop()
            if (accepted)
                target.changeColour(colour())
        }
        if (accepted)
            picker.addRecent(colour())
        const owner = target
        target = null
        owner.endDialog(accepted)
        if (visible)
            close()
        if (returnFocus)
            returnFocus.forceActiveFocus()
    }
    function accept() { finish(true) }
    function reject() { finish(false) }

    modal: true
    dim: false
    focus: true
    padding: 12
    margins: 8
    closePolicy: Popup.CloseOnPressOutside
    onClosed: {
        const pressed = active && popupType === Popup.Window
        const was = anchor
        finish(true)
        if (pressed) {
            pressedAnchor = was
            pressedAt = Date.now()
            Qt.callLater(keepPressedAnchor)
        }
    }
    function keepPressedAnchor() {
        if (pressedAnchor && pressedAnchor.pressed === false)
            pressedAnchor = null
    }
    property Item pressedAnchor: null
    property double pressedAt: 0

    Timer {
        id: changeTimer
        interval: 50
        onTriggered: if (popover.target) popover.target.changeColour(popover.colour())
    }
    Connections {
        target: popover.sampler
        enabled: popover.dropping
        function onPointerEvent(kind, x, y, button, buttons, inside) {
            const left = button === Qt.LeftButton, right = button === Qt.RightButton
            if (!(kind === 0 || (kind === 1 && left) || (kind === 2 && (left || right))))
                return
            const cells = popover.sampler.sample(x, y)
            if (cells.length)
                screenDropper.cells = cells
            if (kind === 2 && right) {
                popover.pickColour(screenDropper.centre())
                popover.stopDropper()
            } else if (kind === 1 && left) {
                popover.stopDropper()
            }
        }
        function onTrackingLost() { popover.stopDropper() }
    }
    Connections {
        target: popover.sampler
        enabled: popover.portalAsked
        function onPortalPicked(colour) {
            popover.portalAsked = false
            if (popover.active)
                popover.pickColour(colour)
        }
        function onPortalFailed(message) {
            popover.portalAsked = false
            popover.portalNote = message
        }
        function onPortalBusyChanged() {
            if (!popover.sampler.portalBusy)
                popover.portalAsked = false
        }
    }

    background: PopoverBackground {}

    contentItem: FocusScope {
        focus: true
        implicitWidth: content.implicitWidth
        implicitHeight: content.implicitHeight
        Accessible.role: Accessible.Dialog
        Accessible.name: popover.title
        Keys.onShortcutOverride: event => event.accepted = event.modifiers === Qt.NoModifier
                                 && [Qt.Key_Escape, Qt.Key_Return, Qt.Key_Enter].includes(event.key)
        Keys.onEscapePressed: popover.reject()
        Keys.onReturnPressed: popover.accept()
        Keys.onEnterPressed: popover.accept()
        Item { id: editEnd; activeFocusOnTab: false }
        RowLayout {
            id: content
            anchors.fill: parent
            spacing: 16

            ColumnLayout {
                id: compact
                Layout.alignment: Qt.AlignTop
                spacing: 12

                RowLayout {
                    spacing: 8
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 28
                        radius: 2
                        color: "transparent"
                        border.color: Theme.control
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 1
                            spacing: 0
                            Rectangle {
                                id: before
                                objectName: "colourBefore"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: Qt.rgba(popover.original.r / 255, popover.original.g / 255, popover.original.b / 255, 1)
                                Accessible.role: Accessible.Graphic
                                Accessible.name: qsTr("Previous colour %1").arg(popover.picker.htmlText(popover.original))
                            }
                            Rectangle {
                                objectName: "colourAfter"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: popover.shown
                                Accessible.role: Accessible.Graphic
                                Accessible.name: qsTr("New colour %1").arg(popover.picker.htmlText(popover.colour()))
                            }
                        }
                    }
                    Rectangle {
                        id: eyedropper
                        objectName: "eyedropper"
                        implicitWidth: 28
                        implicitHeight: 28
                        radius: 2
                        color: popover.dropping ? Theme.select : eyedropperHover.hovered && enabled ? Theme.raised : "transparent"
                        border.color: eyedropper.activeFocus ? Theme.focus : popover.dropping ? Theme.accent : "transparent"
                        enabled: popover.sampler.available && !popover.sampler.portalBusy
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Pick a colour from the screen")
                        Accessible.description: popover.sampler.unavailableReason
                        Accessible.onPressAction: popover.startDropper()
                        Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Space
                        Keys.onSpacePressed: popover.startDropper()
                        Icon {
                            anchors.centerIn: parent
                            iconRole: "eyedropper"
                            size: 16
                            enabled: eyedropper.enabled
                        }
                        HoverHandler { id: eyedropperHover }
                        // The press is not accepted, so nothing stays pressed while the dropper grabs the pointer.
                        MouseArea {
                            objectName: "eyedropperArea"
                            anchors.fill: parent
                            onPressed: (mouse) => {
                                mouse.accepted = false
                                popover.startDropper()
                            }
                        }
                        ToolTip.visible: eyedropperHover.hovered
                        ToolTip.text: popover.sampler.available ? Accessible.name : popover.sampler.unavailableReason
                        ToolTip.delay: 500
                    }
                }
                ScreenDropperView {
                    id: screenDropper
                    objectName: "screenDropper"
                    Layout.alignment: Qt.AlignHCenter
                    visible: popover.expanded || popover.dropping
                    onPicked: (colour) => popover.pickColour(colour)
                }
                Label {
                    objectName: "dropperUnavailable"
                    Layout.maximumWidth: paletteGrid.implicitWidth
                    wrapMode: Text.WordWrap
                    visible: text.length > 0 && (popover.expanded || popover.portalNote.length > 0)
                    color: Theme.muted
                    text: popover.sampler.available ? popover.portalNote : popover.sampler.unavailableReason
                }

                ColourSwatchGrid {
                    id: paletteGrid
                    objectName: "palette"
                    Accessible.name: qsTr("Palette")
                    colours: [
                        { r: 255, g: 255, b: 255, a: 0 }, { r: 192, g: 192, b: 192, a: 0 },
                        { r: 128, g: 128, b: 128, a: 0 }, { r: 64, g: 64, b: 64, a: 0 },
                        { r: 0, g: 0, b: 0, a: 0 }, { r: 255, g: 0, b: 0, a: 0 },
                        { r: 0, g: 255, b: 0, a: 0 }, { r: 0, g: 0, b: 255, a: 0 },
                        { r: 255, g: 255, b: 0, a: 0 }, { r: 0, g: 255, b: 255, a: 0 },
                        { r: 255, g: 0, b: 255, a: 0 }, { r: 255, g: 128, b: 0, a: 0 },
                        { r: 128, g: 255, b: 0, a: 0 }, { r: 0, g: 255, b: 128, a: 0 },
                        { r: 0, g: 128, b: 255, a: 0 }, { r: 128, g: 0, b: 255, a: 0 }
                    ]
                    names: [qsTr("White"), qsTr("Light grey"), qsTr("Grey"), qsTr("Dark grey"), qsTr("Black"),
                            qsTr("Red"), qsTr("Green"), qsTr("Blue"), qsTr("Yellow"), qsTr("Cyan"), qsTr("Magenta"),
                            qsTr("Orange"), qsTr("Lime"), qsTr("Spring green"), qsTr("Azure"), qsTr("Violet")]
                    onPicked: (c) => popover.pickColour(c)
                }
                ColumnLayout {
                    spacing: 8
                    Label { text: qsTr("Recent colours"); color: Theme.muted }
                    ColourSwatchGrid {
                        id: recent
                        objectName: "recent"
                        Accessible.name: qsTr("Recent colours")
                        colours: popover.expanded ? popover.picker.recent : popover.picker.recent.slice(0, popover.compactRecent)
                        names: colours.map(c => popover.picker.assText(c, true))
                        onPicked: (c) => popover.pickColour(c)
                    }
                }
                RowLayout {
                    spacing: 8
                    ColumnLayout {
                        spacing: 4
                        Layout.fillWidth: true
                        Label { text: qsTr("Hex"); color: Theme.muted }
                        TextField {
                            id: hex
                            objectName: "hexColour"
                            Layout.fillWidth: true
                            implicitWidth: 96
                            Accessible.name: qsTr("Hex")
                            text: popover.picker.htmlText(popover.colour())
                            onEditingFinished: {
                                if (!popover.setHex(text))
                                    text = Qt.binding(() => popover.picker.htmlText(popover.colour()))
                            }
                            onAccepted: popover.accept()
                        }
                    }
                    ColumnLayout {
                        spacing: 4
                        visible: popover.hasAlpha
                        Label { text: qsTr("Alpha"); color: Theme.muted }
                        SpinBox {
                            objectName: "alpha"
                            from: 0; to: 255; editable: true
                            implicitWidth: 92
                            value: popover.alpha
                            Accessible.name: qsTr("Alpha")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("0 is opaque, 255 fully transparent")
                            ToolTip.delay: 500
                            onValueModified: popover.setAlpha(value)
                        }
                    }
                }
                Button {
                    objectName: "colourMore"
                    Layout.alignment: Qt.AlignLeft
                    flat: true
                    text: popover.expanded ? qsTr("Less") : qsTr("More…")
                    Accessible.name: popover.expanded ? qsTr("Fewer colour options") : qsTr("More colour options")
                    onClicked: popover.expanded = !popover.expanded
                }
            }

            ColumnLayout {
                id: full
                objectName: "colourFull"
                visible: popover.expanded
                Layout.alignment: Qt.AlignTop
                spacing: 12
                RowLayout {
                    spacing: 8
                    Item {
                        id: spectrum
                        objectName: "spectrum"
                        implicitWidth: 256
                        implicitHeight: 256
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Slider
                        Accessible.name: qsTr("Saturation and value")
                        readonly property var hueColour: popover.picker.hsvToRgb(popover.hsvHue, 255, 255)
                        function step(ds, dv) {
                            popover.setHsv(popover.hsvHue, Math.max(0, Math.min(255, popover.hsvSaturation + ds)),
                                           Math.max(0, Math.min(255, popover.hsvValue + dv)))
                        }
                        Keys.onShortcutOverride: event => event.accepted = [Qt.Key_Left, Qt.Key_Right, Qt.Key_Up,
                                                                            Qt.Key_Down].includes(event.key)
                        Keys.onPressed: (event) => {
                            const n = event.modifiers & Qt.ShiftModifier ? 16 : 1
                            if (event.key === Qt.Key_Left) spectrum.step(-n, 0)
                            else if (event.key === Qt.Key_Right) spectrum.step(n, 0)
                            else if (event.key === Qt.Key_Up) spectrum.step(0, -n)
                            else if (event.key === Qt.Key_Down) spectrum.step(0, n)
                            else return
                            event.accepted = true
                        }
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
                            x: popover.hsvSaturation - 5
                            y: popover.hsvValue - 5
                            width: 10; height: 10; radius: 5
                            color: "transparent"
                            border.color: popover.hsvValue > 127 ? "black" : "white"
                        }
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -1
                            color: "transparent"
                            border.color: spectrum.activeFocus ? Theme.focus : Theme.line
                        }
                        MouseArea {
                            anchors.fill: parent
                            function pick(mouse) {
                                popover.setHsv(popover.hsvHue, Math.max(0, Math.min(255, Math.floor(mouse.x))),
                                               Math.max(0, Math.min(255, Math.floor(mouse.y))))
                            }
                            onPressed: (mouse) => { spectrum.forceActiveFocus(); pick(mouse) }
                            onPositionChanged: (mouse) => pick(mouse)
                        }
                    }
                    Item {
                        id: hueStrip
                        objectName: "hueStrip"
                        implicitWidth: 20
                        implicitHeight: 256
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Slider
                        Accessible.name: qsTr("Hue")
                        function step(d) {
                            popover.setHsv(Math.max(0, Math.min(255, popover.hsvHue + d)), popover.hsvSaturation, popover.hsvValue)
                        }
                        Keys.onShortcutOverride: event => event.accepted = event.key === Qt.Key_Up || event.key === Qt.Key_Down
                        Keys.onPressed: (event) => {
                            const n = event.modifiers & Qt.ShiftModifier ? 16 : 1
                            if (event.key === Qt.Key_Up) hueStrip.step(-n)
                            else if (event.key === Qt.Key_Down) hueStrip.step(n)
                            else return
                            event.accepted = true
                        }
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
                            y: popover.hsvHue - 1
                            width: hueStrip.width; height: 3
                            color: "transparent"
                            border.color: Theme.text
                        }
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -1
                            color: "transparent"
                            border.color: hueStrip.activeFocus ? Theme.focus : Theme.line
                        }
                        MouseArea {
                            anchors.fill: parent
                            function pick(mouse) {
                                popover.setHsv(Math.max(0, Math.min(255, Math.floor(mouse.y))), popover.hsvSaturation,
                                               popover.hsvValue)
                            }
                            onPressed: (mouse) => { hueStrip.forceActiveFocus(); pick(mouse) }
                            onPositionChanged: (mouse) => pick(mouse)
                        }
                    }
                }
                TabBar {
                    id: modes
                    objectName: "colourMode"
                    Layout.fillWidth: true
                    currentIndex: popover.mode
                    onCurrentIndexChanged: popover.mode = currentIndex
                    Accessible.name: qsTr("Colour values")
                    TabButton { objectName: "modeRgb"; text: qsTr("RGB") }
                    TabButton { objectName: "modeHsl"; text: qsTr("HSL") }
                    TabButton { objectName: "modeHsv"; text: qsTr("HSV") }
                }
                StackLayout {
                    Layout.fillWidth: true
                    currentIndex: popover.mode
                    ValueRow {
                        names: [qsTr("Red"), qsTr("Green"), qsTr("Blue")]
                        objectNames: ["red", "green", "blue"]
                        values: [popover.red, popover.green, popover.blue]
                        onModified: (i, v) => popover.setRgb(i === 0 ? v : popover.red, i === 1 ? v : popover.green,
                                                             i === 2 ? v : popover.blue)
                    }
                    ValueRow {
                        names: [qsTr("Hue"), qsTr("Saturation"), qsTr("Lightness")]
                        accessibleNames: [qsTr("HSL hue"), qsTr("HSL saturation"), qsTr("Lightness")]
                        objectNames: ["hslHue", "hslSaturation", "lightness"]
                        values: [popover.hslHue, popover.hslSaturation, popover.lightness]
                        onModified: (i, v) => popover.setHsl(i === 0 ? v : popover.hslHue, i === 1 ? v : popover.hslSaturation,
                                                             i === 2 ? v : popover.lightness)
                    }
                    ValueRow {
                        names: [qsTr("Hue"), qsTr("Saturation"), qsTr("Value")]
                        accessibleNames: [qsTr("HSV hue"), qsTr("HSV saturation"), qsTr("Value")]
                        objectNames: ["hsvHue", "hsvSaturation", "hsvValue"]
                        values: [popover.hsvHue, popover.hsvSaturation, popover.hsvValue]
                        onModified: (i, v) => popover.setHsv(i === 0 ? v : popover.hsvHue, i === 1 ? v : popover.hsvSaturation,
                                                             i === 2 ? v : popover.hsvValue)
                    }
                }
                RowLayout {
                    spacing: 8
                    ColumnLayout {
                        spacing: 4
                        Layout.fillWidth: true
                        Label { text: qsTr("ASS"); color: Theme.muted }
                        TextField {
                            objectName: "assText"
                            Layout.fillWidth: true
                            implicitWidth: 100
                            Accessible.name: qsTr("ASS colour")
                            text: popover.picker.assText(popover.colour(), false)
                            onEditingFinished: {
                                const c = popover.picker.parse(text)
                                popover.setRgb(c.r, c.g, c.b)
                            }
                        }
                    }
                    ColumnLayout {
                        spacing: 4
                        Layout.fillWidth: true
                        Label { text: qsTr("HTML"); color: Theme.muted }
                        TextField {
                            objectName: "htmlText"
                            Layout.fillWidth: true
                            implicitWidth: 100
                            Accessible.name: qsTr("HTML colour")
                            text: popover.picker.htmlText(popover.colour())
                            onEditingFinished: {
                                const c = popover.picker.htmlColour(text)
                                popover.setRgb(c.r, c.g, c.b)
                            }
                        }
                    }
                }
                CheckBox {
                    objectName: "switchClicks"
                    Layout.fillWidth: true
                    Layout.maximumWidth: spectrum.implicitWidth + hueStrip.implicitWidth + 8
                    text: qsTr("Swap shortcuts between the color picker\nand the color selection window").replace("\n", " ")
                    Component.onCompleted: if (contentItem && contentItem.wrapMode !== undefined) contentItem.wrapMode = Text.Wrap
                    Layout.preferredHeight: Math.max(implicitIndicatorHeight, contentItem.implicitHeight) + topPadding + bottomPadding
                    checked: popover.picker.switchClicks
                    onToggled: popover.picker.switchClicks = checked
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Checking this option opens the color picker on left-click,\nand the color selection window on right-click.")
                }
            }
        }
    }

    component ValueRow: RowLayout {
        id: row
        property var names: []
        property var accessibleNames: names
        property var objectNames: []
        property var values: [0, 0, 0]
        signal modified(int index, int value)
        spacing: 8
        Repeater {
            model: 3
            ColumnLayout {
                id: cell
                required property int index
                spacing: 4
                Layout.fillWidth: true
                Label { text: row.names[cell.index]; color: Theme.muted }
                SpinBox {
                    objectName: row.objectNames[cell.index]
                    Layout.fillWidth: true
                    implicitWidth: 80
                    from: 0; to: 255; editable: true
                    value: row.values[cell.index]
                    Accessible.name: row.accessibleNames[cell.index]
                    onValueModified: row.modified(cell.index, value)
                }
            }
        }
    }
}
