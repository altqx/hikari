import QtQuick
import QtQuick.Controls
import Hikari.Ui

// V4: legacy VolSlider (VideoSlider.cpp:294-420), 110 pixels at the right of
// the times field: the video volume (VIDEO_VOLUME) from -86 to 0, which the
// general player's output takes (VideoBox::OnVolume). The wheel is the
// panel's (three a step); the keys are VIDEO_VOLUME_PLUS / _MINUS.
Slider {
    id: slider
    objectName: "videoVolume"
    required property VideoViewController view
    required property bool hasVideo
    implicitWidth: 110
    from: -86
    to: 0
    stepSize: 1
    value: view.volume
    enabled: hasVideo
    focusPolicy: Qt.NoFocus
    Accessible.name: qsTr("Volume")
    onMoved: view.setVolume(Math.round(value))
}
