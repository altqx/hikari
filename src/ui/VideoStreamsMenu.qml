pragma Translator: "HikariSub.Video"
import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: menu
    required property VideoController video
    property var rows: []
    iconRole: "video-streams"
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
            keepsMenuOpen: false
            onTriggered: {
                menu.video.selectStream(index)
                menu.rows = menu.video.streams()
            }
        }
        onObjectAdded: (index, object) => menu.insertItem(index, object)
        onObjectRemoved: (index, object) => menu.removeItem(object)
    }
}
