pragma Singleton
import QtQuick

QtObject {
    readonly property int bodySize: 13

    function sized(base: font, reference: int, weight: int): font {
        const f = Qt.font({family: base.family, weight: weight})
        // A QML font's pointSize is QFont's pointSizeF.
        if (base.pointSize > 0)
            f.pointSize = Math.max(base.pointSize * reference / bodySize, 8)
        else
            f.pixelSize = Math.max(Math.round(base.pixelSize * reference / bodySize), 11)
        return f
    }
}
