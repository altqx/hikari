import QtQuick

QtObject {
    id: check
    required property var catalogs
    property var families: []
    property var found: ({})

    function missing(family) { return found[family]?.installed === false }
    function shownIn(family) { return found[family]?.shownIn ?? "" }

    function run() {
        const unknown = [...new Set(families)].filter(f => f.length > 0 && !(f in found))
        if (unknown.length > 0)
            catalogs.checkFamilies(unknown)
    }
    onFamiliesChanged: run()
    readonly property Connections answers: Connections {
        target: check.catalogs
        function onFamiliesChecked(request, byFamily) {
            if (Object.keys(byFamily).length === 0)
                return
            check.found = Object.assign({}, check.found, byFamily)
            check.run()
        }
        function onFontsChanged() {
            check.found = ({})
            check.run()
        }
    }
}
