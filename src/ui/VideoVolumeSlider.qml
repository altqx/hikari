pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Row {
    id: volume
    required property VideoViewController view
    required property bool hasVideo
    property alias sliderName: slider.objectName
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
        value: view.muted ? from : view.volume
        enabled: hasVideo
        focusPolicy: Qt.NoFocus
        Accessible.name: qsTr("Volume")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Volume")
        onMoved: view.setVolume(Math.round(value))
    }
}
