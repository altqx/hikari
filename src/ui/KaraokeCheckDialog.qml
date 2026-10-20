// KA7: the Karaoke check window (Subtitles > Check karaoke,
// GLOBAL_CHECK_KARAOKE), modeless like the Spellchecker. It lists the editing
// target's karaoke problems (Application::checkKaraoke): unclosed syllables,
// syllables that don't add up to the Line's duration, zero-length syllables
// and syllables outside the Line. Choosing a row selects its Line; Re-check
// reads the Document again.
pragma Translator: "HikariSub.Karaoke"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Dialog {
    id: dialog
    objectName: "karaokeCheckDialog"
    required property var app
    title: qsTr("Karaoke check")
    header: IconDialogHeader { objectName: "karaokeCheckDialogTitle"; iconRole: "karaoke"; text: dialog.title }
    modal: false
    property var problems: []

    function recheck() {
        problems = app.checkKaraoke()
        problemList.currentIndex = -1
    }
    function openDialog() {
        open()
        recheck()
        problemList.forceActiveFocus()
    }
    // A row's Line, selected as a click in the Grid selects it. A Line that
    // has gone since the check (an edit, another tab) checks again instead.
    function choose(index) {
        if (index < 0 || index >= problems.length)
            return
        if (!app.showKaraokeLine(problems[index].line))
            recheck()
    }

    ColumnLayout {
        anchors.fill: parent
        Frame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: 560
            Layout.minimumHeight: 260
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
                // The arrow keys move through the rows and select each Line.
                onCurrentIndexChanged: if (activeFocus) dialog.choose(currentIndex)
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
                        if (problemList.currentIndex === index)
                            dialog.choose(index)
                        else
                            problemList.currentIndex = index
                    }
                }
                Keys.onReturnPressed: dialog.choose(currentIndex)
                Keys.onEnterPressed: dialog.choose(currentIndex)
                Keys.onEscapePressed: dialog.close()
            }
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            Button { objectName: "karaokeRecheck"; text: qsTr("Re-check"); onClicked: dialog.recheck() }
            Button { objectName: "karaokeClose"; text: qsTr("Close"); onClicked: dialog.close() }
        }
    }
}
