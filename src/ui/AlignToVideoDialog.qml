pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "alignToVideoDialog"
    dialogId: "alignToVideo"
    required property var controller
    title: qsTr("Align to video")
    modal: true
    resizable: true
    defaultButton: alignButton
    initialFocus: still

    function problemText(problem) {
        switch (problem) {
        case "noVideo": return qsTr("Open a video first.")
        case "noLine": return qsTr("Select a line to align first.")
        case "decodeFailed": return qsTr("A frame of the video could not be read.")
        case "readOnly": return qsTr("The subtitles are read-only.")
        }
        return ""
    }
    // Empty when the dialog opened; otherwise why it could not.
    function openDialog() {
        if (!controller.open())
            return problemText(controller.problem)
        open()
        return ""
    }
    function align() {
        if (controller.ready && !controller.searching)
            controller.align()
    }

    onVisibleChanged: if (!visible) controller.close()

    Connections {
        target: dialog.controller
        function onAligned() { dialog.accept() }
    }

    Label {
        text: qsTr("Click a point that keeps its colour while the line should show. The line is timed to every frame around this one where it does.")
        color: Theme.muted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        Layout.preferredWidth: 640
    }
    RowLayout {
        spacing: 12
        Layout.fillWidth: true
        Layout.fillHeight: true
        Rectangle {
            id: still
            objectName: "alignToVideoStill"
            color: Theme.field
            border.color: activeFocus ? Theme.focus : Theme.line
            activeFocusOnTab: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 640
            Layout.preferredHeight: 360
            Layout.minimumWidth: 240
            Layout.minimumHeight: 135
            Accessible.role: Accessible.Graphic
            Accessible.name: qsTr("Video frame")
            Accessible.description: qsTr("Arrow keys move the point; Enter aligns")
            readonly property real zoom: image.paintedWidth > 0 ? image.paintedWidth / Math.max(1, dialog.controller.frameWidth) : 1
            readonly property real imageLeft: image.x + (image.width - image.paintedWidth) / 2
            readonly property real imageTop: image.y + (image.height - image.paintedHeight) / 2
            function frameX(x) { return Math.floor((x - imageLeft) / zoom) }
            function frameY(y) { return Math.floor((y - imageTop) / zoom) }
            Keys.onPressed: event => {
                const step = event.modifiers & Qt.ShiftModifier ? 10 : 1
                const c = dialog.controller
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
                source: dialog.controller.ready ? "image://alignframe/" + dialog.controller.stillKey : ""
            }
            BusyIndicator {
                anchors.centerIn: parent
                running: !dialog.controller.ready && dialog.controller.problem.length === 0
                visible: running
            }
            Item {
                visible: dialog.controller.ready && image.status === Image.Ready
                x: still.imageLeft + (dialog.controller.pointX + 0.5) * still.zoom
                y: still.imageTop + (dialog.controller.pointY + 0.5) * still.zoom
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
                objectName: "alignToVideoPicker"
                anchors.fill: parent
                enabled: dialog.controller.ready && !dialog.controller.searching
                cursorShape: Qt.CrossCursor
                onPressed: mouse => {
                    still.forceActiveFocus()
                    dialog.controller.pick(still.frameX(mouse.x), still.frameY(mouse.y))
                }
                onPositionChanged: mouse => dialog.controller.pick(still.frameX(mouse.x), still.frameY(mouse.y))
                onDoubleClicked: dialog.align()
            }
        }
        GridLayout {
            columns: 2
            columnSpacing: 8
            rowSpacing: 8
            Layout.alignment: Qt.AlignTop
            enabled: dialog.controller.ready && !dialog.controller.searching
            Label { text: "X"; Accessible.ignored: true }
            SpinBox {
                objectName: "alignToVideoX"
                from: 0
                to: Math.max(0, dialog.controller.frameWidth - 1)
                editable: true
                value: dialog.controller.pointX
                Accessible.name: qsTr("Point X")
                onValueModified: dialog.controller.pick(value, dialog.controller.pointY)
            }
            Label { text: "Y"; Accessible.ignored: true }
            SpinBox {
                objectName: "alignToVideoY"
                from: 0
                to: Math.max(0, dialog.controller.frameHeight - 1)
                editable: true
                value: dialog.controller.pointY
                Accessible.name: qsTr("Point Y")
                onValueModified: dialog.controller.pick(dialog.controller.pointX, value)
            }
            Label { text: qsTr("Colour") }
            Rectangle {
                objectName: "alignToVideoColour"
                implicitWidth: 56
                implicitHeight: 22
                radius: 3
                color: dialog.controller.colour
                border.color: Theme.line
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("Colour %1").arg(String(dialog.controller.colour).toUpperCase())
            }
            Label { text: qsTr("Tolerance") }
            SpinBox {
                objectName: "alignToVideoTolerance"
                from: 0
                to: dialog.controller.maxTolerance
                editable: true
                value: dialog.controller.tolerance
                Accessible.name: qsTr("Tolerance")
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("How far a colour may drift and still count as the same, from 0 to 255")
                onValueModified: dialog.controller.tolerance = value
            }
        }
    }
    RowLayout {
        visible: dialog.controller.searching
        spacing: 8
        BusyIndicator {
            running: dialog.controller.searching
            implicitWidth: 20
            implicitHeight: 20
        }
        Label {
            objectName: "alignToVideoProgress"
            text: qsTr("Searching… %n frames checked", "", dialog.controller.checked)
        }
    }
    FieldError {
        objectName: "alignToVideoProblem"
        text: dialog.problemText(dialog.controller.problem)
        Layout.fillWidth: true
    }

    buttons: [
        PrimaryButton {
            id: alignButton
            objectName: "alignToVideoAlign"
            text: qsTr("Align")
            enabled: dialog.controller.ready && !dialog.controller.searching
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.align()
        },
        Button {
            objectName: "alignToVideoCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.reject()
        }
    ]
}
