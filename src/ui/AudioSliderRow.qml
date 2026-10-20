import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// AU1 (#267): a row of the audio panel's zoom and speaker popovers: a name,
// the slider laid flat and its value. They hold the slider column that left
// the panel (legacy AudioBox's HorizontalZoom, VerticalZoom and VolumeBar).
// `moved` gives the slider's whole value after each move by the user.
Item {
    id: row
    property alias text: label.text
    property alias from: slider.from
    property alias to: slider.to
    property alias sliderName: slider.objectName
    property real value
    property string readout
    signal moved(int value)

    implicitWidth: 320
    implicitHeight: 32
    width: parent ? parent.width : implicitWidth

    RowLayout {
        anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
        spacing: 10
        Label {
            id: label
            Layout.preferredWidth: 76
            elide: Text.ElideRight
        }
        Slider {
            id: slider
            Layout.fillWidth: true
            stepSize: 1
            value: row.value
            onMoved: row.moved(Math.round(value))
            Accessible.name: label.text
        }
        Label {
            text: row.readout
            font.bold: true
            font.features: ({ "tnum": 1 })
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 44
        }
    }
}
