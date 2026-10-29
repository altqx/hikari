pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: readout
    objectName: "visualPointerReadout"
    required property VisualToolsController tools
    readonly property bool pointer: tools.activeFamily === 0
    visible: pointer
    spacing: 3

    function valueNamed(name) {
        for (const v of tools.values)
            if (v.name === name)
                return v
        return { name: name, label: "", text: "", editable: false, group: "", unit: "", step: 1 }
    }
    readonly property var copyShape: ({ name: "copyPosition", kind: "action", iconRole: "copy-position", tooltip: "",
                                        group: "", checked: false, enabled: false, choices: [], index: 0,
                                        listEnds: false, searchable: false, afterValues: false })
    function optionNamed(name) {
        for (const o of tools.options)
            if (o.name === name)
                return o
        return copyShape
    }

    Repeater {
        model: readout.pointer ? ["x", "y"] : []
        delegate: VisualValueField {
            required property string modelData
            Layout.alignment: Qt.AlignVCenter
            tools: readout.tools
            named: readout.pointer
            name: modelData
            value: readout.valueNamed(modelData)
            accessibleName: [readout.tools.families[0]?.name ?? "", value.label].filter(s => s.length > 0).join(" ")
        }
    }
    // MUST KILL: VisualToolOption's 24 is the times line's height, copied by hand
    VisualToolOption {
        Layout.preferredWidth: 24
        Layout.preferredHeight: 24
        visible: readout.pointer
        named: readout.pointer
        tools: readout.tools
        modelData: readout.copyShape
        live: readout.optionNamed("copyPosition")
    }
}
