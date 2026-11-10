import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Hikari.Ui

Rectangle {
    id: group
    property var choices: []
    property int currentIndex: 0
    property bool wrapsLabels: false
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
        uniformCellSizes: true
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
                readonly property string iconRole: modelData.iconRole || ""
                implicitWidth: Math.ceil(Math.max(label.implicitWidth, boldText.implicitWidth)) + 20
                               + (iconRole.length > 0 ? glyph.size + content.spacing : 0)
                implicitHeight: label.implicitHeight + 10
                Accessible.role: Accessible.RadioButton
                Accessible.name: text
                Accessible.checkable: true
                Accessible.checked: checked
                ToolTip.visible: hovered && (modelData.tip || "").length > 0
                // ToolTip auto-detects rich text; word joiners after "<" keep the tip plain.
                ToolTip.text: (modelData.tip || "").replace(/</g, "<\u2060").replace(/&lt;/g, "&\u2060lt;")
                onClicked: {
                    if (index !== group.currentIndex)
                        group.activated(index)
                }
                background: Rectangle {
                    color: choice.checked ? Theme.select : choice.down ? Theme.raised
                         : choice.hovered && choice.enabled ? Color.transparent(Theme.raised, 0.6)
                         : "transparent"
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
                Label {
                    id: boldText
                    visible: false
                    textFormat: Text.PlainText
                    text: choice.text
                    font.bold: true
                }
                contentItem: Item {
                    RowLayout {
                        id: content
                        anchors.centerIn: parent
                        width: Math.min(implicitWidth, parent.width)
                        spacing: 6
                        Icon {
                            id: glyph
                            visible: choice.iconRole.length > 0
                            iconRole: choice.iconRole
                            enabled: choice.enabled
                        }
                        Label {
                            id: label
                            textFormat: Text.PlainText
                            text: choice.text
                            font.bold: choice.checked
                            color: choice.enabled ? Theme.text : Theme.disabled
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: group.wrapsLabels ? Text.Wrap : Text.NoWrap
                            maximumLineCount: group.wrapsLabels ? 2 : undefined
                            elide: Text.ElideRight
                            Layout.preferredWidth: Math.ceil(Math.max(implicitWidth, boldText.implicitWidth))
                            Layout.minimumWidth: 0
                        }
                    }
                }
            }
        }
    }
}
