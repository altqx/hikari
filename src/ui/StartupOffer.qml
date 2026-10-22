pragma Translator: "HikariSub.Session"
// PU1 (#303, PU1-startup-cards): legacy's startup prompts (hikarisubApp::
// OnInit: "Load last session?" with On start: Ask, and "The program crashed
// or was closed improperly. Load the last session…?") no longer hold the
// start back. They are an offer that the Home page shows as a card where
// the Last session row is, or, while a Document is open (files from the
// command line), the startup bar under the Document tabs. Nothing waits for
// it: files another start hands over open at once.
// * "ask": Restore, and "Restore it at every start" (On start: Restore).
// * "crash": Restore session, when the session has a Document and was not
//   loaded already, and Autosaves… when autosaved work was left; × dismisses.
// The offer ends when it is answered or dismissed, when the session loads,
// or when a Document opens from Home (a start with files keeps it).
import QtQuick

QtObject {
    id: offer
    required property var app
    // "", "ask" or "crash".
    property string kind: ""
    property var names: []
    property int autosaveCount: 0
    // The session can be restored (it has a Document and is not loaded).
    property bool restorable: false
    property bool keepWithDocuments: false
    readonly property bool active: kind.length > 0
    signal restoreRequested()
    signal autosavesRequested()

    // At the start: `loaded` when On start: Restore loaded the session.
    function begin(loaded) {
        const k = app.startupOffer()
        names = app.startupOfferNames()
        autosaveCount = app.recoveryBundles().length
        restorable = !loaded && names.length > 0
        keepWithDocuments = app.startedWithPaths()
        if (k === "ask" && !restorable)
            kind = ""
        else if (k === "crash" && !restorable && autosaveCount === 0)
            kind = ""
        else
            kind = k
    }
    function end() { kind = "" }
    // Autosaves were deleted: the crash offer counts them again, and ends
    // when it has neither a session to restore nor autosaved work left.
    function recountAutosaves() {
        if (kind !== "crash")
            return
        autosaveCount = app.recoveryBundles().length
        if (!restorable && autosaveCount === 0)
            kind = ""
    }
    function restore() { offer.restoreRequested() }
    function openAutosaves() { offer.autosavesRequested() }
    // "Restore it at every start": On start: Restore (Settings › General).
    function setRestoreAlways(on) { app.sessionRestore = on ? 2 : 1 }

    readonly property string sessionText: names.length === 1 ? qsTr("The last session had 1 document: %1.").arg(names[0])
                                                              : qsTr("The last session had %n documents: %1.", "", names.length).arg(names.join(", "))
    readonly property string autosaveText: autosaveCount === 1 ? qsTr("Unsaved work in 1 document was autosaved.")
                                                               : qsTr("Unsaved work in %n documents was autosaved.", "", autosaveCount)
    readonly property string title: kind === "crash" ? qsTr("HikariSub did not close properly") : qsTr("Restore the last session?")
    readonly property string detail: kind === "crash"
        ? [restorable ? sessionText : "", autosaveCount > 0 ? autosaveText : ""].filter(t => t.length > 0).join(" ")
        : (names.length === 1 ? qsTr("1 document") : qsTr("%n documents", "", names.length)) + " · " + names.join(", ")
    // The startup bar's one line.
    readonly property string barText: kind === "crash"
        ? qsTr("HikariSub did not close properly.") + " " + detail
        : (names.length === 1 ? qsTr("Restore the last session (1 document)?")
                              : qsTr("Restore the last session (%n documents)?", "", names.length))

    // A Document opened from Home ends it.
    property bool home: app.home
    onHomeChanged: if (!home && !keepWithDocuments) end()
    property Connections restored: Connections {
        target: offer.app
        function onSessionRestored() { offer.end() }
    }
}
