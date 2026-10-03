// A script's file picker (L3): aegisub.dialog.open and save through a fixed
// FileDialog; the request's title, folder, file and filters come from the
// controller, and the chosen paths go back to it.
import QtQuick
import QtQuick.Dialogs as Dialogs
import Hikari.Ui

Dialogs.FileDialog {
    id: dialog
    required property AutomationFilePickerController picker

    title: picker.title
    fileMode: picker.save ? Dialogs.FileDialog.SaveFile
                          : picker.multiple ? Dialogs.FileDialog.OpenFiles : Dialogs.FileDialog.OpenFile
    // QFileDialogOptions::DontConfirmOverwrite. The enum comes from an
    // extension namespace that the root object's own bindings cannot resolve.
    options: picker.save && !picker.confirmOverwrite ? 0x4 : 0
    currentFolder: picker.folder
    selectedFile: picker.file
    nameFilters: picker.nameFilters
    visible: picker.open

    onAccepted: picker.accept(fileMode === Dialogs.FileDialog.OpenFiles ? selectedFiles : [selectedFile])
    onRejected: picker.reject()
}
