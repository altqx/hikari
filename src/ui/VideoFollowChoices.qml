import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// V6: legacy VideoToolbar's two lists (VideoToolbar.cpp:130-151): "Move video
// to selected line on:" (MOVE_VIDEO_TO_ACTIVE_LINE, video.moveToActiveLine)
// and "On moving to another line play:" (VIDEO_PLAY_AFTER_SELECTION,
// video.playAfterSelection). A choice is stored at once, as legacy's
// Options.SetInt and SaveOptions did; the application reads it when the
// active Line changes (EditBox::SetLine), on a Grid click or double click
// (SetVideoLineTime) and after an edit (ShowEditOnVideo).
RowLayout {
    id: choices
    objectName: "videoFollowChoices"
    required property SettingsStore settings
    spacing: 4

    Item { Layout.fillWidth: true } // legacy draws the lists at the toolbar's right end

    // Each list names itself with a short muted caption (the full legacy
    // label is its tooltip and accessible name): "Nothing" alone said
    // nothing out of context.
    Label {
        text: qsTr("Seek on")
        color: Theme.muted
        Layout.leftMargin: 6
    }
    ComboBox {
        id: seekAfter
        objectName: "videoSeekAfter"
        focusPolicy: Qt.NoFocus
        model: [qsTr("Double-clicking a line (always on)"), qsTr("Every line change"),
                qsTr("Clicking a line or editing when paused"), qsTr("Clicking a line or editing"),
                qsTr("Editing line when paused"), qsTr("Editing")]
        currentIndex: choices.settings.value("video.moveToActiveLine")
        implicitContentWidthPolicy: ComboBox.WidestText
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Move video to selected line on:")
        Accessible.name: qsTr("Move video to selected line on:")
        onActivated: index => choices.settings.setValue("video.moveToActiveLine", index)
    }
    Label {
        text: qsTr("Then play")
        color: Theme.muted
        Layout.leftMargin: 10
    }
    ComboBox {
        id: playAfter
        objectName: "videoPlayAfter"
        focusPolicy: Qt.NoFocus
        model: [qsTr("Nothing"), qsTr("Audio to the line end time"), qsTr("Video and audio to the line end time"),
                qsTr("Video and audio to the next line start time")]
        currentIndex: choices.settings.value("video.playAfterSelection")
        implicitContentWidthPolicy: ComboBox.WidestText
        ToolTip.visible: hovered
        ToolTip.text: qsTr("On moving to another line play:")
        Accessible.name: qsTr("On moving to another line play:")
        onActivated: index => choices.settings.setValue("video.playAfterSelection", index)
    }
    Connections {
        target: choices.settings
        function onChanged(id) {
            if (id === "video.moveToActiveLine")
                seekAfter.currentIndex = choices.settings.value(id)
            else if (id === "video.playAfterSelection")
                playAfter.currentIndex = choices.settings.value(id)
        }
    }
}
