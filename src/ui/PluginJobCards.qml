pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Plugins"
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: cards
    objectName: "pluginJobCards"
    property var runtime: null
    readonly property var jobs: runtime ? runtime.jobs : []
    readonly property bool shown: jobs.length > 0
    visible: shown
    spacing: 8

    ListModel {
        id: shownJobs
    }
    function sync() {
        for (let i = shownJobs.count - 1; i >= 0; --i)
            if (!jobs.some(j => j.plugin === shownJobs.get(i).plugin))
                shownJobs.remove(i)
        jobs.forEach((j, at) => {
            let i = 0
            while (i < shownJobs.count && shownJobs.get(i).plugin !== j.plugin)
                ++i
            if (i === shownJobs.count)
                shownJobs.insert(at, { plugin: j.plugin })
            else if (i !== at)
                shownJobs.move(i, at, 1)
        })
    }
    onJobsChanged: sync()
    Component.onCompleted: sync()

    Repeater {
        model: shownJobs
        FocusScope {
            id: card
            required property string plugin
            readonly property var job: cards.jobs.find(j => j.plugin === plugin) ?? ({})
            objectName: "pluginJobCard_" + plugin
            Layout.fillWidth: true
            implicitWidth: 360
            implicitHeight: body.implicitHeight + 18
            Accessible.role: Accessible.Grouping
            Accessible.name: title.text

            PopoverBackground { anchors.fill: parent }
            ColumnLayout {
                id: body
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                anchors.topMargin: 10
                spacing: 6
                RowLayout {
                    spacing: 10
                    Layout.fillWidth: true
                    Icon {
                        iconRole: "run-busy"
                        accentColor: IconTheme.accent
                    }
                    Label {
                        id: title
                        objectName: "pluginJobTitle"
                        //: A plugin's background job: the plugin's name, then what the job does
                        text: qsTr("%1: %2").arg(card.job.name, card.job.title)
                        textFormat: Text.PlainText
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: card.job.fraction >= 0
                        text: Math.round(card.job.fraction * 100) + " %"
                        color: Theme.muted
                        font.features: { "tnum": 1 }
                        Layout.rightMargin: 4
                    }
                }
                Label {
                    objectName: "pluginJobText"
                    visible: text.length > 0
                    text: card.job.cancelling ? qsTr("Cancelling…") : card.job.text
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    Layout.leftMargin: 26
                }
                T.ProgressBar {
                    id: bar
                    objectName: "pluginJobBar"
                    from: 0
                    to: 1
                    value: Math.max(0, card.job.fraction)
                    indeterminate: card.job.fraction < 0
                    implicitHeight: 6
                    implicitWidth: 60
                    Layout.fillWidth: true
                    Layout.leftMargin: 26
                    Layout.rightMargin: 4
                    Layout.topMargin: 2
                    Accessible.name: qsTr("Progress")
                    background: Rectangle {
                        color: Theme.raised
                        border.color: Theme.line
                        radius: 1
                    }
                    contentItem: Item {
                        clip: true
                        Rectangle {
                            id: fill
                            objectName: "pluginJobFill"
                            height: parent.height
                            color: Theme.accent
                            property real sweep: 0
                            width: bar.indeterminate ? parent.width * 0.3 : parent.width * bar.position
                            x: bar.indeterminate ? sweep : 0
                            NumberAnimation on sweep {
                                running: bar.indeterminate && bar.visible
                                from: -fill.width
                                to: bar.width
                                duration: 1200
                                loops: Animation.Infinite
                            }
                        }
                    }
                }
                Label {
                    objectName: "pluginJobStuck"
                    visible: card.job.stuck
                    text: qsTr("The plugin hasn’t stopped. Stop ends it now; work it hasn’t applied is lost.")
                    color: Theme.warning
                    wrapMode: Text.Wrap
                    font.pixelSize: Math.max(10, Qt.application.font.pixelSize - 1)
                    Layout.fillWidth: true
                    Layout.leftMargin: 26
                }
                RowLayout {
                    spacing: 6
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    Item { Layout.fillWidth: true }
                    DangerButton {
                        objectName: "pluginJobStop"
                        visible: card.job.stuck
                        text: qsTr("Stop")
                        onClicked: cards.runtime.forceStop(card.job.plugin)
                    }
                    IconTextButton {
                        objectName: "pluginJobCancel"
                        visible: !card.job.stuck
                        iconRole: "run-cancelled"
                        enabled: !card.job.cancelling
                        text: card.job.cancelling ? qsTr("Cancelling…") : qsTr("Cancel")
                        onClicked: cards.runtime.cancelJob(card.job.plugin)
                    }
                }
            }
        }
    }
}
