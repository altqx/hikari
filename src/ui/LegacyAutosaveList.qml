import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// P9: the legacy app's autosaves (its Subs folder), read only, in "Open auto
// save" beside this application's own recovery: legacy AutoSaveOpen
// (AutoSaveOpen.cpp at 20d647c4). "Find files" with "Filter list" and "All
// words" (FindFiles), the Files list and the chosen file's Versions, newest
// first (SetSelection), and Open, which opens the chosen version as a new
// unsaved copy (approved L58-recovery-copy; legacy opened the autosave file
// itself). Nothing in the folder is changed.
ColumnLayout {
    id: list
    required property var app
    signal opened()
    property var files: []
    property var shown: []     // indices into files (the filter's result)
    property int selected: 0   // in shown
    readonly property var versions: shown.length > 0 && selected >= 0 && selected < shown.length
                                    ? files[shown[selected]].versions : []
    visible: files.length > 0

    function reload() {
        files = app.legacyAutosaves()
        seekingText.text = ""
        filter()
    }
    function filter() {
        shown = app.filterLegacyAutosaves(files, seekingText.text, seekAllWords.checked)
        selected = 0 // SetSelection(0, true)
        versionList.currentIndex = 0
    }

    Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        text: qsTr("Autosaves of the previous version (read only). Each opens as a new unsaved copy.")
    }
    GroupBox {
        title: qsTr("Find files")
        Layout.fillWidth: true
        RowLayout {
            anchors.fill: parent
            TextField {
                id: seekingText
                objectName: "legacyAutosaveFind"
                Layout.fillWidth: true
                Accessible.name: qsTr("Find files")
                onAccepted: list.filter() // Enter in the field
            }
            Button {
                objectName: "legacyAutosaveFilter"
                text: qsTr("Filter list")
                onClicked: list.filter()
            }
            CheckBox {
                id: seekAllWords
                objectName: "legacyAutosaveAllWords"
                text: qsTr("All words")
                checked: true
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        GroupBox {
            title: qsTr("Files")
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 3
            ListView {
                id: fileList
                objectName: "legacyAutosaveFiles"
                anchors.fill: parent
                clip: true
                model: list.shown
                currentIndex: list.selected
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Files")
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    objectName: "legacyAutosaveFile" + index
                    width: ListView.view.width
                    text: list.files[modelData].name
                    highlighted: ListView.isCurrentItem
                    onClicked: {
                        list.selected = index
                        versionList.currentIndex = 0
                    }
                }
            }
        }
        GroupBox {
            title: qsTr("Versions")
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            ListView {
                id: versionList
                objectName: "legacyAutosaveVersions"
                anchors.fill: parent
                clip: true
                model: list.versions
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Versions")
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    objectName: "legacyAutosaveVersion" + index
                    width: ListView.view.width
                    text: modelData.written
                    highlighted: ListView.isCurrentItem
                    onClicked: versionList.currentIndex = index
                }
            }
        }
    }
    Button {
        objectName: "legacyAutosaveOpen"
        Layout.alignment: Qt.AlignRight
        text: qsTr("Open")
        enabled: list.versions.length > 0 && versionList.currentIndex >= 0
        onClicked: {
            if (list.app.openLegacyAutosave(list.versions[versionList.currentIndex].file))
                list.opened()
        }
    }
}
