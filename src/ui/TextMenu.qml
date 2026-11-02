import QtQuick
import QtQuick.Controls
import Hikari.Ui

ShellMenu {
    id: menu
    required property Item editor
    TextMenuItem { kind: TextMenuItem.Undo; editor: menu.editor }
    TextMenuItem { kind: TextMenuItem.Redo; editor: menu.editor }
    MenuSeparator {}
    TextMenuItem { kind: TextMenuItem.Cut; editor: menu.editor }
    TextMenuItem { kind: TextMenuItem.Copy; editor: menu.editor }
    TextMenuItem { kind: TextMenuItem.Paste; editor: menu.editor }
    TextMenuItem { kind: TextMenuItem.Delete; editor: menu.editor }
    MenuSeparator {}
    TextMenuItem { kind: TextMenuItem.SelectAll; editor: menu.editor }
}
