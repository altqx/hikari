import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// T1: the visual tool rail (accepted layout A on #55): legacy VideoToolbar's
// eleven families beside the Video canvas, in its order, with its help texts.
// One family is on; choosing it again goes back to the crosshair
// (VideoToolbar.cpp:205-233). For a Document that is not ASS the rail is
// disabled (VideoToolbar::DisableVisuals). A family whose tool has not
// landed yet (T2-T6) is selectable and draws nothing; its tooltip says so.
// The rail is an icon-only tool strip (visual-language.md, "Tool strips"):
// each family's button shows its icon of the K1 set (legacy VideoToolbar's
// bitmaps, Cross.png to AllTags.png), the family's name is its tooltip and
// accessible name, and the on family is the style's checked button. The
// buttons take keyboard focus by Tab (not by a click, which leaves it with
// the video), Up and Down move between them, Space chooses.
Frame {
    id: rail
    objectName: "visualToolRail"
    required property VisualToolsController tools
    padding: 2
    implicitWidth: 28 + leftPadding + rightPadding

    ScrollView {
        anchors.fill: parent
        clip: true
        ScrollBar.vertical.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: rail.availableWidth
            spacing: 1
            Repeater {
                id: families
                model: rail.tools.families
                delegate: ToolButton {
                    id: familyButton
                    required property var modelData
                    required property int index
                    objectName: "visualTool" + index
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    Layout.alignment: Qt.AlignHCenter
                    text: modelData.name
                    display: AbstractButton.IconOnly
                    checked: rail.tools.activeFamily === index
                    enabled: rail.tools.railEnabled
                    focusPolicy: Qt.TabFocus
                    contentItem: Icon {
                        objectName: "visualToolIcon" + familyButton.index
                        // In application::visual::Family's order (on one line: icon_tests reads the roles from it).
                        iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags"][familyButton.index] ?? ""
                        hovered: familyButton.hovered
                        pressed: familyButton.down
                    }
                    ToolTip.visible: hovered || visualFocus
                    ToolTip.text: modelData.available ? modelData.name
                                                      : qsTr("%1 (not available yet)").arg(modelData.name)
                    Accessible.name: modelData.name
                    Accessible.checkable: true
                    Keys.onUpPressed: rail.focusFamily(index - 1)
                    Keys.onDownPressed: rail.focusFamily(index + 1)
                    onClicked: rail.tools.selectFamily(index)
                }
            }
        }
    }

    function focusFamily(i) {
        const button = families.itemAt((i + families.count) % families.count)
        if (button)
            button.forceActiveFocus(Qt.TabFocusReason)
    }
}
