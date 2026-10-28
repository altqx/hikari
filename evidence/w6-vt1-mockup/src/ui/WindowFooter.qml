// A window's closing row: its buttons at the trailing edge in the platform's
// order (DialogButtonBox by role: OK before Cancel on Windows and KDE, after
// it on GNOME and macOS), as the Settings dialog's. The window's own margins
// frame it, so it has no padding or fill of its own.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogButtonBox {
    Layout.fillWidth: true
    alignment: Qt.AlignRight
    padding: 0
    background: null
}
