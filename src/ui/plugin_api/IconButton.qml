import QtQuick
import Hikari.Ui as Ui

// An icon-only button for the icon role `name`; `text` is its tooltip and accessible name.
Ui.IconButton {
    property string name
    iconRole: name
}
