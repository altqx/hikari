pragma Singleton
// PU3 (#305): the program's one "Manage font catalogs" window and one "Add
// fonts from subtitles" window (Main makes them), for every opener: the
// font picker, the Style editor's catalog bar and each other. A window of
// its own, never made inside the window that opens it.
import QtQuick

QtObject {
    property var catalogWindow: null
    property var fromSubtitles: null
}
