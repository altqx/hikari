// Y7: legacy ColorPickerScreenDropper (ColorPicker.cpp:326-457): the 7x7
// screen pixels last captured, each drawn 8 pixels wide, white until the
// first capture; a left press on a pixel picks it (SendGetColorEvent).
pragma ComponentBehavior: Bound
import QtQuick
import Hikari.Ui

Rectangle {
    id: view
    // 49 {r, g, b}, row by row (ScreenSampler.sample).
    property var cells: []
    signal picked(var colour)

    readonly property int size: 7
    readonly property int magnification: 8
    implicitWidth: size * magnification + 2
    implicitHeight: size * magnification + 2
    color: "transparent"
    border.color: Theme.line // legacy wxSTATIC_BORDER
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("Screen pixels around the pointer")

    // The centre pixel, the one a right click picks (SendGetColorEvent(3, 3)).
    function centre() {
        return cellAt(3, 3)
    }
    function cellAt(x, y) {
        const c = cells.length === size * size ? cells[y * size + x] : undefined
        return c !== undefined ? { r: c.r, g: c.g, b: c.b, a: 0 } : { r: 255, g: 255, b: 255, a: 0 }
    }

    Grid {
        x: 1
        y: 1
        columns: view.size
        Repeater {
            model: view.size * view.size
            Rectangle {
                id: pixel
                required property int index
                width: view.magnification
                height: view.magnification
                readonly property var cell: view.cellAt(pixel.index % view.size, Math.floor(pixel.index / view.size))
                color: Qt.rgba(pixel.cell.r / 255, pixel.cell.g / 255, pixel.cell.b / 255, 1)
            }
        }
    }
    MouseArea {
        objectName: "dropperCells"
        anchors.fill: parent
        anchors.margins: 1
        cursorShape: Qt.CrossCursor // legacy wxCROSS_CURSOR
        onPressed: (mouse) => {
            const x = Math.floor(mouse.x / view.magnification)
            const y = Math.floor(mouse.y / view.magnification)
            if (x >= 0 && y >= 0 && x < view.size && y < view.size)
                view.picked(view.cellAt(x, y))
        }
    }
}
