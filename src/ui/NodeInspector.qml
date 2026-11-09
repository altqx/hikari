pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.NodeEffects"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Pane {
    id: root
    objectName: "nodeInspector"
    required property NodeEffectsController controller
    property ColourPickerController colourPicker: null
    padding: 0
    implicitWidth: 300
    implicitHeight: 480
    readonly property size minimumSize: Qt.size(240, 200)
    readonly property var node: controller.inspector
    readonly property bool hasNode: node.id !== undefined
    readonly property bool editable: controller.editable && !controller.newerFormat
    readonly property font monoFont: Qt.font({ family: "monospace", pixelSize: Qt.application.font.pixelSize })

    function label(name) {
        return name.charAt(0).toUpperCase() + name.slice(1).replace(/_/g, " ")
    }
    function milliseconds(ms) {
        //: A time a node effect took: %1 milliseconds
        return qsTr("%1 ms").arg(ms < 10 ? ms.toFixed(2) : Math.round(ms))
    }
    function listType() {
        const t = root.node.fields?.find(f => f.name === "type")
        return t ? t.value : ""
    }
    function colourOf(text) {
        return /^#[0-9A-Fa-f]{6}$/.test(text) ? text : "transparent"
    }

    component Section: Label {
        Layout.fillWidth: true
        Layout.topMargin: 6
        font.bold: true
        elide: Text.ElideRight
    }
    component Rule: Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: Theme.line
    }
    component FromRow: RowLayout {
        id: fromRow
        required property string port
        required property string from
        Layout.fillWidth: true
        spacing: 6
        Label {
            Layout.fillWidth: true
            elide: Text.ElideRight
            textFormat: Text.PlainText
            color: fromRow.from === "" ? Theme.muted : Theme.text
            //: A wired input in the inspector: "From palette › value"
            text: fromRow.from === "" ? qsTr("Not wired") : qsTr("From %1").arg(fromRow.from)
        }
        IconToolButton {
            objectName: "nodeDisconnect_" + fromRow.port
            visible: fromRow.from !== ""
            iconRole: "remove"
            text: qsTr("Disconnect")
            enabled: root.editable
            onClicked: root.controller.disconnectInput(root.node.id, fromRow.port)
        }
    }
    component ColourSwatch: Button {
        id: swatch
        required property string value
        signal picked(string colour)
        implicitWidth: 28
        implicitHeight: 22
        enabled: root.editable && root.colourPicker !== null
        Accessible.name: qsTr("Choose a colour")
        background: Rectangle {
            color: root.colourOf(swatch.value)
            border.width: 1
            border.color: Theme.line
            radius: 2
        }
        contentItem: Item {}
        onClicked: {
            const start = Color.rgba(0, 0, 0, 1)
            const c = /^#[0-9A-Fa-f]{6}$/.test(value) ? Qt.color(value) : start
            (colourPopover.item as ColourPopover).openFor(swatch, {
                changeColour: c => swatch.picked("#" + [c.r, c.g, c.b].map(v => v.toString(16).padStart(2, "0")).join("").toUpperCase()),
                endDialog: accepted => {}
            }, () => {
                root.colourPicker.opened("nodeEffects")
                return { r: Math.round(c.r * 255), g: Math.round(c.g * 255), b: Math.round(c.b * 255), a: 0 }
            }, { alpha: false, focus: swatch })
        }
    }

    Label {
        objectName: "nodeInspectorEmpty"
        visible: !root.hasNode
        anchors.fill: parent
        anchors.margins: 12
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
        color: Theme.muted
        text: root.controller.selection.length > 1 ? qsTr("%n nodes selected.", "", root.controller.selection.length)
                                                   : qsTr("Select a node to see its fields.")
    }

    Flickable {
        id: scroller
        visible: root.hasNode
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: body.implicitHeight + 24
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: body
            x: 12
            y: 12
            width: scroller.width - 24
            spacing: 6

            Repeater {
                model: root.node.warnings ?? []
                delegate: RowLayout {
                    id: warning
                    required property var modelData
                    required property int index
                    objectName: "nodeInspectorWarning_" + index
                    Layout.fillWidth: true
                    spacing: 6
                    Icon {
                        Layout.alignment: Qt.AlignTop
                        iconRole: "problem"
                        color: Theme.danger
                        accentColor: Theme.danger
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        text: (warning.modelData.port !== "" ? root.label(warning.modelData.port) + " " : "") + warning.modelData.message
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    objectName: "nodeInspectorTitle"
                    text: root.node.title ?? ""
                    font.bold: true
                    font.pixelSize: Qt.application.font.pixelSize + 2
                    textFormat: Text.PlainText
                }
                Label {
                    Layout.fillWidth: true
                    text: root.node.category ?? ""
                    color: Theme.muted
                    elide: Text.ElideRight
                }
            }
            Label {
                objectName: "nodeInspectorSummary"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                textFormat: Text.PlainText
                text: root.node.known === false
                      ? qsTr("“%1” is not a node this HikariSub knows. It may come from a newer version. Update HikariSub, or delete the node; its fields and wires are kept until then.").arg(root.node.type)
                      : root.node.summary ?? ""
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 6
                Label {
                    text: qsTr("Name")
                }
                TextField {
                    id: nameField
                    objectName: "nodeInspectorName"
                    Layout.fillWidth: true
                    text: root.node.id ?? ""
                    enabled: root.editable
                    font: root.monoFont
                    Accessible.name: qsTr("Name")
                    onEditingFinished: {
                        if (text !== root.node.id && !root.controller.renameNode(root.node.id, text))
                            text = Qt.binding(() => root.node.id ?? "")
                    }
                }
            }

            Section {
                visible: (root.node.fields ?? []).length > 0
                text: qsTr("Fields")
            }
            Repeater {
                model: root.node.fields ?? []
                delegate: ColumnLayout {
                    id: field
                    required property var modelData
                    readonly property string kind: modelData.kind
                    readonly property bool wired: modelData.from !== ""
                    readonly property bool code: root.node.lua && modelData.name === "code"
                    objectName: "nodeField_" + modelData.name
                    visible: kind !== "Ports" && kind !== "Graph"
                    Layout.fillWidth: true
                    spacing: 3
                    function set(value) {
                        root.controller.setField(root.node.id, modelData.name, value)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            Layout.preferredWidth: field.kind === "List" || field.code || field.wired ? -1 : 80
                            Layout.fillWidth: field.kind === "List" || field.code || field.wired
                            text: root.label(field.modelData.name)
                            elide: Text.ElideRight
                            ToolTip.visible: fieldHover.hovered && field.modelData.summary !== ""
                            ToolTip.text: field.modelData.summary
                            HoverHandler {
                                id: fieldHover
                            }
                        }
                        Loader {
                            Layout.fillWidth: true
                            active: !field.wired && !field.code && field.kind !== "List"
                            visible: active
                            sourceComponent: field.kind === "Bool" ? boolEditor
                                             : field.kind === "Choice" ? choiceEditor
                                             : field.kind === "Colour" ? colourEditor
                                             : field.kind === "Point" ? pointEditor
                                             : textEditor
                        }
                    }
                    FromRow {
                        visible: field.wired
                        port: field.modelData.name
                        from: field.modelData.from
                    }
                    Label {
                        visible: root.node.random === true && field.modelData.name === "seed"
                        Layout.fillWidth: true
                        color: Theme.muted
                        //: Under a Random node's seed: its numbers also depend on the graph's seed
                        text: qsTr("+ graph seed %1").arg(root.node.graphSeed ?? 0)
                    }
                    Rectangle {
                        visible: field.code
                        Layout.fillWidth: true
                        implicitHeight: Math.max(120, codeArea.implicitHeight + 2)
                        color: Theme.field
                        border.width: 1
                        border.color: codeArea.activeFocus ? Theme.focus : Theme.line
                        TextArea {
                            id: codeArea
                            objectName: "nodeLuaCode"
                            anchors.fill: parent
                            anchors.margins: 1
                            background: null
                            font: root.monoFont
                            wrapMode: TextEdit.NoWrap
                            enabled: root.editable
                            text: field.code ? String(field.modelData.value ?? "") : ""
                            Accessible.name: qsTr("Lua code")
                            onTextChanged: if (activeFocus && text !== String(field.modelData.value ?? "")) field.set(text)
                            onActiveFocusChanged: if (!activeFocus) root.controller.commit()
                        }
                    }
                    ColumnLayout {
                        visible: field.kind === "List" && !field.wired
                        Layout.fillWidth: true
                        spacing: 3
                        Repeater {
                            model: field.kind === "List" && Array.isArray(field.modelData.value) ? field.modelData.value : []
                            delegate: RowLayout {
                                id: item
                                required property var modelData
                                required property int index
                                readonly property bool colour: root.listType() === "colour"
                                Layout.fillWidth: true
                                spacing: 4
                                function replace(value) {
                                    const next = field.modelData.value.slice()
                                    next[index] = value
                                    field.set(next)
                                }
                                ColourSwatch {
                                    visible: item.colour
                                    value: String(item.modelData)
                                    onPicked: c => item.replace(c)
                                }
                                TextField {
                                    objectName: "nodeListValue_" + field.modelData.name + "_" + item.index
                                    Layout.fillWidth: true
                                    font: root.monoFont
                                    enabled: root.editable
                                    text: String(item.modelData)
                                    Accessible.name: qsTr("Value %1").arg(item.index + 1)
                                    onTextEdited: item.replace(root.listType() === "number" && text.trim() !== "" && !isNaN(Number(text)) ? Number(text) : text)
                                    onEditingFinished: root.controller.commit()
                                }
                                IconToolButton {
                                    iconRole: "remove"
                                    text: qsTr("Remove value %1").arg(item.index + 1)
                                    enabled: root.editable
                                    onClicked: {
                                        const next = field.modelData.value.slice()
                                        next.splice(item.index, 1)
                                        field.set(next)
                                    }
                                }
                            }
                        }
                        IconToolButton {
                            objectName: "nodeListAdd_" + field.modelData.name
                            iconRole: "add"
                            text: qsTr("Add value")
                            display: AbstractButton.TextBesideIcon
                            enabled: root.editable
                            onClicked: {
                                const values = Array.isArray(field.modelData.value) ? field.modelData.value.slice() : []
                                const type = root.listType()
                                values.push(type === "colour" ? "#FFFFFF" : type === "number" ? 0 : "")
                                field.set(values)
                            }
                        }
                    }

                    Component {
                        id: textEditor
                        RowLayout {
                            spacing: 4
                            TextField {
                                objectName: "nodeFieldText_" + field.modelData.name
                                Layout.fillWidth: true
                                enabled: root.editable
                                text: field.modelData.value === undefined || field.modelData.value === null ? "" : String(field.modelData.value)
                                inputMethodHints: field.kind === "Text" ? Qt.ImhNone : Qt.ImhFormattedNumbersOnly
                                Accessible.name: root.label(field.modelData.name)
                                onTextEdited: field.set(text)
                                onEditingFinished: root.controller.commit()
                            }
                            Label {
                                visible: field.kind === "Time"
                                text: qsTr("ms")
                                color: Theme.muted
                            }
                            IconToolButton {
                                objectName: "nodeNewSeed"
                                visible: root.node.random === true && field.modelData.name === "seed"
                                iconRole: "refresh"
                                text: qsTr("New seed")
                                enabled: root.editable
                                onClicked: field.set(Math.floor(Math.random() * 1000000))
                            }
                        }
                    }
                    Component {
                        id: boolEditor
                        CheckBox {
                            objectName: "nodeFieldBool_" + field.modelData.name
                            checked: field.modelData.value === true
                            enabled: root.editable
                            Accessible.name: root.label(field.modelData.name)
                            onToggled: field.set(checked)
                        }
                    }
                    Component {
                        id: choiceEditor
                        ComboBox {
                            objectName: "nodeFieldChoice_" + field.modelData.name
                            model: field.modelData.choices
                            currentIndex: field.modelData.choices.indexOf(String(field.modelData.value))
                            enabled: root.editable
                            Accessible.name: root.label(field.modelData.name)
                            onActivated: index => field.set(field.modelData.choices[index])
                        }
                    }
                    Component {
                        id: colourEditor
                        RowLayout {
                            spacing: 4
                            ColourSwatch {
                                value: String(field.modelData.value ?? "")
                                onPicked: c => field.set(c)
                            }
                            TextField {
                                objectName: "nodeFieldColour_" + field.modelData.name
                                Layout.fillWidth: true
                                font: root.monoFont
                                enabled: root.editable
                                text: String(field.modelData.value ?? "")
                                Accessible.name: root.label(field.modelData.name)
                                onTextEdited: field.set(text)
                                onEditingFinished: root.controller.commit()
                            }
                        }
                    }
                    Component {
                        id: pointEditor
                        RowLayout {
                            id: point
                            readonly property var value: Array.isArray(field.modelData.value) ? field.modelData.value : [0, 0]
                            spacing: 4
                            Repeater {
                                model: 2
                                delegate: TextField {
                                    required property int index
                                    Layout.fillWidth: true
                                    enabled: root.editable
                                    text: String(point.value[index] ?? 0)
                                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    Accessible.name: index === 0 ? qsTr("X position") : qsTr("Y position")
                                    onTextEdited: {
                                        const next = point.value.slice()
                                        next[index] = Number(text) || 0
                                        field.set(next)
                                    }
                                    onEditingFinished: root.controller.commit()
                                }
                            }
                        }
                    }
                }
            }

            Section {
                visible: (root.node.inputs ?? []).length > 0
                text: qsTr("Inputs")
            }
            Repeater {
                model: root.node.inputs ?? []
                delegate: ColumnLayout {
                    id: input
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        spacing: 6
                        NodePort {
                            type: input.modelData.type
                        }
                        Label {
                            text: input.modelData.name
                            textFormat: Text.PlainText
                        }
                        Label {
                            text: input.modelData.typeTitle
                            color: Theme.muted
                        }
                    }
                    FromRow {
                        port: input.modelData.name
                        from: input.modelData.from
                    }
                }
            }

            Section {
                visible: (root.node.outputs ?? []).length > 0
                text: qsTr("Outputs")
            }
            Repeater {
                model: root.node.outputs ?? []
                delegate: RowLayout {
                    id: output
                    required property var modelData
                    readonly property var feeds: root.controller.wires.filter(w => w.fromNode === root.node.id && w.fromPort === modelData.name)
                    Layout.fillWidth: true
                    spacing: 6
                    NodePort {
                        type: output.modelData.type
                    }
                    Label {
                        text: output.modelData.name
                        textFormat: Text.PlainText
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: Theme.muted
                        textFormat: Text.PlainText
                        //: An output's wires in the inspector: "to fade › tags, emit › tags"
                        text: output.feeds.length === 0 ? output.modelData.typeTitle
                              : qsTr("to %1").arg(output.feeds.map(w => w.toNode + " › " + w.toPort).join(", "))
                    }
                }
            }

            Rule {
                Layout.topMargin: 6
            }
            Section {
                objectName: "nodeInspectorValuesTitle"
                text: qsTr("On line %1, from the last run").arg(root.node.valuesLine ?? 0)
            }
            Label {
                objectName: "nodeInspectorNoValues"
                visible: (root.node.values ?? []).length === 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                text: root.controller.problems.length > 0 ? qsTr("Not run: the graph has problems.")
                      : root.node.lua && !root.controller.trusted ? qsTr("Not run: the file’s Lua is not trusted.")
                      : qsTr("No values: this node didn’t run on the line.")
            }
            Repeater {
                model: root.node.values ?? []
                delegate: RowLayout {
                    id: value
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.preferredWidth: 80
                        text: value.modelData.port
                        color: Theme.muted
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                    Label {
                        Layout.fillWidth: true
                        text: value.modelData.text
                        font: root.monoFont
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                }
            }

            Section {
                text: qsTr("Timing")
            }
            Label {
                objectName: "nodeInspectorTiming"
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Theme.muted
                //: A node's timing: "Last run 0.42 ms on line 12"; an Emit adds how many lines it wrote
                text: root.node.emitted?.lines !== undefined
                      ? qsTr("Wrote %n lines in %1 on the last run", "", root.node.emitted.lines).arg(root.milliseconds(root.node.emitted.ms))
                      : qsTr("Last run %1 on line %2").arg(root.milliseconds(root.controller.previewMs), root.node.valuesLine ?? 0)
            }
        }
    }

    Loader {
        id: colourPopover
        active: root.colourPicker !== null
        sourceComponent: ColourPopover {
            objectName: "nodeColourPopover"
            picker: root.colourPicker
        }
    }
}
