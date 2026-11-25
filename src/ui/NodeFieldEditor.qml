pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.NodeEffects"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: editor
    required property NodeEffectsController controller
    required property var field
    property string node: ""
    property bool editable: true
    property real labelWidth: 80
    property real trailWidth: 0
    readonly property real naturalTrailWidth: Math.max(rangeLabel.shows ? rangeLabel.implicitWidth : 0,
                                                       newSeed.visible ? newSeed.implicitWidth : 0)
    property font labelFont: Qt.application.font
    property font valueFont: Qt.application.font
    property font monoFont: Qt.application.font
    property bool random: false
    property int graphSeed: 0
    property var openColour: null
    property string refusal: ""
    signal goTo(string node)

    readonly property string name: field.name ?? ""
    readonly property string kind: field.kind ?? ""
    readonly property bool port: kind === "Port"
    readonly property bool wired: (field.from ?? "") !== ""
    readonly property bool mixed: field.mixed === true
    readonly property bool code: kind === "Text" && name === "code" && editor.lua
    property bool lua: false
    property string listType: ""
    readonly property bool scrubbed: kind === "Number" || kind === "Integer" || kind === "Time" || kind === "Alpha"
    readonly property bool slid: kind === "Alpha" || (field.minimum === 0 && field.maximum === 1)
    readonly property string mixedText: qsTr("Mixed")
    //: Read out by screen readers for a Node panel field whose value isn't the node's default
    readonly property string changedText: editor.field.changed === true ? qsTr("Changed from the default") : ""

    objectName: "nodeField_" + name
    spacing: 4
    onNameChanged: refusal = ""

    function label(text) {
        return text.charAt(0).toUpperCase() + text.slice(1).replace(/_/g, " ")
    }
    function apply(value) {
        editor.refusal = editor.controller.fieldRefusal(editor.name, value)
        if (editor.refusal === "")
            editor.controller.setSelectedField(editor.name, value)
        return editor.refusal === ""
    }
    function applyTyped(text) {
        if (text.trim() === "") {
            editor.refusal = ""
            editor.controller.resetSelectedField(editor.name)
        } else {
            editor.apply(text)
        }
    }
    function commit() {
        editor.controller.commit()
    }

    component ValueField: TextField {
        implicitHeight: Theme.controlHeight
        topPadding: 0
        bottomPadding: 0
        enabled: editor.editable
        font: editor.valueFont
        placeholderText: editor.mixed ? editor.mixedText : ""
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        RowLayout {
            Layout.preferredWidth: editor.labelWidth
            Layout.maximumWidth: editor.labelWidth
            Layout.alignment: editor.code || editor.kind === "List" ? Qt.AlignTop : Qt.AlignVCenter
            Layout.topMargin: editor.code || editor.kind === "List" ? 4 : 0
            spacing: 4
            Rectangle {
                objectName: editor.objectName + "Changed"
                implicitWidth: 6
                implicitHeight: 6
                radius: 3
                color: Theme.accent
                opacity: editor.field.changed === true ? 1 : 0
                Accessible.ignored: true
            }
            Label {
                objectName: editor.objectName + "Label"
                Layout.fillWidth: true
                text: editor.label(editor.name)
                font: editor.labelFont
                color: Theme.muted
                elide: Text.ElideRight
                textFormat: Text.PlainText
                ToolTip.visible: labelHover.hovered && (editor.field.summary ?? "") !== ""
                ToolTip.text: editor.field.summary ?? ""
                HoverHandler {
                    id: labelHover
                }
            }
        }

        Loader {
            Layout.fillWidth: true
            sourceComponent: editor.wired || editor.port ? wiredChip
                             : editor.code ? codeEditor
                             : editor.kind === "List" ? listEditor
                             : editor.slid ? sliderEditor
                             : editor.scrubbed ? scrubEditor
                             : editor.kind === "Bool" ? boolEditor
                             : editor.kind === "Choice" && (editor.field.choices ?? []).length <= 4 ? segmentEditor
                             : editor.kind === "Choice" ? choiceEditor
                             : editor.kind === "Colour" ? colourEditor
                             : editor.kind === "Point" ? pointEditor
                             : textEditor
        }

        Item {
            visible: !editor.port && editor.trailWidth > 0
            implicitWidth: editor.trailWidth
            implicitHeight: Theme.controlHeight
            IconToolButton {
                id: newSeed
                objectName: "nodeNewSeed"
                anchors.verticalCenter: parent.verticalCenter
                visible: editor.random && editor.name === "seed" && !editor.wired
                iconRole: "refresh"
                text: qsTr("New seed")
                enabled: editor.editable
                onClicked: editor.apply(Math.floor(Math.random() * 1000000))
            }
            Label {
                id: rangeLabel
                readonly property bool shows: editor.scrubbed && !editor.wired && !editor.slid && text !== ""
                objectName: editor.objectName + "Range"
                anchors.verticalCenter: parent.verticalCenter
                visible: shows && editor.refusal === ""
                text: editor.field.range ?? ""
                font: editor.labelFont
                color: Theme.muted
            }
        }

        Item {
            visible: !editor.port
            implicitWidth: Theme.controlHeight
            implicitHeight: Theme.controlHeight
            IconToolButton {
                objectName: editor.objectName + "Reset"
                anchors.fill: parent
                visible: editor.field.changed === true && !editor.wired
                iconRole: "undo"
                text: qsTr("Reset to the default")
                enabled: editor.editable
                onClicked: {
                    editor.refusal = ""
                    editor.controller.resetSelectedField(editor.name)
                }
            }
        }
    }

    Label {
        objectName: editor.objectName + "Refusal"
        visible: editor.refusal !== ""
        Layout.fillWidth: true
        Layout.leftMargin: editor.labelWidth + 8
        text: editor.refusal
        font: editor.labelFont
        color: Theme.danger
        wrapMode: Text.Wrap
    }
    Label {
        visible: editor.random && editor.name === "seed"
        Layout.fillWidth: true
        Layout.leftMargin: editor.labelWidth + 8
        font: editor.labelFont
        color: Theme.muted
        //: Under a Random node's seed: its numbers also depend on the graph's seed
        text: qsTr("+ graph seed %1").arg(editor.graphSeed)
    }

    Component {
        id: wiredChip
        RowLayout {
            spacing: 4
            Rectangle {
                objectName: editor.objectName + "Chip"
                visible: editor.wired
                implicitHeight: Theme.smallControlHeight
                implicitWidth: chipRow.implicitWidth + 16
                Layout.maximumWidth: parent.width - 2 * Theme.controlHeight - 8
                radius: Theme.radius
                color: Theme.raised
                border.width: 1
                border.color: Theme.line
                RowLayout {
                    id: chipRow
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 4
                    NodePort {
                        visible: (editor.field.type ?? "") !== ""
                        type: editor.field.type ?? "Any"
                    }
                    Label {
                        Layout.fillWidth: true
                        text: editor.field.from ?? ""
                        font: editor.monoFont
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                }
            }
            Label {
                visible: !editor.wired
                text: qsTr("Not wired")
                font: editor.labelFont
                color: Theme.muted
            }
            Item {
                Layout.fillWidth: true
            }
            IconToolButton {
                objectName: "nodeGoTo_" + editor.name
                visible: editor.wired
                iconRole: "go-to-selection"
                //: Selects and centres the node a wired input comes from
                text: qsTr("Go to “%1”").arg(editor.field.fromNode ?? "")
                onClicked: editor.goTo(editor.field.fromNode)
            }
            IconToolButton {
                objectName: "nodeDisconnect_" + editor.name
                visible: editor.wired
                iconRole: "remove"
                text: qsTr("Disconnect")
                enabled: editor.editable
                onClicked: editor.controller.disconnectInput(editor.node, editor.name)
            }
        }
    }

    Component {
        id: scrubEditor
        ScrubField {
            objectName: "nodeFieldValue_" + editor.name
            enabled: editor.editable
            value: Number(editor.field.value ?? 0)
            placeholder: editor.mixed ? editor.mixedText
                         : editor.field.value === undefined || editor.field.value === null ? qsTr("Default") : ""
            unit: editor.field.unit ?? ""
            minimum: editor.field.minimum ?? undefined
            maximum: editor.field.maximum ?? undefined
            step: editor.kind === "Time" ? 10 : 1
            fineStep: editor.kind === "Number" ? 0.1 : 1
            decimals: editor.kind === "Integer" || editor.kind === "Alpha" ? 0 : 3
            refused: editor.refusal !== ""
            valueFont: editor.valueFont
            Accessible.name: editor.label(editor.name)
            stateText: editor.changedText
            onEdited: value => editor.apply(value)
            onTyped: text => editor.applyTyped(text)
            onFinished: editor.commit()
        }
    }
    Component {
        id: sliderEditor
        RowLayout {
            spacing: 8
            Slider {
                objectName: "nodeFieldSlider_" + editor.name
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                from: editor.field.minimum ?? 0
                to: editor.field.maximum ?? 1
                stepSize: editor.kind === "Alpha" ? 1 : 0.01
                value: Number(editor.field.value ?? 0)
                enabled: editor.editable
                Accessible.name: editor.label(editor.name)
                Accessible.description: editor.changedText
                onMoved: editor.apply(Number(value.toFixed(editor.kind === "Alpha" ? 0 : 2)))
                onPressedChanged: if (!pressed) editor.commit()
            }
            ScrubField {
                objectName: "nodeFieldValue_" + editor.name
                implicitWidth: 72
                enabled: editor.editable
                value: Number(editor.field.value ?? 0)
                placeholder: editor.mixed ? editor.mixedText : ""
                minimum: editor.field.minimum ?? undefined
                maximum: editor.field.maximum ?? undefined
                step: editor.kind === "Alpha" ? 1 : 0.01
                fineStep: editor.kind === "Alpha" ? 1 : 0.001
                decimals: editor.kind === "Alpha" ? 0 : 3
                refused: editor.refusal !== ""
                valueFont: editor.valueFont
                Accessible.name: editor.label(editor.name)
                stateText: editor.changedText
                onEdited: value => editor.apply(value)
                onTyped: text => editor.applyTyped(text)
                onFinished: editor.commit()
            }
        }
    }
    Component {
        id: boolEditor
        CheckBox {
            objectName: "nodeFieldBool_" + editor.name
            implicitHeight: Theme.controlHeight
            topPadding: 0
            bottomPadding: 0
            font: editor.labelFont
            checkState: editor.mixed ? Qt.PartiallyChecked : editor.field.value === true ? Qt.Checked : Qt.Unchecked
            enabled: editor.editable
            Accessible.name: editor.label(editor.name)
            Accessible.description: editor.changedText
            onToggled: editor.apply(checkState !== Qt.Unchecked)
        }
    }
    Component {
        id: segmentEditor
        SegmentedButtons {
            objectName: "nodeFieldChoice_" + editor.name
            enabled: editor.editable
            choices: (editor.field.choices ?? []).map(c => ({ text: editor.label(c), name: "nodeFieldChoice_" + editor.name + "_" + c }))
            currentIndex: editor.mixed ? -1 : (editor.field.choices ?? []).indexOf(String(editor.field.value))
            Accessible.name: editor.label(editor.name)
            Accessible.description: editor.changedText
            onActivated: index => editor.apply(editor.field.choices[index])
        }
    }
    Component {
        id: choiceEditor
        ComboBox {
            objectName: "nodeFieldChoice_" + editor.name
            implicitHeight: Theme.controlHeight
            topPadding: 0
            bottomPadding: 0
            font: editor.labelFont
            model: editor.field.choices
            currentIndex: editor.mixed ? -1 : (editor.field.choices ?? []).indexOf(String(editor.field.value))
            displayText: editor.mixed ? editor.mixedText : currentText
            enabled: editor.editable
            Accessible.name: editor.label(editor.name)
            Accessible.description: editor.changedText
            onActivated: index => editor.apply(editor.field.choices[index])
        }
    }
    Component {
        id: colourEditor
        RowLayout {
            spacing: 4
            ColourButton {
                value: editor.mixed ? "" : String(editor.field.value ?? "")
                onPicked: colour => editor.apply(colour)
            }
            ValueField {
                objectName: "nodeFieldColour_" + editor.name
                Layout.fillWidth: true
                font: editor.monoFont
                text: editor.mixed ? "" : String(editor.field.value ?? "")
                Accessible.name: editor.label(editor.name)
                Accessible.description: editor.changedText
                onEditingFinished: {
                    if (text !== String(editor.field.value ?? "") && editor.apply(text))
                        editor.commit()
                }
            }
        }
    }
    component ColourButton: Button {
        id: swatch
        required property string value
        signal picked(string colour)
        implicitWidth: Theme.controlHeight
        implicitHeight: Theme.controlHeight
        enabled: editor.editable && editor.openColour !== null
        Accessible.name: qsTr("Choose a colour")
        background: Rectangle {
            radius: Theme.radius
            color: /^#[0-9A-Fa-f]{6}$/.test(swatch.value) ? swatch.value : "transparent"
            border.width: 1
            border.color: Theme.line
        }
        contentItem: Item {}
        onClicked: editor.openColour(swatch, swatch.value, colour => swatch.picked(colour))
    }
    Component {
        id: pointEditor
        RowLayout {
            id: point
            readonly property var value: Array.isArray(editor.field.value) ? editor.field.value : [0, 0]
            spacing: 4
            Repeater {
                model: 2
                delegate: ScrubField {
                    required property int index
                    objectName: "nodeFieldValue_" + editor.name + (index === 0 ? "X" : "Y")
                    Layout.fillWidth: true
                    enabled: editor.editable
                    value: Number(point.value[index] ?? 0)
                    placeholder: editor.mixed ? editor.mixedText : ""
                    //: The axis after a point's coordinate in the Node panel
                    unit: index === 0 ? qsTr("x") : qsTr("y")
                    decimals: 3
                    refused: editor.refusal !== ""
                    valueFont: editor.valueFont
                    Accessible.name: index === 0 ? qsTr("X position") : qsTr("Y position")
                    stateText: editor.changedText
                    function withAxis(v) {
                        const next = point.value.slice()
                        next[index] = v
                        return next
                    }
                    onEdited: value => editor.apply(withAxis(value))
                    onTyped: text => editor.apply(withAxis(text.trim() !== "" && !isNaN(Number(text)) ? Number(text) : text))
                    onFinished: editor.commit()
                }
            }
            IconToolButton {
                objectName: "nodePick_" + editor.name
                iconRole: "tool-crosshair"
                text: qsTr("Pick on the video")
                checkable: true
                checked: editor.controller.pickingPoint === editor.name
                enabled: editor.editable
                onClicked: checked ? editor.controller.pickPoint(editor.name) : editor.controller.cancelPick()
            }
        }
    }
    Component {
        id: textEditor
        RowLayout {
            spacing: 4
            ValueField {
                objectName: "nodeFieldText_" + editor.name
                Layout.fillWidth: true
                text: editor.mixed || editor.field.value === undefined || editor.field.value === null ? "" : String(editor.field.value)
                Accessible.name: editor.label(editor.name)
                Accessible.description: editor.changedText
                onTextEdited: editor.apply(text)
                onEditingFinished: editor.commit()
            }
        }
    }
    Component {
        id: codeEditor
        Rectangle {
            implicitHeight: Math.max(120, codeArea.implicitHeight + 2)
            radius: Theme.radius
            color: Theme.field
            border.width: 1
            border.color: !codeArea.enabled ? Theme.disabled
                          : codeArea.activeFocus ? Theme.accent
                          : codeHover.hovered ? Theme.text
                          : Theme.control
            HoverHandler {
                id: codeHover
            }
            FocusRing {
                control: codeArea
            }
            TextArea {
                id: codeArea
                objectName: "nodeLuaCode"
                anchors.fill: parent
                anchors.margins: 1
                background: null
                font: editor.monoFont
                wrapMode: TextEdit.NoWrap
                enabled: editor.editable
                text: String(editor.field.value ?? "")
                Accessible.name: qsTr("Lua code")
                onTextChanged: if (activeFocus && text !== String(editor.field.value ?? "")) editor.apply(text)
                onActiveFocusChanged: if (!activeFocus) editor.commit()
            }
        }
    }
    Component {
        id: listEditor
        ColumnLayout {
            id: list
            readonly property var values: Array.isArray(editor.field.value) ? editor.field.value : []
            readonly property string type: editor.listType
            spacing: 4
            function replace(index, value) {
                const next = list.values.slice()
                next[index] = value
                editor.apply(next)
            }
            Repeater {
                model: list.values.length
                delegate: RowLayout {
                    id: item
                    required property int index
                    readonly property var value: list.values[index]
                    Layout.fillWidth: true
                    spacing: 4
                    ColourButton {
                        visible: list.type === "colour"
                        value: String(item.value)
                        onPicked: colour => list.replace(item.index, colour)
                    }
                    ValueField {
                        objectName: "nodeListValue_" + editor.name + "_" + item.index
                        Layout.fillWidth: true
                        font: editor.monoFont
                        text: String(item.value)
                        Accessible.name: qsTr("Value %1").arg(item.index + 1)
                        onTextEdited: list.replace(item.index, list.type === "number" && text.trim() !== "" && !isNaN(Number(text)) ? Number(text) : text)
                        onEditingFinished: editor.commit()
                    }
                    IconToolButton {
                        iconRole: "remove"
                        text: qsTr("Remove value %1").arg(item.index + 1)
                        enabled: editor.editable
                        onClicked: {
                            const next = list.values.slice()
                            next.splice(item.index, 1)
                            editor.apply(next)
                        }
                    }
                }
            }
            IconTextButton {
                objectName: "nodeListAdd_" + editor.name
                iconRole: "add"
                text: qsTr("Add value")
                flat: true
                enabled: editor.editable
                onClicked: {
                    const next = list.values.slice()
                    next.push(list.type === "colour" ? "#FFFFFF" : list.type === "number" ? 0 : "")
                    editor.apply(next)
                }
            }
        }
    }
}
