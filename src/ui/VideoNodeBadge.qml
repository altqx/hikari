pragma Translator: "HikariSub.NodeEffects"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Rectangle {
    id: badge
    objectName: "videoNodeBadge"
    required property NodeEffectsController controller
    readonly property bool warns: controller.previewKept || controller.previewBlocked.length > 0
    implicitWidth: row.implicitWidth + 12
    implicitHeight: row.implicitHeight + 6
    radius: 3
    color: Theme.panel
    border.width: 1
    border.color: warns ? Theme.warning : Theme.line
    Accessible.role: Accessible.StaticText
    Accessible.name: label.text.replace(/<[^>]*>/g, "")

    Row {
        id: row
        x: 6
        y: 3
        spacing: 6
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            iconRole: badge.warns ? "problem" : "node-editor"
            color: badge.warns ? Theme.warning : IconTheme.normal
            accentColor: badge.warns ? Theme.warning : IconTheme.accent
        }
        Label {
            id: label
            objectName: "videoNodeBadgeText"
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.StyledText
            color: badge.warns ? Theme.muted : Theme.text
            text: badge.controller.previewBlocked === "noSource"
                  //: Over the video: the node effects' preview has no source line chosen, or none at the playhead
                  ? qsTr("<b>Preview</b> · no source line here")
                  : badge.controller.previewBlocked === "untrusted"
                  //: Over the video: the node effects' preview waits until the file's Lua is trusted
                  ? qsTr("Preview off until you trust this file")
                  : badge.controller.previewBlocked === "problems"
                  //: Over the video: the graph has problems and no earlier run worked
                  ? qsTr("<b>Preview</b> · fix the problems to see it")
                  //: Over the video: the node effects' preview of Line %1, %2 "not applied" or "last run that worked"
                  : qsTr("<b>Preview</b> · line %1 · %2").arg(badge.controller.sourceNumber,
                        badge.controller.previewKept ? qsTr("last run that worked") : qsTr("not applied"))
        }
    }
}
