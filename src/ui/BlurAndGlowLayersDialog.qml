pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Layers"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "blurAndGlowLayersDialog"
    required property var app
    required property var shell
    required property ColourPickerController colourPicker
    title: qsTr("Blur and glow layers")
    dialogId: "blurAndGlowLayers"
    defaultButton: buildButton
    property var document: 0
    property string failure: ""
    property int tick: 0
    property var glows: [defaultGlow()]
    property string secondColour: ""
    property string nearColour: ""
    property string farColour: ""
    readonly property var status: visible ? (tick, app.checkLayerBuilders(options())) : ({})

    function defaultGlow() {
        return { blur: "3", border: "+0", colour: "", alpha: "80" }
    }
    Connections {
        target: dialog.app
        function onTabsChanged() {
            if (dialog.visible && dialog.app.editingDocument() !== dialog.document)
                dialog.reject()
        }
    }
    Connections {
        target: dialog.shell
        function onActiveLineChanged() { if (dialog.visible) dialog.tick++ }
    }

    function options() {
        return {
            glow: glowOn.checked,
            glows: glows,
            border: borderOn.checked,
            under: under.currentIndex === 1 ? "transparent" : "border",
            borderBlur: borderBlur.text,
            fadeFix: fadeFix.checked,
            fadeMs: fadeMs.text,
            secondBorder: second.checked,
            secondWidth: secondWidth.text,
            secondColour: secondColour,
            shadow: shadowOn.checked,
            steps: steps.text,
            nearColour: nearColour,
            farColour: farColour,
            nearAlpha: nearAlpha.text,
            farAlpha: farAlpha.text,
            defaultBlur: defaultBlur.text
        }
    }
    function remembered() {
        try {
            return JSON.parse(dialog.settings ? dialog.settings.value("typesetting.layerOptions") || "{}" : "{}")
        } catch (e) {
            return {}
        }
    }
    function text(v, fallback) {
        return v === undefined || v === null ? fallback : String(v)
    }
    function load(o) {
        glowOn.checked = o.glow !== false
        glows = Array.isArray(o.glows) && o.glows.length > 0
                ? o.glows.slice(0, 8).map(g => ({ blur: text(g.blur, "3"), border: text(g.border, "+0"),
                                                  colour: text(g.colour, ""), alpha: text(g.alpha, "80") }))
                : [defaultGlow()]
        borderOn.checked = o.border !== false
        under.currentIndex = o.under === "transparent" ? 1 : 0
        borderBlur.text = text(o.borderBlur, "")
        fadeFix.checked = o.fadeFix !== false
        fadeMs.text = text(o.fadeMs, "45")
        second.checked = o.secondBorder === true
        secondWidth.text = text(o.secondWidth, "2")
        secondColour = text(o.secondColour, "")
        shadowOn.checked = o.shadow === true
        steps.text = text(o.steps, "")
        nearColour = text(o.nearColour, "")
        farColour = text(o.farColour, "")
        nearAlpha.text = text(o.nearAlpha, "00")
        farAlpha.text = text(o.farAlpha, "A0")
        defaultBlur.text = text(o.defaultBlur, "0.6")
    }
    function openDialog() {
        if (visible) {
            raise()
            requestActivate()
            return
        }
        load(remembered())
        failure = ""
        document = app.editingDocument()
        open()
    }
    function setGlow(i, key, value) {
        const next = glows.slice()
        next[i] = Object.assign({}, next[i], { [key]: value })
        glows = next
    }
    function build() {
        if (!status.canApply || app.editingDocument() !== document)
            return
        previewTimer.stop()
        failure = app.applyLayerBuilders(options())
        if (failure.length > 0)
            return
        if (dialog.settings)
            dialog.settings.setValue("typesetting.layerOptions", JSON.stringify(options()))
        close()
    }
    onStatusChanged: if (visible) previewTimer.restart()
    onVisibleChanged: {
        if (visible)
            return
        previewTimer.stop()
        app.clearLayerPreview()
    }
    Timer {
        id: previewTimer
        interval: 100
        onTriggered: {
            if (dialog.visible && preview.checked && dialog.status.canApply)
                dialog.app.setLayerPreview(dialog.options())
            else
                dialog.app.clearLayerPreview()
        }
    }

    function rgbOf(ass) {
        const m = /&?H?([0-9A-Fa-f]{1,8})&?/.exec(ass || "")
        if (!m)
            return { r: 255, g: 255, b: 255, a: 0 }
        const v = parseInt(m[1], 16)
        return { r: v & 0xFF, g: (v >> 8) & 0xFF, b: (v >> 16) & 0xFF, a: 0 }
    }
    function pickColour(swatch, current, set, title) {
        let before = current
        colourPopover.openFor(swatch, {
            changeColour: v => set(dialog.colourPicker.assText(v, false)),
            clearColour: () => set(""),
            endDialog: accepted => { if (!accepted) set(before) }
        }, () => {
            dialog.colourPicker.opened("blurAndGlowLayers")
            return dialog.rgbOf(current)
        }, { title: title, alpha: false, clear: true, focus: swatch })
    }

    component Hint: Label {
        wrapMode: Text.Wrap
        color: Theme.muted
        font: dialog.fontAt(12)
    }
    component Rule: Rectangle {
        color: Theme.line
        implicitHeight: 1
        Layout.fillWidth: true
    }
    component Field: NumberField {
        implicitWidth: 56
        horizontalAlignment: Text.AlignRight
        onTextEdited: dialog.failure = ""
    }
    component Swatch: Button {
        id: swatch
        property string colour: ""
        property string lineText: ""
        implicitWidth: 34
        implicitHeight: 24
        padding: 3
        Accessible.name: text
        ToolTip.visible: hovered
        ToolTip.text: text
        readonly property var rgb: dialog.rgbOf(colour)
        contentItem: Rectangle {
            radius: 3
            color: swatch.colour.length === 0 ? Theme.field : Color.rgba(swatch.rgb.r / 255, swatch.rgb.g / 255, swatch.rgb.b / 255, 1)
            border.color: swatch.visualFocus ? Theme.focus : Theme.line
            border.width: 1
            Label {
                anchors.centerIn: parent
                visible: swatch.colour.length === 0
                text: qsTr("Line")
                color: Theme.muted
                font: dialog.fontAt(10)
            }
        }
    }
    component SectionHead: RowLayout {
        id: head
        property string title
        property string summary
        property alias checked: toggle.checked
        property alias toggleName: toggle.objectName
        spacing: 8
        Layout.fillWidth: true
        Layout.topMargin: 4
        Label {
            text: head.title
            font: dialog.fontAt(13, Font.DemiBold)
        }
        Label {
            text: head.summary
            visible: !head.checked
            color: Theme.muted
            font: dialog.fontAt(12)
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Item { Layout.fillWidth: true; visible: head.checked }
        Switch {
            id: toggle
            Accessible.name: head.title
            onToggled: dialog.failure = ""
        }
    }

    ColumnLayout {
        spacing: 8
        Layout.preferredWidth: 470

        SectionHead {
            id: glowOn
            title: qsTr("Glow")
            summary: qsTr("Blurred copies under each line")
            toggleName: "layersGlowOn"
        }
        GridLayout {
            visible: glowOn.checked
            columns: 5
            columnSpacing: 10
            rowSpacing: 6
            Layout.leftMargin: 2
            Label { text: qsTr("Blur"); color: Theme.muted; font: dialog.fontAt(12) }
            Label { text: qsTr("Border"); color: Theme.muted; font: dialog.fontAt(12) }
            Label { text: qsTr("Colour"); color: Theme.muted; font: dialog.fontAt(12) }
            Label { text: qsTr("Alpha"); color: Theme.muted; font: dialog.fontAt(12) }
            Item { implicitWidth: 1 }
            Repeater {
                model: dialog.glows.length * 5
                delegate: Loader {
                    id: cell
                    required property int index
                    readonly property int row: Math.floor(index / 5)
                    readonly property int column: index % 5
                    sourceComponent: [blurCell, borderCell, colourCell, alphaCell, removeCell][column]
                    Component {
                        id: blurCell
                        Field {
                            objectName: "layersGlowBlur" + cell.row
                            text: dialog.glows[cell.row].blur
                            Accessible.name: qsTr("Glow %1 blur").arg(cell.row + 1)
                            onTextEdited: dialog.setGlow(cell.row, "blur", text)
                        }
                    }
                    Component {
                        id: borderCell
                        Field {
                            text: dialog.glows[cell.row].border
                            Accessible.name: qsTr("Glow %1 border, added to the line’s").arg(cell.row + 1)
                            onTextEdited: dialog.setGlow(cell.row, "border", text)
                        }
                    }
                    Component {
                        id: colourCell
                        Swatch {
                            id: glowSwatch
                            colour: dialog.glows[cell.row].colour
                            text: colour.length === 0 ? qsTr("Glow %1 colour: the line’s border colour").arg(cell.row + 1)
                                                      : qsTr("Glow %1 colour").arg(cell.row + 1)
                            onClicked: dialog.pickColour(glowSwatch, colour, v => dialog.setGlow(cell.row, "colour", v),
                                                         qsTr("Glow %1 colour").arg(cell.row + 1))
                        }
                    }
                    Component {
                        id: alphaCell
                        Field {
                            text: dialog.glows[cell.row].alpha
                            Accessible.name: qsTr("Glow %1 alpha, hex").arg(cell.row + 1)
                            onTextEdited: dialog.setGlow(cell.row, "alpha", text)
                        }
                    }
                    Component {
                        id: removeCell
                        IconButton {
                            iconRole: "remove"
                            text: qsTr("Remove glow %1").arg(cell.row + 1)
                            enabled: dialog.glows.length > 1
                            onClicked: {
                                const next = dialog.glows.slice()
                                next.splice(cell.row, 1)
                                dialog.glows = next
                            }
                        }
                    }
                }
            }
        }
        IconButton {
            objectName: "layersAddGlow"
            visible: glowOn.checked
            enabled: dialog.glows.length < 8
            iconRole: "add"
            text: qsTr("Add a glow layer")
            Layout.leftMargin: -4
            onClicked: dialog.glows = dialog.glows.concat([dialog.defaultGlow()])
        }

        Rule {}
        SectionHead {
            id: borderOn
            title: qsTr("Border")
            summary: qsTr("The border on its own layer, blurred apart from the fill")
            toggleName: "layersBorderOn"
        }
        GridLayout {
            visible: borderOn.checked
            columns: 2
            columnSpacing: 10
            rowSpacing: 8
            Label { text: qsTr("Under the border") }
            SegmentedButtons {
                id: under
                objectName: "layersUnder"
                choices: [{ text: qsTr("Border colour") },
                          { text: qsTr("Transparent"), tip: qsTr("A thin fill-coloured border closes the gap under the blur") }]
                onActivated: index => currentIndex = index
            }
            Label { text: qsTr("Border blur") }
            RowLayout {
                spacing: 6
                Field {
                    id: borderBlur
                    placeholderText: qsTr("Line’s")
                    Accessible.name: qsTr("Border blur")
                }
                Hint { text: qsTr("The fill keeps the line’s blur.") }
            }
        }
        RowLayout {
            visible: borderOn.checked
            spacing: 6
            CheckBox { id: fadeFix; text: qsTr("Hide the fill under the border during fades") }
            Field { id: fadeMs; enabled: fadeFix.checked; implicitWidth: 48; Accessible.name: qsTr("Fade transition, ms") }
            Label { text: qsTr("ms") }
        }
        RowLayout {
            visible: borderOn.checked
            spacing: 6
            CheckBox { id: second; objectName: "layersSecondBorder"; text: qsTr("Second border") }
            Field { id: secondWidth; enabled: second.checked; implicitWidth: 48; Accessible.name: qsTr("Second border width") }
            Swatch {
                id: secondSwatch
                enabled: second.checked
                colour: dialog.secondColour
                text: colour.length === 0 ? qsTr("Second border colour: the line’s fill colour") : qsTr("Second border colour")
                onClicked: dialog.pickColour(secondSwatch, colour, v => dialog.secondColour = v, qsTr("Second border colour"))
            }
        }

        Rule {}
        SectionHead {
            id: shadowOn
            title: qsTr("Shadow")
            summary: qsTr("A 3D shadow of stacked copies")
            toggleName: "layersShadowOn"
        }
        GridLayout {
            visible: shadowOn.checked
            columns: 2
            columnSpacing: 10
            rowSpacing: 8
            Label { text: qsTr("Steps") }
            RowLayout {
                spacing: 6
                Field {
                    id: steps
                    objectName: "layersSteps"
                    placeholderText: qsTr("Auto")
                    Accessible.name: qsTr("Steps")
                }
                Hint { text: qsTr("Auto: one a pixel of each line’s shadow offset") }
            }
            Label { text: qsTr("Colour") }
            RowLayout {
                spacing: 6
                Swatch {
                    id: nearSwatch
                    colour: dialog.nearColour
                    text: colour.length === 0 ? qsTr("Nearest copy: the line’s shadow colour") : qsTr("Nearest copy colour")
                    onClicked: dialog.pickColour(nearSwatch, colour, v => dialog.nearColour = v, qsTr("Nearest copy colour"))
                }
                Icon { iconRole: "chevron-right"; size: 12; color: Theme.muted }
                Swatch {
                    id: farSwatch
                    colour: dialog.farColour
                    text: colour.length === 0 ? qsTr("Farthest copy: the line’s shadow colour") : qsTr("Farthest copy colour")
                    onClicked: dialog.pickColour(farSwatch, colour, v => dialog.farColour = v, qsTr("Farthest copy colour"))
                }
            }
            Label { text: qsTr("Alpha") }
            RowLayout {
                spacing: 6
                Field { id: nearAlpha; Accessible.name: qsTr("Nearest copy alpha, hex") }
                Icon { iconRole: "chevron-right"; size: 12; color: Theme.muted }
                Field { id: farAlpha; Accessible.name: qsTr("Farthest copy alpha, hex") }
            }
        }

        Rule {}
        RowLayout {
            spacing: 6
            Label { text: qsTr("Blur where a line has none") }
            Field { id: defaultBlur; Accessible.name: qsTr("Blur where a line has none") }
        }
        RowLayout {
            id: statusRow
            spacing: 6
            Layout.fillWidth: true
            Layout.topMargin: 4
            Layout.minimumHeight: 34
            readonly property string kind: dialog.failure.length > 0 ? "error" : dialog.status.state ?? "idle"
            readonly property color tint: kind === "error" ? Theme.danger : kind === "warning" ? Theme.warning : Theme.text
            Icon {
                iconRole: ({ warning: "notice-warning", error: "notice-error" })[statusRow.kind] || "notice-info"
                size: 14
                color: statusRow.tint
                accentColor: statusRow.tint
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 1
            }
            Label {
                objectName: "layersStatus"
                text: dialog.failure.length > 0 ? dialog.failure : dialog.status.text ?? ""
                color: statusRow.tint
                wrapMode: Text.Wrap
                font: dialog.fontAt(12)
                Layout.fillWidth: true
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
    }

    leadingActions: [
        CheckBox {
            id: preview
            objectName: "layersPreview"
            text: qsTr("Preview on video")
            checked: true
            onToggled: previewTimer.restart()
        }
    ]
    buttons: [
        PrimaryButton {
            id: buildButton
            objectName: "layersBuild"
            text: qsTr("Build layers")
            enabled: dialog.status.canApply === true
            onClicked: dialog.build()
        },
        Button {
            objectName: "layersCancel"
            text: qsTr("Cancel")
            onClicked: dialog.reject()
        }
    ]

    ColourPopover {
        id: colourPopover
        objectName: "layersColourPopover"
        picker: dialog.colourPicker
    }
}
