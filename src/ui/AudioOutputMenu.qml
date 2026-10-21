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
//
// KA2 (#277, KA2-slowed-playback): under the volume, the playback speed's
// five presets with the pitch kept, and the keys that step it (`slowerKey`
// and `fasterKey`, AUDIO_SPEED_DOWN and AUDIO_SPEED_UP).
ShellMenu {
    id: menu
    required property var app
    property var audio: null
    property string slowerKey
    property string fasterKey
    property var rows: []
    // the items before the devices' (the volume, its note, the speed, its
    // hint and the heading)
    readonly property int firstDevice: 5
    signal settingsRequested()
    onAboutToShow: rows = app.audioOutputMenu()
    AudioSliderRow {
        visible: menu.audio !== null
        height: visible ? implicitHeight : 0
        // Named only where it is the audio box's: FS1's fullscreen speaker
        // shows this menu too, without `audio`.
        sliderName: menu.audio !== null ? "audioVolume" : ""
        text: qsTr("Volume")
        from: 1
        to: 100
        value: menu.audio ? menu.audio.volume : 50
        readout: Math.round(value)
        onMoved: v => menu.audio.setVolume(v)
    }
    Label {
        objectName: menu.audio !== null ? "audioVolumeLinked" : ""
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
        implicitWidth: 320
        implicitHeight: visible ? 32 : 0
        height: implicitHeight
        RowLayout {
            anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
            spacing: 10
            Label {
                text: qsTr("Speed")
                Layout.preferredWidth: 76
                elide: Text.ElideRight
            }
            // a segmented control: the speed's presets, the current one marked
            Rectangle {
                objectName: "audioSpeed"
                Layout.fillWidth: true
                implicitHeight: 26
                color: "transparent"
                radius: 3
                border.color: Theme.control
                Accessible.role: Accessible.Grouping
                Accessible.name: qsTr("Speed")
                Row {
                    id: speeds
                    anchors { fill: parent; margins: 1 }
                    Repeater {
                        model: [25, 33, 50, 75, 100]
                        delegate: AbstractButton {
                            id: speed
                            required property int modelData
                            required property int index
                            objectName: "audioSpeed" + modelData
                            width: speeds.width / 5
                            height: speeds.height
                            focusPolicy: Qt.NoFocus
                            checked: menu.audio !== null && menu.audio.speedPercent === modelData
                            //: A playback speed in percent
                            text: qsTr("%1%").arg(modelData)
                            Accessible.role: Accessible.RadioButton
                            Accessible.checkable: true
                            Accessible.checked: checked
                            Accessible.name: text
                            onClicked: {
                                menu.audio.setSpeedPercent(modelData)
                                checked = Qt.binding(() => menu.audio !== null && menu.audio.speedPercent === modelData)
                            }
                            background: Rectangle {
                                color: speed.checked ? Theme.select : speed.hovered ? Theme.raised : "transparent"
                                Rectangle {
                                    width: 1
                                    height: parent.height
                                    color: Theme.line
                                    visible: speed.index > 0
                                }
                                // the chosen one's accent line: not its fill alone
                                Rectangle {
                                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                    height: 2
                                    color: Theme.accent
                                    visible: speed.checked
                                }
                            }
                            contentItem: Label {
                                text: speed.text
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.bold: speed.checked
                                font.features: ({ "tnum": 1 })
                            }
                        }
                    }
                }
            }
        }
    }
    // "Pitch is kept. [ and ] change the speed.", under the presets
    Item {
        objectName: "audioSpeedHint"
        readonly property bool keys: menu.slowerKey.length > 0 && menu.fasterKey.length > 0
        visible: menu.audio !== null
        width: parent ? parent.width : implicitWidth
        implicitWidth: hint.implicitWidth + 98 + 12
        implicitHeight: visible ? hint.implicitHeight + 6 : 0
        height: implicitHeight
        RowLayout {
            id: hint
            x: 98
            spacing: 4
            component Hint: Label {
                color: Theme.muted
                font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85)
            }
            Hint {
                text: qsTr("Pitch is kept.")
            }
            Keycap {
                visible: hint.parent.keys
                text: menu.slowerKey
            }
            Hint {
                visible: hint.parent.keys
                //: Between the two keys of "[ and ] change the speed."
                text: qsTr("and")
            }
            Keycap {
                visible: hint.parent.keys
                text: menu.fasterKey
            }
            Hint {
                visible: hint.parent.keys
                text: qsTr("change the speed.")
            }
        }
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
