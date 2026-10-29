pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts
import Hikari.Ui

Loader {
    id: option
    required property var modelData
    required property VisualToolsController tools
    property bool measuring: false
    property bool named: !measuring
    property var live: modelData
    readonly property string nameOf: named ? "visualOption_" + modelData.name : ""
    readonly property bool grouped: modelData.group.length > 0
    readonly property string tooltip: modelData.kind === "action" && modelData.name === "copyPosition"
                                      ? qsTr("Copy video position") : modelData.tooltip
    readonly property var entryNames: ({
        "Set": qsTr("Set"),
        "Add": qsTr("Add"),
        "Multiply": qsTr("Multiply"),
        "Gradient": qsTr("Gradient"),
        "At the cursor": qsTr("At the cursor"),
        "At the start of the text": qsTr("At the start of the text"),
        // lupdate reads "\\t" as a tab, so the tag stays outside the text.
        "Clip rectangle with \\t animation": qsTr("Clip rectangle with %1 animation").arg("\\t"),
        "Set by line number": qsTr("Set by line number"),
        "Add by line number": qsTr("Add by line number"),
        "Across characters, increasing": qsTr("Across characters, increasing"),
        "Across characters, decreasing": qsTr("Across characters, decreasing"),
        "Across lines, increasing": qsTr("Across lines, increasing"),
        "Across lines, decreasing": qsTr("Across lines, decreasing")
    })
    function entryName(text) {
        return option.entryNames[text] ?? text
    }
    sourceComponent: modelData.kind === "switch" ? switchComponent
                     : modelData.kind !== "choice" ? buttonComponent
                     : modelData.searchable ? pickerComponent
                     : modelData.iconRole.length === 0 ? listComponent : choiceComponent
    Layout.alignment: Qt.AlignVCenter

    Component {
        id: buttonComponent
        IconToolButton {
            objectName: option.nameOf
            // MUST KILL: icon_tests parses this iconRole list from its single source line
            iconRole: ["frame-to-scale", "scale-x", "link", "scale-y", "original-frame", "tool-scale-rotation", "resample", "two-points", "vector-drag", "vector-line", "vector-bezier", "vector-bspline", "vector-point", "vector-delete", "clip-invert", "shift-position", "shift-move-start", "shift-move-end", "shift-clips", "shift-drawings", "shift-origins", "tag-edit", "copy-position", "perspective-plane", "perspective-lock-outer", "perspective-grid"].includes(option.modelData.iconRole) ? option.modelData.iconRole : ""
            text: option.tooltip.split("\n")[0]
            tip: option.tooltip
            checkable: option.modelData.kind === "toggle"
            checked: option.live.checked
            enabled: option.live.enabled && option.tools.railEnabled
            focusPolicy: Qt.NoFocus
            onClicked: {
                option.tools.setOption(option.modelData.name,
                                       option.modelData.kind === "toggle" ? (checked ? 1 : 0) : 1)
                checked = Qt.binding(() => option.live.checked)
            }
        }
    }
    Component {
        id: choiceComponent
        AbstractButton {
            id: choice
            objectName: option.nameOf
            readonly property var model: option.live.choices
            readonly property int currentIndex: option.live.index
            readonly property bool listEnds: option.modelData.listEnds
            readonly property string current: currentIndex >= 0 && currentIndex < model.length ? model[currentIndex] : ""
            readonly property bool asText: option.grouped
            signal activated(int index)
            function entryText(index) {
                if (listEnds && index === 0)
                    return qsTr("Free drawing")
                if (listEnds && index === model.length - 1 && index > 0)
                    return qsTr("Edit shapes…")
                return option.entryName(model[index] ?? "")
            }
            readonly property string role: option.modelData.iconRole
            text: option.modelData.tooltip.split("\n")[0]
            Accessible.role: asText ? Accessible.ComboBox : Accessible.Button
            Accessible.name: text
            Accessible.description: entryText(currentIndex)
            checkable: false
            checked: !asText && listEnds && currentIndex > 0
            down: pressed || choiceMenu.visible
            enabled: option.live.enabled && option.tools.railEnabled
            focusPolicy: Qt.NoFocus
            hoverEnabled: true
            padding: asText ? 0 : 2
            implicitWidth: asText ? Math.max(textBox.implicitWidth, 72) : 24
            implicitHeight: 24
            ToolTip.visible: hovered && !choiceMenu.visible
            ToolTip.text: (asText || current.length === 0 ? ""
                           : listEnds ? (currentIndex > 0 ? qsTr("Shape: %1").arg(current) + "\n" : "")
                           : current + "\n")
                          + option.modelData.tooltip
            contentItem: Item {
                implicitWidth: choice.asText ? textBox.implicitWidth : 20
                implicitHeight: 20
                Icon {
                    anchors.centerIn: parent
                    visible: !choice.asText
                    // MUST KILL: icon_tests parses this iconRole list from its single source line
                    iconRole: ["shape-presets", "tag-list", "tag-change-option"].indexOf(choice.role) >= 0 ? choice.role : ""
                }
                RowLayout {
                    id: textBox
                    visible: choice.asText
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 6
                    spacing: 8
                    Label {
                        id: entryLabel
                        objectName: choice.objectName.length > 0 ? choice.objectName + "_text" : ""
                        Layout.fillWidth: true
                        Layout.preferredWidth: Math.min(widest, 200)
                        text: choice.entryText(choice.currentIndex)
                        elide: Text.ElideRight
                        color: choice.enabled ? Theme.text : Theme.disabled
                        readonly property real widest: {
                            let w = 0
                            for (let i = 0; i < choice.model.length; ++i)
                                w = Math.max(w, entryMetrics.advanceWidth(choice.entryText(i)))
                            return Math.ceil(w)
                        }
                        FontMetrics {
                            id: entryMetrics
                            font: entryLabel.font
                        }
                    }
                    ColorImage {
                        source: "qrc:/qt-project.org/imports/QtQuick/Controls/Fusion/images/arrow.png"
                        color: choice.enabled ? Theme.text : Theme.disabled
                        width: 10
                        height: 10
                        fillMode: Image.PreserveAspectFit
                    }
                }
            }
            background: Rectangle {
                implicitHeight: 24
                radius: 2
                visible: choice.asText || choice.down || choice.checked || (choice.enabled && choice.hovered)
                color: choice.down || choice.checked ? Qt.darker(Theme.raised, 1.08) : choice.hovered ? Theme.raised
                       : choice.asText ? Theme.raised : "transparent"
                border.color: choiceMenu.visible ? Theme.accent : choice.enabled ? Theme.control : Theme.line
                border.width: 1
            }
            onClicked: {
                if (choiceMenu.visible)
                    choiceMenu.close()
                else
                    choiceMenu.popup(choice, 0, choice.height)
            }
            onActivated: index => option.tools.setOption(option.modelData.name, index)
            ShellMenu {
                id: choiceMenu
                objectName: choice.objectName.length > 0 ? choice.objectName + "_menu" : ""
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                // Qt: made before the Loader puts it in the window, the menu does not inherit the window's palette.
                palette: choice.palette
                MenuSeparator {
                    visible: choice.listEnds && choice.model.length > 1
                    height: visible ? implicitHeight : 0
                }
                Instantiator {
                    model: choice.model
                    delegate: ShellMenuItem {
                        required property int index
                        required property string modelData
                        readonly property bool isAction: choice.listEnds && index === choice.model.length - 1 && index > 0
                        iconRole: isAction ? "edit" : ""
                        objectName: choice.objectName.length > 0 ? choice.objectName + "_" + index : ""
                        text: choice.entryText(index)
                        checkable: !isAction
                        checked: index === choice.currentIndex
                        keepsMenuOpen: false
                        onTriggered: {
                            choice.activated(index)
                            checked = Qt.binding(() => index === choice.currentIndex)
                        }
                    }
                    onObjectAdded: (index, object) => choiceMenu.insertItem(object.isAction ? index + 1 : index, object)
                    onObjectRemoved: (index, object) => choiceMenu.removeItem(object)
                }
            }
        }
    }
    Component {
        id: pickerComponent
        VisualTagPicker {
            modelData: option.modelData
            live: option.live
            tools: option.tools
            measuring: option.measuring
            named: option.named
        }
    }
    Component {
        id: switchComponent
        Row {
            id: segments
            objectName: option.nameOf
            spacing: -1
            Accessible.role: Accessible.PageTabList
            Accessible.name: option.modelData.tooltip.split("\n")[0]
            Repeater {
                model: option.live.choices
                delegate: AbstractButton {
                    id: segment
                    required property string modelData
                    required property int index
                    readonly property bool chosen: index === option.live.index
                    objectName: option.nameOf.length > 0 ? option.nameOf + "_" + index : ""
                    text: option.entryName(modelData)
                    implicitHeight: 24
                    implicitWidth: segmentText.implicitWidth + 20
                    enabled: option.live.enabled && option.tools.railEnabled
                    focusPolicy: Qt.NoFocus
                    hoverEnabled: true
                    checkable: false
                    checked: chosen
                    Accessible.role: Accessible.PageTab
                    Accessible.name: text
                    Accessible.checked: chosen
                    ToolTip.visible: hovered && index === 0
                    ToolTip.text: option.modelData.tooltip
                    contentItem: Label {
                        id: segmentText
                        text: segment.text
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: segment.chosen ? Theme.onAccent : segment.enabled ? Theme.text : Theme.disabled
                    }
                    background: Rectangle {
                        radius: 2
                        color: segment.chosen ? Theme.accent : segment.down ? Qt.darker(Theme.raised, 1.08)
                               : segment.hovered ? Qt.lighter(Theme.raised, 1.06) : Theme.raised
                        border.color: segment.chosen ? Theme.accent : segment.enabled ? Theme.control : Theme.line
                    }
                    z: chosen ? 1 : 0
                    onClicked: if (!chosen) option.tools.setOption(option.modelData.name, index)
                }
            }
        }
    }
    Component {
        id: listComponent
        ComboBox {
            objectName: option.nameOf
            model: option.live.choices
            currentIndex: option.live.index
            enabled: option.live.enabled && option.tools.railEnabled
            focusPolicy: Qt.NoFocus
            implicitHeight: 24
            implicitContentWidthPolicy: ComboBox.WidestText
            Accessible.name: option.modelData.tooltip
            ToolTip.visible: hovered
            ToolTip.text: option.modelData.tooltip
            onActivated: index => option.tools.setOption(option.modelData.name, index)
        }
    }
}
