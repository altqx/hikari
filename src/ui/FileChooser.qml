import QtQuick.Controls
import QtQuick.Dialogs as Dialogs

// Qt 6.12's fallback dialog crashes at exit when it owns a popup window: https://github.com/altqx/hikari/issues/781
Dialogs.FileDialog {
    popupType: Popup.Item
}
