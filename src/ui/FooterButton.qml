// A footer button that can be hidden (Force stop, Remove group).
// DialogButtonBox keeps a hidden button's slot in its row, so a hidden one
// takes minus the row's spacing and the others close up.
import QtQuick
import QtQuick.Controls

Button {
    implicitWidth: visible ? Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                      implicitContentWidth + leftPadding + rightPadding)
                           : -(DialogButtonBox.buttonBox ? DialogButtonBox.buttonBox.spacing : 0)
}
