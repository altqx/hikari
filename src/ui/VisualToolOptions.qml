import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// T2-T4: the active family's own options, legacy VideoToolbar's second row
// (the VisualItem of the family: PositionItem's "by rectangle", X and Y
// toggles and its alignment choice, MoveItem's two points, ScaleItem,
// RotationZItem, RotationXYItem, VectorItem's point modes (one on at a time)
// and Invert clip, ClipRectangleItem's Invert clip, ...), in a row above the
// values below the canvas (layout A). Each button shows its icon of the K1
// set with legacy's help text as its tooltip; a greyed icon is a disabled
// button (X and Y without the rectangle, as legacy greyed them), a pushed
// one a checked toggle; an action acts at once. The item's links (an option
// that switches another on) are the tool's (setOption). A notice a tool
// gives is legacy's HikariMessageBox titled "Warning" (VisualClips.cpp:
// 1013-1016): a modal box in the window, closed by OK. T5: the drawing's
// point modes (not usable while a shape is chosen) and its shape list
// ("Choose", the presets, "Edit", which opens the "Vector shape editing"
// dialog), an icon with its menu; a choice without an icon is a list. T6:
// the Position shifter's six kinds (MoveAllItem) and the all-tags tool's
// tag list, change options and Edit (AllTagsItem: legacy's two text choices
// and a text button, icons with their menus here), which opens the "Tag
// editing" dialog.
RowLayout {
    id: optionsRow
    objectName: "visualToolOptions"
    required property VisualToolsController tools
    spacing: 1
    visible: repeater.count > 0

    Repeater {
        id: repeater
        model: optionsRow.tools.options
        delegate: Loader {
            id: option
            required property var modelData
            sourceComponent: modelData.kind !== "choice" ? buttonComponent
                             : modelData.iconRole.length > 0 ? choiceComponent : listComponent
            Component {
                id: buttonComponent
                IconToolButton {
                    objectName: "visualOption_" + option.modelData.name
                    // The roles the families' options show (one line: icon_tests reads the roles from it).
                    iconRole: ["frame-to-scale", "scale-x", "link", "scale-y", "original-frame", "tool-scale-rotation", "resample", "two-points", "vector-drag", "vector-line", "vector-bezier", "vector-bspline", "vector-point", "vector-delete", "clip-invert", "shift-position", "shift-move-start", "shift-move-end", "shift-clips", "shift-drawings", "shift-origins", "tag-edit"].includes(option.modelData.iconRole) ? option.modelData.iconRole : ""
                    text: option.modelData.tooltip.split("\n")[0]
                    tip: option.modelData.tooltip
                    checkable: option.modelData.kind === "toggle"
                    checked: option.modelData.checked
                    enabled: option.modelData.enabled && optionsRow.tools.railEnabled
                    focusPolicy: Qt.NoFocus
                    // The tool decides (a greyed or linked option); the binding then
                    // shows what it took, not the click's state.
                    onClicked: {
                        optionsRow.tools.setOption(option.modelData.name,
                                                   option.modelData.kind === "toggle" ? (checked ? 1 : 0) : 1)
                        checked = Qt.binding(() => option.modelData.checked)
                    }
                }
            }
            Component {
                id: choiceComponent
                // A choice is an icon with its menu, as the row's other
                // options (the tool strip is icon-only; T5 review): legacy's
                // HikariChoice (the shape list, VideoToolbar.cpp:503-512; the
                // tag list and change options, 737-799) as a ShellMenu of its
                // entries, the chosen one checked; the tooltip and the
                // accessible description name it. The shape list's
                // (listEnds) first entry ("Choose") is no shape, the button
                // on while another is chosen, and its last ("Edit") an
                // action after a separator, with no check.
                IconToolButton {
                    id: choice
                    objectName: "visualOption_" + option.modelData.name
                    readonly property var model: option.modelData.choices
                    readonly property int currentIndex: option.modelData.index
                    readonly property bool listEnds: option.modelData.listEnds
                    readonly property string current: currentIndex >= 0 && currentIndex < model.length ? model[currentIndex] : ""
                    signal activated(int index)
                    // The K1 roles the choices name (on one line: icon_tests reads them from it).
                    iconRole: ["shape-presets", "tag-list", "tag-change-option"].indexOf(option.modelData.iconRole) >= 0 ? option.modelData.iconRole : ""
                    text: option.modelData.tooltip.split("\n")[0]
                    tip: (listEnds ? (currentIndex > 0 && current.length > 0 ? qsTr("Shape: %1").arg(current) + "\n" : "")
                                   : (current.length > 0 ? current + "\n" : ""))
                         + option.modelData.tooltip
                    Accessible.description: current
                    checked: listEnds && currentIndex > 0
                    down: pressed || choiceMenu.visible
                    enabled: option.modelData.enabled && optionsRow.tools.railEnabled
                    focusPolicy: Qt.NoFocus
                    onClicked: {
                        if (choiceMenu.visible)
                            choiceMenu.close()
                        else
                            choiceMenu.popup(choice, 0, choice.height)
                    }
                    onActivated: index => optionsRow.tools.setOption(option.modelData.name, index)
                    ShellMenu {
                        id: choiceMenu
                        objectName: choice.objectName + "_menu"
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
                                objectName: choice.objectName + "_" + index
                                text: modelData
                                checkable: !isAction
                                checked: index === choice.currentIndex
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
                id: listComponent
                // T2: a choice without an icon (PositionItem's alignment).
                ComboBox {
                    objectName: "visualOption_" + option.modelData.name
                    model: option.modelData.choices
                    currentIndex: option.modelData.index
                    enabled: option.modelData.enabled && optionsRow.tools.railEnabled
                    focusPolicy: Qt.NoFocus
                    implicitContentWidthPolicy: ComboBox.WidestText
                    Accessible.name: option.modelData.tooltip
                    ToolTip.visible: hovered
                    ToolTip.text: option.modelData.tooltip
                    onActivated: index => optionsRow.tools.setOption(option.modelData.name, index)
                }
            }
        }
    }
    Dialog {
        id: noticeBox
        objectName: "visualNotice"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true // legacy HikariMessageBox is modal
        title: qsTr("Warning")
        standardButtons: Dialog.Ok
        // The title without the style's eliding header, whose width loops
        // through the box's implicit width.
        header: Label {
            text: noticeBox.title
            font.bold: true
            padding: 12
            bottomPadding: 0
        }
        Label {
            id: noticeText
            objectName: "visualNoticeText"
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }
        onClosed: optionsRow.tools.dismissNotice()
    }
    // T5: the shape list's "Edit".
    ShapesEditionDialog {
        id: shapesDialog
        editor: optionsRow.tools.shapeEditor
    }
    // T6: the all-tags tool's Edit.
    AllTagsEditionDialog {
        id: tagsDialog
        editor: optionsRow.tools.tagsEditor
    }
    Connections {
        target: optionsRow.tools
        function onChanged() {
            if (optionsRow.tools.notice.length > 0 && !noticeBox.visible) {
                noticeText.text = optionsRow.tools.notice
                noticeBox.open()
            }
            else if (optionsRow.tools.notice.length === 0 && noticeBox.visible)
                noticeBox.close()
        }
        function onShapeEditorChanged() {
            if (optionsRow.tools.shapeEditor)
                shapesDialog.open()
            else if (shapesDialog.visible)
                shapesDialog.close()
        }
        function onTagsEditorChanged() {
            if (optionsRow.tools.tagsEditor)
                tagsDialog.open()
            else if (tagsDialog.visible)
                tagsDialog.close()
        }
    }
}
