import QtQuick

Item {
    id: chevron
    property color color
    property bool up: false
    implicitWidth: 10
    implicitHeight: 6
    Repeater {
        model: 2
        Rectangle {
            required property int index
            width: Math.SQRT2 * chevron.height
            height: 1.5
            antialiasing: true
            color: chevron.color
            x: chevron.width / 4 - width / 2 + index * chevron.width / 2
            y: chevron.height / 2 - height / 2
            rotation: (index === 0) !== chevron.up ? 45 : -45
        }
    }
}
