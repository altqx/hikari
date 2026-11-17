pragma Translator: "HikariSub.QuickPick"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

Popup {
    id: box
    objectName: "inputBox"

    property string title: ""
    property string step: ""
    property string owner: ""
    property string placeholder: ""
    property string prompt: ""
    property bool password: false
    property bool busy: false
    property bool back: false
    property string severity: ""
    property string message: ""
    property real hangFrom: 8
    property alias text: field.text

    signal edited(string text)
    signal accepted(string text)
    signal backRequested()
    signal cancelled()

    property Item restoreFocus: null
    property bool answered: false

    function show(initialText, selectionFrom, selectionTo) {
        answered = false
        restoreFocus = parent && parent.Window.window ? parent.Window.window.activeFocusItem : null
        open()
        setText(initialText, selectionFrom, selectionTo)
        field.forceActiveFocus()
    }
    function setText(value, selectionFrom, selectionTo) {
        field.text = value ?? ""
        if (selectionFrom === undefined || selectionFrom < 0)
            field.selectAll()
        else
            field.select(Math.min(selectionFrom, field.length), Math.min(selectionTo, field.length))
    }
    function finish() {
        answered = true
        close()
    }
    function cancel() {
        if (visible)
            close()
    }

    parent: Overlay.overlay
    x: Math.round((parent ? parent.width - width : 0) / 2)
    y: hangFrom
    width: Math.max(360, Math.min(600, (parent ? parent.width : 600) - 48))
    padding: 6
    modal: false
    focus: true
    closePolicy: Popup.CloseOnPressOutside
    onClosed: {
        if (!answered)
            cancelled()
        answered = true
        if (restoreFocus)
            restoreFocus.forceActiveFocus()
        restoreFocus = null
    }
    onMessageChanged: {
        if (visible && message.length > 0)
            field.Accessible.announce(message, severity === "error" ? Accessible.Assertive : Accessible.Polite)
    }

    Connections {
        target: box.parent ? box.parent.Window.window : null
        function onActiveChanged() {
            if (box.visible && !box.parent.Window.window.active)
                box.cancel()
        }
    }

    background: Rectangle {
        color: Theme.panel
        radius: 6
        border.color: Theme.line
        Rectangle {
            z: -1
            x: 0; y: 2
            width: parent.width
            height: parent.height
            radius: parent.radius
            color: "black"
            opacity: 0.25
            visible: !Theme.highContrast
        }
    }

    contentItem: Column {
        spacing: 0

        Item {
            objectName: "inputBoxTitleRow"
            width: parent.width
            height: visible ? 30 : 0
            visible: box.title.length > 0 || box.owner.length > 0 || box.step.length > 0 || box.back
            IconToolButton {
                id: backButton
                objectName: "inputBoxBack"
                visible: box.back
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: visible ? 24 : 0
                height: 24
                padding: 0
                focusPolicy: Qt.NoFocus
                iconRole: "chevron-left"
                text: qsTr("Back")
                tip: qsTr("Back (Alt+Left)")
                onClicked: box.backRequested()
            }
            Text {
                anchors.left: backButton.right
                anchors.leftMargin: 6
                anchors.right: trailing.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: box.title
                textFormat: Text.PlainText
                font.weight: Font.DemiBold
                color: Theme.text
                elide: Text.ElideRight
            }
            Text {
                id: trailing
                objectName: "inputBoxTitleTrailing"
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: [box.step, box.owner].filter(t => t.length > 0).join("  ")
                textFormat: Text.PlainText
                color: Theme.muted
            }
        }

        Rectangle {
            width: parent.width
            height: 28
            radius: 4
            color: Theme.field
            border.color: box.severity === "error" ? Theme.danger : field.activeFocus ? Theme.accent : Theme.line
            TextField {
                id: field
                objectName: "inputBoxField"
                anchors.left: parent.left
                anchors.right: showButton.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.leftMargin: 6
                background: null
                placeholderText: box.placeholder
                placeholderTextColor: Theme.muted
                color: Theme.text
                verticalAlignment: TextInput.AlignVCenter
                echoMode: box.password && !showButton.checked ? TextInput.Password : TextInput.Normal
                Accessible.name: box.title.length > 0 ? box.title : box.prompt
                Accessible.description: box.message.length > 0 ? box.message : box.prompt
                onTextEdited: box.edited(text)
                Keys.onPressed: event => {
                    const mods = event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && mods === 0)
                        box.accepted(text)
                    else if (event.key === Qt.Key_Escape)
                        box.cancel()
                    else if (event.key === Qt.Key_Left && mods === Qt.AltModifier && box.back)
                        box.backRequested()
                    else
                        return
                    event.accepted = true
                }
            }
            IconToolButton {
                id: showButton
                objectName: "inputBoxShow"
                visible: box.password
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: visible ? 24 : 0
                height: 24
                padding: 0
                focusPolicy: Qt.NoFocus
                checkable: true
                iconRole: checked ? "hide" : "show"
                text: checked ? qsTr("Hide") : qsTr("Show")
            }
            Rectangle {
                objectName: "inputBoxBusy"
                anchors.bottom: parent.bottom
                height: 2
                width: parent.width / 3
                color: Theme.accent
                visible: box.busy
                NumberAnimation on x {
                    running: box.busy && box.visible
                    from: 0
                    to: field.parent.width - field.parent.width / 3
                    duration: 900
                    loops: Animation.Infinite
                }
            }
        }

        Item {
            objectName: "inputBoxLine"
            width: parent.width
            height: visible ? lineText.implicitHeight + 8 : 0
            visible: lineText.text.length > 0
            Icon {
                id: lineIcon
                visible: box.message.length > 0 && box.severity.length > 0
                anchors.left: parent.left
                anchors.leftMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                width: visible ? 16 : 0
                iconRole: "notice-" + (box.severity.length > 0 ? box.severity : "info")
            }
            Text {
                id: lineText
                objectName: "inputBoxMessage"
                anchors.left: lineIcon.right
                anchors.leftMargin: 6
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: box.message.length > 0 ? box.message : box.prompt
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: box.message.length === 0 ? Theme.muted
                     : box.severity === "error" ? Theme.danger
                     : box.severity === "warning" ? Theme.warning : Theme.text
            }
        }
    }
}
