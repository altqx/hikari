// TS1: a form's primary action, filled in the accent with its text in
// `onAccent` (visual-language.md: the accent marks the primary action;
// MuseScore 4's accent button, docs/research/musescore-shell.md §7). Hover
// and press shade the fill; disabled, it is outlined in `line` with the
// disabled text. Keyboard focus is the 1-wide ring in the text colour
// (FocusRing), apart from the accent fill.
import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import Hikari.Ui

T.Button {
    id: button
    implicitWidth: Math.max(80, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(30, implicitContentHeight + topPadding + bottomPadding)
    padding: 5
    leftPadding: 10
    rightPadding: 10
    focusPolicy: Qt.StrongFocus
    background: Rectangle {
        color: !button.enabled ? "transparent"
             : button.down ? Qt.darker(Theme.accent, Theme.dark ? 1.25 : 1.35)
             : button.hovered ? Qt.darker(Theme.accent, Theme.dark ? 1.1 : 1.15)
             : Theme.accent
        border.width: button.enabled ? 0 : 1
        border.color: Theme.line
        radius: 1
        FocusRing { control: button }
    }
    contentItem: Label {
        text: button.text
        font.bold: true
        color: button.enabled ? Theme.onAccent : Theme.disabled
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
