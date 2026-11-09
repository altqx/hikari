import QtQuick

Item {
    required property var api
    objectName: "noisy"
    Repeater {
        model: 25
        Item {
            width: missingName
        }
    }
}
