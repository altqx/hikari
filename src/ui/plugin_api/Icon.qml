import QtQuick
import Hikari.Ui as Ui

// One of HikariSub's icons by its role `name`, tinted for the theme; `size` in px.
Ui.Icon {
    property string name
    iconRole: name
}
