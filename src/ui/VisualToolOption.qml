pragma Translator: "HikariSub.VisualTools"
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts
import Hikari.Ui

// T2-T6, VT1: one of the active family's options in the inspector bar
// (legacy VideoToolbar's second row: the VisualItem of the family). A toggle
// or an action is an icon button of the K1 set with legacy's help text as
// its tooltip; a greyed icon is a disabled button (X and Y without the
// rectangle, as legacy greyed them), a pushed one a checked toggle; an
// action acts at once. The item's links (an option that switches another
// on) are the tool's (setOption). A choice is legacy's HikariChoice (the
// shape list, VideoToolbar.cpp:503-512; the tag list and change options,
// 737-799): in a group (VT1: the shape, the tag, the change option) a
// dropdown that shows the chosen entry as text, opening a ShellMenu of the
// entries (the chosen one checked); without one an icon with that menu, and
// without an icon (PositionItem's alignment) a combo box. The shape list's
// (listEnds) first entry ("Choose") is no shape and its last ("Edit") an
// action after a separator, with no check; the bar names them "Free
// drawing" and "Edit shapes…".
//
// HY1 (#271): a switch is a row of segments, the chosen one pushed
// (Hydra's change option); a searchable choice is VisualTagPicker (Hydra's
// tag list). Hydra's entries are named in the catalog.
Loader {
    id: option
    required property var modelData
    required property VisualToolsController tools
    // VT1: an unseen copy the bar measures (no names, no focus).
    property bool measuring: false
    // VB1: named, unless unseen (measured, or folded off the bar's line).
    property bool named: !measuring
    // What changes while the option stays (checked, enabled, the entries
    // and the chosen one): the bar gives the tool's current state by name,
    // so a toggle does not make the bar's items again.
    property var live: modelData
    readonly property string nameOf: named ? "visualOption_" + modelData.name : ""
    readonly property bool grouped: modelData.group.length > 0
    // The tools' help texts are legacy's; the bar's own action (VT1's Copy
    // video position) is named in the catalog.
    readonly property string tooltip: modelData.kind === "action" && modelData.name === "copyPosition"
                                      ? qsTr("Copy video position") : modelData.tooltip
    // HY1: the entries the bar names (Hydra's switch and its choices).
    readonly property var entryNames: ({
        "Set": qsTr("Set"),
        "Add": qsTr("Add"),
        "Multiply": qsTr("Multiply"),
        "Gradient": qsTr("Gradient"),
        "At the cursor": qsTr("At the cursor"),
        "At the start of the text": qsTr("At the start of the text"),
        // (the tag outside the text: lupdate reads "\\t" as a tab)
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
            // The roles the families' options show (one line: icon_tests reads the roles from it).
            iconRole: ["frame-to-scale", "scale-x", "link", "scale-y", "original-frame", "tool-scale-rotation", "resample", "two-points", "vector-drag", "vector-line", "vector-bezier", "vector-bspline", "vector-point", "vector-delete", "clip-invert", "shift-position", "shift-move-start", "shift-move-end", "shift-clips", "shift-drawings", "shift-origins", "tag-edit", "copy-position", "perspective-plane", "perspective-lock-outer", "perspective-grid"].includes(option.modelData.iconRole) ? option.modelData.iconRole : ""
            text: option.tooltip.split("\n")[0]
            tip: option.tooltip
            checkable: option.modelData.kind === "toggle"
            checked: option.live.checked
            enabled: option.live.enabled && option.tools.railEnabled
            focusPolicy: Qt.NoFocus
            // The tool decides (a greyed or linked option); the binding then
            // shows what it took, not the click's state.
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
            // An icon button with its menu outside a group (the tool strip's
            // look); in one, the chosen entry as text.
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
            // The chosen entry first, but where the button shows it.
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
                    // The K1 roles the choices name (on one line: icon_tests reads them from it).
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
                        // As wide as the longest entry, so the bar does not
                        // move as the choice changes (up to a limit).
                        Layout.preferredWidth: Math.min(widest, 200)
                        text: choice.entryText(choice.currentIndex)
                        elide: Text.ElideRight
                        color: choice.enabled ? Theme.text : Theme.disabled
                        // Each entry as drawn (a translation's fewer, wider
                        // glyphs can be the widest).
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
                // A press on the button closes it through onClicked.
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
                // Made with the button before the Loader puts it in the
                // window, the menu does not inherit the window's palette:
                // it takes the button's (the theme's, live).
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
                        keepsMenuOpen: false // one choice of several
                        onTriggered: {
                            choice.activated(index)
                            checked = Qt.binding(() => index === choice.currentIndex)
                        }
                    }
                    // The entries before the separator, the action after it.
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
        // HY1: the segments, the chosen one pushed; one click picks.
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
        // T2: a choice without an icon (PositionItem's alignment).
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
