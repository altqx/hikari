import QtQuick
import Hikari.Ui

Item {
    id: slot
    property Item toolbar: null
    objectName: "dockToolbarSlot"
    visible: toolbar !== null
    function release(keep: Item) {
        for (let i = children.length - 1; i >= 0; --i) {
            const item = children[i]
            if (item && item !== keep)
                item.parent = null
        }
    }
    function host() {
        release(toolbar)
        if (toolbar) {
            toolbar.parent = slot
            toolbar.anchors.fill = slot
        }
    }
    onToolbarChanged: host()
    onVisibleChanged: if (visible && toolbar && toolbar.parent !== slot) host()
    Component.onCompleted: host()
    Component.onDestruction: release(null)
}
