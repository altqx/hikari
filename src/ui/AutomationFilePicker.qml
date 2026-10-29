import QtQuick
import QtQuick.Dialogs as Dialogs
import Hikari.Ui

Dialogs.FileDialog {
    id: dialog
    required property AutomationFilePickerController picker

    title: picker.title
    fileMode: picker.save ? Dialogs.FileDialog.SaveFile
                          : picker.multiple ? Dialogs.FileDialog.OpenFiles : Dialogs.FileDialog.OpenFile
    // 0x4 is QFileDialogOptions::DontConfirmOverwrite: the root object's bindings can't resolve that extension enum.
    options: picker.save && !picker.confirmOverwrite ? 0x4 : 0
    currentFolder: picker.folder
    selectedFile: picker.file
    nameFilters: picker.nameFilters
    visible: picker.open

    onAccepted: picker.accept(fileMode === Dialogs.FileDialog.OpenFiles ? selectedFiles : [selectedFile])
    onRejected: picker.reject()
}
