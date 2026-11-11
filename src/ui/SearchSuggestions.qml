pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import Hikari.Ui

// Google requires these exactly as its snippet draws them: https://docs.cloud.google.com/vertex-ai/generative-ai/docs/grounding/grounding-search-suggestions
Rectangle {
    id: strip
    objectName: "searchSuggestions"

    property var chips: []
    property url lightLogo
    property url darkLogo
    readonly property bool googleDark: Theme.dark

    signal chipActivated(url url)

    implicitHeight: 48
    radius: 8
    color: googleDark ? "#1f1f1f" : "#fafafa"
    border.width: 1
    border.color: googleDark ? "#26ffffff" : "#0f000000"

    Image {
        id: logo
        objectName: "searchSuggestionsLogo"
        x: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 18
        height: 18
        sourceSize: Qt.size(18, 18)
        source: strip.googleDark ? strip.darkLogo : strip.lightLogo
    }

    Flickable {
        id: carousel
        anchors.left: logo.right
        anchors.leftMargin: 4
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: row.height
        contentWidth: row.width
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Row {
            id: row
            Repeater {
                model: strip.chips
                delegate: AbstractButton {
                    id: chip
                    required property var modelData
                    required property int index
                    objectName: "searchSuggestion_" + index
                    text: modelData.text
                    leftInset: 8
                    rightInset: 8
                    leftPadding: 24
                    rightPadding: 24
                    topPadding: 5
                    bottomPadding: 5
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Link
                    Accessible.name: text
                    font.family: "Roboto"
                    font.pixelSize: 14
                    contentItem: Text {
                        text: chip.text
                        font: chip.font
                        lineHeight: 20
                        lineHeightMode: Text.FixedHeight
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: strip.googleDark ? "#ffffff" : "#5e5e5e"
                    }
                    background: Rectangle {
                        radius: 16
                        border.width: 1
                        border.color: chip.down ? (strip.googleDark ? "#53575b" : "#b6b6b6")
                                                : (strip.googleDark ? "#3c4043" : "#d2d2d2")
                        color: chip.down ? (strip.googleDark ? "#464849" : "#d8d8d8")
                             : chip.hovered || chip.visualFocus ? (strip.googleDark ? "#353536" : "#f2f2f2")
                             : (strip.googleDark ? "#2c2c2c" : "#ffffff")
                    }
                    onClicked: strip.chipActivated(modelData.url)
                }
            }
        }
    }

    Rectangle {
        anchors.left: carousel.left
        anchors.verticalCenter: parent.verticalCenter
        width: 9
        height: 36
        visible: carousel.contentX > 0
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.15; color: strip.color }
            GradientStop { position: 1.0; color: Qt.rgba(strip.color.r, strip.color.g, strip.color.b, 0) }
        }
    }
}
