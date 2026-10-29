pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

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
    function joined(options, values) {
        if (options.length > 0 && values.length > 0 && options[options.length - 1].group.length > 0
                && options[options.length - 1].group === values[0].group) {
            options = options.slice()
            options[options.length - 1] = Object.assign({}, options[options.length - 1], { values: values[0].items })
            values = values.slice(1)
        }
        return options.concat(values)
    }
    function ordered(options, values, measuring) {
        return joined(groupsOf(options.filter(o => !o.afterValues), "options", measuring),
                      groupsOf(values, "values", measuring))
            .concat(groupsOf(options.filter(o => o.afterValues), "options", measuring))
    }
    function measuredSegments(layout) {
        const values = (layout.values ?? []).map(v => ({ name: v.name, group: v.group, value: v }))
        return ordered(layout.options ?? [], values, true)
    }
    // A Repeater remakes every item for a new array: this string changes only with the layout, not each sample.
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
    // MUST KILL: pointer means family 0, the crosshair
    readonly property bool pointer: family === 0
    readonly property var segments: pointer ? []
        : ordered(JSON.parse(optionShape), valueItems, false).map((g, i) => Object.assign(g, { at: i }))
    // MUST KILL: hydra compares family with the magic int 10 (Family::Hydra), as pickerWidthOf does
    readonly property bool hydra: family === 10
    function targetText(count) {
        return count > 1 || count === 0 && tools.batchCount > 0 ? qsTr("Changes %n lines", "", count)
             : tools.batchCount > 0 ? qsTr("Changes 1 line") : qsTr("Changes the active line")
    }
    function pickerWidthOf(f) {
        const hy = f === 10
        return pick.implicitWidth + 4 + (hy ? targets.implicitWidth + 4 : 0)
               + (tools.batchCount > 0 ? (hy ? 0 : picked.implicitWidth + 4) + clear.implicitWidth + 4 : 0)
    }
    readonly property real pickerWidth: pickerWidthOf(family)
    readonly property string measureShape: pointer ? "" : JSON.stringify({
        options: tools.options.map(o => ({ name: o.name, kind: o.kind, iconRole: o.iconRole, tooltip: o.tooltip,
                                           group: o.group, choices: o.choices, listEnds: o.listEnds,
                                           searchable: o.searchable, afterValues: o.afterValues,
                                           notes: o.notes, sections: o.sections, sectionNames: o.sectionNames,
                                           checked: false, enabled: false, index: 0 })),
        values: tools.values.map(v => ({ name: v.name, label: v.label, text: v.editable ? "" : "-00000.00", editable: v.editable,
                                         group: v.group, unit: v.unit, step: v.step,
                                         swatch: v.swatch ? "#000000" : "", marked: v.marked === undefined ? undefined : false }))
    })
    readonly property real groupsRoom: availableWidth - (batchTool ? pickerWidth : 0)
    readonly property int fitCount: {
        const n = segments.length
        if (!(availableWidth > 0))
            return n
        const widths = []
        void measuredItems
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
    // Repeater.itemAt() is not bindable: bumped as the unseen copies are made.
    property int measuredItems: 0
    readonly property var foldedSegments: segments.slice(fitCount)
    readonly property bool popoverOpen: morePopover.opened
    readonly property real lineHeight: topPadding + 26 + bottomPadding
    readonly property bool shown: !pointer && (segments.length > 0 || batchTool)
    implicitHeight: shown ? lineHeight : 0
    visible: shown

    Component {
        id: segmentDelegate
        RowLayout {
            id: segment
            required property var modelData
            required property int index
            readonly property bool folded: !modelData.measuring && !(modelData.popover ?? false) && index >= bar.fitCount
            readonly property bool named: !modelData.measuring && !folded
            visible: !folded
            objectName: !named ? ""
                        : modelData.kind === "options" ? "visualOptionGroup" + modelData.at : "visualValueGroup" + modelData.at
            height: 26
            spacing: 3
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
        IconToolButton {
            id: pick
            objectName: "visualPickBatch"
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 1
            visible: bar.batchTool
            iconRole: "pick-lines"
            text: qsTr("Pick selected lines")
            // Without an English catalog %n has no singular, so one line has its own text.
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
    Popover {
        id: morePopover
        objectName: "visualToolMorePopover"
        title: qsTr("More options")
        Repeater {
            model: morePopover.visible ? bar.foldedSegments.map(g => Object.assign({}, g, { popover: true })) : []
            delegate: segmentDelegate
        }
    }
    onFamilyChanged: morePopover.close()
    onFoldedSegmentsChanged: if (foldedSegments.length === 0) morePopover.close()

    property bool dialogs: true
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
