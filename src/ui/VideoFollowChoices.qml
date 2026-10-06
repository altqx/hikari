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
//
// D3: a Video panel too narrow for the two side by side puts them on two
// rows (each caption beside its list), so neither list is cut off; the
// narrowest is `minimumWidth` (the panel's reported minimum,
// docs/qt/docking.md).
GridLayout {
    id: choices
    objectName: "videoFollowChoices"
    required property SettingsStore settings
    readonly property real oneRowWidth: seekLabel.implicitWidth + seekLabel.Layout.leftMargin + seekAfter.implicitWidth
                                        + playLabel.implicitWidth + 10 + playAfter.implicitWidth + 4 * columnSpacing
    readonly property real minimumWidth: 6 + Math.max(seekLabel.implicitWidth, playLabel.implicitWidth)
                                         + Math.max(seekAfter.implicitWidth, playAfter.implicitWidth) + columnSpacing
    // The column's width, not the layout's own: laid out at its minimum, a
    // row too wide for the column would only overflow it.
    readonly property real available: parent ? parent.width : width
    readonly property bool wrapped: available > 0 && available < oneRowWidth
    columns: wrapped ? 2 : 5
    columnSpacing: 4
    rowSpacing: 2

    Item { Layout.fillWidth: true; visible: !choices.wrapped } // legacy draws the lists at the toolbar's right end
    // Each list shows its widest choice when the panel has the room and
    // narrows, its text cut, when it has not: the lists must not set the
    // controls column's minimum width, which would widen the transport row
    // and push Next frame and the volume past the panel's edge, under the
    // next dock.

    // Each list names itself with a short muted caption (the full legacy
    // label is its tooltip and accessible name): "Nothing" alone said
    // nothing out of context.
    Label {
        id: seekLabel
        text: qsTr("Seek on")
        color: Theme.muted
        Layout.leftMargin: 6
    }
    ComboBox {
        id: seekAfter
        objectName: "videoSeekAfter"
        Layout.fillWidth: true // a layout gives an item that does not fill its preferred width only
        Layout.minimumWidth: 60
        Layout.preferredWidth: implicitWidth
        Layout.maximumWidth: implicitWidth
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
        id: playLabel
        text: qsTr("Then play")
        color: Theme.muted
        Layout.leftMargin: choices.wrapped ? 6 : 10
    }
    ComboBox {
        id: playAfter
        objectName: "videoPlayAfter"
        Layout.fillWidth: true // a layout gives an item that does not fill its preferred width only
        Layout.minimumWidth: 60
        Layout.preferredWidth: implicitWidth
        Layout.maximumWidth: implicitWidth
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
