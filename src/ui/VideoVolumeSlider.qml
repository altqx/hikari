import QtQuick
import QtQuick.Controls
import Hikari.Ui

// V4: legacy VolSlider (VideoSlider.cpp:294-420), 110 pixels at the right of
// the times field: the video volume (VIDEO_VOLUME) from -86 to 0, which the
// general player's output takes (VideoBox::OnVolume). The wheel is the
// panel's (three a step); the keys are VIDEO_VOLUME_PLUS / _MINUS. A
// speaker icon of the set before it names it (legacy's had none), and the
// tooltip. The fullscreen panel shows the same control (V5).
Row {
    id: volume
    required property VideoViewController view
    required property bool hasVideo
    property alias sliderName: slider.objectName
    spacing: 2
    Icon {
        anchors.verticalCenter: parent.verticalCenter
        iconRole: "volume"
        enabled: volume.hasVideo
    }
    Slider {
        id: slider
        objectName: "videoVolume"
        readonly property VideoViewController view: volume.view
        readonly property bool hasVideo: volume.hasVideo
        implicitWidth: 110
        from: -86
        to: 0
        stepSize: 1
        value: view.volume
        enabled: hasVideo
        focusPolicy: Qt.NoFocus
        Accessible.name: qsTr("Volume")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Volume")
        onMoved: view.setVolume(Math.round(value))
    }
}
