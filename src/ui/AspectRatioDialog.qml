pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Item {
    id: host
    objectName: "aspectRatioDialog"
    required property VideoViewController view
    property VideoController video: null
    property SettingsStore settings: null
    property var editedOpening: 0
    property double before: 0
    property int sliderValue: 0
    property string label: ""
    readonly property bool opened: ratioPopover.visible || (compactWindow !== null && compactWindow.visible)
    readonly property alias popover: ratioPopover
    readonly property DialogWindow compactWindow: windowLoader.item
    visible: false

    function start() {
        close()
        editedOpening = video ? video.opening() : 0
        before = view.aspectRatio()
        sliderValue = Math.max(100000, Math.min(1000000, view.aspectSliderValue()))
        label = view.aspectLabel(-1)
    }
    function openAtCursor(area) {
        start()
        const at = view.cursorIn(area)
        ratioPopover.openAt(area, null, Qt.rect(at.x, at.y, 1, 1))
    }
    function openWindow(from) {
        start()
        windowLoader.active = true
        compactWindow.openFrom(from || null)
    }
    function moved(value) {
        sliderValue = Math.round(value)
        view.setAspectFromSlider(sliderValue)
        label = view.aspectLabel(sliderValue)
    }
    function reset() {
        view.resetAspectRatio()
        sliderValue = Math.max(100000, Math.min(1000000, view.aspectSliderValue()))
        label = view.aspectLabel(-1)
    }
    function putBack() {
        view.setAspectRatio(before)
    }
    function close() {
        if (ratioPopover.visible)
            ratioPopover.keep()
        if (compactWindow && compactWindow.visible)
            compactWindow.accept()
    }

    Connections {
        target: host.video
        function onChanged() {
            if (host.opened && host.video.opening() !== host.editedOpening)
                host.close()
        }
    }

    Popover {
        id: ratioPopover
        objectName: "aspectRatioPopover"
        title: qsTr("Change aspect ratio")
        showTitle: true
        initialFocus: popoverForm.slider
        onReverted: host.putBack()
        AspectForm {
            id: popoverForm
            prefix: "aspectRatio"
        }
        Button {
            objectName: "aspectRatioReset"
            text: qsTr("Reset")
            Layout.alignment: Qt.AlignLeft
            onClicked: host.reset()
        }
    }
    Loader {
        id: windowLoader
        active: false
        sourceComponent: DialogWindow {
            id: compact
            objectName: "aspectRatioWindow"
            dialogId: "aspectRatio"
            settings: host.settings
            title: qsTr("Change aspect ratio")
            modal: true
            initialFocus: windowForm.slider
            defaultButton: keepButton
            onRejected: host.putBack()
            AspectForm {
                id: windowForm
                prefix: "aspectRatioWindow"
            }
            leadingActions: [
                Button {
                    objectName: "aspectRatioWindowReset"
                    text: qsTr("Reset")
                    onClicked: host.reset()
                }
            ]
            buttons: [
                PrimaryButton {
                    id: keepButton
                    objectName: "aspectRatioWindowOk"
                    text: qsTr("OK")
                    DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                    onClicked: compact.accept()
                },
                Button {
                    objectName: "aspectRatioWindowCancel"
                    text: qsTr("Cancel")
                    DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                    onClicked: compact.reject()
                }
            ]
        }
    }

    component AspectForm: ColumnLayout {
        id: form
        required property string prefix
        readonly property alias slider: slider
        spacing: 4
        Label {
            objectName: form.prefix + "Label"
            text: host.label
        }
        Slider {
            id: slider
            objectName: form.prefix + "Slider"
            Layout.preferredWidth: 400
            from: 1000000
            to: 100000
            stepSize: 1
            value: host.sliderValue
            Accessible.name: qsTr("Aspect ratio")
            onMoved: host.moved(value)
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Taller"); color: Theme.muted; font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85) }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("Wider"); color: Theme.muted; font.pixelSize: Math.round(Qt.application.font.pixelSize * 0.85) }
        }
    }
}
