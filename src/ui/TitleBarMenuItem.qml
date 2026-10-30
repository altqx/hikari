pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import Hikari.Ui

MenuBarItem {
    id: item
    readonly property TitleBarMenus strip: menuBar as TitleBarMenus
    readonly property bool keyboardCurrent: strip !== null && strip.keyboardTarget === item
    readonly property bool letterShown: strip !== null && strip.showMnemonics

    function titleText(underline: bool): string {
        let out = ""
        for (let i = 0; i < text.length; ++i) {
            const c = text[i]
            if (c === "&" && i + 1 < text.length) {
                const next = text[++i]
                out += next !== "&" && underline ? "<u>" + Qt.escapeHtml(next) + "</u>" : Qt.escapeHtml(next)
            } else {
                out += Qt.escapeHtml(c)
            }
        }
        return out
    }

    focusPolicy: Qt.NoFocus
    leftPadding: 8
    rightPadding: 8
    topPadding: 3
    bottomPadding: 3
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    implicitHeight: Math.max(24, implicitContentHeight + topPadding + bottomPadding)
    contentItem: Text {
        objectName: item.objectName + "Title"
        text: item.titleText(item.letterShown)
        textFormat: Text.StyledText
        font: item.font
        color: item.enabled && (item.strip === null || item.strip.windowActive) ? Theme.text : Theme.disabled
        verticalAlignment: Text.AlignVCenter
        Accessible.ignored: true
    }
    background: Rectangle {
        radius: 1
        color: item.highlighted || item.down || item.keyboardCurrent ? Theme.raised : "transparent"
        border.width: item.keyboardCurrent ? 1 : 0
        border.color: Theme.focus
    }
    Accessible.role: Accessible.MenuItem
    Accessible.name: titleText(false)
}
