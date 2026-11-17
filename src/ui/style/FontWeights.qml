pragma Singleton
import QtQuick

QtObject {
    function semibold(font) {
        return Qt.font({family: font.family, pointSize: font.pointSize > 0 ? font.pointSize : undefined,
                        pixelSize: font.pixelSize > 0 ? font.pixelSize : undefined, weight: Font.DemiBold,
                        italic: font.italic})
    }
}
