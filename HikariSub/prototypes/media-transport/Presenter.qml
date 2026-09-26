import QtQuick
import QtQuick.Window
Window {
    id: root
    width: 640; height: 440; visible: true
    color: "#171b20"
    title: "HikariSub · Actual owned-frame software presenter"
    property string frameUrl: ""
    property string label: "Waiting for an owned frame"
    property bool imageReady: picture.status === Image.Ready
    Text { x: 12; y: 10; color: "#9cdbc9"; text: root.label; font.pixelSize: 15 }
    Text { x: 12; y: 33; color: "#a5b1bd"; text: "Offscreen Qt Quick software rendering · not a GPU / scanout acknowledgement"; font.pixelSize: 11 }
    Image { id: picture; x: 0; y: 60; width: 640; height: 360; source: root.frameUrl; asynchronous: false; cache: false; fillMode: Image.Stretch }
    Text { x: 12; y: 423; color: "#a5b1bd"; text: "Original synthetic 320×180 fixture · scale 2×"; font.pixelSize: 10 }
}
