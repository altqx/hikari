pragma Translator: "HikariSub.Session"
import QtQuick

QtObject {
    id: offer
    required property var app
    property string kind: ""
    property var names: []
    property int autosaveCount: 0
    property bool restorable: false
    property bool keepWithDocuments: false
    readonly property bool active: kind.length > 0
    signal restoreRequested()
    signal autosavesRequested()

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
    function recountAutosaves() {
        if (kind !== "crash")
            return
        autosaveCount = app.recoveryBundles().length
        if (!restorable && autosaveCount === 0)
            kind = ""
    }
    property bool restoring: false
    function restore() {
        restoring = true
        offer.restoreRequested()
    }
    function sessionLoaded() {
        restoring = false
        restorable = false
        if (kind === "crash" && autosaveCount > 0)
            keepWithDocuments = true
        else
            end()
    }
    function openAutosaves() { offer.autosavesRequested() }
    function setRestoreAlways(on) { app.sessionRestore = on ? 2 : 1 }

    readonly property string sessionText: names.length === 1 ? qsTr("The last session had 1 document: %1.").arg(names[0])
                                                              : qsTr("The last session had %n documents: %1.", "", names.length).arg(names.join(", "))
    readonly property string autosaveText: autosaveCount === 1 ? qsTr("Unsaved work in 1 document was autosaved.")
                                                               : qsTr("Unsaved work in %n documents was autosaved.", "", autosaveCount)
    readonly property string title: kind === "crash" ? qsTr("HikariSub did not close properly") : qsTr("Restore the last session?")
    readonly property string detail: kind === "crash"
        ? [restorable ? sessionText : "", autosaveCount > 0 ? autosaveText : ""].filter(t => t.length > 0).join(" ")
        : (names.length === 1 ? qsTr("1 document") : qsTr("%n documents", "", names.length)) + " · " + names.join(", ")
    readonly property string barText: kind === "crash"
        ? qsTr("HikariSub did not close properly.") + " " + detail
        : (names.length === 1 ? qsTr("Restore the last session (1 document)?")
                              : qsTr("Restore the last session (%n documents)?", "", names.length))

    property bool home: app.home
    onHomeChanged: if (!home && !keepWithDocuments && !restoring) end()
    property Connections restored: Connections {
        target: offer.app
        function onSessionRestored() { offer.sessionLoaded() }
        function onFilesForwarded() { offer.end() }
    }
}
