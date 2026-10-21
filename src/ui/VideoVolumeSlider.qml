pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

// V4: legacy VolSlider (VideoSlider.cpp:294-420), 110 pixels at the right of
// the times field: the video volume (VIDEO_VOLUME) from -86 to 0, which the
// general player's output takes (VideoBox::OnVolume). The wheel is the
// panel's (three a step); the keys are VIDEO_VOLUME_PLUS / _MINUS. A
// speaker icon of the set before it names it (legacy's had none), and the
// tooltip. The fullscreen panel shows the same control (V5), beside its
// speaker button (FS1); muted, the slider is at the bottom.
Row {
    id: volume
    required property VideoViewController view
    required property bool hasVideo
    property alias sliderName: slider.objectName
    // FS1: the fullscreen bar has its speaker button instead, and a shorter slider.
    property bool showIcon: true
    property int sliderWidth: 110
    spacing: 2
    Icon {
        anchors.verticalCenter: parent.verticalCenter
        visible: volume.showIcon
        iconRole: volume.view.muted ? "volume-muted" : "volume"
        enabled: volume.hasVideo
    }
    Slider {
        id: slider
        objectName: "videoVolume"
        readonly property VideoViewController view: volume.view
        readonly property bool hasVideo: volume.hasVideo
        implicitWidth: volume.sliderWidth
        from: -86
        to: 0
        stepSize: 1
        // FS1-speaker-gestures: muted, at the bottom; a move unmutes.
        value: view.muted ? from : view.volume
        enabled: hasVideo
        focusPolicy: Qt.NoFocus
        Accessible.name: qsTr("Volume")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Volume")
        onMoved: view.setVolume(Math.round(value))
    }
}
