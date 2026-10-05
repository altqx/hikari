import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

// V3: legacy ProgressSink "Indexing video" (ProviderFFMS2.cpp:89-97) inline
// in the Video panel rather than a modal window: the indexing's progress and
// Cancel (FFMS_CancelIndexing; the panel then has no video). Subtitle
// editing goes on meanwhile.
Frame {
    id: frame
    required property VideoController video
    objectName: "videoIndexing"
    visible: video.indexing && !video.dummy
    RowLayout {
        anchors.fill: parent
        Label {
            text: qsTr("Indexing video")
        }
        ProgressBar {
            objectName: "videoIndexingProgress"
            Layout.fillWidth: true
            from: 0
            to: 1
            indeterminate: frame.video.indexingProgress < 0
            value: Math.max(0, frame.video.indexingProgress)
            Accessible.name: qsTr("Indexing video")
        }
        Button {
            objectName: "cancelIndexing"
            text: qsTr("Cancel")
            onClicked: frame.video.cancelIndexing()
        }
    }
}
