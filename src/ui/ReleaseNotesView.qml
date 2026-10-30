pragma ComponentBehavior: Bound
pragma Translator: "HikariSub.MainWindow"
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ScrollView {
    id: view
    required property var whatsNew
    property int sidePadding: 16
    property font smallFont: Qt.font({})
    property alias count: versions.count
    contentWidth: availableWidth
    clip: true
    activeFocusOnTab: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    Accessible.role: Accessible.Document
    Keys.onUpPressed: ScrollBar.vertical.decrease()
    Keys.onDownPressed: ScrollBar.vertical.increase()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp) {
            const bar = ScrollBar.vertical
            const step = bar.size * 0.9
            bar.position = Math.max(0, Math.min(1 - bar.size, bar.position + (event.key === Qt.Key_PageDown ? step : -step)))
            event.accepted = true
        }
    }

    function open(link) {
        view.whatsNew.openLink(link)
    }

    ColumnLayout {
        width: view.availableWidth
        spacing: 0
        Repeater {
            id: versions
            model: view.whatsNew.releases
            delegate: ColumnLayout {
                id: version
                required property var modelData
                required property int index
                objectName: "releaseNotes" + index
                Layout.fillWidth: true
                Layout.leftMargin: view.sidePadding
                Layout.rightMargin: view.sidePadding
                Layout.topMargin: index > 0 ? 10 : 0
                spacing: 4
                Accessible.role: Accessible.Section
                Accessible.name: modelData.name

                Rectangle {
                    visible: version.index > 0
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: Theme.highContrast ? Theme.line : Theme.raised
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 12
                    spacing: 8
                    Label {
                        objectName: "releaseName"
                        Layout.fillWidth: true
                        text: version.modelData.name
                        textFormat: Text.PlainText
                        font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                    }
                    Rectangle {
                        objectName: "releasePrerelease"
                        visible: version.modelData.prerelease
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 1
                        implicitWidth: badge.implicitWidth + 10
                        implicitHeight: badge.implicitHeight + 2
                        radius: 3
                        color: "transparent"
                        border.color: Theme.control
                        Label {
                            id: badge
                            anchors.centerIn: parent
                            text: qsTr("Pre-release")
                            color: Theme.muted
                            font: view.smallFont
                        }
                    }
                    Label {
                        objectName: "releaseDate"
                        visible: !isNaN(version.modelData.date)
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 1
                        text: visible ? version.modelData.date.toLocaleDateString(Qt.locale(), Locale.ShortFormat) : ""
                        color: Theme.muted
                        font: view.smallFont
                    }
                }
                Text {
                    objectName: "releaseText"
                    Layout.fillWidth: true
                    textFormat: Text.RichText
                    wrapMode: Text.Wrap
                    color: Theme.text
                    linkColor: Theme.accent
                    lineHeight: 1.1
                    text: view.whatsNew.render(version.modelData.notes, Theme.muted, Theme.accent, Theme.raised)
                    onLinkActivated: link => view.open(link)
                    HoverHandler {
                        cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
                    ToolTip.visible: hoveredLink.length > 0
                    ToolTip.text: hoveredLink
                }
            }
        }
        Text {
            objectName: "releaseNotesMore"
            visible: view.whatsNew.more > 0
            Layout.fillWidth: true
            Layout.leftMargin: view.sidePadding
            Layout.rightMargin: view.sidePadding
            Layout.topMargin: 14
            textFormat: Text.StyledText
            wrapMode: Text.Wrap
            color: Theme.muted
            linkColor: Theme.accent
            // Without a catalog Qt picks no English plural form, so one is spelled out.
            text: (view.whatsNew.more === 1 ? qsTr("And 1 earlier version: %1")
                                            : qsTr("And %n earlier versions: %1", "", view.whatsNew.more))
                  .arg("<a href=\"all\">" + qsTr("all release notes") + "</a>")
            onLinkActivated: view.whatsNew.openAllReleaseNotes()
            HoverHandler { cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor }
        }
        Item { implicitHeight: 12 }
    }
}
