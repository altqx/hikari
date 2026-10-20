pragma Translator: "HikariSub.Audio"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// AV1 (#238): the audio panel's output device menu, from its speaker button.
// The entries are Settings > Audio's device choice (H2): System default, the
// connected devices and a chosen device that is not connected, the stored
// choice checked; choosing one stores it as the dialog's OK does, so the
// outputs move at once. "Audio settings…" opens the dialog on its Audio page.
//
// AU1 (#267): given the audio box's controller (`audio`), the menu is the
// speaker's popover: the volume slider that left the panel's slider column
// above the devices, with a note while the volume is linked to the vertical
// stretch, and the devices under their heading.
ShellMenu {
    id: menu
    required property var app
    property var audio: null
    property var rows: []
    // the items before the devices' (the volume, its note and the heading)
    readonly property int firstDevice: 3
    signal settingsRequested()
    onAboutToShow: rows = app.audioOutputMenu()
    AudioSliderRow {
        visible: menu.audio !== null
        height: visible ? implicitHeight : 0
        sliderName: "audioVolume"
        text: qsTr("Volume")
        from: 1
        to: 100
        value: menu.audio ? menu.audio.volume : 50
        readout: Math.round(value)
        onMoved: v => menu.audio.setVolume(v)
    }
    Label {
        objectName: "audioVolumeLinked"
        visible: menu.audio !== null && menu.audio.linked
        height: visible ? implicitHeight : 0
        leftPadding: 98
        bottomPadding: 4
        color: Theme.muted
        font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85)
        text: qsTr("Linked to vertical stretch")
    }
    Item {
        visible: menu.audio !== null
        width: parent ? parent.width : implicitWidth
        implicitWidth: heading.implicitWidth + 24
        implicitHeight: visible ? heading.implicitHeight + 10 : 0
        height: implicitHeight
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 1
            color: Theme.line
        }
        RowLayout {
            id: heading
            anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter; verticalCenterOffset: 2 }
            spacing: 6
            Icon {
                iconRole: "audio-output"
            }
            Label {
                text: qsTr("Output device")
                color: Theme.muted
                font.bold: true
            }
        }
    }
    Instantiator {
        model: menu.rows
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: "audioOutput" + index
            text: modelData.label
            checkable: true
            checked: modelData.checked
            keepsMenuOpen: false
            onTriggered: {
                menu.app.chooseAudioOutput(modelData.id, modelData.name)
                menu.rows = menu.app.audioOutputMenu()
            }
        }
        onObjectAdded: (index, object) => menu.insertItem(menu.firstDevice + index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
    MenuSeparator {}
    ShellMenuItem {
        objectName: "audioOutputSettings"
        iconRole: "settings-audio"
        text: qsTr("Audio settings…")
        onTriggered: menu.settingsRequested()
    }
}
