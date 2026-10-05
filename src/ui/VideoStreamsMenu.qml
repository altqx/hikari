import QtQuick
import QtQuick.Controls
import Hikari.Ui

// V3: the stream menu (legacy VideoBox::ContextMenu's stream entries,
// VideoBox.cpp:997-1008, and EnableStream, :1042-1045): the video's audio
// tracks, the one general playback plays marked; choosing one switches it.
// The rewrite always has the editor, so subtitle streams are never listed
// (legacy stops at the first one with the editor on). Its own submenu here;
// the video context menu (V4) can show the same entries.
ShellMenu {
    id: menu
    required property VideoController video
    property var rows: []
    title: qsTr("Streams")
    enabled: video.hasVideo && video.streamCount > 0
    onAboutToShow: rows = video.streams()
    Instantiator {
        model: menu.rows
        delegate: ShellMenuItem {
            required property var modelData
            required property int index
            objectName: "videoStream" + index
            text: modelData.label
            checkable: true
            checked: modelData.checked
            onTriggered: {
                menu.video.selectStream(index)
                menu.rows = menu.video.streams()
            }
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
}
