// Y4-playres-ask (user decision 2026-10-13): a macro reads the script
// resolution of a Document without PlayResX/PlayResY (legacy GetASSRes wrote
// 1280x720 silently). The user sets it here, from the open video's size or
// legacy's values to start with; "Use video resolution" puts the video's size
// back, and "Always set it from the video" sets it from the video in later
// runs without asking (both keys, the video's size; the fields show it).
// Both appear only with a video open. Set lets the macro go on
// with it (one undo step); Cancel, Escape or closing the window stops the
// macro. Return presses the focused button, else Set.
pragma Translator: "HikariSub.Automation"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Window {
    color: Theme.panel // K2: a Window draws white unless told
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
    // The window's close button answers as Cancel.
    onClosing: close => {
        if (prompt.open)
            prompt.reject()
    }

    // The values typed before "Always" was ticked, put back when it is unticked.
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
    // As a dialog's default button: Return on a button presses that button,
    // anywhere else Set.
    function activate() {
        if (activeFocusItem === cancelButton)
            prompt.reject()
        else if (activeFocusItem === useVideoButton)
            useVideo()
        else
            set()
    }
    function set() {
        // A typed value counts even before the field gives up the focus.
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
            // one line for a usual script name; a long one wraps
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
            Button {
                id: useVideoButton
                objectName: "scriptResolutionUseVideo"
                text: qsTr("Use video resolution")
                visible: window.prompt.hasVideo
                enabled: !always.checked
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: window.prompt.videoWidth + " × " + window.prompt.videoHeight
                onClicked: window.useVideo()
            }
        }
        CheckBox {
            id: always
            objectName: "scriptResolutionAlways"
            // its box under the fields' left edge
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
