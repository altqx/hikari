pragma Translator: "HikariSub.NodeEffects"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Rectangle {
    id: badge
    objectName: "videoNodeBadge"
    required property NodeEffectsController controller
    readonly property bool warns: controller.previewKept || controller.previewBlocked.length > 0
                                  || controller.previewResolution.width > 0
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
            textFormat: Text.RichText
            color: badge.warns ? Theme.muted : Theme.text
            readonly property string title: "<span style=\"font-weight:600\">" + qsTr("Preview") + "</span>"
            text: badge.controller.previewBlocked === "noSource"
                  //: Over the video: the node effects' preview has no source line chosen, or none at the playhead; %1 is "Preview"
                  ? qsTr("%1 · no source line here").arg(title)
                  : badge.controller.previewBlocked === "untrusted"
                  //: Over the video: the node effects' preview waits until the file's Lua is trusted; %1 is "Preview"
                  ? qsTr("%1 off until you trust this file").arg(title)
                  : badge.controller.previewBlocked === "problems"
                  //: Over the video: the graph has problems and no earlier run worked; %1 is "Preview"
                  ? qsTr("%1 · fix the problems to see it").arg(title)
                  : badge.controller.previewResolution.width > 0
                  //: Over the video: the file has no PlayRes, and Apply writes this size with the lines; %1 is "Preview", %2×%3 the size
                  ? qsTr("%1 · no script resolution · Apply sets %2×%3").arg(title, badge.controller.previewResolution.width,
                                                                          badge.controller.previewResolution.height)
                  //: Over the video: %1 is "Preview", then the node effects' preview of Line %2, %3 "not applied" or "last run that worked"
                  : qsTr("%1 · line %2 · %3").arg(title, badge.controller.sourceNumber,
                        badge.controller.previewKept ? qsTr("last run that worked") : qsTr("not applied"))
        }
    }
}
