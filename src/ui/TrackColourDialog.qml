pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "trackColourDialog"
    dialogId: "trackColour"
    required property var controller
    title: qsTr("Track colour")
    modal: true
    resizable: true
    defaultButton: applyButton
    initialFocus: still

    function timeText(ms) {
        const cs = Math.floor(ms / 10) % 100, s = Math.floor(ms / 1000) % 60
        const m = Math.floor(ms / 60000) % 60, h = Math.floor(ms / 3600000)
        return h + ":" + String(m).padStart(2, "0") + ":" + String(s).padStart(2, "0") + "." + String(cs).padStart(2, "0")
    }
    function problemText(problem) {
        switch (problem) {
        case "noVideo": return qsTr("Open a video first.")
        case "noLine": return qsTr("Select the lines to track first.")
        case "decodeFailed": return qsTr("A frame of the video could not be read.")
        case "readOnly": return qsTr("The subtitles are read-only.")
        case "noTrack": return qsTr("The clipboard holds no After Effects tracking data.")
        case "trackLength": return qsTr("The tracking data doesn’t have one row for each frame of the active line.")
        }
        return ""
    }
    function openDialog() {
        if (!controller.open("track"))
            return problemText(controller.problem)
        open()
        return ""
    }
    function apply() {
        if (controller.ready && !controller.reading && controller.result.length > 0)
            controller.apply()
    }
    function hex(c) {
        return String(c).toUpperCase()
    }

    onVisibleChanged: if (!visible) controller.close()

    Connections {
        target: dialog.controller
        function onApplied() { dialog.accept() }
    }

    Label {
        text: qsTr("%n selected lines, %1 to %2, %3 frames. Frames are read without subtitles.", "", dialog.controller.lineCount)
            .arg(dialog.timeText(dialog.controller.startMs)).arg(dialog.timeText(dialog.controller.endMs))
            .arg(dialog.controller.lineFrames)
        color: Theme.muted
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        Layout.preferredWidth: 584
    }
    VideoPointPicker {
        id: still
        controller: dialog.controller
        namePrefix: "trackColour"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumWidth: 240
        Layout.minimumHeight: 135
        onAccepted: dialog.apply()
    }
    RowLayout {
        spacing: 8
        enabled: dialog.controller.ready
        Label { text: "X"; Accessible.ignored: true }
        SpinBox {
            objectName: "trackColourX"
            from: 0
            to: Math.max(0, dialog.controller.frameWidth - 1)
            editable: true
            value: dialog.controller.pointX
            Accessible.name: qsTr("Point X")
            onValueModified: dialog.controller.pick(value, dialog.controller.pointY)
        }
        Label { text: "Y"; Accessible.ignored: true }
        SpinBox {
            objectName: "trackColourY"
            from: 0
            to: Math.max(0, dialog.controller.frameHeight - 1)
            editable: true
            value: dialog.controller.pointY
            Accessible.name: qsTr("Point Y")
            onValueModified: dialog.controller.pick(dialog.controller.pointX, value)
        }
        CheckBox {
            objectName: "trackColourFollow"
            text: qsTr("Follow tracking data")
            checked: dialog.controller.follow
            onToggled: dialog.controller.follow = checked
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Moves the point with After Effects tracking data from the clipboard, one row for each frame of the active line")
        }
        Label {
            visible: dialog.controller.follow
            text: qsTr("After Effects data, %n frames, from the clipboard", "", dialog.controller.trackFrames)
            color: Theme.muted
        }
    }
    RowLayout {
        spacing: 8
        enabled: dialog.controller.ready
        TabBar {
            objectName: "trackColourMode"
            currentIndex: dialog.controller.alpha ? 1 : 0
            onCurrentIndexChanged: dialog.controller.alpha = currentIndex === 1
            TabButton { text: qsTr("Colour"); width: implicitWidth }
            TabButton { text: qsTr("Alpha"); width: implicitWidth }
        }
        Repeater {
            model: dialog.controller.alpha ? ["\\alpha", "\\1a", "\\2a", "\\3a", "\\4a"] : ["\\c", "\\2c", "\\3c", "\\4c"]
            delegate: CheckBox {
                required property string modelData
                required property int index
                objectName: "trackColourTarget" + index
                text: modelData
                font.family: "monospace"
                enabled: !dialog.controller.alpha || index === 0 || !dialog.controller.allAlpha
                checked: !dialog.controller.alpha ? dialog.controller.colourTargets[index] === true
                       : index === 0 ? dialog.controller.allAlpha : dialog.controller.alphaTargets[index - 1] === true
                onToggled: {
                    if (!dialog.controller.alpha)
                        dialog.controller.setColourTarget(index, checked)
                    else if (index === 0)
                        dialog.controller.allAlpha = checked
                    else
                        dialog.controller.setAlphaTarget(index - 1, checked)
                }
            }
        }
        CheckBox {
            objectName: "trackColourSkipRepeats"
            text: qsTr("Skip repeated colours")
            checked: dialog.controller.skipRepeats
            onToggled: dialog.controller.skipRepeats = checked
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("A frame whose value is the same as the frame before writes no %1").arg("\\t")
        }
    }
    GridLayout {
        visible: dialog.controller.alpha
        enabled: dialog.controller.ready
        columns: 4
        columnSpacing: 8
        Repeater {
            model: [
                { name: qsTr("Sign colour"), key: "sign" },
                { name: qsTr("Background colour"), key: "background" }
            ]
            delegate: RowLayout {
                id: colourRow
                required property var modelData
                readonly property color value: modelData.key === "sign" ? dialog.controller.signColour : dialog.controller.backgroundColour
                function set(c) {
                    if (modelData.key === "sign")
                        dialog.controller.signColour = c
                    else
                        dialog.controller.backgroundColour = c
                }
                spacing: 6
                Layout.columnSpan: 2
                Label { text: colourRow.modelData.name }
                Rectangle {
                    implicitWidth: 22
                    implicitHeight: 22
                    radius: 3
                    color: colourRow.value
                    border.color: Theme.line
                    Accessible.ignored: true
                }
                TextField {
                    objectName: "trackColour" + colourRow.modelData.key + "Hex"
                    implicitWidth: 86
                    font.family: "monospace"
                    text: dialog.hex(colourRow.value)
                    validator: RegularExpressionValidator { regularExpression: /#?[0-9A-Fa-f]{6}/ }
                    Accessible.name: colourRow.modelData.name
                    onEditingFinished: colourRow.set(text.startsWith("#") ? text : "#" + text)
                }
                ToolButton {
                    objectName: "trackColour" + colourRow.modelData.key + "FromPoint"
                    text: qsTr("Use the point’s colour")
                    onClicked: colourRow.set(dialog.controller.colour)
                }
            }
        }
    }
    ColumnLayout {
        spacing: 2
        Layout.fillWidth: true
        Label { text: qsTr("Colour on each frame"); font.bold: true }
        Rectangle {
            objectName: "trackColourStrip"
            Layout.fillWidth: true
            implicitHeight: 22
            color: Theme.field
            border.color: Theme.line
            radius: 3
            Accessible.role: Accessible.Graphic
            Accessible.name: qsTr("Colour on each frame")
            Row {
                anchors.fill: parent
                anchors.margins: 1
                Repeater {
                    model: dialog.controller.swatches
                    delegate: Rectangle {
                        required property var modelData
                        width: parent.width / Math.max(1, dialog.controller.swatches.length)
                        height: parent.height
                        color: modelData.valid ? modelData : "transparent"
                    }
                }
            }
        }
    }
    ColumnLayout {
        spacing: 2
        Layout.fillWidth: true
        Label { text: qsTr("Result"); font.bold: true }
        TextField {
            objectName: "trackColourResult"
            Layout.fillWidth: true
            readOnly: true
            font.family: "monospace"
            text: dialog.controller.result
            Accessible.name: qsTr("Result")
        }
    }
    FieldError {
        objectName: "trackColourProblem"
        text: dialog.problemText(dialog.controller.problem)
        Layout.fillWidth: true
    }

    leadingActions: Label {
        objectName: "trackColourProgress"
        color: Theme.muted
        text: dialog.controller.reading
              ? qsTr("Reading frame %1 of %2…").arg(dialog.controller.framesRead + 1).arg(dialog.controller.framesToRead)
              : qsTr("Read %1 of %2 frames").arg(dialog.controller.framesRead).arg(dialog.controller.framesToRead)
    }
    buttons: [
        PrimaryButton {
            id: applyButton
            objectName: "trackColourApply"
            text: qsTr("Apply")
            enabled: dialog.controller.ready && !dialog.controller.reading && dialog.controller.result.length > 0
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.apply()
        },
        Button {
            objectName: "trackColourCancel"
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: dialog.reject()
        }
    ]
}
