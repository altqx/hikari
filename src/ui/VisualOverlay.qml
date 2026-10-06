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
    // V4: the view's commands take the pointer first (VideoBox::OnMouseEvent's
    // order: the zoom mode, the wheel's zoom, then the tool, the context
    // menu and VIDEO_PAUSE_ON_CLICK); without one the tool has it alone.
    property VideoViewController view: null
    // V5: fullscreen's pointer (VideoBox::OnMouseEvent's fullscreen part):
    // each move reported, and the cursor it asks for over the video (-1: the
    // tool's).
    property int cursorOverride: -1
    // D2: false in the player layout: no tool takes the pointer or draws
    // (the view's commands still do, VideoViewController.toolsOff).
    property bool toolsShown: true
    signal contextMenuRequested(real x, real y)
    signal fullScreenRequested()
    signal pointerMoved(real x, real y)

    function route(kind, x, y, button, buttons, modifiers, steps) {
        if (!view)
            return toolsShown ? tools.pointer(kind, x, y, button, buttons, modifiers, steps ?? 0) : undefined
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
    // V5: moved into the fullscreen window (another screen, another scale).
    readonly property var shownIn: Window.window
    readonly property real shownScale: Window.window ? Window.window.devicePixelRatio : 1
    onShownInChanged: sync()
    onShownScaleChanged: sync()

    MouseArea {
        id: pointerArea
        objectName: "visualPointer"
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        cursorShape: overlayItem.view && overlayItem.view.zoomMode
                     ? [Qt.ArrowCursor, Qt.SizeHorCursor, Qt.SizeVerCursor][overlayItem.view.zoomCursor]
                     : overlayItem.cursorOverride >= 0 ? overlayItem.cursorOverride
                     : overlayItem.toolsShown && overlayItem.tools.hideCursor ? Qt.BlankCursor : Qt.ArrowCursor
        onEntered: overlayItem.route(0, mouseX, mouseY, Qt.NoButton, pressedButtons, 0)
        onExited: overlayItem.route(1, mouseX, mouseY, Qt.NoButton, pressedButtons, 0)
        onPositionChanged: mouse => overlayItem.route(2, mouse.x, mouse.y, Qt.NoButton, mouse.buttons, mouse.modifiers)
        onPressed: mouse => {
            if (overlayItem.focusTarget)
                overlayItem.focusTarget.forceActiveFocus() // legacy SetFocus on the video
            overlayItem.route(3, mouse.x, mouse.y, mouse.button, mouse.buttons, mouse.modifiers)
        }
        onReleased: mouse => overlayItem.route(4, mouse.x, mouse.y, mouse.button, mouse.buttons, mouse.modifiers)
        // T2: legacy's wxEVT_LEFT_DCLICK (Position puts the Line there).
        onDoubleClicked: mouse => overlayItem.route(6, mouse.x, mouse.y, mouse.button, mouse.buttons, mouse.modifiers)
        onWheel: wheel => {
            overlayItem.route(5, wheel.x, wheel.y, Qt.NoButton, wheel.buttons, wheel.modifiers,
                              Math.round(wheel.angleDelta.y / 120))
            wheel.accepted = overlayItem.view !== null // V4: the view takes the wheel over the video
        }
    }

    Canvas {
        id: canvas
        objectName: "visualCanvas"
        anchors.fill: parent
        visible: overlayItem.toolsShown
        property var shapes: overlayItem.tools.overlay
        onShapesChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            for (const s of shapes) {
                if (s.type === "polygon") {
                    // T2-T4: filled contours (one path, non-zero: the
                    // rectangle clip's two fans without a seam), then the
                    // first contour's one-pixel border; an empty fill or
                    // border is not drawn (T3: RotationZ's ring).
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
    // (DRAWOUTTEXT). Direct3D centred it in {0, 0, VideoSize.width,
    // VideoSize.height}, the window's corner to the video's right and bottom
    // edges (RendererVideo.cpp:321), off-centre with a bar. Approved
    // departure T1-warning-centre (docs/qt/compatibility-decisions.md): it is
    // centred on the video rectangle, as legacy's wx path did
    // (DrawWarningWx, Visuals.cpp:479-493).
    Label {
        objectName: "visualWarning"
        visible: overlayItem.toolsShown && text.length > 0
        text: overlayItem.tools.warning
        // Legacy's text, size and centring; drawn in the theme's danger
        // colour and the application's font on a scrim of the panel colour
        // (legacy's pure red on a black outline in Tahoma read as an error
        // banner).
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
