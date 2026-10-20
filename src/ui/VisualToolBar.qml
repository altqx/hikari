pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// VT1 (#269): the visual tool inspector bar below the canvas, in the Video
// panel and in fullscreen's tool row. It replaces T1-T6's options row,
// values row and the readout beside the video times. The active tool's icon
// and name lead; then its options (VisualToolOption: legacy VideoToolbar's
// second row) and its values (VisualValueField), each group after its
// short label (Scale's Rectangle and Tags, Move's Start and End, the
// rotations' Origin) and a thin rule; at the trailing end the batch picker
// (accepted on #55): the Lines a batch tool edits are picked from the Grid's
// selection and kept, whatever the active Line or a later selection; with
// none picked a tool edits the active Line. The picker is checked while
// Lines are picked, their number beside it, and Clear shows then.
//
// The bar is there for every tool, the crosshair included (its position
// under the pointer and Copy video position), so the video does not resize
// as the tool changes or the pointer moves. On a narrow panel the groups
// wrap onto another line (a group stays whole; the rule before the first
// group of a line is not drawn) and the picker keeps its slot; the bar keeps
// the height of the family that needs the most lines at its width, laid out
// unseen, so a tool switch does not resize the video either.
Control {
    id: bar
    objectName: "visualToolBar"
    required property VisualToolsController tools
    // Fullscreen's tool row shows the families' buttons before the bar.
    property bool showsName: true
    readonly property int family: tools.activeFamily
    readonly property var familyInfo: tools.families[family] ?? ({})
    readonly property bool batchTool: family > 0 && (familyInfo.available ?? false)
    padding: 0
    topPadding: 3
    bottomPadding: 3
    leftPadding: 6
    rightPadding: 4

    // The group labels the families give (a marked table: the tools' texts
    // are legacy's, the groups the bar's).
    readonly property var groupNames: ({
        "Rectangle": qsTr("Rectangle"),
        "Tags": qsTr("Tags"),
        "Points": qsTr("Points"),
        "Shape": qsTr("Shape"),
        "Move": qsTr("Move"),
        "Tag": qsTr("Tag"),
        "Change": qsTr("Change"),
        "Start": qsTr("Start"),
        "End": qsTr("End"),
        "Origin": qsTr("Origin")
    })
    function groupName(group) {
        return bar.groupNames[group] ?? group
    }
    // Consecutive items of one group, in the tool's order.
    function groupsOf(list, kind, measuring) {
        const out = []
        for (const item of list) {
            const g = item.group ?? ""
            if (out.length === 0 || out[out.length - 1].group !== g)
                out.push({ kind: kind, group: g, items: [], measuring: measuring })
            out[out.length - 1].items.push(item)
        }
        return out
    }
    // A family's groups for its unseen copy (its values carried whole).
    function measuredSegments(layout) {
        const values = (layout.values ?? []).map(v => ({ name: v.name, group: v.group, value: v }))
        return groupsOf(layout.options ?? [], "options", true).concat(groupsOf(values, "values", true))
    }
    // The values' layout changes only with the tool (or a value coming or
    // going), not with every sample, so the fields are not rebuilt (and a
    // drag on a label goes on) while the numbers change.
    readonly property string valueShape: {
        const parts = []
        for (const v of tools.values)
            parts.push(v.name + "\u0001" + v.group + "\u0001" + v.label + "\u0001" + v.unit + "\u0001" + v.editable)
        return parts.join("\u0002")
    }
    readonly property var valueGroups: {
        const shape = []
        if (valueShape.length > 0)
            for (const part of valueShape.split("\u0002")) {
                const f = part.split("\u0001")
                shape.push({ name: f[0], group: f[1] })
            }
        return groupsOf(shape, "values", false)
    }
    function valueNamed(name) {
        for (const v of tools.values)
            if (v.name === name)
                return v
        return { name: name, label: "", text: "", editable: false, group: "", unit: "", step: 1 }
    }
    // The options' layout likewise: a toggle, a greyed option or the chosen
    // entry changes no item (each reads its state by name).
    readonly property string optionShape: JSON.stringify(tools.options.map(o => ({
        name: o.name, kind: o.kind, iconRole: o.iconRole, tooltip: o.tooltip, group: o.group, listEnds: o.listEnds
    })))
    function optionNamed(name, fallback) {
        for (const o of tools.options)
            if (o.name === name)
                return o
        return fallback
    }
    readonly property var segments: groupsOf(JSON.parse(optionShape), "options", false).concat(valueGroups)
    // The picker's slot, which a batch tool's groups give up.
    readonly property real pickerWidth: pick.implicitWidth + 4
                                        + (tools.batchCount > 0 ? picked.implicitWidth + clear.implicitWidth + 8 : 0)
    function batchFamily(f) {
        return f > 0 && (tools.families[f]?.available ?? false)
    }
    // Every family's layout without what does not size it (checked,
    // enabled, the chosen entry, the values' numbers): the unseen copies are
    // made again only when a family's groups change, not on each toggle.
    readonly property string layoutShape: JSON.stringify(tools.familyLayouts.map(l => l.options === undefined ? {} : {
        options: l.options.map(o => ({ name: o.name, kind: o.kind, iconRole: o.iconRole, tooltip: o.tooltip,
                                       group: o.group, choices: o.choices, listEnds: o.listEnds,
                                       checked: false, enabled: false, index: 0 })),
        values: l.values.map(v => ({ name: v.name, label: v.label, text: "", editable: v.editable,
                                     group: v.group, unit: v.unit, step: v.step }))
    }))
    readonly property var measuredLayouts: JSON.parse(layoutShape)
    // The height the panel can spare above its minimum (which counts only
    // the tool's own lines): the bar holds the tallest family's height as
    // far as that goes, so in a small window the panel's minimum still fits
    // the screen and only then does a tool switch resize the video.
    property real room: Infinity
    // What the bar holds below its own lines.
    readonly property real held: Math.max(0, Math.min(reservedHeight - flow.implicitHeight, room))
    // The tallest family's groups at the width each would have.
    readonly property real reservedHeight: {
        let h = 0
        for (let i = 0; i < measures.count; ++i) {
            const m = measures.itemAt(i)
            if (m)
                h = Math.max(h, m.implicitHeight)
        }
        return h
    }

    // The tool, as the rail names it.
    component ToolName: RowLayout {
        property int family
        property string name
        height: 26
        spacing: 6
        Icon {
            Layout.alignment: Qt.AlignVCenter
            // In application::visual::Family's order (on one line: icon_tests reads the roles from it).
            iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags"][family] ?? ""
        }
        Label {
            Layout.alignment: Qt.AlignVCenter
            text: name
            font.bold: true
        }
    }

    // A group: the rule, its label, its options or values.
    Component {
        id: segmentDelegate
        RowLayout {
            id: segment
            required property var modelData
            required property int index
            objectName: modelData.measuring ? ""
                        : modelData.kind === "options" ? "visualOptionGroup" + index : "visualValueGroup" + index
            height: 26
            spacing: 3
            // The rule before a group; none at the start of a line.
            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 18
                Layout.rightMargin: 3
                Layout.alignment: Qt.AlignVCenter
                color: Theme.line
                opacity: segment.x > 0 ? 1 : 0
                visible: bar.showsName || segment.index > 0
            }
            Label {
                visible: segment.modelData.group.length > 0
                Layout.alignment: Qt.AlignVCenter
                Layout.rightMargin: 3
                text: bar.groupName(segment.modelData.group)
                color: Theme.muted
            }
            Repeater {
                model: segment.modelData.kind === "options" ? segment.modelData.items : []
                delegate: VisualToolOption {
                    tools: bar.tools
                    measuring: segment.modelData.measuring
                    live: measuring ? modelData : bar.optionNamed(modelData.name, modelData)
                }
            }
            Repeater {
                model: segment.modelData.kind === "values" ? segment.modelData.items : []
                delegate: VisualValueField {
                    required property var modelData
                    Layout.alignment: Qt.AlignVCenter
                    tools: bar.tools
                    measuring: segment.modelData.measuring
                    name: modelData.name
                    value: segment.modelData.measuring ? modelData.value : bar.valueNamed(modelData.name)
                    accessibleName: [bar.familyInfo.name ?? "", bar.groupName(segment.modelData.group),
                                     value.label].filter(s => s.length > 0).join(" ")
                }
            }
        }
    }

    contentItem: RowLayout {
        spacing: 4
        Flow {
            id: flow
            objectName: "visualToolGroups"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
            Layout.preferredHeight: implicitHeight + bar.held
            Layout.alignment: Qt.AlignTop
            spacing: 6
            ToolName {
                objectName: "visualToolName"
                visible: bar.showsName
                family: bar.family
                name: bar.familyInfo.name ?? ""
            }
            Repeater {
                id: groups
                model: bar.segments
                delegate: segmentDelegate
            }
        }
        // The batch picker: checked while Lines are picked, their number
        // beside it; Clear only while there is a batch.
        IconToolButton {
            id: pick
            objectName: "visualPickBatch"
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
            visible: bar.batchTool
            iconRole: "pick-lines"
            text: qsTr("Pick selected lines")
            // English has no plural catalog: one says "1 line".
            tip: bar.tools.batchCount === 1
                 ? qsTr("Pick selected lines (1 line picked: the tools edit it, not the active line)")
                 : bar.tools.batchCount > 1
                 ? qsTr("Pick selected lines (%n lines picked: the tools edit them, not the active line)", "",
                        bar.tools.batchCount)
                 : qsTr("Pick selected lines (the tools edit them, not the active line)")
            checked: bar.tools.batchCount > 0
            Accessible.description: bar.tools.batchCount === 1 ? qsTr("1 line picked")
                                    : bar.tools.batchCount > 1 ? qsTr("%n lines picked", "", bar.tools.batchCount) : ""
            focusPolicy: Qt.TabFocus
            enabled: bar.tools.railEnabled
            onClicked: bar.tools.pickBatch()
        }
        Label {
            id: picked
            objectName: "visualPickedCount"
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 5
            visible: bar.batchTool && bar.tools.batchCount > 0
            text: bar.tools.batchCount === 1 ? qsTr("1 line") : qsTr("%n lines", "", bar.tools.batchCount)
            font.features: { "tnum": 1 }
        }
        IconToolButton {
            id: clear
            objectName: "visualClearBatch"
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
            visible: bar.batchTool && bar.tools.batchCount > 0
            iconRole: "clear"
            text: qsTr("Clear picked lines")
            focusPolicy: Qt.TabFocus
            onClicked: bar.tools.clearBatch()
        }
    }
    background: Rectangle {
        color: Theme.panel
    }
    // Every family's groups, unseen, at the width the bar gives each (less
    // the picker's slot for a batch tool): reservedHeight is the tallest.
    Item {
        objectName: "visualToolMeasure"
        x: bar.leftPadding
        width: bar.availableWidth
        height: 0
        clip: true
        opacity: 0
        enabled: false
        Accessible.ignored: true
        Repeater {
            id: measures
            model: bar.measuredLayouts
            delegate: Flow {
                required property var modelData
                required property int index
                width: Math.max(0, parent.width - (bar.batchFamily(index) ? bar.pickerWidth : 0))
                spacing: 6
                ToolName {
                    visible: bar.showsName && (modelData.options !== undefined)
                    family: index
                    name: bar.tools.families[index]?.name ?? ""
                }
                Repeater {
                    model: modelData.options !== undefined ? bar.measuredSegments(modelData) : []
                    delegate: segmentDelegate
                }
            }
        }
    }
    VisualToolDialogs {
        tools: bar.tools
    }
}
