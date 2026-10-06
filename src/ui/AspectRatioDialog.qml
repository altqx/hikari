import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// V4: VIDEO_ASPECT_RATIO's AspectRatioDialog (VideoBox.cpp:96-125): no
// title, "Aspect ratio: %5.3f" (width / height) over a 400-pixel slider from
// 100000 to 1000000, inverted (a taller picture to the left), opened at the
// pointer. Dragging, a click beside the thumb or the arrow keys set the
// video's aspect ratio at once (wxEVT_SCROLL_THUMBTRACK); the wheel does
// not (the slider's wheel sends wxEVT_SCROLL_CHANGED, which the dialog does
// not take). Closing keeps the ratio until the next video opens.
Dialog {
    id: dialog
    objectName: "aspectRatioDialog"
    required property VideoViewController view
    property int sliderValue: 0
    modal: true
    padding: 6
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // MoveToMousePosition: opens at the pointer, kept inside the window.
    function openAtCursor() {
        sliderValue = Math.max(100000, Math.min(1000000, view.aspectSliderValue()))
        label.text = view.aspectLabel(-1)
        slider.value = sliderValue
        open()
        const at = view.cursorIn(parent)
        x = Math.max(0, Math.min(at.x, (parent ? parent.width : width) - width))
        y = Math.max(0, Math.min(at.y, (parent ? parent.height : height) - height))
    }

    contentItem: ColumnLayout {
        spacing: 3
        Label {
            id: label
            objectName: "aspectRatioLabel"
        }
        Slider {
            id: slider
            objectName: "aspectRatioSlider"
            Layout.preferredWidth: 400
            from: 1000000
            to: 100000
            stepSize: 1
            Accessible.name: qsTr("Aspect ratio")
            onMoved: {
                dialog.sliderValue = Math.round(value)
                dialog.view.setAspectFromSlider(dialog.sliderValue)
                label.text = dialog.view.aspectLabel(dialog.sliderValue)
            }
        }
    }
}
