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
    readonly property real scale: view.width > 0 || view.height > 0
                                  ? Math.min(view.width > 0 ? width / view.width : Infinity,
                                             view.height > 0 ? height / view.height : Infinity) : 1
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
