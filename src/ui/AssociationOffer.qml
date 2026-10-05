import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// P9: legacy Notebook::LoadVideo's question when subtitles open (Notebook.cpp:
// 1191-1222 at 20d647c4), shown inline over the Video panel so the Document
// stays editable: "Associated files:" with the Script Info video, audio and
// keyframes, "Video from directory:" with the same-named video beside the
// subtitles; "Load associated" and "Load from directory" (each "Yes" when
// alone), "No", and "Apply to All" for the rest of the same opening
// (HikariMessageDialog's ASK_ONCE check). Escape answers No, as legacy's
// escape id did.
Frame {
    id: offer
    objectName: "associationOffer"
    required property var video
    visible: video.offering
    RowLayout {
        anchors.fill: parent
        Label {
            objectName: "associationText"
            text: offer.video.offer
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        CheckBox {
            objectName: "associationApplyToAll"
            text: qsTr("Apply to All")
            checked: offer.video.offerApplyToAll
            onToggled: offer.video.offerApplyToAll = checked
        }
        Button {
            objectName: "loadAssociated"
            visible: offer.video.offerAssociatedLabel.length > 0
            text: offer.video.offerAssociatedLabel
            onClicked: offer.video.loadAssociated()
        }
        Button {
            objectName: "loadFromDirectory"
            visible: offer.video.offerDirectoryLabel.length > 0
            text: offer.video.offerDirectoryLabel
            onClicked: offer.video.loadFromDirectory()
        }
        Button {
            objectName: "dismissAssociation"
            text: qsTr("No")
            onClicked: offer.video.dismissOffer()
        }
    }
    Keys.onEscapePressed: offer.video.dismissOffer()
}
