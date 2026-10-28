// TS1: a row of mutually exclusive choices that fill the width evenly
// (MuseScore 4's FlatRadioButton groups, docs/research/musescore-shell.md §5),
// for the two-way choices of a dock form (Time/Frames, Forward/Backward).
// Each choice is a radio button for assistive technology; the chosen one is
// filled in `select` with an accent line under it and bold text. Tab
// reaches each choice, Space or a click chooses it, and the keyboard focus
// ring is the style's, 1 wide in the text colour (FocusRing).
// `choices`: [{text, name, tip, enabled}], `name` the button's objectName.
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Hikari.Ui

Rectangle {
    id: group
    property var choices: []
    property int currentIndex: 0
    // A choice made by the user (not a change of currentIndex).
    signal activated(int index)

    implicitWidth: row.implicitWidth + 2
    implicitHeight: Math.max(30, row.implicitHeight + 2)
    color: "transparent"
    border.width: 1
    border.color: enabled ? Theme.control : Theme.line
    Accessible.role: Accessible.Grouping

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0
        uniformCellSizes: true // the choices share the width evenly
        Repeater {
            model: group.choices
            T.AbstractButton {
                id: choice
                required property var modelData
                required property int index
                objectName: modelData.name || ""
                text: modelData.text
                enabled: modelData.enabled === undefined || modelData.enabled
                checkable: false
                checked: index === group.currentIndex
                focusPolicy: Qt.StrongFocus
                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitWidth: label.implicitWidth + 20
                implicitHeight: label.implicitHeight + 10
                Accessible.role: Accessible.RadioButton
                Accessible.name: text
                Accessible.checkable: true
                Accessible.checked: checked
                ToolTip.visible: hovered && (modelData.tip || "").length > 0
                ToolTip.text: modelData.tip || ""
                onClicked: {
                    if (index !== group.currentIndex)
                        group.activated(index)
                }
                background: Rectangle {
                    color: choice.checked ? Theme.select : choice.down ? Theme.raised
                         : choice.hovered && choice.enabled ? Qt.rgba(Theme.raised.r, Theme.raised.g, Theme.raised.b, 0.6)
                         : "transparent"
                    // The separator before every choice but the first.
                    Rectangle {
                        visible: choice.index > 0
                        width: 1
                        height: parent.height
                        x: choice.mirrored ? parent.width - 1 : 0
                        color: group.enabled ? Theme.control : Theme.line
                    }
                    Rectangle {
                        visible: choice.checked
                        width: parent.width
                        height: 2
                        anchors.bottom: parent.bottom
                        color: Theme.accent
                    }
                    FocusRing { control: choice }
                }
                contentItem: Label {
                    id: label
                    text: choice.text
                    font.bold: choice.checked
                    color: choice.enabled ? Theme.text : Theme.disabled
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
    }
}
