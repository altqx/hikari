pragma Translator: "HikariSub.Video"
import QtQuick
import Hikari.Ui

QtObject {
    id: follow
    required property SettingsStore settings
    readonly property string seekName: qsTr("Seek on")
    readonly property string playName: qsTr("Then play")
    readonly property string seekLabel: qsTr("Move video to selected line on:")
    readonly property string playLabel: qsTr("On moving to another line play:")
    readonly property var seekChoices: [qsTr("Double-clicking a line (always on)"), qsTr("Every line change"),
                                        qsTr("Clicking a line or editing when paused"), qsTr("Clicking a line or editing"),
                                        qsTr("Editing line when paused"), qsTr("Editing")]
    readonly property var playChoices: [qsTr("Nothing"), qsTr("Audio to the line end time"),
                                        qsTr("Video and audio to the line end time"),
                                        qsTr("Video and audio to the next line start time")]
    property int seek: settings.value("video.moveToActiveLine")
    property int play: settings.value("video.playAfterSelection")
    function setSeek(index) { settings.setValue("video.moveToActiveLine", index) }
    function setPlay(index) { settings.setValue("video.playAfterSelection", index) }
    readonly property Connections watch: Connections {
        target: follow.settings
        function onChanged(id) {
            if (id === "video.moveToActiveLine")
                follow.seek = follow.settings.value(id)
            else if (id === "video.playAfterSelection")
                follow.play = follow.settings.value(id)
        }
    }
}
