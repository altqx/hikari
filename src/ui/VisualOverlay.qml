import QtQuick
import QtQuick.Controls
import Hikari.Ui

// T1: the visual tools over the Video canvas. The pointer goes to the active
// tool (legacy VideoBox::OnMouseEvent hands it to the Visuals) and what the
// tool draws is painted here, apart from the frame (docs/qt/media.md: tool
// handles stay separate from the decoded textures). The crosshair hides the
// pointer over the video. The Line warning of the other tools is drawn
// centred on the video in red (Visuals::DrawWarning), unless
// VIDEO_VISUAL_WARNINGS_OFF.
Item {
    id: overlayItem
    objectName: "visualOverlay"
    required property VisualToolsController tools
    // The panel that takes the keys (and Esc) after a click on the video.
    property Item focusTarget: null
    // The height of the panel below the canvas (legacy m_PanelHeight).
    property real panelHeight: 0

    function sync() {
        const dpr = overlayItem.Window.window ? overlayItem.Window.window.devicePixelRatio : Screen.devicePixelRatio
        tools.setViewport(width, height, panelHeight, dpr)
    }
    onWidthChanged: sync()
    onHeightChanged: sync()
    onPanelHeightChanged: sync()
    Component.onCompleted: sync()

    MouseArea {
        id: pointerArea
        objectName: "visualPointer"
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        cursorShape: overlayItem.tools.hideCursor ? Qt.BlankCursor : Qt.ArrowCursor
        onEntered: overlayItem.tools.pointer(0, mouseX, mouseY, Qt.NoButton, pressedButtons, 0)
        onExited: overlayItem.tools.pointer(1, mouseX, mouseY, Qt.NoButton, pressedButtons, 0)
        onPositionChanged: mouse => overlayItem.tools.pointer(2, mouse.x, mouse.y, Qt.NoButton, mouse.buttons, mouse.modifiers)
        onPressed: mouse => {
            if (overlayItem.focusTarget)
                overlayItem.focusTarget.forceActiveFocus() // legacy SetFocus on the video
            overlayItem.tools.pointer(3, mouse.x, mouse.y, mouse.button, mouse.buttons, mouse.modifiers)
        }
        onReleased: mouse => overlayItem.tools.pointer(4, mouse.x, mouse.y, mouse.button, mouse.buttons, mouse.modifiers)
        onWheel: wheel => {
            overlayItem.tools.pointer(5, wheel.x, wheel.y, Qt.NoButton, wheel.buttons, wheel.modifiers,
                                      Math.round(wheel.angleDelta.y / 120))
            wheel.accepted = false // the zoom and volume wheel stay the panel's (V4)
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
                if (s.type === "line") {
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
                    if (s.outline) { // DRAWOUTTEXT: the eight one-pixel offsets in black
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

    // Visuals::DrawWarning (Visuals.cpp:531-546, the Direct3D path): bold
    // Tahoma a twentieth of the video's width high, red with a black outline
    // (DRAWOUTTEXT), centred in the rectangle from the window's corner to the
    // video's right and bottom edges ({0, 0, VideoSize.width, VideoSize.height},
    // where SizeChanged's width and height are m_BackBufferRect's right and
    // bottom, RendererVideo.cpp:321).
    Label {
        objectName: "visualWarning"
        visible: text.length > 0
        text: overlayItem.tools.warning
        color: "red"
        style: Text.Outline
        styleColor: "black"
        font.family: "Tahoma"
        font.bold: true
        font.pixelSize: Math.max(1, overlayItem.tools.videoRect.width / 20)
        horizontalAlignment: Text.AlignHCenter
        x: (overlayItem.tools.videoRect.x + overlayItem.tools.videoRect.width - width) / 2
        y: (overlayItem.tools.videoRect.y + overlayItem.tools.videoRect.height - height) / 2
    }
}
