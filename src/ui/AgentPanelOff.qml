pragma Translator: "HikariSub.Agent"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: off
    objectName: "agentPanelOff"
    signal turnOnRequested()
    implicitHeight: card.implicitHeight + 24

    Rectangle {
        id: card
        objectName: "agentAiOffCard"
        x: 12
        y: 12
        width: off.width - 24
        implicitHeight: column.implicitHeight + 16
        height: implicitHeight
        radius: Theme.popupRadius
        color: Theme.panel
        border.color: Theme.line
        border.width: 1
        ColumnLayout {
            id: column
            x: 12
            y: 8
            width: parent.width - 24
            spacing: 8
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                Icon { size: 14; iconRole: "ai-assistant" }
                Label {
                    objectName: "agentAiOffTitle"
                    text: qsTr("AI is off")
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
            }
            Label {
                objectName: "agentAiOffBody"
                text: qsTr("Turn it on to use agents and AI actions. HikariSub then downloads the AI component once.")
                color: Theme.muted
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            PrimaryButton {
                objectName: "agentTurnOnAi"
                text: qsTr("Turn on AI")
                onClicked: off.turnOnRequested()
            }
        }
    }
}
