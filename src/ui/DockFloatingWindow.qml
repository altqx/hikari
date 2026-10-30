import QtQuick
import QtQuick.Controls
import Hikari.Ui
import com.kdab.dockwidgets 2.0 as KDDW
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDWViews

Item {
    id: root
    objectName: "dockFloatingWindow"

    readonly property KDDW.FloatingWindowView floatingWindowCpp: parent
    readonly property KDDW.TitleBarView titleBarCpp: floatingWindowCpp ? floatingWindowCpp.titleBar : null
    readonly property KDDW.DropAreaView dropAreaCpp: floatingWindowCpp ? floatingWindowCpp.dropArea : null
    // Compared, not just read: a compiled binding drops an unused read and its dependency.
    readonly property int shadow: Docking.windowTitleRevision >= 0 ? Docking.windowShadow(Window.window) : 0
    // KDDockWidgets reads titleBarHeight and margins.
    readonly property int titleBarHeight: titleBar.item && titleBar.item.visible ? titleBar.item.heightWhenVisible : 0
    readonly property int margins: shadow + 1

    anchors.fill: parent

    onTitleBarHeightChanged: {
        if (floatingWindowCpp)
            floatingWindowCpp.geometryUpdated()
    }
    onMarginsChanged: {
        if (floatingWindowCpp)
            floatingWindowCpp.geometryUpdated()
    }

    // Qt places the popup overlay when the content item resizes, which KDDW does before the window; re-place it here.
    readonly property size windowSize: Qt.size(Window.width, Window.height)
    onWindowSizeChanged: placeOverlay()
    Component.onCompleted: Qt.callLater(placeOverlay)
    function placeOverlay() {
        const w = root.Window.window
        const overlay = w ? root.Overlay.overlay : null
        if (!overlay || !w.contentItem)
            return
        overlay.x = (w.width - w.contentItem.width) / 2
        overlay.y = (w.height - w.contentItem.height) / 2
    }

    Repeater {
        model: root.shadow
        delegate: Rectangle {
            required property int index
            anchors.fill: parent
            anchors.margins: index
            radius: 3 + root.shadow - index
            color: "transparent"
            border.width: 1
            border.color: Color.rgba(0, 0, 0, 0.14 * Math.pow((index + 1) / root.shadow, 2))
        }
    }
    Rectangle {
        objectName: "dockFloatingFrame"
        anchors.fill: parent
        anchors.margins: root.shadow
        color: Theme.field
        border.width: 1
        border.color: Theme.line
        radius: root.shadow > 0 ? 3 : 0
    }

    Loader {
        id: titleBar
        readonly property KDDW.TitleBarView titleBarCpp: root.titleBarCpp
        source: KDDW.Singletons.widgetFactory.titleBarFilename()
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
            margins: root.margins
        }
    }

    KDDWViews.DropArea {
        id: dropArea
        dropAreaCpp: root.dropAreaCpp
        anchors {
            left: parent.left
            right: parent.right
            top: titleBar.item && titleBar.item.visible ? titleBar.bottom : parent.top
            bottom: parent.bottom
            leftMargin: root.margins
            rightMargin: root.margins
            bottomMargin: root.margins
            topMargin: titleBar.item && titleBar.item.visible ? 0 : root.margins
        }
    }
    onDropAreaCppChanged: {
        if (dropAreaCpp) {
            dropAreaCpp.parent = dropArea
            dropAreaCpp.anchors.fill = dropArea
        }
    }

    readonly property int grip: Math.max(shadow, Docking.resizeGrip)
    component ResizeEdge: MouseArea {
        required property int edges
        z: 10
        cursorShape: {
            const horizontal = (edges & (Qt.LeftEdge | Qt.RightEdge)) !== 0
            const vertical = (edges & (Qt.TopEdge | Qt.BottomEdge)) !== 0
            if (horizontal && vertical)
                return (edges === (Qt.LeftEdge | Qt.TopEdge) || edges === (Qt.RightEdge | Qt.BottomEdge))
                       ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
            return horizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor
        }
        onPressed: root.Window.window.startSystemResize(edges)
    }
    readonly property int corner: grip + 6
    ResizeEdge { objectName: "resizeLeft"; edges: Qt.LeftEdge; x: 0; y: root.corner; width: root.grip; height: root.height - 2 * root.corner }
    ResizeEdge { objectName: "resizeRight"; edges: Qt.RightEdge; x: root.width - root.grip; y: root.corner; width: root.grip; height: root.height - 2 * root.corner }
    ResizeEdge { objectName: "resizeTop"; edges: Qt.TopEdge; x: root.corner; y: 0; width: root.width - 2 * root.corner; height: root.grip }
    ResizeEdge { objectName: "resizeBottom"; edges: Qt.BottomEdge; x: root.corner; y: root.height - root.grip; width: root.width - 2 * root.corner; height: root.grip }
    ResizeEdge { edges: Qt.LeftEdge | Qt.TopEdge; x: 0; y: 0; width: root.corner; height: root.grip }
    ResizeEdge { edges: Qt.LeftEdge | Qt.TopEdge; x: 0; y: 0; width: root.grip; height: root.corner }
    ResizeEdge { edges: Qt.RightEdge | Qt.TopEdge; x: root.width - root.corner; y: 0; width: root.corner; height: root.grip }
    ResizeEdge { edges: Qt.RightEdge | Qt.TopEdge; x: root.width - root.grip; y: 0; width: root.grip; height: root.corner }
    ResizeEdge { edges: Qt.LeftEdge | Qt.BottomEdge; x: 0; y: root.height - root.grip; width: root.corner; height: root.grip }
    ResizeEdge { edges: Qt.LeftEdge | Qt.BottomEdge; x: 0; y: root.height - root.corner; width: root.grip; height: root.corner }
    ResizeEdge { edges: Qt.RightEdge | Qt.BottomEdge; x: root.width - root.corner; y: root.height - root.grip; width: root.corner; height: root.grip }
    ResizeEdge { edges: Qt.RightEdge | Qt.BottomEdge; x: root.width - root.grip; y: root.height - root.corner; width: root.grip; height: root.corner }
}
