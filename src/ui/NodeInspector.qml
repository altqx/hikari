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
    signal goToNode(string node)
    padding: 0
    implicitWidth: 300
    implicitHeight: 480
    readonly property size minimumSize: Qt.size(240, 200)
    readonly property var node: controller.inspector
    readonly property var form: controller.form
    readonly property int count: form.count ?? 0
    readonly property bool one: count === 1 && node.id !== undefined
    readonly property bool editable: controller.editable && !controller.newerFormat
    readonly property var fields: form.fields ?? []
    readonly property var wired: one ? form.wired ?? [] : []

    readonly property font labelFont: TypeRoles.sized(fontSource.font, 12, Font.Normal)
    readonly property font bodyFont: TypeRoles.sized(fontSource.font, 13, Font.Normal)
    readonly property font sectionFont: TypeRoles.sized(fontSource.font, 13, Font.DemiBold)
    readonly property font valueFont: {
        const f = TypeRoles.sized(fontSource.font, 12, Font.Normal)
        f.features = { "tnum": 1 }
        return f
    }
    readonly property font monoFont: Qt.font({ family: "monospace", pixelSize: labelFont.pixelSize })
    readonly property real labelWidth: {
        let widest = 0
        for (const f of root.fields.concat(root.wired))
            widest = Math.max(widest, labelMetrics.advanceWidth(root.label(f.name)))
        return Math.ceil(Math.min(widest, 110)) + 10
    }

    function label(name) {
        return name.charAt(0).toUpperCase() + name.slice(1).replace(/_/g, " ")
    }
    function milliseconds(ms) {
        //: A time a node effect took: %1 milliseconds
        return qsTr("%1 ms").arg(ms < 10 ? ms.toFixed(2) : Math.round(ms))
    }
    function listType() {
        const t = root.fields.find(f => f.name === "type")
        return t ? String(t.value) : ""
    }
    function openColour(item, value, picked) {
        const c = /^#[0-9A-Fa-f]{6}$/.test(value) ? Qt.color(value) : Color.rgba(0, 0, 0, 1)
        ;(colourPopover.item as ColourPopover).openFor(item, {
            changeColour: c => picked("#" + [c.r, c.g, c.b].map(v => v.toString(16).padStart(2, "0")).join("").toUpperCase()),
            endDialog: accepted => {}
        }, () => {
            root.colourPicker.opened("nodeEffects")
            return { r: Math.round(c.r * 255), g: Math.round(c.g * 255), b: Math.round(c.b * 255), a: 0 }
        }, { alpha: false, focus: item })
    }
    function goTo(node) {
        root.controller.selection = [node]
        root.goToNode(node)
    }

    Label { id: fontSource; visible: false }
    FontMetrics {
        id: labelMetrics
        font: root.labelFont
    }

    component Section: RowLayout {
        id: section
        property alias text: heading.text
        property alias detail: detailLabel.text
        Layout.fillWidth: true
        Layout.topMargin: 8
        spacing: 8
        Label {
            id: heading
            font: root.sectionFont
            textFormat: Text.PlainText
        }
        Label {
            id: detailLabel
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            font: root.labelFont
            color: Theme.muted
            elide: Text.ElideRight
        }
    }

    ColumnLayout {
        objectName: "nodeInspectorEmpty"
        visible: root.count === 0
        anchors.fill: parent
        anchors.margins: 12
        spacing: 4
        Label {
            objectName: "nodeInspectorGraphName"
            visible: root.controller.hasGraph
            Layout.fillWidth: true
            text: root.controller.name
            font: root.sectionFont
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }
        Label {
            visible: root.controller.hasGraph
            Layout.fillWidth: true
            text: qsTr("%n nodes", "", root.controller.nodes.length)
            font: root.labelFont
            color: Theme.muted
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            wrapMode: Text.Wrap
            font: root.bodyFont
            color: Theme.muted
            text: qsTr("Select a node to see its fields.")
        }
        Item {
            Layout.fillHeight: true
        }
    }

    Flickable {
        id: scroller
        visible: root.count > 0
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

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Label {
                    objectName: "nodeInspectorTitle"
                    Layout.fillWidth: !root.one
                    text: root.one ? root.node.title ?? ""
                          : root.form.oneType ? qsTr("%n %1 nodes", "", root.count).arg(root.form.title)
                          : qsTr("%n nodes", "", root.count)
                    font: root.sectionFont
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                Label {
                    visible: root.one
                    Layout.fillWidth: true
                    text: root.node.category ?? ""
                    font: root.labelFont
                    color: Theme.muted
                    elide: Text.ElideRight
                }
                TextField {
                    id: nameField
                    objectName: "nodeInspectorName"
                    visible: root.one
                    property string refusal: ""
                    implicitHeight: Theme.controlHeight
                    topPadding: 0
                    bottomPadding: 0
                    Layout.preferredWidth: Math.max(72, Math.min(140, nameMetrics.advanceWidth + leftPadding + rightPadding + 8))
                    text: root.node.id ?? ""
                    enabled: root.editable
                    font: root.monoFont
                    Accessible.name: qsTr("Name")
                    TextMetrics {
                        id: nameMetrics
                        font: root.monoFont
                        text: nameField.text
                    }
                    background: Rectangle {
                        radius: Theme.radius
                        color: Theme.field
                        border.width: 1
                        border.color: nameField.refusal !== "" ? Theme.danger : nameField.activeFocus ? Theme.focus : Theme.line
                    }
                    onTextEdited: refusal = ""
                    onEditingFinished: {
                        if (text === root.node.id)
                            return
                        refusal = root.controller.nodeNameRefusal(root.node.id, text)
                        if (refusal === "" && !root.controller.renameNode(root.node.id, text))
                            text = Qt.binding(() => root.node.id ?? "")
                    }
                    Connections {
                        target: root.controller
                        function onSelectionChanged() {
                            root.controller.cancelPick()
                            nameField.refusal = ""
                            nameField.text = Qt.binding(() => root.node.id ?? "")
                        }
                    }
                }
            }
            Label {
                objectName: "nodeInspectorNameRefusal"
                visible: root.one && nameField.refusal !== ""
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.Wrap
                text: nameField.refusal
                font: root.labelFont
                color: Theme.danger
            }
            Label {
                objectName: "nodeInspectorIds"
                visible: !root.one
                Layout.fillWidth: true
                text: (root.form.ids ?? []).join(", ")
                font: root.monoFont
                color: Theme.muted
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Label {
                objectName: "nodeInspectorSummary"
                visible: root.one
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font: root.bodyFont
                color: Theme.muted
                textFormat: Text.PlainText
                text: root.node.known === false
                      ? qsTr("“%1” is not a node this HikariSub knows. It may come from a newer version. Update HikariSub, or delete the node; its fields and wires are kept until then.").arg(root.node.type)
                      : root.node.summary ?? ""
            }

            Section {
                objectName: "nodeInspectorFields"
                visible: root.fields.length > 0
                text: qsTr("Fields")
                detail: qsTr("Drag a value to change it · Shift for fine steps")
            }
            Repeater {
                model: root.fields.length
                delegate: NodeFieldEditor {
                    required property int index
                    Layout.fillWidth: true
                    controller: root.controller
                    field: root.fields[index] ?? ({})
                    node: root.one ? root.node.id : ""
                    editable: root.editable
                    labelWidth: root.labelWidth
                    labelFont: root.labelFont
                    valueFont: root.valueFont
                    monoFont: root.monoFont
                    lua: root.one && root.node.lua === true
                    random: root.one && root.node.random === true
                    graphSeed: root.node.graphSeed ?? 0
                    listType: root.listType()
                    openColour: root.colourPicker !== null ? root.openColour : null
                    onGoTo: node => root.goTo(node)
                }
            }

            Section {
                objectName: "nodeInspectorWired"
                visible: root.wired.length > 0
                text: qsTr("Wired")
            }
            Repeater {
                model: root.wired.length
                delegate: NodeFieldEditor {
                    required property int index
                    Layout.fillWidth: true
                    controller: root.controller
                    field: Object.assign({ kind: "Port" }, root.wired[index] ?? {})
                    node: root.node.id ?? ""
                    editable: root.editable
                    labelWidth: root.labelWidth
                    labelFont: root.labelFont
                    valueFont: root.valueFont
                    monoFont: root.monoFont
                    onGoTo: node => root.goTo(node)
                }
            }

            Section {
                objectName: "nodeInspectorValuesTitle"
                visible: root.one
                //: The Node panel's heading for the values from the last preview run on a line
                text: qsTr("On line %1").arg(root.node.valuesLine ?? 0)
                //: A node's timing beside "On line 46": "from the last run · 0.3 ms a line"; an Emit says how many lines it wrote
                detail: root.node.emitted?.lines !== undefined
                        ? qsTr("Wrote %n lines in %1 on the last run", "", root.node.emitted.lines).arg(root.milliseconds(root.node.emitted.ms))
                        : (root.node.values ?? []).length > 0 ? qsTr("from the last run · %1 a line").arg(root.milliseconds(root.controller.previewMs)) : ""
            }
            Label {
                objectName: "nodeInspectorNoValues"
                visible: root.one && (root.node.values ?? []).length === 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font: root.labelFont
                color: Theme.muted
                text: root.controller.problems.length > 0 ? qsTr("Not run: the graph has problems.")
                      : root.node.lua && !root.controller.trusted ? qsTr("Not run: the file’s Lua is not trusted.")
                      : qsTr("No values: this node didn’t run on the line.")
            }
            Repeater {
                model: root.one ? root.node.values ?? [] : []
                delegate: RowLayout {
                    id: value
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8
                    Label {
                        Layout.preferredWidth: root.labelWidth
                        Layout.alignment: Qt.AlignTop
                        text: value.modelData.port
                        font: root.labelFont
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
                visible: root.one && (root.node.outputs ?? []).length > 0
                text: qsTr("Outputs")
            }
            Repeater {
                model: root.one ? root.node.outputs ?? [] : []
                delegate: RowLayout {
                    id: output
                    required property var modelData
                    readonly property var feeds: root.controller.wires.filter(w => w.fromNode === root.node.id && w.fromPort === modelData.name)
                    Layout.fillWidth: true
                    spacing: 8
                    RowLayout {
                        Layout.preferredWidth: root.labelWidth
                        spacing: 4
                        NodePort {
                            type: output.modelData.type
                        }
                        Label {
                            Layout.fillWidth: true
                            text: output.modelData.name
                            font: root.labelFont
                            color: Theme.muted
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font: output.feeds.length === 0 ? root.labelFont : root.monoFont
                        color: output.feeds.length === 0 ? Theme.muted : Theme.text
                        textFormat: Text.PlainText
                        //: An output's wires in the Node panel: "→ fade › tags, emit › tags"
                        text: output.feeds.length === 0 ? output.modelData.typeTitle
                              : qsTr("→ %1").arg(output.feeds.map(w => w.toNode + " › " + w.toPort).join(", "))
                    }
                }
            }

            Section {
                objectName: "nodeInspectorSelection"
                visible: root.count > 1
                text: qsTr("Selection")
            }
            Flow {
                visible: root.count > 1
                Layout.fillWidth: true
                spacing: 4
                IconTextButton {
                    objectName: "nodeSelectionGroup"
                    flat: true
                    implicitHeight: Theme.controlHeight
                    text: qsTr("Group")
                    enabled: root.editable
                    onClicked: root.controller.groupSelection()
                }
                IconTextButton {
                    objectName: "nodeSelectionDelete"
                    flat: true
                    implicitHeight: Theme.controlHeight
                    iconRole: "delete"
                    text: qsTr("Delete")
                    enabled: root.editable
                    onClicked: root.controller.removeSelected()
                }
            }
            Label {
                visible: root.count > 1 && root.form.oneType === true
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font: root.labelFont
                color: Theme.muted
                text: qsTr("Values from the last run show for one node at a time.")
            }

            Section {
                objectName: "nodeInspectorProblems"
                visible: root.one && (root.node.warnings ?? []).length > 0
                text: qsTr("Problems")
            }
            Repeater {
                model: root.one ? root.node.warnings ?? [] : []
                delegate: RowLayout {
                    id: warning
                    required property var modelData
                    required property int index
                    objectName: "nodeInspectorWarning_" + index
                    Layout.fillWidth: true
                    spacing: 6
                    Icon {
                        Layout.alignment: Qt.AlignTop
                        iconRole: "notice-warning"
                        color: Theme.danger
                        accentColor: Theme.danger
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            visible: warning.modelData.port !== ""
                            Layout.fillWidth: true
                            text: root.label(warning.modelData.port)
                            font: root.sectionFont
                            textFormat: Text.PlainText
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font: root.labelFont
                            color: Theme.muted
                            textFormat: Text.PlainText
                            text: warning.modelData.message
                        }
                    }
                }
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
