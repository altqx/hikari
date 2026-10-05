import QtQuick
import Hikari.Ui
import com.kdab.dockwidgets 2.0 as KDDW
import "qrc:/kddockwidgets/qtquick/views/qml/" as KDDWViews

// D3: a floating panel window, after MuseScore's DockFloatingWindow.qml
// (docs/research/musescore-docking.md §1, §5): a borderless tool window
// whose panel keeps its header (DockTabBar.qml; a window of several groups
// gets DockTitleBar.qml above them), framed in the panel body's colour with
// a 1-pixel boundary, 3-pixel corners and a drawn 8-pixel shadow around it.
// The window is transparent around the frame on every platform (the
// adapter, ui/docking.cpp, makes it so), and the shadow's edges resize it
// through the window system (startSystemResize). Loaded through the
// adapter's view factory.
Item {
    id: root
    objectName: "dockFloatingWindow"

    readonly property KDDW.FloatingWindowView floatingWindowCpp: parent
    readonly property KDDW.TitleBarView titleBarCpp: floatingWindowCpp ? floatingWindowCpp.titleBar : null
    readonly property KDDW.DropAreaView dropAreaCpp: floatingWindowCpp ? floatingWindowCpp.dropArea : null
    readonly property int shadow: Docking.floatingShadow
    // Read by the engine: the title bar's height and the contents' inset.
    readonly property int titleBarHeight: titleBar.item && titleBar.item.visible ? titleBar.item.heightWhenVisible : 0
    readonly property int margins: shadow + 1

    anchors.fill: parent

    onTitleBarHeightChanged: {
        if (floatingWindowCpp)
            floatingWindowCpp.geometryUpdated()
    }

    // The shadow: rings fading outwards from the frame.
    Repeater {
        model: root.shadow
        delegate: Rectangle {
            required property int index
            anchors.fill: parent
            anchors.margins: index
            radius: 3 + root.shadow - index
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(0, 0, 0, 0.14 * Math.pow((index + 1) / root.shadow, 2))
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
        // The engine's drop area lives in the visual one, as the engine's own
        // FloatingWindow.qml has it.
        if (dropAreaCpp) {
            dropAreaCpp.parent = dropArea
            dropAreaCpp.anchors.fill = dropArea
        }
    }

    // Resizing from the shadow, through the compositor.
    component ResizeEdge: MouseArea {
        required property int edges
        enabled: root.shadow > 0
        visible: enabled
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
    readonly property int corner: shadow + 6
    ResizeEdge { objectName: "resizeLeft"; edges: Qt.LeftEdge; x: 0; y: root.corner; width: root.shadow; height: root.height - 2 * root.corner }
    ResizeEdge { objectName: "resizeRight"; edges: Qt.RightEdge; x: root.width - root.shadow; y: root.corner; width: root.shadow; height: root.height - 2 * root.corner }
    ResizeEdge { objectName: "resizeTop"; edges: Qt.TopEdge; x: root.corner; y: 0; width: root.width - 2 * root.corner; height: root.shadow }
    ResizeEdge { objectName: "resizeBottom"; edges: Qt.BottomEdge; x: root.corner; y: root.height - root.shadow; width: root.width - 2 * root.corner; height: root.shadow }
    ResizeEdge { edges: Qt.LeftEdge | Qt.TopEdge; x: 0; y: 0; width: root.corner; height: root.shadow }
    ResizeEdge { edges: Qt.LeftEdge | Qt.TopEdge; x: 0; y: 0; width: root.shadow; height: root.corner }
    ResizeEdge { edges: Qt.RightEdge | Qt.TopEdge; x: root.width - root.corner; y: 0; width: root.corner; height: root.shadow }
    ResizeEdge { edges: Qt.RightEdge | Qt.TopEdge; x: root.width - root.shadow; y: 0; width: root.shadow; height: root.corner }
    ResizeEdge { edges: Qt.LeftEdge | Qt.BottomEdge; x: 0; y: root.height - root.shadow; width: root.corner; height: root.shadow }
    ResizeEdge { edges: Qt.LeftEdge | Qt.BottomEdge; x: 0; y: root.height - root.corner; width: root.shadow; height: root.corner }
    ResizeEdge { edges: Qt.RightEdge | Qt.BottomEdge; x: root.width - root.corner; y: root.height - root.shadow; width: root.corner; height: root.shadow }
    ResizeEdge { edges: Qt.RightEdge | Qt.BottomEdge; x: root.width - root.shadow; y: root.height - root.corner; width: root.shadow; height: root.corner }
}
