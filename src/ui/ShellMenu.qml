import QtQuick
import QtQuick.Controls
import Hikari.Ui

// A shell menu: its submenu entries are ShellMenuItems too.
Menu {
    delegate: ShellMenuItem {}
}
