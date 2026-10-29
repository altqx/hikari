pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// VT1 (#269): the visual tool inspector bar below the canvas, in the Video
// panel and in fullscreen's bar (FS1's inspector row, while a tool other
// than the crosshair is on). It replaces T1-T6's options row and values
// row. The active tool's options (VisualToolOption: legacy VideoToolbar's
// second row) and its values (VisualValueField), each group after its
// short label (Scale's Rectangle and Tags, Move's Start and End, the
// rotations' Origin) and a thin rule; at the trailing end the batch picker
// (accepted on #55): the Lines a batch tool edits are picked from the Grid's
// selection and kept, whatever the active Line or a later selection; with
// none picked a tool edits the active Line. The picker is checked while
// Lines are picked, their number beside it, and Clear shows then.
//
// VB1 (#481): the bar is one line. The groups that do not fit its width
// fold, whole and last first, under More options at its trailing end (the
// Line editor strip's pattern), which opens the popup kit's popover with
// them, one group a line; a wider bar unfolds them. So the video keeps its
// size across every tool with options. The tool's name is the rail's (its
// checked button, the name on hover), not repeated here. The crosshair has
// no bar: its position and Copy video position are on the video times line
// (VisualPointerReadout).
//
// HY1 (#271): Hydra's change option comes after its values (Tag, the
// values, Change), and its picker slot says which Lines a value changes
// ("Changes 3 lines", "Changes the active line"); they are outlined on the
// video.
Control {
    id: bar
    objectName: "visualToolBar"
    required property VisualToolsController tools
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
        "Origin": qsTr("Origin"),
        "Plane": qsTr("Plane"),
        "Rotation": qsTr("Rotation"),
        "By": qsTr("By"),
        "Per line": qsTr("Per line"),
        "From": qsTr("From"),
        "To": qsTr("To")
    })
    // HY1: Hydra's value labels (one value: its role; several: what each is).
    readonly property var valueLabels: ({
        "Value": qsTr("Value"),
        "By": qsTr("By"),
        "Start": qsTr("Start"),
        "Per line": qsTr("Per line"),
        "From": qsTr("From"),
        "To": qsTr("To"),
        "In": qsTr("In"),
        "Out": qsTr("Out"),
        "End": qsTr("End")
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
    // The options' groups, then the values'. VT3: values that go on the
    // last option's group (the Perspective tool's Origin mode, then its X
    // and Y) join that group, one item of the Flow, so a wrap cannot part
    // them.
    function joined(options, values) {
        if (options.length > 0 && values.length > 0 && options[options.length - 1].group.length > 0
                && options[options.length - 1].group === values[0].group) {
            options = options.slice()
            options[options.length - 1] = Object.assign({}, options[options.length - 1], { values: values[0].items })
            values = values.slice(1)
        }
        return options.concat(values)
    }
    // The options, the values, then the options shown after them (HY1).
    function ordered(options, values, measuring) {
        return joined(groupsOf(options.filter(o => !o.afterValues), "options", measuring),
                      groupsOf(values, "values", measuring))
            .concat(groupsOf(options.filter(o => o.afterValues), "options", measuring))
    }
    // The groups for the unseen copy (its values carried whole).
    function measuredSegments(layout) {
        const values = (layout.values ?? []).map(v => ({ name: v.name, group: v.group, value: v }))
        return ordered(layout.options ?? [], values, true)
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
    readonly property var valueItems: {
        const shape = []
        if (valueShape.length > 0)
            for (const part of valueShape.split("\u0002")) {
                const f = part.split("\u0001")
                shape.push({ name: f[0], group: f[1] })
            }
        return shape
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
        name: o.name, kind: o.kind, iconRole: o.iconRole, tooltip: o.tooltip, group: o.group, listEnds: o.listEnds,
        searchable: o.searchable, afterValues: o.afterValues
    })))
    function optionNamed(name, fallback) {
        for (const o of tools.options)
            if (o.name === name)
                return o
        return fallback
    }
    // The crosshair's are on the times line (VisualPointerReadout).
    readonly property bool pointer: family === 0
    readonly property var segments: pointer ? []
        : ordered(JSON.parse(optionShape), valueItems, false).map((g, i) => Object.assign(g, { at: i }))
    // HY1: Hydra's picker slot names the Lines a value changes.
    readonly property bool hydra: family === 10 // Family::Hydra
    // A picked Line is named as one line: it need not be the active one.
    function targetText(count) {
        return count > 1 || count === 0 && tools.batchCount > 0 ? qsTr("Changes %n lines", "", count)
             : tools.batchCount > 0 ? qsTr("Changes 1 line") : qsTr("Changes the active line")
    }
    // The picker's slot, which a batch tool's groups give up: Hydra's names
    // its Lines instead of counting the picked ones.
    function pickerWidthOf(f) {
        const hy = f === 10
        return pick.implicitWidth + 4 + (hy ? targets.implicitWidth + 4 : 0)
               + (tools.batchCount > 0 ? (hy ? 0 : picked.implicitWidth + 4) + clear.implicitWidth + 4 : 0)
    }
    readonly property real pickerWidth: pickerWidthOf(family)
    // The tool's groups without what does not size them (checked,
    // enabled, the chosen entry, the values' numbers): the unseen copies are
    // made again only when the groups change, not on each toggle or sample.
    readonly property string measureShape: pointer ? "" : JSON.stringify({
        options: tools.options.map(o => ({ name: o.name, kind: o.kind, iconRole: o.iconRole, tooltip: o.tooltip,
                                           group: o.group, choices: o.choices, listEnds: o.listEnds,
                                           searchable: o.searchable, afterValues: o.afterValues,
                                           notes: o.notes, sections: o.sections, sectionNames: o.sectionNames,
                                           checked: false, enabled: false, index: 0 })),
        values: tools.values.map(v => ({ name: v.name, label: v.label, text: "", editable: v.editable,
                                         group: v.group, unit: v.unit, step: v.step,
                                         swatch: v.swatch ? "#000000" : "", marked: v.marked === undefined ? undefined : false }))
    })
    // VB1: the groups that fit the line (measured unseen, so what is shown
    // does not decide what fits), the rest under More options.
    readonly property real groupsRoom: availableWidth - (batchTool ? pickerWidth : 0)
    readonly property int fitCount: {
        const n = segments.length
        // A bar not laid out yet (hidden for the crosshair until now) folds
        // nothing: its width comes with its first layout.
        if (!(availableWidth > 0))
            return n
        // Until the unseen copies are the shown groups and laid out, every
        // group shows (a tool's items are there as soon as it is chosen).
        const widths = []
        void measuredItems // the copies are made again with the same count
        for (let i = 0; i < measures.count; ++i) {
            const m = measures.itemAt(i)
            if (!m || !(m.implicitWidth > 0))
                return n
            widths.push(m.implicitWidth)
        }
        const right = k => widths.slice(0, k).reduce((sum, w) => sum + w, 0) + Math.max(0, k - 1) * groupSpacing
        if (widths.length !== n || right(n) <= groupsRoom)
            return n
        let k = 0
        while (k < n && right(k + 1) <= groupsRoom - more.implicitWidth - 4)
            ++k
        return k
    }
    readonly property int groupSpacing: 6
    // Bumped as the unseen copies are made: itemAt() is not a binding's.
    property int measuredItems: 0
    readonly property var foldedSegments: segments.slice(fitCount)
    // The line's height, and whether the tool has one.
    readonly property real lineHeight: topPadding + 26 + bottomPadding
    readonly property bool shown: !pointer && (segments.length > 0 || batchTool)
    implicitHeight: shown ? lineHeight : 0
    visible: shown

    // A group: the rule, its label, its options or values.
    Component {
        id: segmentDelegate
        RowLayout {
            id: segment
            required property var modelData
            required property int index
            // VB1: a group of the line folded under More options is hidden
            // there, unnamed (its live copy is in the popover).
            readonly property bool folded: !modelData.measuring && !(modelData.popover ?? false) && index >= bar.fitCount
            readonly property bool named: !modelData.measuring && !folded
            visible: !folded
            objectName: !named ? ""
                        : modelData.kind === "options" ? "visualOptionGroup" + modelData.at : "visualValueGroup" + modelData.at
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
                    named: segment.named
                    live: measuring ? modelData : bar.optionNamed(modelData.name, modelData)
                }
            }
            Repeater {
                model: segment.modelData.kind === "values" ? segment.modelData.items : (segment.modelData.values ?? [])
                delegate: VisualValueField {
                    required property var modelData
                    Layout.alignment: Qt.AlignVCenter
                    tools: bar.tools
                    measuring: segment.modelData.measuring
                    named: segment.named
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
        Row {
            id: groupsRow
            objectName: "visualToolGroups"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
            Layout.alignment: Qt.AlignTop
            clip: true
            spacing: bar.groupSpacing
            Repeater {
                id: groups
                model: bar.segments
                delegate: segmentDelegate
            }
        }
        // VB1: the groups that do not fit, in the popover.
        IconToolButton {
            id: more
            objectName: "visualToolMore"
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
            visible: bar.foldedSegments.length > 0
            iconRole: "menu-more"
            text: qsTr("More options")
            checked: morePopover.visible
            focusPolicy: Qt.TabFocus
            onClicked: morePopover.openAt(more)
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
            id: targets
            objectName: "visualTargetCount"
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 5
            Layout.leftMargin: 4
            visible: bar.batchTool && bar.hydra
            text: bar.targetText(bar.tools.targetCount)
            font.features: { "tnum": 1 }
        }
        Label {
            id: picked
            objectName: "visualPickedCount"
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 5
            visible: bar.batchTool && !bar.hydra && bar.tools.batchCount > 0
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
    // The tool's groups, unseen, each measured whole: fitCount
    // takes as many as the line has room for.
    Item {
        objectName: "visualToolMeasure"
        width: 0
        height: 0
        clip: true
        opacity: 0
        enabled: false
        Accessible.ignored: true
        Row {
            Repeater {
                id: measures
                model: bar.measureShape.length > 0 ? bar.measuredSegments(JSON.parse(bar.measureShape)) : []
                delegate: segmentDelegate
                onItemAdded: ++bar.measuredItems
                onItemRemoved: ++bar.measuredItems
            }
        }
    }
    // More options: the folded groups, live, one a line.
    Popover {
        id: morePopover
        objectName: "visualToolMorePopover"
        title: qsTr("More options")
        Repeater {
            model: morePopover.visible ? bar.foldedSegments.map(g => Object.assign({}, g, { popover: true })) : []
            delegate: segmentDelegate
        }
    }
    // It goes with the tool, and when every group fits again.
    onFamilyChanged: morePopover.close()
    onFoldedSegmentsChanged: if (foldedSegments.length === 0) morePopover.close()

    // The dialogs a tool opens (its notice, the shape and tag editors), in
    // the window showing them: the Video panel's bar and fullscreen's share
    // one controller, so one bar's at a time (Codex on #293).
    property bool dialogs: true
    // Where the dialogs' places are kept (DG1).
    property SettingsStore settings: null
    Loader {
        active: bar.dialogs
        sourceComponent: Component {
            VisualToolDialogs {
                tools: bar.tools
                settings: bar.settings
            }
        }
    }
}
