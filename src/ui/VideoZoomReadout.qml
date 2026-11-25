pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

AbstractButton {
    id: readout
    objectName: "videoZoomReadout"
    required property var shell
    property color ink: palette.windowText
    property color hoverFill: Theme.hover
    property string itemPrefix: "videoZoomReadout"
    readonly property bool fits: !shell.videoView.zoomed
    readonly property alias menu: levels
    text: fits
          //: The video zoom readout while the whole frame fits the panel
          ? qsTr("Fit")
          //: The video zoom readout: %1 the zoom as a whole percentage, as in "200 %"
          : qsTr("%1 %").arg(shell.videoView.readoutPercent)
    visible: shell.video.hasVideo
    focusPolicy: Qt.TabFocus
    padding: 2
    leftPadding: 6
    rightPadding: 6
    down: levels.visible
    contentItem: Text {
        text: readout.text
        color: readout.ink
        opacity: readout.enabled ? 1 : 0.5
        font.features: { "tnum": 1 }
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
    }
    background: Rectangle {
        implicitHeight: 24
        color: readout.down || readout.hovered ? readout.hoverFill : "transparent"
        radius: 2
        FocusRing { control: readout }
    }
    Accessible.role: Accessible.ButtonMenu
    Accessible.name: qsTr("Video zoom, %1").arg(text)
    ToolTip.visible: hovered && !levels.visible
    ToolTip.text: qsTr("Video zoom")
    onClicked: levels.visible ? levels.close() : levels.popup(readout, 0, readout.height)

    function actionOf(symbol) {
        return readout.shell.globalActions()[symbol]
    }

    ShellMenu {
        id: levels
        objectName: readout.itemPrefix + "Menu"
        ShellMenuItem {
            objectName: readout.itemPrefix + "Fit"
            command: "GLOBAL_VIDEO_ZOOM_FIT"
            iconRole: "zoom-reset"
            action: readout.actionOf(command)
        }
        ShellMenuItem {
            objectName: readout.itemPrefix + "50"
            command: "GLOBAL_VIDEO_ZOOM_50"
            iconRole: "zoom"
            action: readout.actionOf(command)
            //: A zoom level in the video zoom readout's menu
            text: qsTr("50 %")
        }
        ShellMenuItem {
            objectName: readout.itemPrefix + "100"
            command: "GLOBAL_VIDEO_ZOOM_100"
            iconRole: "zoom"
            action: readout.actionOf(command)
            //: A zoom level in the video zoom readout's menu
            text: qsTr("100 %")
        }
        ShellMenuItem {
            objectName: readout.itemPrefix + "200"
            command: "GLOBAL_VIDEO_ZOOM_200"
            iconRole: "zoom"
            action: readout.actionOf(command)
            //: A zoom level in the video zoom readout's menu
            text: qsTr("200 %")
        }
        ShellMenuItem {
            objectName: readout.itemPrefix + "DevicePixels"
            command: "GLOBAL_VIDEO_ZOOM_DEVICE_PIXELS"
            iconRole: "zoom"
            action: readout.actionOf(command)
        }
        MenuSeparator {}
        ShellMenuItem {
            objectName: readout.itemPrefix + "In"
            command: "GLOBAL_VIDEO_ZOOM_IN"
            iconRole: "add"
            action: readout.actionOf(command)
        }
        ShellMenuItem {
            objectName: readout.itemPrefix + "Out"
            command: "GLOBAL_VIDEO_ZOOM_OUT"
            iconRole: "remove"
            action: readout.actionOf(command)
        }
    }
}
