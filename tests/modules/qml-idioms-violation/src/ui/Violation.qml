import QtQuick

Text {
    property string name: "50%1 off"
    text: qsTr("%1 is %2").arg(name.trim())
        .arg(Qt.platform.os)
    function escaped(s) { return s.replace(/&/g, "&amp;").replace(/</g, "&lt;") }
}
