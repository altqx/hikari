import QtQuick
import QtQuick.Controls
import Hikari.Ui

// SH1 (#220), DG1 (#222): a window button of a drawn title bar (MainTitleBar,
// DialogTitleBar): 46 × 32, IC1's glyph, `raised` under the pointer and a
// shade darker pressed (high contrast: `select`, with a 1-pixel `line`
// ring); Close too, with no red (the owner's SH1 answer). While the window
// is not active the glyph takes the disabled colour. Not a Tab stop: the
// window menu (Alt+Space) reaches the buttons from the keyboard.
AbstractButton {
    id: windowButton
    required property string iconRole
    // Whether the button's window is active.
    property bool active: true
    // The pointer state as shown (Windows tracks Maximise's itself: SH1's snap layouts).
    property bool shownHover: hovered
    property bool shownDown: down
    readonly property color pressColor: Theme.highContrast
        ? Theme.select : Qt.tint(Theme.raised, Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06))
    implicitWidth: 46
    implicitHeight: 32
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            iconRole: windowButton.iconRole
            color: windowButton.active ? IconTheme.normal : IconTheme.disabled
        }
    }
    background: Rectangle {
        color: windowButton.shownDown ? windowButton.pressColor : windowButton.shownHover ? Theme.raised : "transparent"
        border.width: Theme.highContrast && (windowButton.shownDown || windowButton.shownHover) ? 1 : 0
        border.color: Theme.line
    }
    Accessible.role: Accessible.Button
    Accessible.name: text
    ToolTip.visible: shownHover && !shownDown
    ToolTip.text: text
    ToolTip.delay: Application.styleHints.mousePressAndHoldInterval
}
