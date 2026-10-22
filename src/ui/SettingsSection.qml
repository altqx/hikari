// PF1 (#225): a titled section of a Settings page (MuseScore 4's
// preferences sections, docs/research/musescore-shell.md §PF1): the title
// in bold, its rows 8 apart under it, and a 1-pixel `line` rule above every
// section but the page's first, 16 from the section before.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Hikari.Ui

ColumnLayout {
    id: section
    property string title
    // The rule above it (off for the page's first section).
    property bool ruled: true
    default property alias content: rows.data

    spacing: 10
    Layout.fillWidth: true
    Accessible.role: Accessible.Grouping
    Accessible.name: title

    Rectangle {
        visible: section.ruled
        color: Theme.line
        implicitHeight: 1
        Layout.fillWidth: true
        Layout.topMargin: 6 // 16 from the section above with the page's spacing
        Layout.bottomMargin: 6
    }
    Label {
        text: section.title
        font.bold: true
        color: Theme.text
        Layout.fillWidth: true
    }
    ColumnLayout {
        id: rows
        spacing: 8
        Layout.fillWidth: true
    }
}
