pragma Translator: "HikariSub.Review"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Pane {
    id: strip
    required property QtObject review
    readonly property var change: review ? review.current : ({})
    readonly property bool waiting: change.count !== undefined
    property bool confirming: false
    signal undoRequested()
    signal keepRequested()
    visible: waiting
    onChangeChanged: confirming = false
    padding: 6
    leftPadding: 10
    objectName: "reviewStrip"
    background: Rectangle {
        color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.12)
        border.color: Qt.rgba(Theme.success.r, Theme.success.g, Theme.success.b, 0.45)
        radius: 4
    }
    Accessible.role: Accessible.Grouping
    Accessible.name: title.text
    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            spacing: 8
            Layout.fillWidth: true
            Icon {
                iconRole: "ai-assistant"
                size: 16
                color: IconTheme.normal
                accentColor: IconTheme.accent
                Layout.alignment: Qt.AlignVCenter
            }
            Label {
                id: title
                objectName: "reviewStripTitle"
                font.bold: true
                textFormat: Text.PlainText
                text: strip.waiting ? qsTr("Changed by %1").arg(strip.change.caller) : ""
            }
            Label {
                objectName: "reviewStripName"
                Layout.fillWidth: true
                elide: Text.ElideRight
                textFormat: Text.PlainText
                text: strip.waiting ? strip.change.name : ""
            }
            IconTextButton {
                objectName: "reviewStripUndo"
                iconRole: "undo"
                text: qsTr("Undo")
                tip: qsTr("Undo AI change")
                onClicked: strip.undoRequested()
            }
            IconTextButton {
                objectName: "reviewStripKeep"
                iconRole: "review-keep"
                text: qsTr("Keep")
                tip: qsTr("Keep AI change")
                highlighted: true
                onClicked: strip.keepRequested()
            }
            IconToolButton {
                objectName: "reviewStripPrevious"
                iconRole: "chevron-left"
                text: qsTr("Previous AI change")
                onClicked: strip.review.step(-1)
            }
            Label {
                objectName: "reviewStripPosition"
                color: Theme.muted
                text: strip.waiting ? qsTr("%1 of %2").arg(strip.change.index).arg(strip.change.count) : ""
            }
            IconToolButton {
                objectName: "reviewStripNext"
                iconRole: "chevron-right"
                text: qsTr("Next AI change")
                onClicked: strip.review.step(1)
            }
        }
        Label {
            objectName: "reviewStripDiff"
            visible: strip.waiting && !strip.change.inserted && strip.change.before.length > 0
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            textFormat: Text.RichText
            text: strip.waiting ? strip.change.before : ""
        }
        Label {
            objectName: "reviewStripNew"
            visible: strip.waiting && strip.change.inserted
            color: Theme.muted
            text: qsTr("The AI added this Line.")
        }
        RowLayout {
            objectName: "reviewStripConfirm"
            visible: strip.confirming
            spacing: 8
            Layout.fillWidth: true
            Icon {
                iconRole: "notice-warning"
                size: 16
                color: Theme.warning
                accentColor: Theme.warning
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("You changed this Line after the AI did. Undo puts it back as it was before the AI, which drops your edit.")
            }
            Button {
                objectName: "reviewStripUndoAnyway"
                text: qsTr("Undo anyway")
                onClicked: {
                    strip.confirming = false
                    strip.review.undoLine(true)
                }
            }
            Button {
                text: qsTr("Cancel")
                onClicked: strip.confirming = false
            }
        }
    }
}
