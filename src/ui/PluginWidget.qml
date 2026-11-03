import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Loader {
    id: widget
    property var node: null
    property var host: null
    readonly property string kind: node && typeof node.kind === "string" ? node.kind : ""
    readonly property var childNodes: node ? listOf(node.children) : []
    readonly property string widgetId: node && typeof node.id === "string" ? node.id : ""
    readonly property string accessibleName: node ? (node.name || node.label || node.text || node.title || "") : ""

    enabled: !(node && node.disabled === true)
    sourceComponent: ({
        label: labelKind, button: buttonKind, icon_button: iconButtonKind, text: textKind, text_area: textAreaKind,
        number: numberKind, time: timeKind, check: checkKind, choice: choiceKind, radio: radioKind,
        dropdown: dropdownKind, colour: colourKind, slider: sliderKind, progress: progressKind, list: listKind,
        tree: treeKind, tabs: tabsKind, group: groupKind, link: linkKind, image: imageKind, row: rowKind,
        column: columnKind, grid: gridKind, empty: emptyKind, canvas: canvasKind
    })[kind] ?? null

    function listOf(value) { return value && typeof value === "object" && typeof value.length === "number" ? value : [] }
    // Controls that guess rich text show a plugin's "<b>" as written: a word joiner after "<" keeps it plain.
    function plain(text) { return String(text ?? "").replace(/</g, "<⁠") }
    function send(event) {
        if (widget.host)
            widget.host.send(Object.assign({ widget: widget.widgetId }, event))
    }
    function change(value) { widget.send({ kind: "change", value: value }) }
    function options() {
        return (node ? listOf(node.options) : [])
            .map(o => typeof o === "string" ? { value: o, text: o } : o)
    }
    function optionIndex() {
        const all = widget.options()
        const i = all.findIndex(o => o.value === node.value)
        return i
    }
    function fillsHeight(child) {
        return !!child && (child.grow === true || ((child.kind === "list" || child.kind === "tree") && !child.height))
    }
    function fills(child) {
        return child && (child.grow === true
                         || ["text", "text_area", "dropdown", "list", "tree", "tabs", "progress", "slider", "group",
                             "row", "column", "grid", "empty"].includes(child.kind)
                         || (child.kind === "canvas" && child.width === undefined)
                         || (child.kind === "label" && child.wrap === true))
    }

    HoverHandler { id: hover }
    ToolTip.visible: hover.hovered && !!node && !!node.tooltip && widget.kind !== "icon_button"
    ToolTip.text: node && node.tooltip ? widget.plain(node.tooltip) : ""

    component Labelled: RowLayout {
        id: labelled
        required property string label
        default property alias control: holder.data
        spacing: 8
        Label {
            visible: labelled.label.length > 0
            text: labelled.label
            textFormat: Text.PlainText
            Layout.alignment: Qt.AlignVCenter
        }
        Item {
            id: holder
            Layout.fillWidth: true
            implicitWidth: children.length ? children[0].implicitWidth : 0
            implicitHeight: children.length ? children[0].implicitHeight : 0
        }
    }

    component Child: Loader {
        id: slot
        required property int index
        required property var nodes
        required property var surface
        readonly property var childNode: nodes[index]
        source: Qt.resolvedUrl("PluginWidget.qml")
        onLoaded: {
            item.host = Qt.binding(() => slot.surface)
            item.node = Qt.binding(() => slot.childNode)
        }
    }

    Component {
        id: labelKind
        Label {
            objectName: "pluginWidget_" + widget.widgetId
            text: widget.node.text
            textFormat: Text.PlainText
            wrapMode: widget.node.wrap === true ? Text.Wrap : Text.NoWrap
            elide: widget.node.wrap === true ? Text.ElideNone : Text.ElideRight
            color: widget.node.style === "muted" ? Theme.muted : palette.windowText
            font.bold: widget.node.style === "heading"
            font.pointSize: Qt.application.font.pointSize + (widget.node.style === "heading" ? 1
                                                            : widget.node.style === "small" ? -1 : 0)
            Accessible.name: text
        }
    }
    Component {
        id: buttonKind
        Loader {
            sourceComponent: widget.node.style === "primary" ? primary
                           : widget.node.style === "danger" ? danger : normal
            Component {
                id: normal
                IconTextButton {
                    objectName: "pluginWidget_" + widget.widgetId
                    text: widget.plain(widget.node.text)
                    iconRole: widget.node.icon ?? ""
                    Accessible.name: widget.accessibleName
                    onClicked: widget.send({ kind: "click" })
                }
            }
            Component {
                id: primary
                PrimaryButton {
                    objectName: "pluginWidget_" + widget.widgetId
                    text: widget.plain(widget.node.text)
                    Accessible.name: widget.accessibleName
                    onClicked: widget.send({ kind: "click" })
                }
            }
            Component {
                id: danger
                DangerButton {
                    objectName: "pluginWidget_" + widget.widgetId
                    text: widget.plain(widget.node.text)
                    Accessible.name: widget.accessibleName
                    onClicked: widget.send({ kind: "click" })
                }
            }
        }
    }
    Component {
        id: iconButtonKind
        IconButton {
            objectName: "pluginWidget_" + widget.widgetId
            iconRole: widget.node.icon
            text: widget.node.name
            tip: widget.plain(widget.node.name)
            checkable: widget.node.checked !== undefined
            checked: widget.node.checked === true
            onClicked: {
                widget.send({ kind: "click" })
                if (checkable)
                    checked = Qt.binding(() => widget.node.checked === true)
            }
        }
    }
    Component {
        id: textKind
        Labelled {
            label: widget.node.label ?? ""
            TextField {
                id: field
                objectName: "pluginWidget_" + widget.widgetId
                anchors.left: parent.left
                anchors.right: parent.right
                readonly property string given: widget.node.value ?? ""
                text: given
                placeholderText: widget.node.placeholder ?? ""
                selectByMouse: true
                Accessible.name: widget.accessibleName
                onTextEdited: widget.change(text)
                Connections {
                    target: widget
                    function onNodeChanged() { if (!field.activeFocus) field.text = field.given }
                }
                onAccepted: widget.send({ kind: "submit", value: text })
            }
        }
    }
    Component {
        id: textAreaKind
        Labelled {
            label: widget.node.label ?? ""
            TextArea {
                id: area
                objectName: "pluginWidget_" + widget.widgetId
                anchors.left: parent.left
                anchors.right: parent.right
                readonly property string given: widget.node.value ?? ""
                text: given
                placeholderText: widget.node.placeholder ?? ""
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                implicitHeight: Math.max(contentHeight, fontMetrics.height * (widget.node.rows || 3))
                                + topPadding + bottomPadding
                Accessible.name: widget.accessibleName
                onTextChanged: if (activeFocus) widget.change(text)
                FontMetrics { id: fontMetrics; font: area.font }
                Connections {
                    target: widget
                    function onNodeChanged() { if (!area.activeFocus) area.text = area.given }
                }
            }
        }
    }
    Component {
        id: numberKind
        Labelled {
            label: widget.node.label ?? ""
            RowLayout {
                anchors.left: parent.left
                spacing: 4
                TextField {
                    objectName: "pluginWidget_" + widget.widgetId
                    readonly property real given: widget.node.value ?? 0
                    readonly property int decimals: widget.node.decimals ?? 0
                    function shown(v) { return Number(v).toFixed(decimals) }
                    function clamped(v) {
                        let n = v
                        if (widget.node.minimum !== undefined)
                            n = Math.max(n, widget.node.minimum)
                        if (widget.node.maximum !== undefined)
                            n = Math.min(n, widget.node.maximum)
                        return n
                    }
                    text: shown(given)
                    onGivenChanged: if (!activeFocus) text = shown(given)
                    horizontalAlignment: Text.AlignRight
                    implicitWidth: 80
                    selectByMouse: true
                    validator: DoubleValidator { notation: DoubleValidator.StandardNotation }
                    Accessible.name: widget.accessibleName
                    onEditingFinished: {
                        const v = parseFloat(text.replace(",", "."))
                        const n = isNaN(v) ? given : clamped(v)
                        text = shown(n)
                        if (n !== given)
                            widget.change(n)
                    }
                    Keys.onUpPressed: { text = shown(clamped(parseFloat(text) + (widget.node.step || 1))); widget.change(parseFloat(text)) }
                    Keys.onDownPressed: { text = shown(clamped(parseFloat(text) - (widget.node.step || 1))); widget.change(parseFloat(text)) }
                }
                Label {
                    visible: !!widget.node.unit
                    text: widget.node.unit ?? ""
                    textFormat: Text.PlainText
                    color: Theme.muted
                }
            }
        }
    }
    Component {
        id: timeKind
        Labelled {
            label: widget.node.label ?? ""
            TextField {
                objectName: "pluginWidget_" + widget.widgetId
                anchors.left: parent.left
                readonly property real given: widget.node.value ?? 0
                function shown(ms) {
                    const cs = Math.round(Math.max(0, ms) / 10)
                    const pad = n => String(n).padStart(2, "0")
                    return Math.floor(cs / 360000) + ":" + pad(Math.floor(cs / 6000) % 60) + ":"
                           + pad(Math.floor(cs / 100) % 60) + "." + pad(cs % 100)
                }
                function parsed(t) {
                    const m = /^\s*(\d+):(\d{1,2}):(\d{1,2})(?:[.,](\d{1,2}))?\s*$/.exec(t)
                    if (!m)
                        return NaN
                    return ((+m[1] * 60 + +m[2]) * 60 + +m[3]) * 1000 + (m[4] ? +m[4].padEnd(2, "0") * 10 : 0)
                }
                text: shown(given)
                onGivenChanged: if (!activeFocus) text = shown(given)
                font.family: "monospace"
                horizontalAlignment: Text.AlignRight
                implicitWidth: 100
                selectByMouse: true
                Accessible.name: widget.accessibleName
                onEditingFinished: {
                    const ms = parsed(text)
                    text = shown(isNaN(ms) ? given : ms)
                    if (!isNaN(ms) && ms !== given)
                        widget.change(ms)
                }
            }
        }
    }
    Component {
        id: checkKind
        CheckBox {
            id: check
            objectName: "pluginWidget_" + widget.widgetId
            text: widget.plain(widget.node.text)
            checked: widget.node.value === true
            Accessible.name: widget.accessibleName
            onToggled: widget.change(checked)
            Connections {
                target: widget
                function onNodeChanged() { check.checked = widget.node.value === true }
            }
        }
    }
    Component {
        id: choiceKind
        Labelled {
            label: widget.node.label ?? ""
            SegmentedButtons {
                objectName: "pluginWidget_" + widget.widgetId
                anchors.left: parent.left
                choices: widget.options().map(o => ({ text: widget.plain(o.text) }))
                id: segments
                currentIndex: widget.optionIndex()
                Accessible.name: widget.accessibleName
                onActivated: index => {
                    currentIndex = index
                    widget.change(widget.options()[index].value)
                }
                Connections {
                    target: widget
                    function onNodeChanged() { segments.currentIndex = widget.optionIndex() }
                }
            }
        }
    }
    Component {
        id: radioKind
        Labelled {
            label: widget.node.label ?? ""
            Flow {
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: 12
                Accessible.role: Accessible.Grouping
                Accessible.name: widget.accessibleName
                Repeater {
                    model: widget.options()
                    RadioButton {
                        id: radio
                        required property var modelData
                        objectName: "pluginWidget_" + widget.widgetId + "_" + modelData.value
                        text: widget.plain(modelData.text)
                        checked: widget.node.value === modelData.value
                        onToggled: if (checked) widget.change(modelData.value)
                        Connections {
                            target: widget
                            function onNodeChanged() { radio.checked = widget.node.value === radio.modelData.value }
                        }
                    }
                }
            }
        }
    }
    Component {
        id: dropdownKind
        Labelled {
            label: widget.node.label ?? ""
            ComboBox {
                id: combo
                objectName: "pluginWidget_" + widget.widgetId
                anchors.left: parent.left
                anchors.right: parent.right
                model: widget.options().map(o => widget.plain(o.text))
                currentIndex: widget.optionIndex()
                Accessible.name: widget.accessibleName
                onActivated: index => widget.change(widget.options()[index].value)
                Connections {
                    target: widget
                    function onNodeChanged() { combo.currentIndex = widget.optionIndex() }
                }
            }
        }
    }
    Component {
        id: colourKind
        Labelled {
            label: widget.node.label ?? ""
            RowLayout {
                id: colourRow
                anchors.left: parent.left
                spacing: 6
                readonly property bool alpha: widget.node.alpha === true
                readonly property string given: widget.node.value ?? (alpha ? "&H00FFFFFF" : "&HFFFFFF&")
                property string value: given
                Connections {
                    target: widget
                    function onNodeChanged() { colourRow.value = colourRow.given }
                }
                function parts(text) {
                    const m = /^&H([0-9A-Fa-f]{2})?([0-9A-Fa-f]{6})&?$/.exec(text)
                    const v = m ? parseInt(m[2], 16) : 0xFFFFFF
                    return { r: v & 255, g: (v >> 8) & 255, b: (v >> 16) & 255, a: m && m[1] ? parseInt(m[1], 16) : 0 }
                }
                function text(c) {
                    const hex = n => ("0" + Math.round(n).toString(16).toUpperCase()).slice(-2)
                    return alpha ? "&H" + hex(c.a) + hex(c.b) + hex(c.g) + hex(c.r) : "&H" + hex(c.b) + hex(c.g) + hex(c.r) + "&"
                }
                Button {
                    id: swatch
                    objectName: "pluginWidget_" + widget.widgetId
                    implicitWidth: 40
                    Accessible.name: widget.accessibleName
                    contentItem: Rectangle {
                        readonly property var c: colourRow.parts(colourRow.value)
                        color: Qt.rgba(c.r / 255, c.g / 255, c.b / 255, 1)
                        border.color: Theme.line
                    }
                    enabled: !!widget.host && !!widget.host.colourPopover
                    onClicked: {
                        const row = colourRow
                        let before = ""
                        widget.host.colourPopover.openFor(swatch, {
                            changeColour: c => row.value = row.text(c),
                            endDialog: accepted => {
                                if (!accepted)
                                    row.value = before
                                else if (row.value !== before)
                                    widget.change(row.value)
                            }
                        }, () => {
                            before = row.value
                            if (widget.host.colourPicker)
                                widget.host.colourPicker.opened("plugin")
                            return row.parts(row.value)
                        }, { title: widget.accessibleName, alpha: row.alpha, focus: swatch })
                    }
                }
                Label {
                    text: colourRow.value
                    textFormat: Text.PlainText
                    font.family: "monospace"
                }
            }
        }
    }
    Component {
        id: sliderKind
        Labelled {
            label: widget.node.label ?? ""
            Slider {
                id: slider
                objectName: "pluginWidget_" + widget.widgetId
                anchors.left: parent.left
                anchors.right: parent.right
                from: widget.node.minimum ?? 0
                to: widget.node.maximum ?? 1
                stepSize: widget.node.step ?? 0
                value: widget.node.value ?? from
                Accessible.name: widget.accessibleName
                onMoved: widget.change(value)
                Connections {
                    target: widget
                    function onNodeChanged() { if (!slider.pressed) slider.value = widget.node.value ?? slider.from }
                }
            }
        }
    }
    Component {
        id: progressKind
        ColumnLayout {
            objectName: "pluginWidget_" + widget.widgetId
            spacing: 2
            Label {
                visible: !!widget.node.text
                text: widget.node.text ?? ""
                textFormat: Text.PlainText
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            ProgressBar {
                Layout.fillWidth: true
                indeterminate: widget.node.value === undefined
                value: widget.node.value ?? 0
                Accessible.name: widget.node.text || widget.accessibleName
            }
        }
    }
    Component {
        id: listKind
        PluginList {
            objectName: "pluginWidget_" + widget.widgetId
            rows: widget.node.rows
            selected: widget.node.selected ?? ""
            fixedHeight: widget.node.height ?? 0
            accessibleName: widget.accessibleName
            onSelect: id => widget.send({ kind: "select", value: id })
            onActivate: id => widget.send({ kind: "activate", value: id })
        }
    }
    Component {
        id: treeKind
        PluginList {
            objectName: "pluginWidget_" + widget.widgetId
            nested: true
            rows: widget.node.rows
            selected: widget.node.selected ?? ""
            fixedHeight: widget.node.height ?? 0
            accessibleName: widget.accessibleName
            onSelect: id => widget.send({ kind: "select", value: id })
            onActivate: id => widget.send({ kind: "activate", value: id })
        }
    }
    Component {
        id: tabsKind
        ColumnLayout {
            spacing: 4
            TabBar {
                id: bar
                objectName: "pluginWidget_" + widget.widgetId
                Layout.fillWidth: true
                currentIndex: widget.node.selected ?? 0
                Accessible.name: widget.accessibleName
                Connections {
                    target: widget
                    function onNodeChanged() { bar.currentIndex = widget.node.selected ?? 0 }
                }
                Repeater {
                    model: widget.node.tabs
                    TabButton {
                        required property var modelData
                        required property int index
                        text: widget.plain(modelData.text)
                        width: implicitWidth
                        onClicked: widget.change(index)
                    }
                }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: bar.currentIndex
                Repeater {
                    model: widget.node.tabs.length
                    Child {
                        nodes: widget.node.tabs.map(t => t.body)
                        surface: widget.host
                    }
                }
            }
        }
    }
    Component {
        id: groupKind
        ColumnLayout {
            objectName: "pluginWidget_" + widget.widgetId
            spacing: 8
            Accessible.role: Accessible.Grouping
            Accessible.name: widget.node.title
            Label {
                text: widget.node.title
                textFormat: Text.PlainText
                font.bold: true
            }
            Repeater {
                model: widget.childNodes.length
                Child {
                    nodes: widget.childNodes
                    surface: widget.host
                    Layout.fillWidth: widget.fills(childNode)
                    Layout.fillHeight: childNode.grow === true
                }
            }
        }
    }
    Component {
        id: linkKind
        AbstractButton {
            objectName: "pluginWidget_" + widget.widgetId
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Link
            Accessible.name: widget.node.text
            contentItem: Text {
                text: widget.node.text
                textFormat: Text.PlainText
                color: Theme.accent
                font.underline: true
            }
            background: FocusRing { visible: parent.visualFocus }
            onClicked: widget.send({ kind: "click" })
        }
    }
    Component {
        id: imageKind
        Image {
            objectName: "pluginWidget_" + widget.widgetId
            source: widget.host && widget.host.packageRoot ? widget.host.packageRoot + "/" + widget.node.src : ""
            sourceSize: Qt.size(widget.node.width ?? 0, widget.node.height ?? 0)
            fillMode: Image.PreserveAspectFit
            Accessible.role: Accessible.Graphic
            Accessible.name: widget.node.name ?? ""
            Accessible.ignored: !widget.node.name
        }
    }
    Component {
        id: rowKind
        RowLayout {
            spacing: 8
            Repeater {
                model: widget.childNodes.length
                Child {
                    nodes: widget.childNodes
                    surface: widget.host
                    readonly property bool grows: !!childNode && (childNode.grow === true || childNode.kind === "text")
                    readonly property bool shrinks: !!childNode && ["number", "time", "dropdown", "choice"].includes(childNode.kind)
                    Layout.fillWidth: grows || shrinks
                    Layout.maximumWidth: grows ? Number.POSITIVE_INFINITY : implicitWidth
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }
    }
    Component {
        id: columnKind
        ColumnLayout {
            spacing: 8
            Repeater {
                model: widget.childNodes.length
                Child {
                    nodes: widget.childNodes
                    surface: widget.host
                    Layout.fillWidth: widget.fills(childNode)
                    Layout.fillHeight: widget.fillsHeight(childNode)
                }
            }
            Item {
                visible: !Array.prototype.some.call(widget.childNodes, child => widget.fillsHeight(child))
                Layout.fillHeight: true
            }
        }
    }
    Component {
        id: gridKind
        GridLayout {
            columns: Math.max(1, widget.node.columns)
            columnSpacing: 8
            rowSpacing: 8
            Repeater {
                model: widget.childNodes.length
                Child {
                    nodes: widget.childNodes
                    surface: widget.host
                    Layout.fillWidth: widget.fills(childNode)
                }
            }
        }
    }
    Component {
        id: emptyKind
        Label {
            text: widget.node.text
            textFormat: Text.PlainText
            color: Theme.muted
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
            topPadding: 12
            bottomPadding: 12
        }
    }
    Component {
        id: canvasKind
        PluginCanvas {
            objectName: "pluginWidget_" + widget.widgetId
            node: widget.node
            packageRoot: widget.host ? widget.host.packageRoot : ""
            onPointer: event => widget.send(Object.assign({ kind: "pointer" }, event))
        }
    }
}
