import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// T1: the visual tool rail (accepted layout A on #55): legacy VideoToolbar's
// eleven families beside the Video canvas, in its order, with its help texts.
// One family is on; choosing it again goes back to the crosshair
// (VideoToolbar.cpp:205-233). For a Document that is not ASS the rail is
// disabled (VideoToolbar::DisableVisuals). A family whose tool has not
// landed yet (T2-T6) is selectable and draws nothing.
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
                    required property var modelData
                    required property int index
                    objectName: "visualTool" + index
                    Layout.fillWidth: true
                    text: modelData.name
                    checked: rail.tools.activeFamily === index
                    enabled: rail.tools.railEnabled
                    focusPolicy: Qt.NoFocus
                    font.pixelSize: 11
                    contentItem: Label {
                        text: parent.text
                        font: parent.font
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                        opacity: modelData.available ? 1 : 0.7
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
