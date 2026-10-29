pragma Translator: "HikariSub.Documents"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Frame {
    id: offer
    objectName: "associationOffer"
    required property var video
    visible: video.offering
    background: Rectangle {
        objectName: "associationOfferBackground"
        color: Theme.panel
        border.color: Theme.line
    }
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
