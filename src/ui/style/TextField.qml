import QtQuick
import QtQuick.Controls.Fusion as F
import QtQuick.Templates as T

F.TextField {
    id: control
    leftPadding: 8
    rightPadding: 8
    topPadding: 3
    bottomPadding: 3
    color: StyleColours.themed ? (enabled ? StyleColours.text : StyleColours.disabled) : palette.text
    placeholderTextColor: StyleColours.themed ? (enabled ? StyleColours.muted : StyleColours.disabled) : palette.placeholderText
    T.ContextMenu.menu: null
    T.ContextMenu.onRequested: position => {
        if (!T.ContextMenu.menu)
            TextMenus.menuFor(control)?.popup(control, position)
    }
    background: FieldBackground {
        control: control
        keyboardFocus: control.activeFocus && (control.focusReason === Qt.TabFocusReason
            || control.focusReason === Qt.BacktabFocusReason || control.focusReason === Qt.ShortcutFocusReason)
    }
}
