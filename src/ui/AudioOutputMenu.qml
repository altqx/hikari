pragma Translator: "HikariSub.Audio"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ShellMenu {
    id: outputMenu
    required property var app
    property var audio: null
    property string slowerKey
    property string fasterKey
    property var rows: []
    // MUST KILL: firstDevice is a hand count of the menu items above the devices
    readonly property int firstDevice: 5
    signal settingsRequested()
    onAboutToShow: rows = app.audioOutputMenu()
    AudioSliderRow {
        visible: outputMenu.audio !== null
        height: visible ? implicitHeight : 0
        sliderName: outputMenu.audio !== null ? "audioVolume" : ""
        text: qsTr("Volume")
        from: 1
        to: 100
        value: outputMenu.audio ? outputMenu.audio.volume : 50
        readout: Math.round(value)
        onMoved: v => outputMenu.audio.setVolume(v)
    }
    Label {
        objectName: outputMenu.audio !== null ? "audioVolumeLinked" : ""
        visible: outputMenu.audio !== null && outputMenu.audio.linked
        height: visible ? implicitHeight : 0
        leftPadding: 98
        bottomPadding: 4
        color: Theme.muted
        font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85)
        text: qsTr("Linked to vertical stretch")
    }
    Item {
        visible: outputMenu.audio !== null
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
                            checked: outputMenu.audio !== null && outputMenu.audio.speedPercent === modelData
                            //: A playback speed in percent
                            text: qsTr("%1%").arg(modelData)
                            Accessible.role: Accessible.RadioButton
                            Accessible.checkable: true
                            Accessible.checked: checked
                            Accessible.name: text
                            onClicked: {
                                outputMenu.audio.setSpeedPercent(modelData)
                                checked = Qt.binding(() => outputMenu.audio !== null && outputMenu.audio.speedPercent === modelData)
                            }
                            background: Rectangle {
                                color: speed.checked ? Theme.select : speed.hovered ? Theme.raised : "transparent"
                                Rectangle {
                                    width: 1
                                    height: parent.height
                                    color: Theme.line
                                    visible: speed.index > 0
                                }
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
    Item {
        objectName: "audioSpeedHint"
        readonly property bool keys: outputMenu.slowerKey.length > 0 && outputMenu.fasterKey.length > 0
        visible: outputMenu.audio !== null
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
            KeycapSentence {
                visible: hint.parent.keys
                //: %1 the key that slows the audio down, %2 the key that speeds it up
                text: qsTr("%1 and %2 change the speed.")
                keys: [outputMenu.slowerKey, outputMenu.fasterKey]
                color: Theme.muted
                font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85)
            }
        }
    }
    Item {
        visible: outputMenu.audio !== null
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
        model: outputMenu.rows
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: "audioOutput" + index
            text: modelData.label
            checkable: true
            checked: modelData.checked
            keepsMenuOpen: false
            onTriggered: {
                outputMenu.app.chooseAudioOutput(modelData.id, modelData.name)
                outputMenu.rows = outputMenu.app.audioOutputMenu()
            }
        }
        onObjectAdded: (index, object) => outputMenu.insertItem(outputMenu.firstDevice + index, object)
        onObjectRemoved: (index, object) => outputMenu.removeItem(object)
    }
    MenuSeparator {}
    ShellMenuItem {
        objectName: "audioOutputSettings"
        iconRole: "settings-audio"
        text: qsTr("Audio settings…")
        onTriggered: outputMenu.settingsRequested()
    }
}
