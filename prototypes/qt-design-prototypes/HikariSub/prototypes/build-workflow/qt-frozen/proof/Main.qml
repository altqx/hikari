import QtQuick
import QtMultimedia

Window {
    width: 160
    height: 120
    visible: true
    title: qsTranslate("Proof", "Frozen Qt proof")

    // Instantiating a type proves the QtMultimedia QML module resolves.
    MediaPlayer { id: player }

    Rectangle {
        id: whiteSource
        anchors.fill: parent
        color: "white"
        layer.enabled: true
        visible: false
    }

    ShaderEffect {
        anchors.fill: parent
        property var source: whiteSource
        fragmentShader: "qrc:/tint.frag.qsb"
    }
}
