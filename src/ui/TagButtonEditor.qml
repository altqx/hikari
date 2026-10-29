// LE5 (#230): the custom tag button popover, legacy TagButtonDialog ("Enter
// ASS tag") as a popover at the button it edits or at the strip's "+": the
// name, the tag text, where it goes (legacy's three insertion types) and an
// icon of the set or none, with the button as the strip will show it. Save
// takes a tag text the controller accepts (TagButtonsController::problem);
// Enter saves, Esc or a press outside cancels. `saved` hands the button to
// the owner: the strip adds or edits it, Settings > Editor stages it.
pragma Translator: "HikariSub.Editor"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Popup {
    id: popover
    objectName: "tagButtonEditor"
    required property TagButtonsController tagButtons
    // The place the button has (-1: a new one).
    property int index: -1
    signal saved(int index, var button)

    // qsTr on every evaluation: a language switch re-evaluates only the
    // bindings that called it, and the controller's texts follow with it.
    readonly property string problem: {
        const noName = qsTr("Enter a name or choose an icon")
        if (!tagButtons)
            return ""
        const p = tagButtons.problem(tagText.text, tagType.currentIndex)
        if (p.length > 0)
            return p
        return tagName.text.trim().length === 0 && icons.chosen.length === 0 ? noName : ""
    }
    readonly property var button: ({ name: tagName.text, tag: tagText.text, type: tagType.currentIndex, icon: icons.chosen })

    // `button` {name, tag, type, icon} or null for a new one; at `anchor`.
    function openFor(anchor, i, b) {
        parent = anchor
        index = i
        tagName.text = b ? b.name : ""
        tagText.text = b ? b.tag : ""
        tagType.currentIndex = b ? b.type : 0
        icons.chosen = b && b.icon ? b.icon : ""
        touched = false
        open()
        tagText.selectAll()
        tagText.forceActiveFocus()
    }
    // The problem shows once the tag text was typed in or Save was tried.
    property bool touched: false
    function save() {
        touched = true
        if (problem.length > 0)
            return
        saved(index, button)
        close()
    }

    y: parent ? parent.height + 2 : 0
    // Kept inside the window (a button at the strip's right end).
    margins: 8
    modal: false
    focus: true
    padding: 12
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        color: Theme.panel
        border.color: Theme.line
        radius: 4
    }

    contentItem: ColumnLayout {
        spacing: 8
        Keys.onReturnPressed: popover.save()
        Keys.onEnterPressed: popover.save()
        GridLayout {
            columns: 2
            columnSpacing: 8
            rowSpacing: 6
            Label { text: qsTr("Name") }
            TextField {
                id: tagName
                objectName: "tagName"
                Layout.fillWidth: true
                Layout.preferredWidth: 220
                Accessible.name: qsTr("Button name")
                onAccepted: popover.save()
            }
            Label { text: qsTr("Tag") }
            TextField {
                id: tagText
                objectName: "tagText"
                Layout.fillWidth: true
                placeholderText: "\\blur2"
                Accessible.name: qsTr("Button tag")
                onTextEdited: popover.touched = true
                onAccepted: popover.save()
            }
            Label { text: qsTr("Insert") }
            ComboBox {
                id: tagType
                objectName: "tagType"
                Layout.fillWidth: true
                model: [qsTr("Tag inserted in place of cursor"), qsTr("Insert Tag at text beginning"), qsTr("Plain text")]
                Accessible.name: qsTr("Insertion")
            }
            Label { text: qsTr("Icon"); Layout.alignment: Qt.AlignTop; Layout.topMargin: 4 }
            // "None" above a grid of equal cells, so that its width leaves
            // the icons' columns alone.
            Column {
                id: icons
                objectName: "tagIcons"
                property string chosen
                spacing: 2
                ToolButton {
                    objectName: "tagIcon_none"
                    text: qsTr("None")
                    checkable: true
                    checked: icons.chosen.length === 0
                    focusPolicy: Qt.TabFocus
                    // The check shows the choice, not the click's toggle.
                    onClicked: {
                        icons.chosen = ""
                        checked = Qt.binding(() => icons.chosen.length === 0)
                    }
                }
                Grid {
                    columns: 7
                    spacing: 2
                    Repeater {
                        model: popover.tagButtons ? popover.tagButtons.iconRoles : []
                        IconToolButton {
                            required property string modelData
                            objectName: "tagIcon_" + modelData
                            iconRole: modelData
                            text: IconTheme.label(modelData)
                            checkable: true
                            checked: icons.chosen === modelData
                            focusPolicy: Qt.TabFocus
                            onClicked: {
                                icons.chosen = modelData
                                checked = Qt.binding(() => icons.chosen === modelData)
                            }
                        }
                    }
                }
            }
        }
        Label {
            objectName: "tagProblem"
            Layout.fillWidth: true
            visible: popover.touched && popover.problem.length > 0
            text: popover.problem
            color: Theme.danger
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Preview"); color: Theme.muted }
            TagButton {
                objectName: "tagPreview"
                text: tagName.text
                iconRole: icons.chosen
                tip: tagName.text.length > 0 ? tagName.text : tagText.text
                focusPolicy: Qt.NoFocus
            }
            // Save and Cancel in the platform's order, as every footer.
            DialogButtonBox {
                Layout.fillWidth: true
                padding: 0
                Button {
                    objectName: "saveTag"
                    text: qsTr("Save")
                    enabled: !popover.touched || popover.problem.length === 0
                    DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                }
                Button {
                    objectName: "cancelTag"
                    text: qsTr("Cancel")
                    DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                }
                onAccepted: popover.save()
                onRejected: popover.close()
            }
        }
    }
}
