pragma Translator: "HikariSub.Video"
import QtQuick
import Hikari.Ui

// V6: legacy VideoToolbar's two lists (VideoToolbar.cpp:130-151): "Move video
// to selected line on:" (MOVE_VIDEO_TO_ACTIVE_LINE, video.moveToActiveLine)
// and "On moving to another line play:" (VIDEO_PLAY_AFTER_SELECTION,
// video.playAfterSelection). A choice is stored at once, as legacy's
// Options.SetInt and SaveOptions did; the application reads it when the
// active Line changes (EditBox::SetLine), on a Grid click or double click
// (SetVideoLineTime) and after an edit (ShowEditOnVideo).
//
// VF1 (#288): the lists left the panel. This is their choices and texts, for
// the Video panel's menu ("Seek on" and "Then play", radio items) and the
// Settings dialog's Video page; `seek` and `play` follow the stored choices,
// wherever they are made.
QtObject {
    id: follow
    required property SettingsStore settings
    // The short names (the menu's), and legacy's labels (Settings' and the
    // assistive name).
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
