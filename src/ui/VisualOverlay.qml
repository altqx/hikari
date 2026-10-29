import QtQuick
import QtQuick.Controls
import Hikari.Ui

Item {
    id: overlayItem
    objectName: "visualOverlay"
    required property VisualToolsController tools
    property Item focusTarget: null
    property real panelHeight: 0
    property VideoViewController view: null
    // MUST KILL: cursorOverride -1 means the tool's cursor
    property int cursorOverride: -1
    signal contextMenuRequested(real x, real y)
    signal fullScreenRequested()
    signal pointerMoved(real x, real y)

    property bool pointerInside: false

    function route(kind, x, y, button, buttons, modifiers, steps) {
        if (kind === 1) {
            if (!pointerInside)
                return
            pointerInside = false
        } else {
            pointerInside = true
        }
        if (!view)
            return tools.pointer(kind, x, y, button, buttons, modifiers, steps ?? 0)
        const request = view.pointer(kind, x, y, button, buttons, modifiers, steps ?? 0)
        if (request === VideoViewController.ContextMenuRequest)
            contextMenuRequested(x, y)
        else if (request === VideoViewController.FullScreenRequest)
            fullScreenRequested()
        else if (request === VideoViewController.MovedOverVideo)
            pointerMoved(x, y)
    }

    function sync() {
        const dpr = overlayItem.Window.window ? overlayItem.Window.window.devicePixelRatio : Screen.devicePixelRatio
        tools.setViewport(width, height, panelHeight, dpr)
    }
    onWidthChanged: sync()
    onHeightChanged: sync()
    onPanelHeightChanged: sync()
    Component.onCompleted: sync()
    readonly property var shownIn: Window.window
    readonly property real shownScale: Window.window ? Window.window.devicePixelRatio : 1
    onShownInChanged: sync()
    onShownScaleChanged: sync()

    // A MouseArea moved into another window loses its hover and never sends exited: this handler sends the Leave.
    HoverHandler {
        id: pointerHover
        onHoveredChanged: {
            if (!hovered && !pointerArea.pressed)
                overlayItem.route(1, point.position.x, point.position.y, Qt.NoButton, Qt.NoButton, 0)
        }
    }

    MouseArea {
        id: pointerArea
        objectName: "visualPointer"
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        cursorShape: overlayItem.cursorOverride >= 0 ? overlayItem.cursorOverride
                     : overlayItem.view && overlayItem.view.zoomMode
                     ? [Qt.ArrowCursor, Qt.SizeHorCursor, Qt.SizeVerCursor][overlayItem.view.zoomCursor]
                     : overlayItem.tools.hideCursor ? Qt.BlankCursor
                     : overlayItem.tools.pointerShape
        onEntered: overlayItem.route(0, mouseX, mouseY, Qt.NoButton, pressedButtons, 0)
        onExited: overlayItem.route(1, mouseX, mouseY, Qt.NoButton, pressedButtons, 0)
        onPositionChanged: mouse => overlayItem.route(2, mouse.x, mouse.y, Qt.NoButton, mouse.buttons, mouse.modifiers)
        onPressed: mouse => {
            if (overlayItem.focusTarget)
                overlayItem.focusTarget.forceActiveFocus()
            overlayItem.route(3, mouse.x, mouse.y, mouse.button, mouse.buttons, mouse.modifiers)
        }
        onReleased: mouse => overlayItem.route(4, mouse.x, mouse.y, mouse.button, mouse.buttons, mouse.modifiers)
        onDoubleClicked: mouse => overlayItem.route(6, mouse.x, mouse.y, mouse.button, mouse.buttons, mouse.modifiers)
        onWheel: wheel => {
            overlayItem.route(5, wheel.x, wheel.y, Qt.NoButton, wheel.buttons, wheel.modifiers,
                              Math.round(wheel.angleDelta.y / 120))
            wheel.accepted = overlayItem.view !== null
        }
    }

    Canvas {
        id: canvas
        objectName: "visualCanvas"
        anchors.fill: parent
        property var shapes: overlayItem.tools.overlay
        onShapesChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            for (const s of shapes) {
                if (s.type === "polygon") {
                    const first = s.contours.length > 0 ? s.contours[0] : []
                    if (first.length < 2)
                        continue
                    if (s.fill !== "") {
                        ctx.beginPath()
                        for (const c of s.contours) {
                            if (c.length === 0)
                                continue
                            ctx.moveTo(c[0].x, c[0].y)
                            for (let i = 1; i < c.length; ++i)
                                ctx.lineTo(c[i].x, c[i].y)
                            ctx.closePath()
                        }
                        ctx.fillStyle = s.fill
                        ctx.fill()
                    }
                    if (s.border !== "") {
                        ctx.beginPath()
                        ctx.moveTo(first[0].x, first[0].y)
                        for (let i = 1; i < first.length; ++i)
                            ctx.lineTo(first[i].x, first[i].y)
                        ctx.closePath()
                        ctx.strokeStyle = s.border
                        ctx.lineWidth = 1
                        ctx.stroke()
                    }
                } else if (s.type === "line") {
                    ctx.strokeStyle = s.color
                    ctx.lineWidth = s.width
                    ctx.beginPath()
                    ctx.moveTo(s.x1, s.y1)
                    ctx.lineTo(s.x2, s.y2)
                    ctx.stroke()
                } else if (s.type === "circle") {
                    ctx.beginPath()
                    ctx.arc(s.x, s.y, s.radius, 0, 2 * Math.PI)
                    if (s.filled) {
                        ctx.fillStyle = s.color
                        ctx.fill()
                    } else {
                        ctx.strokeStyle = s.color
                        ctx.lineWidth = 1
                        ctx.stroke()
                    }
                } else if (s.type === "text") {
                    ctx.font = "bold " + s.pixelSize + "px \"" + overlayItem.tools.labelFamily + "\""
                    ctx.textBaseline = "top"
                    if (s.outline) {
                        ctx.fillStyle = "#000000"
                        for (let dx = -1; dx <= 1; ++dx)
                            for (let dy = -1; dy <= 1; ++dy)
                                if (dx !== 0 || dy !== 0)
                                    ctx.fillText(s.text, s.x + dx, s.y + dy)
                    }
                    ctx.fillStyle = s.color
                    ctx.fillText(s.text, s.x, s.y)
                }
            }
        }
    }

    Label {
        objectName: "visualWarning"
        visible: text.length > 0
        text: overlayItem.tools.warning
        color: Theme.danger
        font.bold: true
        font.pixelSize: Math.max(1, overlayItem.tools.videoRect.width / 20)
        padding: Math.max(2, font.pixelSize / 3)
        background: Rectangle {
            color: Qt.alpha(Theme.panel, 0.85)
            border.color: Theme.line
            radius: 4
        }
        horizontalAlignment: Text.AlignHCenter
        x: overlayItem.tools.videoRect.x + (overlayItem.tools.videoRect.width - width) / 2
        y: overlayItem.tools.videoRect.y + (overlayItem.tools.videoRect.height - height) / 2
    }
}
