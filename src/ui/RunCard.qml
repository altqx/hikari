pragma Translator: "HikariSub.Export"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: card
    required property string iconRole
    required property string title
    property string doing: ""
    // 0 to 1, or below 0 while the run doesn't know how far it is.
    property real progress: -1
    property bool running: false
    property bool canStop: true
    property bool hidden: false
    readonly property bool shown: running && !hidden
    signal stopRequested()
    signal openRequested()

    onRunningChanged: if (!running) hidden = false
    visible: shown
    implicitWidth: 340
    implicitHeight: body.implicitHeight + 2 * 12
    Accessible.role: Accessible.Grouping
    Accessible.name: title

    PopoverBackground { anchors.fill: parent }
    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 4
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        spacing: 8
        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            Icon { iconRole: card.iconRole; size: 16 }
            Label {
                objectName: card.objectName + "Title"
                text: card.title
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            IconToolButton {
                objectName: card.objectName + "Hide"
                iconRole: "hide"
                text: qsTr("Hide")
                onClicked: card.hidden = true
            }
        }
        Label {
            objectName: card.objectName + "Doing"
            visible: text.length > 0
            text: card.doing
            color: Theme.muted
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.rightMargin: 8
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.rightMargin: 8
            implicitHeight: 2
            color: Theme.line
            Rectangle {
                height: parent.height
                width: card.progress < 0 ? parent.width : parent.width * Math.min(1, card.progress)
                color: Theme.accent
            }
        }
        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button {
                objectName: card.objectName + "Stop"
                visible: card.canStop
                flat: true
                text: qsTr("Stop")
                onClicked: card.stopRequested()
            }
            Button {
                objectName: card.objectName + "Open"
                flat: true
                text: qsTr("Open")
                onClicked: card.openRequested()
            }
        }
    }
}
