import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// The active family's own options, legacy VideoToolbar's second row
// (VisualItem). T4: the clips' buttons: VectorItem's point modes (one on at
// a time) and Invert clip, ClipRectangleItem's Invert clip, each with its
// icon of the K1 set and legacy's help text. A toggle shows its state; an
// action acts at once. T5: the drawing's point modes (not usable while a
// shape is chosen) and its shape list ("Choose", the presets, "Edit", which
// opens the "Vector shape editing" dialog), an icon with its menu. A notice a tool gives is
// legacy's HikariMessageBox titled "Warning" (VisualClips.cpp:1013-1016): a
// modal box in the window, closed by OK.
RowLayout {
    id: options
    objectName: "visualToolOptions"
    required property VisualToolsController tools
    spacing: 1

    Repeater {
        model: options.tools.options
        delegate: Loader {
            id: option
            required property var modelData
            sourceComponent: modelData.kind === "choice" ? choiceComponent : buttonComponent
            Component {
                id: buttonComponent
                IconToolButton {
                    objectName: "visualOption_" + option.modelData.name
                    // The K1 roles the options name (on one line: icon_tests reads them from it).
                    iconRole: ["vector-drag", "vector-line", "vector-bezier", "vector-bspline", "vector-point", "vector-delete", "clip-invert"].indexOf(option.modelData.iconRole) >= 0 ? option.modelData.iconRole : ""
                    text: option.modelData.tooltip.split("\n")[0]
                    tip: option.modelData.tooltip
                    checkable: option.modelData.kind === "toggle"
                    checked: option.modelData.checked
                    enabled: option.modelData.enabled && options.tools.railEnabled
                    focusPolicy: Qt.NoFocus
                    onClicked: {
                        options.tools.setOption(option.modelData.name, option.modelData.kind === "toggle" ? (checked ? 1 : 0) : 1)
                        checked = Qt.binding(() => option.modelData.checked) // the tool's state, not the click's
                    }
                }
            }
            Component {
                id: choiceComponent
                // A choice is an icon with its menu, as the row's other
                // options (the tool strip is icon-only; T5 review): legacy's
                // shape list (HikariChoice, VideoToolbar.cpp:503-512) as a
                // ShellMenu of its entries, the chosen one checked. Its last
                // entry ("Edit") is an action: after a separator, with no
                // check. The button shows itself on while an entry other
                // than the first ("Choose", no shape) is chosen; the tooltip
                // names it.
                IconToolButton {
                    id: choice
                    objectName: "visualOption_" + option.modelData.name
                    readonly property var model: option.modelData.choices
                    readonly property int currentIndex: option.modelData.index
                    signal activated(int index)
                    // The K1 roles the choices name (on one line: icon_tests reads them from it).
                    iconRole: ["shape-presets"].indexOf(option.modelData.iconRole) >= 0 ? option.modelData.iconRole : ""
                    text: option.modelData.tooltip.split("\n")[0]
                    tip: (currentIndex > 0 && currentIndex < model.length ? qsTr("Shape: %1").arg(model[currentIndex]) + "\n" : "")
                         + option.modelData.tooltip
                    Accessible.description: currentIndex >= 0 && currentIndex < model.length ? model[currentIndex] : ""
                    checked: currentIndex > 0
                    down: pressed || choiceMenu.visible
                    enabled: option.modelData.enabled && options.tools.railEnabled
                    focusPolicy: Qt.NoFocus
                    onClicked: {
                        if (choiceMenu.visible)
                            choiceMenu.close()
                        else
                            choiceMenu.popup(choice, 0, choice.height)
                    }
                    onActivated: index => options.tools.setOption(option.modelData.name, index)
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
                            visible: choice.model.length > 1
                            height: visible ? implicitHeight : 0
                        }
                        Instantiator {
                            model: choice.model
                            delegate: ShellMenuItem {
                                required property int index
                                required property string modelData
                                readonly property bool isAction: index === choice.model.length - 1 && index > 0
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
        onClosed: options.tools.dismissNotice()
    }
    // T5: the shape list's "Edit".
    ShapesEditionDialog {
        id: shapesDialog
        editor: options.tools.shapeEditor
    }
    Connections {
        target: options.tools
        function onChanged() {
            if (options.tools.notice.length > 0 && !noticeBox.visible) {
                noticeText.text = options.tools.notice
                noticeBox.open()
            }
            else if (options.tools.notice.length === 0 && noticeBox.visible)
                noticeBox.close()
        }
        function onShapeEditorChanged() {
            if (options.tools.shapeEditor)
                shapesDialog.open()
            else if (shapesDialog.visible)
                shapesDialog.close()
        }
    }
}
