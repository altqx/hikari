pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: history
    objectName: "historyWindow"
    required property var editor
    property QtObject review: null
    property string documentName: ""
    property var target: null
    property var selectedFor: null
    onTargetChanged: {
        if (target !== selectedFor && visible)
            historyList.currentIndex = editor.historyCursor
        selectedFor = target
    }
    dialogId: "history"
    title: qsTr("History")
    resizable: true
    defaultButton: go
    initialFocus: historyList
    Component.onCompleted: IconTheme.setWindowIcon(history, "history")
    onVisibleChanged: if (visible) historyList.currentIndex = editor.historyCursor

    Label {
        objectName: "historyDocument"
        text: history.documentName
        visible: text.length > 0
        font.bold: true
        elide: Text.ElideMiddle
        Layout.fillWidth: true
        Layout.maximumWidth: 332
    }
    Label {
        objectName: "historyCount"
        text: history.editor.history.length === 1 ? qsTr("1 step. Steps after the current one can be redone.")
                                                  : qsTr("%n steps. Steps after the current one can be redone.", "",
                                                         history.editor.history.length)
        color: Theme.muted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    Frame {
        padding: 1
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 332
        Layout.preferredHeight: 300
        ListView {
            id: historyList
            objectName: "historyList"
            anchors.fill: parent
            clip: true
            focus: true
            model: history.editor.history
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            Accessible.role: Accessible.List
            Accessible.name: history.title
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: step
                required property int index
                required property string modelData
                readonly property bool current: index === history.editor.historyCursor
                readonly property bool undone: index > history.editor.historyCursor
                readonly property bool reviewed: history.review !== null && history.review.on
                readonly property bool ai: reviewed && history.review.isAiStep(index)
                readonly property int turnBefore: reviewed ? history.review.turnBefore(index) : 0
                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                Accessible.name: current ? qsTr("%1, current").arg(modelData) : modelData
                contentItem: ColumnLayout {
                    spacing: 2
                    RowLayout {
                        objectName: "historyCheckpoint"
                        visible: step.turnBefore > 0
                        spacing: 6
                        Layout.fillWidth: true
                        Icon {
                            iconRole: "checkpoint"
                            size: 12
                            color: Theme.muted
                            accentColor: Theme.muted
                        }
                        Label {
                            text: qsTranslate("HikariSub.Review", "Before turn %1").arg(step.turnBefore)
                            color: Theme.muted
                            font: history.fontAt(11)
                        }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.line }
                    }
                    RowLayout {
                        spacing: 10
                        Label {
                            text: step.index + 1
                            color: step.undone ? Theme.disabled : Theme.muted
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 22
                        }
                        Icon {
                            objectName: "historyAiStep"
                            visible: step.ai
                            iconRole: "ai-assistant"
                            size: 14
                            color: step.undone ? Theme.disabled : IconTheme.normal
                            accentColor: step.undone ? Theme.disabled : IconTheme.accent
                            Accessible.name: qsTranslate("HikariSub.Review", "AI change")
                        }
                        Label {
                            text: step.modelData
                            font.bold: step.current
                            color: step.undone ? Theme.disabled : Theme.text
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            objectName: "historyCurrentTag"
                            visible: step.current
                            text: qsTr("Current")
                            color: Theme.accent
                            font: history.fontAt(11)
                            leftPadding: 6
                            rightPadding: 6
                            background: Rectangle { color: "transparent"; border.color: Theme.accent; radius: 3 }
                        }
                    }
                }
                onClicked: historyList.currentIndex = index
                onDoubleClicked: history.editor.goToHistory(index)
            }
            Keys.onReturnPressed: history.editor.goToHistory(currentIndex)
            Keys.onEnterPressed: history.editor.goToHistory(currentIndex)
        }
    }

    buttons: [
        PrimaryButton {
            id: go
            objectName: "historyGo"
            text: qsTr("Go to step")
            enabled: historyList.currentIndex >= 0
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: history.editor.goToHistory(historyList.currentIndex)
        },
        Button {
            objectName: "historyClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: history.close()
        }
    ]
}
