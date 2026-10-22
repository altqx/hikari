// PU5 (#307): a shape preset's drawing (ShapeEditor.svgPath, in the
// drawing's units) fitted into the item: `view` is the part of the drawing's
// plane shown, centred and scaled to fit; the outline keeps its width in
// pixels at any scale. "Edit shapes" draws its list's thumbnails and its
// preview with it.
import QtQuick
import QtQuick.Shapes
import Hikari.Ui

Item {
    id: drawing
    property string path
    property rect view
    property color fill: Theme.select
    property color stroke: Theme.text
    property real strokeWidth: 1
    readonly property real scale: view.width > 0 && view.height > 0
                                  ? Math.min(width / view.width, height / view.height) : 1
    // The plane's point in the item.
    function mapX(x) { return (width - view.width * scale) / 2 + (x - view.x) * scale }
    function mapY(y) { return (height - view.height * scale) / 2 + (y - view.y) * scale }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        transform: [
            Translate { x: -drawing.view.x; y: -drawing.view.y },
            Scale { xScale: drawing.scale; yScale: drawing.scale },
            Translate {
                x: (drawing.width - drawing.view.width * drawing.scale) / 2
                y: (drawing.height - drawing.view.height * drawing.scale) / 2
            }
        ]
        ShapePath {
            fillColor: drawing.fill
            strokeColor: drawing.stroke
            strokeWidth: drawing.strokeWidth / Math.max(drawing.scale, 0.0001)
            joinStyle: ShapePath.RoundJoin
            fillRule: ShapePath.WindingFill
            PathSvg { path: drawing.path }
        }
    }
}
