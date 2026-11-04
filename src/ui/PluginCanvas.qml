import QtQuick
import QtQuick.Controls
import Hikari.Ui

Item {
    id: surface
    property var node: ({})
    property string packageRoot
    signal pointer(var event)

    readonly property var roles: ({
        accent: Theme.accent, text: Theme.text, muted: Theme.muted, line: Theme.line, danger: Theme.danger,
        warning: Theme.warning, success: Theme.success, panel: Theme.panel, field: Theme.field
    })
    function listOf(value) { return value && typeof value === "object" && typeof value.length === "number" ? value : [] }
    function literal(value) { return typeof value === "string" && value.startsWith("&H") }
    // ASS colours are &HAABBGGRR, with alpha as transparency.
    function colour(value) {
        if (!value)
            return null
        if (!surface.literal(value))
            return surface.roles[value] ?? null
        const m = /^&H([0-9A-Fa-f]{2})?([0-9A-Fa-f]{6})&?$/.exec(value)
        const v = parseInt(m[2], 16)
        const a = m[1] ? parseInt(m[1], 16) : 0
        return Qt.rgba((v & 255) / 255, ((v >> 8) & 255) / 255, ((v >> 16) & 255) / 255, (255 - a) / 255)
    }
    function imageUrl(src) { return surface.packageRoot + "/" + src }

    implicitWidth: node.width ?? 200
    implicitHeight: node.height ?? 100
    Accessible.role: Accessible.Chart
    Accessible.name: node.name ?? ""
    Accessible.description: node.description ?? ""

    onNodeChanged: canvas.requestPaint()
    Connections {
        target: Theme
        function onChanged() { canvas.requestPaint() }
    }
    FontMetrics { id: metrics }

    Canvas {
        id: canvas
        objectName: "pluginCanvasPaint"
        anchors.fill: parent
        onImageLoaded: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const ops = surface.listOf(surface.node.draw)
            for (const op of ops) {
                ctx.save()
                const fill = surface.colour(op.fill)
                const stroke = surface.colour(op.stroke)
                const outline = Theme.highContrast && surface.literal(op.fill)
                ctx.lineWidth = op.width ?? 1
                if (surface.listOf(op.dash).length > 0)
                    ctx.setLineDash(Array.from(op.dash))
                ctx.beginPath()
                if (op.op === "rect") {
                    if (op.radius)
                        ctx.roundedRect(op.x, op.y, op.w, op.h, op.radius, op.radius)
                    else
                        ctx.rect(op.x, op.y, op.w, op.h)
                } else if (op.op === "ellipse") {
                    ctx.ellipse(op.x, op.y, op.w, op.h)
                } else if (op.op === "line") {
                    ctx.moveTo(op.x1, op.y1)
                    ctx.lineTo(op.x2, op.y2)
                } else if (op.op === "polyline") {
                    ctx.moveTo(op.points[0], op.points[1])
                    for (let i = 2; i + 1 < op.points.length; i += 2)
                        ctx.lineTo(op.points[i], op.points[i + 1])
                    if (op.closed === true)
                        ctx.closePath()
                } else if (op.op === "path") {
                    ctx.path = op.d
                } else if (op.op === "text") {
                    const size = op.size === "small" ? metrics.font.pixelSize - 2 : metrics.font.pixelSize
                    ctx.font = size + "px \"" + metrics.font.family + "\""
                    ctx.textAlign = op.align === "center" ? "center" : op.align === "right" ? "right" : "left"
                    ctx.fillStyle = fill ?? Theme.text
                    ctx.fillText(op.text, op.x, op.y)
                    ctx.restore()
                    continue
                } else if (op.op === "image") {
                    const url = surface.imageUrl(op.src)
                    if (canvas.isImageLoaded(url))
                        ctx.drawImage(url, op.x, op.y, op.w, op.h)
                    else if (!canvas.isImageLoading(url) && !canvas.isImageError(url))
                        canvas.loadImage(url)
                    ctx.restore()
                    continue
                }
                if (fill && op.op !== "line") {
                    ctx.fillStyle = fill
                    ctx.fill()
                }
                if (stroke || outline) {
                    ctx.strokeStyle = stroke ?? Theme.text
                    ctx.stroke()
                }
                ctx.restore()
            }
        }
    }

    Rectangle {
        objectName: "pluginCanvasTip"
        visible: !!surface.node.tip
        x: Math.max(0, Math.min((surface.node.tip?.x ?? 0), surface.width - width))
        y: Math.max(0, Math.min((surface.node.tip?.y ?? 0), surface.height - height))
        width: tipText.implicitWidth + 12
        height: tipText.implicitHeight + 6
        color: Theme.raised
        border.color: Theme.line
        Label {
            id: tipText
            anchors.centerIn: parent
            text: surface.node.tip?.text ?? ""
            textFormat: Text.PlainText
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: surface.node.hover === true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        function report(type, mouse) {
            surface.pointer({ type: type, x: Math.round(mouse.x), y: Math.round(mouse.y), buttons: mouse.buttons,
                              modifiers: mouse.modifiers })
        }
        onPressed: mouse => report("press", mouse)
        onReleased: mouse => report("release", mouse)
        onPositionChanged: mouse => report("move", mouse)
        onExited: surface.pointer({ type: "leave" })
    }
}
