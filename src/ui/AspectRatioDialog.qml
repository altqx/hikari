pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// V4: VIDEO_ASPECT_RATIO's AspectRatioDialog (VideoBox.cpp:96-125): titled
// with the command (legacy had none, TI1-popup-titles), "Aspect ratio: %5.3f" (width / height) over a 400-pixel slider from
// 100000 to 1000000, inverted (a taller picture to the left). Dragging, a
// click beside the thumb or the arrow keys set the video's aspect ratio at
// once (wxEVT_SCROLL_THUMBTRACK); the wheel does not (the slider's wheel
// sends wxEVT_SCROLL_CHANGED, which the dialog does not take). The ratio
// lasts until the next video opens.
// PU4 (#306, PU4-aspect-ratio), by PK1's small forms rule: a popover at the
// pointer from the Video context menu (legacy MoveToMousePosition), a compact
// window on DG1's frame from the hotkey. Esc (and the window's Cancel or
// Close) puts back the ratio it opened with; Enter, a press outside the
// popover or the window's OK keep it (legacy kept it on any closing). The
// slider's ends are named, and Reset gives the video's own ratio back.
// Another video opened meanwhile (the main window stays in use under the
// fullscreen video window's) ends the edit: the ratio it opened with was
// the other video's.
Item {
    id: host
    objectName: "aspectRatioDialog"
    required property VideoViewController view
    property VideoController video: null
    property SettingsStore settings: null
    // The video the edit began on.
    property string editedVideo: ""
    // The ratio it opened with (Esc, Cancel), the slider's value and its label.
    property double before: 0
    property int sliderValue: 0
    property string label: ""
    readonly property bool opened: popover.visible || (compactWindow !== null && compactWindow.visible)
    readonly property alias popover: popover
    // The compact window, made at its first opening (null till then): every
    // window made at the start costs the program (PK1).
    readonly property DialogWindow compactWindow: windowLoader.item
    visible: false

    function start() {
        editedVideo = video ? video.path() : ""
        before = view.aspectRatio()
        sliderValue = Math.max(100000, Math.min(1000000, view.aspectSliderValue()))
        label = view.aspectLabel(-1)
    }
    // The context menu's Change aspect ratio: the popover at the pointer in
    // `area` (the content of the window the menu was in).
    function openAtCursor(area) {
        start()
        const at = view.cursorIn(area)
        popover.openAt(area, null, Qt.rect(at.x, at.y, 1, 1))
    }
    // VIDEO_ASPECT_RATIO: the compact window, opened from `from` (null: the
    // window with the keyboard).
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
    // Ends the edit, keeping the ratio.
    function close() {
        if (popover.visible)
            popover.keep()
        if (compactWindow && compactWindow.visible)
            compactWindow.accept()
    }

    Connections {
        target: host.video
        function onChanged() {
            if (host.opened && host.video.path() !== host.editedVideo)
                host.close()
        }
    }

    Popover {
        id: popover
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

    // The label over the slider, and the slider's ends named under it.
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
