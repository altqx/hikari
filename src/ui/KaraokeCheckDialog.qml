// KA7: the Karaoke check window (Subtitles > Check karaoke,
// GLOBAL_CHECK_KARAOKE), modeless like the Spellchecker. It lists the editing
// target's karaoke problems (Application::checkKaraoke): unclosed syllables,
// syllables that don't add up to the Line's duration, zero-length syllables
// and syllables outside the Line, the first thousand and how many more.
// Choosing a row selects its Line; Re-check reads the Document again.
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
    // A row's Line, selected as a click in the Grid selects it. When the
    // editing target is another Document now, the Line has gone since the
    // check, or the editor keeps its Line (a draft it cannot commit, which
    // it reports), the window checks again instead.
    function choose(index) {
        if (index < 0 || index >= problems.length)
            return
        if (!app.showKaraokeLine(problems[index].document, problems[index].line))
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
            Label {
                objectName: "karaokeUnlisted"
                visible: dialog.unlisted > 0
                color: Theme.muted
                text: dialog.unlisted === 1 ? qsTr("1 more problem not listed; fix these and check again")
                                            : qsTr("%n more problems not listed; fix these and check again", "", dialog.unlisted)
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Item { Layout.fillWidth: true; visible: dialog.unlisted === 0 }
            Button { objectName: "karaokeRecheck"; text: qsTr("Re-check"); onClicked: dialog.recheck() }
            Button { objectName: "karaokeClose"; text: qsTr("Close"); onClicked: dialog.close() }
        }
    }
}
