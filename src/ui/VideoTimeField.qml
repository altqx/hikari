pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: field
    required property VideoController video
    property string time: video.timecode
    property bool keyframe: video.keyframeShown
    property color ink: palette.windowText
    property int pixelSize: 20
    property string tip
    readonly property bool editing: input.visible
    readonly property string jumpError: input.error
    signal closed()

    function edit() {
        if (!video.hasVideo)
            return false
        input.error = ""
        input.text = time
        input.visible = true
        input.forceActiveFocus(Qt.ShortcutFocusReason)
        input.selectAll()
        return true
    }
    function close() {
        if (!input.visible)
            return
        input.error = ""
        input.visible = false
        closed()
    }
    function jump() {
        if (video.jumpTo(input.text)) {
            close()
            return
        }
        //: Jump to's bad input: %1 what was typed, %2 the time shown now
        input.error = qsTr("Not a time or frame: %1. Try %2 or a frame number.").arg(input.text.trim()).arg(time)
    }

    implicitWidth: row.implicitWidth
    implicitHeight: Math.max(row.implicitHeight, 24)

    RowLayout {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        AbstractButton {
            id: timeButton
            objectName: "videoTime"
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            enabled: field.video.hasVideo
            visible: !input.visible
            focusPolicy: Qt.TabFocus
            padding: 2
            leftPadding: 4
            rightPadding: 4
            contentItem: Text {
                text: field.time.length > 0 ? field.time : "0:00:00.00"
                font.family: "monospace"
                font.pixelSize: field.pixelSize
                font.features: { "tnum": 1 }
                color: field.keyframe ? Theme.warning : field.ink
                opacity: field.video.hasVideo ? 1 : 0.5
            }
            background: Rectangle {
                color: timeButton.hovered ? Theme.hover : "transparent"
                border.width: timeButton.hovered ? 1 : 0
                border.color: Theme.control
                radius: 2
                FocusRing { control: timeButton }
            }
            Accessible.name: qsTr("Video time, %1, editable").arg(field.time)
            ToolTip.visible: hovered && field.tip.length > 0
            ToolTip.text: field.tip
            onClicked: field.edit()
        }
        TextField {
            id: input
            objectName: "videoTimeField"
            property string error
            visible: false
            Layout.preferredWidth: timeButton.implicitWidth
            font.family: "monospace"
            font.pixelSize: field.pixelSize
            font.features: { "tnum": 1 }
            padding: 2
            leftPadding: 4
            rightPadding: 4
            inputMethodHints: Qt.ImhNoPredictiveText
            Accessible.name: qsTr("Jump to")
            Accessible.description: error
            background: Rectangle {
                color: palette.base
                border.width: 1
                border.color: input.error.length > 0 ? Theme.danger : Theme.focus
                radius: 2
            }
            onTextEdited: error = ""
            onAccepted: field.jump()
            onActiveFocusChanged: if (!activeFocus && visible) field.close()
            Keys.onEscapePressed: event => {
                event.accepted = true
                field.close()
            }
        }
        Icon {
            objectName: "videoKeyframeMark"
            visible: field.keyframe
            iconRole: "keyframe-next"
            size: 16
            color: Theme.warning
            accentColor: Theme.warning
        }
    }
}
