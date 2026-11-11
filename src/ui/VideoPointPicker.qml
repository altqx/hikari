pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Rectangle {
    id: still
    required property var controller
    required property string namePrefix
    property bool busy: false
    signal accepted()
    objectName: namePrefix + "Still"
    color: Theme.field
    border.color: activeFocus ? Theme.focus : Theme.line
    activeFocusOnTab: true
    implicitWidth: 584
    implicitHeight: 250
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Video frame")
    Accessible.description: qsTr("Arrow keys move the point; Enter applies")
    readonly property real zoom: image.paintedWidth > 0 ? image.paintedWidth / Math.max(1, controller.frameWidth) : 1
    readonly property real imageLeft: image.x + (image.width - image.paintedWidth) / 2
    readonly property real imageTop: image.y + (image.height - image.paintedHeight) / 2
    function frameX(x) { return Math.floor((x - imageLeft) / zoom) }
    function frameY(y) { return Math.floor((y - imageTop) / zoom) }
    Keys.onPressed: event => {
        const step = event.modifiers & Qt.ShiftModifier ? 10 : 1
        const c = still.controller
        if (event.key === Qt.Key_Left) c.pick(c.pointX - step, c.pointY)
        else if (event.key === Qt.Key_Right) c.pick(c.pointX + step, c.pointY)
        else if (event.key === Qt.Key_Up) c.pick(c.pointX, c.pointY - step)
        else if (event.key === Qt.Key_Down) c.pick(c.pointX, c.pointY + step)
        else return
        event.accepted = true
    }
    Image {
        id: image
        anchors.fill: parent
        anchors.margins: 1
        fillMode: Image.PreserveAspectFit
        cache: false
        smooth: false
        source: still.controller.ready ? "image://analysisframe/" + still.controller.stillKey : ""
    }
    BusyIndicator {
        anchors.centerIn: parent
        running: !still.controller.ready && still.controller.problem.length === 0
        visible: running
    }
    Item {
        visible: still.controller.ready && image.status === Image.Ready
        x: still.imageLeft + (still.controller.pointX + 0.5) * still.zoom
        y: still.imageTop + (still.controller.pointY + 0.5) * still.zoom
        Rectangle {
            width: 13; height: 13; radius: 6.5
            x: -6.5; y: -6.5
            color: "transparent"
            border.width: 2
            border.color: "black"
        }
        Rectangle {
            width: 11; height: 11; radius: 5.5
            x: -5.5; y: -5.5
            color: "transparent"
            border.width: 1
            border.color: "white"
        }
    }
    MouseArea {
        objectName: still.namePrefix + "Picker"
        anchors.fill: parent
        enabled: still.controller.ready && !still.busy
        cursorShape: Qt.CrossCursor
        onPressed: mouse => {
            still.forceActiveFocus()
            still.controller.pick(still.frameX(mouse.x), still.frameY(mouse.y))
        }
        onPositionChanged: mouse => still.controller.pick(still.frameX(mouse.x), still.frameY(mouse.y))
        onDoubleClicked: still.accepted()
    }
}
