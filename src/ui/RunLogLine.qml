// AD3 (#312): one line of a macro run's log (AutomationRunLines), on the
// progress card and in the Automation panel's Runs: its time since the run
// started when `times` is on, the level the script gave (error and fatal in
// the danger colour, warning in the warning colour, the word always shown),
// then the text in the monospace font. HikariSub's own lines (the run's
// error, how it ended) carry an icon in the level's place, in the
// interface font. The text is the script's: plain, never markup.
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

RowLayout {
    id: line
    required property int level
    required property string levelName
    required property string text
    required property string time
    property bool times: false
    property font font
    readonly property bool host: level <= -2
    spacing: 8
    Accessible.role: Accessible.ListItem
    Accessible.name: [levelName, text].filter(t => t.length > 0).join(": ")

    Label {
        visible: line.times
        text: line.time
        color: Theme.muted
        font.family: line.font.family
        font.pixelSize: line.font.pixelSize
        font.features: { "tnum": 1 }
        Layout.alignment: Qt.AlignTop
    }
    Item {
        implicitWidth: levelMetrics.advanceWidth("warning") + 4
        implicitHeight: levelLabel.implicitHeight
        Layout.alignment: Qt.AlignTop
        Label {
            id: levelLabel
            visible: !line.host
            text: line.levelName
            color: line.level === 0 || line.level === 1 ? Theme.danger : line.level === 2 ? Theme.warning : Theme.muted
            font: levelMetrics.font
            anchors.verticalCenter: parent.verticalCenter
        }
        Icon {
            visible: line.host
            size: 14
            iconRole: line.level === -2 ? "notice-error" : "about"
            color: line.level === -2 ? Theme.danger : IconTheme.normal
            accentColor: line.level === -2 ? Theme.danger : IconTheme.accent
            anchors.verticalCenter: parent.verticalCenter
        }
        FontMetrics {
            id: levelMetrics
            font.family: line.font.family
            font.pixelSize: Math.max(9, line.font.pixelSize - 1)
        }
    }
    Label {
        objectName: "runLogText"
        text: line.text
        textFormat: Text.PlainText
        wrapMode: Text.WrapAnywhere
        font.family: line.host ? Qt.application.font.family : line.font.family
        font.pixelSize: line.font.pixelSize
        Layout.fillWidth: true
    }
}
