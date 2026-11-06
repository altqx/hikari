pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

DialogWindow {
    id: dialog
    objectName: "videoDetailsDialog"
    dialogId: "videoDetails"
    required property var video
    property string timecodesFile
    title: qsTr("Video details")
    defaultButton: closeButton
    property var details: ({})

    function show() {
        refresh()
        open()
    }
    function refresh() { details = video.details() }
    onTimecodesFileChanged: if (visible) refresh()
    Connections {
        target: dialog.video
        enabled: dialog.visible
        function onChanged() {
            dialog.refresh()
            if (!dialog.video.hasVideo)
                dialog.reject()
        }
    }

    component DetailName: Label {
        color: Theme.muted
        Layout.alignment: Qt.AlignTop | Qt.AlignLeft
        Layout.topMargin: 4
    }
    component DetailValue: TextField {
        readOnly: true
        selectByMouse: true
        background: null
        leftPadding: 0
        rightPadding: 0
        Layout.fillWidth: true
        Accessible.role: Accessible.StaticText
    }

    GridLayout {
        columns: 2
        columnSpacing: 16
        rowSpacing: 4
        Layout.preferredWidth: 460
        DetailName { text: qsTr("File") }
        DetailValue {
            objectName: "videoDetailsFile"
            text: dialog.details.file ?? ""
            Accessible.name: qsTr("File")
        }
        DetailName { text: qsTr("Resolution") }
        DetailValue {
            objectName: "videoDetailsResolution"
            text: dialog.details.resolution ?? ""
            Accessible.name: qsTr("Resolution")
        }
        DetailName {
            objectName: "videoDetailsFrameRateName"
            text: dialog.details.frameTimesReplaced ? qsTr("Video stream frame rate") : qsTr("Frame rate")
        }
        DetailValue {
            objectName: "videoDetailsFrameRate"
            text: dialog.details.frameRate ?? ""
            Accessible.name: dialog.details.frameTimesReplaced ? qsTr("Video stream frame rate") : qsTr("Frame rate")
        }
        DetailName {
            visible: frameTimes.visible
            text: qsTr("Frame times from")
        }
        DetailValue {
            id: frameTimes
            objectName: "videoDetailsFrameTimes"
            visible: (dialog.details.frameTimesReplaced ?? false) && dialog.timecodesFile.length > 0
            text: dialog.timecodesFile
            Accessible.name: qsTr("Frame times from")
        }
        DetailName { text: qsTr("Length") }
        DetailValue {
            objectName: "videoDetailsLength"
            text: dialog.details.length ?? ""
            Accessible.name: qsTr("Length")
        }
        DetailName { text: qsTr("Colour space") }
        DetailValue {
            objectName: "videoDetailsColourSpace"
            text: dialog.details.colourSpace ?? ""
            Accessible.name: qsTr("Colour space")
            ToolTip.visible: hovered && (dialog.details.pixelFormat ?? "").length > 0
            ToolTip.text: dialog.details.pixelFormat ?? ""
        }
        DetailName { text: qsTr("Colour matrix") }
        DetailValue {
            objectName: "videoDetailsMatrix"
            text: dialog.details.matrix ?? ""
            Accessible.name: qsTr("Colour matrix")
        }
        DetailName {
            visible: (dialog.details.scriptMatrix ?? "").length > 0
            text: qsTr("Used for the subtitles")
        }
        DetailValue {
            objectName: "videoDetailsScriptMatrix"
            visible: (dialog.details.scriptMatrix ?? "").length > 0
            text: dialog.details.scriptMatrix ?? ""
            Accessible.name: qsTr("Used for the subtitles")
        }
    }

    buttons: [
        PrimaryButton {
            id: closeButton
            objectName: "videoDetailsClose"
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: dialog.accept()
        }
    ]
}
