import QtQuick
import QtQuick.Controls

Button {
    // DialogButtonBox keeps a hidden button's slot, so a hidden one takes minus the spacing.
    implicitWidth: visible ? Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                      implicitContentWidth + leftPadding + rightPadding)
                           : -(DialogButtonBox.buttonBox ? DialogButtonBox.buttonBox.spacing : 0)
}
