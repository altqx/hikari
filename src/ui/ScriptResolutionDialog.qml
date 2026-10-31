pragma Translator: "HikariSub.Automation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Window {
    color: Theme.panel
    id: window
    objectName: "scriptResolutionDialog"
    required property ScriptResolutionPromptController prompt

    title: qsTr("Script resolution")
    visible: prompt.open
    modality: Qt.ApplicationModal
    flags: Qt.Dialog
    width: content.implicitWidth + 32
    height: content.implicitHeight + 32

    Component.onCompleted: {
        prompt.setView(window)
        IconTheme.setWindowIcon(window, "script-properties")
    }
    onVisibleChanged: {
        if (!visible)
            return
        widthBox.value = prompt.width
        heightBox.value = prompt.height
        always.checked = false
        requestActivate()
        widthBox.forceActiveFocus()
    }
    onClosing: close => {
        if (prompt.open)
            prompt.reject()
    }

    property int typedWidth: 0
    property int typedHeight: 0

    function useVideo() {
        widthBox.value = prompt.videoWidth
        heightBox.value = prompt.videoHeight
    }
    function alwaysToggled() {
        if (always.checked) {
            typedWidth = widthBox.value
            typedHeight = heightBox.value
            useVideo()
        } else {
            widthBox.value = typedWidth
            heightBox.value = typedHeight
        }
    }
    function activate() {
        if (activeFocusItem === cancelButton)
            prompt.reject()
        else if (activeFocusItem === useVideoButton)
            useVideo()
        else
            set()
    }
    function set() {
        // SpinBox commits typed text only when editing finishes.
        widthBox.value = widthBox.valueFromText(widthBox.contentItem.text, widthBox.locale)
        heightBox.value = heightBox.valueFromText(heightBox.contentItem.text, heightBox.locale)
        prompt.accept(widthBox.value, heightBox.value, always.checked)
    }

    Shortcut { sequences: ["Return", "Enter"]; enabled: window.visible; onActivated: window.activate() }
    Shortcut { sequence: "Escape"; enabled: window.visible; onActivated: window.prompt.reject() }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        Label {
            objectName: "scriptResolutionText"
            text: qsTr("This file has no script resolution, and “%1” needs one.").arg(window.prompt.script)
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            Layout.preferredWidth: Math.max(fields.implicitWidth, 460)
            Accessible.role: Accessible.StaticText
        }
        RowLayout {
            id: fields
            spacing: 8
            SpinBox {
                id: widthBox
                objectName: "scriptResolutionWidth"
                from: 1; to: 16384; editable: true
                enabled: !always.checked
                Accessible.name: qsTr("Width")
            }
            Label { text: "×"; Accessible.ignored: true }
            SpinBox {
                id: heightBox
                objectName: "scriptResolutionHeight"
                from: 1; to: 16384; editable: true
                enabled: !always.checked
                Accessible.name: qsTr("Height")
            }
            IconTextButton {
                iconRole: "resolution-from-video"
                id: useVideoButton
                objectName: "scriptResolutionUseVideo"
                text: qsTr("Use video resolution")
                visible: window.prompt.hasVideo
                enabled: !always.checked
                ToolTip.visible: hovered
                ToolTip.text: window.prompt.videoWidth + " × " + window.prompt.videoHeight
                onClicked: window.useVideo()
            }
        }
        CheckBox {
            id: always
            objectName: "scriptResolutionAlways"
            Layout.leftMargin: -leftPadding
            text: qsTr("Always set it from the video without asking")
            visible: window.prompt.hasVideo
            onToggled: window.alwaysToggled()
        }
        WindowFooter {
            Button {
                objectName: "scriptResolutionSet"
                text: qsTr("Set")
                highlighted: true
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                onClicked: window.set()
            }
            Button {
                id: cancelButton
                objectName: "scriptResolutionCancel"
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: window.prompt.reject()
            }
        }
    }
}
