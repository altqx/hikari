// The control for check_desktop_handoffs.cmake: a hand-off past
// backends::desktop, which the check must reject.
import QtQuick
Item {
    Component.onCompleted: Qt.openUrlExternally("https://example.invalid")
}
