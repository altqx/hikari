pragma Translator: "HikariSub.TemplateEditor"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

// KE2: while the Template editor's preview is on the video, a badge in the
// picture's top left corner says so: the frame draws the current Line's
// preview in place of its generated Lines, which Apply has not written. After
// a problem the preview kept is the last run that worked, in the warning
// colour. It takes no input: the pointer goes through to the view.
Rectangle {
    id: badge
    objectName: "videoTemplateBadge"
    required property TemplateEditorController controller
    implicitWidth: row.implicitWidth + 12
    implicitHeight: row.implicitHeight + 6
    radius: 3
    color: Theme.panel
    border.width: 1
    border.color: controller.previewKept ? Theme.warning : Theme.line
    Accessible.role: Accessible.StaticText
    Accessible.name: label.text.replace(/<[^>]*>/g, "")

    Row {
        id: row
        x: 6
        y: 3
        spacing: 6
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            iconRole: badge.controller.previewKept ? "problem" : "template-editor"
            color: badge.controller.previewKept ? Theme.warning : IconTheme.normal
            accentColor: badge.controller.previewKept ? Theme.warning : IconTheme.accent
        }
        Label {
            id: label
            objectName: "videoTemplateBadgeText"
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.StyledText
            //: Over the video: the Template editor's preview of Line %1, %2 "not applied" or "last run that worked"
            text: qsTr("<b>Preview</b> · Line %1 · %2").arg(badge.controller.sourceNumber)
                  .arg(badge.controller.previewKept ? qsTr("last run that worked") : qsTr("not applied"))
        }
    }
}
