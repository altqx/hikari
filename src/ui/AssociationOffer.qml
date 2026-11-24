pragma Translator: "HikariSub.Documents"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

Frame {
    id: offer
    objectName: "associationOffer"
    required property var video
    readonly property bool reportOnly: video.offerAssociatedLabel.length === 0 && video.offerDirectoryLabel.length === 0
    readonly property real minimumTextWidth: 16 * offerText.fontInfo.pixelSize
    readonly property real actionsWidth: {
        const shown = [applyToAll, loadAssociated, loadFromDirectory, dismiss].filter(item => item.shown)
        return Math.ceil(shown.reduce((sum, item) => sum + item.implicitWidth, 0) + Math.max(0, shown.length - 1) * actions.spacing)
    }
    readonly property bool stacked: availableWidth - actionsWidth - layout.columnSpacing < minimumTextWidth
    visible: video.offering
    background: Rectangle {
        objectName: "associationOfferBackground"
        color: Theme.panel
        border.color: Theme.line
    }
    GridLayout {
        id: layout
        anchors.fill: parent
        columns: offer.stacked ? 1 : 2
        Label {
            id: offerText
            objectName: "associationText"
            text: offer.video.offer
            Layout.fillWidth: true
            wrapMode: Text.Wrap
        }
        Flow {
            id: actions
            objectName: "associationActions"
            spacing: 6
            Layout.fillWidth: offer.stacked
            Layout.preferredWidth: offer.stacked ? -1 : offer.actionsWidth
            Layout.alignment: Qt.AlignVCenter
            CheckBox {
                id: applyToAll
                readonly property bool shown: !offer.reportOnly
                objectName: "associationApplyToAll"
                visible: shown
                text: qsTr("Apply to all")
                checked: offer.video.offerApplyToAll
                onToggled: offer.video.offerApplyToAll = checked
            }
            Button {
                id: loadAssociated
                readonly property bool shown: offer.video.offerAssociatedLabel.length > 0
                objectName: "loadAssociated"
                visible: shown
                text: offer.video.offerAssociatedLabel
                onClicked: offer.video.loadAssociated()
            }
            Button {
                id: loadFromDirectory
                readonly property bool shown: offer.video.offerDirectoryLabel.length > 0
                objectName: "loadFromDirectory"
                visible: shown
                text: offer.video.offerDirectoryLabel
                onClicked: offer.video.loadFromDirectory()
            }
            Button {
                id: dismiss
                readonly property bool shown: true
                objectName: "dismissAssociation"
                text: offer.reportOnly ? qsTr("OK") : qsTr("No")
                onClicked: offer.video.dismissOffer()
            }
        }
    }
    Keys.onEscapePressed: offer.video.dismissOffer()
}
