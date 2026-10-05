import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// T1: the visual tool rail (accepted layout A on #55): legacy VideoToolbar's
// eleven families beside the Video canvas, in its order, with its help texts.
// One family is on; choosing it again goes back to the crosshair
// (VideoToolbar.cpp:205-233). For a Document that is not ASS the rail is
// disabled (VideoToolbar::DisableVisuals). A family whose tool has not
// landed yet (T2-T6) is selectable and draws nothing. K1: each family's
// button shows its icon of the set (legacy VideoToolbar's bitmaps, Cross.png
// to AllTags.png) beside its name.
Frame {
    id: rail
    objectName: "visualToolRail"
    required property VisualToolsController tools
    padding: 2
    implicitWidth: 132

    ScrollView {
        anchors.fill: parent
        clip: true
        ColumnLayout {
            width: rail.availableWidth
            spacing: 1
            Repeater {
                model: rail.tools.families
                delegate: ToolButton {
                    id: familyButton
                    required property var modelData
                    required property int index
                    objectName: "visualTool" + index
                    Layout.fillWidth: true
                    text: modelData.name
                    checked: rail.tools.activeFamily === index
                    enabled: rail.tools.railEnabled
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: 11
                    contentItem: RowLayout {
                        spacing: 4
                        opacity: familyButton.modelData.available ? 1 : 0.7
                        Icon {
                            objectName: "visualToolIcon" + familyButton.index
                            // In application::visual::Family's order (on one line: icon_tests reads the roles from it).
                            iconRole: ["tool-crosshair", "tool-position", "tool-move", "tool-scale", "tool-rotate-z", "tool-rotate-xy", "tool-clip-rect", "tool-clip-vector", "tool-drawing", "tool-move-all", "tool-all-tags"][familyButton.index] ?? ""
                            hovered: familyButton.hovered
                            pressed: familyButton.down
                        }
                        Label {
                            Layout.fillWidth: true
                            text: familyButton.text
                            font: familyButton.font
                            elide: Text.ElideRight
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: modelData.name
                    Accessible.name: modelData.name
                    onClicked: rail.tools.selectFamily(index)
                }
            }
        }
    }
}
