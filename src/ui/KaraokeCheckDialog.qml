pragma Translator: "HikariSub.Karaoke"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "karaokeCheckDialog"
    required property var app
    title: qsTr("Karaoke check")
    dialogId: "karaokeCheck"
    resizable: true
    initialFocus: problemList
    property var problems: []
    property real unlisted: 0

    function recheck() {
        const checked = app.checkKaraoke()
        problems = checked.rows
        unlisted = checked.unlisted
        problemList.currentIndex = -1
    }
    function openDialog() {
        open()
        recheck()
        problemList.forceActiveFocus()
    }
    function choose(index) {
        if (index < 0 || index >= problems.length)
            return
        if (!app.showKaraokeLine(problems[index].document, problems[index].line))
            recheck()
    }

    Frame {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredWidth: 640
        Layout.preferredHeight: 320
        padding: 1
        background: Rectangle { color: Theme.field; border.color: Theme.line }
        Label {
            anchors.centerIn: parent
            objectName: "karaokeCheckEmpty"
            visible: problemList.count === 0
            color: Theme.muted
            text: qsTr("No karaoke problems found")
        }
        ListView {
            id: problemList
            objectName: "karaokeProblems"
            anchors.fill: parent
            clip: true
            model: dialog.problems
            keyNavigationEnabled: true
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Karaoke problems")
            // MUST KILL: clicking stops onCurrentIndexChanged choosing the row a click chooses itself
            property bool clicking: false
            onCurrentIndexChanged: if (activeFocus && !clicking) dialog.choose(currentIndex)
            header: RowLayout {
                visible: problemList.count > 0
                width: ListView.view.width
                height: visible ? implicitHeight + 8 : 0
                spacing: 12
                Label { text: qsTr("Line"); color: Theme.muted; Layout.preferredWidth: 48; Layout.leftMargin: 12 }
                Label { text: qsTr("Check"); color: Theme.muted; Layout.preferredWidth: 190 }
                Label { text: qsTr("Detail"); color: Theme.muted; Layout.fillWidth: true }
            }
            delegate: ItemDelegate {
                required property var modelData
                required property int index
                width: ListView.view.width
                highlighted: ListView.isCurrentItem
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("Line %1: %2. %3").arg(modelData.number).arg(modelData.check).arg(modelData.detail)
                contentItem: RowLayout {
                    spacing: 12
                    Label { text: modelData.number; Layout.preferredWidth: 48 }
                    Label { text: modelData.check; Layout.preferredWidth: 190; elide: Text.ElideRight }
                    Label { text: modelData.detail; Layout.fillWidth: true; elide: Text.ElideRight }
                }
                onClicked: {
                    problemList.forceActiveFocus()
                    problemList.clicking = true
                    problemList.currentIndex = index
                    problemList.clicking = false
                    dialog.choose(index)
                }
            }
            Keys.onReturnPressed: dialog.choose(currentIndex)
            Keys.onEnterPressed: dialog.choose(currentIndex)
        }
    }
    Label {
        objectName: "karaokeUnlisted"
        visible: dialog.unlisted > 0
        color: Theme.muted
        text: dialog.unlisted === 1 ? qsTr("1 more problem not listed; fix these and check again")
                                    : qsTr("%n more problems not listed; fix these and check again", "", dialog.unlisted)
        Layout.fillWidth: true
        elide: Text.ElideRight
    }

    leadingActions: Button { objectName: "karaokeRecheck"; text: qsTr("Re-check"); onClicked: dialog.recheck() }
    buttons: Button {
        objectName: "karaokeClose"
        text: qsTr("Close")
        DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        onClicked: dialog.close()
    }
}
