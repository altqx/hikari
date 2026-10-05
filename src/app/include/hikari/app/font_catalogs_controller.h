#pragma once

// Y6: Font catalogs and the font picker's lists for QML (legacy
// FontCatalogManagement, FontCatalogList, GetFontsFromASSDialog and
// FontEnumerator at 20d647c4). One instance serves the font dialog (E1),
// the Style editor (Y1) and the "Manage font catalogs" window, as legacy's
// FCManagement and FontEnum globals did. FontCatalogs.txt and the
// FontCatalogsAutosave<n>.txt safety copies live in the catalog folder
// (legacy <program>/Config).
//
// The font lists come from the FontService (FontFamilies over the
// renderer's provider) with EXTERNAL_FONTS_DIRECTORY's fonts added. A
// change in the font folders re-lists the fonts after a second, as
// legacy's watcher did, and refreshFonts() does so at once (F47-refresh):
// each listing after a refresh is a new font environment generation.

#include "hikari/application/font_catalogs.h"
#include "hikari/application/font_families.h"
#include "hikari/application/subtitle_render.h"
#include "hikari/backends/libass_renderer.h"
#include "hikari/core/document.h"

#include <QFileSystemWatcher>
#include <QImage>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

class FontCatalogsController : public QObject {
    Q_OBJECT
    // fontCatalogsNames: the catalog choices, in the order catalogs were added.
    Q_PROPERTY(QStringList catalogNames READ catalogNames NOTIFY catalogsChanged)
    // STYLE_EDIT_FILTER_TEXT and STYLE_EDIT_FILTER_TEXT_ON.
    Q_PROPERTY(QString filterText READ filterText NOTIFY filterChanged)
    Q_PROPERTY(bool filterOn READ filterOn NOTIFY filterChanged)
    // STYLE_PREVIEW_TEXT (the management window's Sample column).
    Q_PROPERTY(QString previewText READ previewText NOTIFY filterChanged)
    // The management window's status bar field 0: "Autosave" for 10 s after
    // each safety copy.
    Q_PROPERTY(QString autosaveStatus READ autosaveStatus NOTIFY autosaveStatusChanged)
    // The font environment generation: each re-listing makes a new one.
    Q_PROPERTY(int generation READ generation NOTIFY fontsChanged)
    Q_PROPERTY(int previewKey READ previewKey NOTIFY previewChanged)
public:
    struct Hooks {
        // CollectFontsFromSubtitles' Documents: every tab's from the first
        // (`all`), else the active tab's.
        std::function<std::vector<std::shared_ptr<const core::Document>>(bool all)> documents;
        std::function<void(const QString &)> log; // HikariLog
        // The external fonts changed (renderers take them on their next context).
        std::function<void()> fontsChanged;
    };

    FontCatalogsController(ui::SettingsStore &settings, std::filesystem::path catalogDir, Hooks hooks,
                           QObject *parent = nullptr);
    ~FontCatalogsController() override;

    QStringList catalogNames() const;
    QString filterText() const;
    bool filterOn() const;
    QString previewText() const;
    QString autosaveStatus() const { return m_autosaveStatus; }
    int generation() const { return int(m_generation); }
    int previewKey() const { return m_previewKey; }
    QImage preview() const { return m_preview; }

    // The catalog choice of the font dialog and the Style editor: "All
    // fonts" and "Without catalog" inserted at 0 and 1 (HikariChoice::Insert).
    Q_INVOKABLE QStringList catalogChoices() const;
    // ChangeCatalog's list (FontDialog.cpp:656-695, StyleChange.cpp:626-658)
    // for the choice's selection and value. The filtered list is used when
    // `filterOn` and the filter text is not empty (GetFontsTable);
    // `filterText` is the text the window read (the Style editor reads it
    // once when it opens). `fontDialog`: "Without catalog" removes the first
    // font per catalog entry (FontList::FindString answers 0).
    Q_INVOKABLE QStringList fontList(int selection, const QString &value, bool filterOn, const QString &filterText,
                                     bool fontDialog);
    // GetFontsTable(save = true): the Filter toggle saves STYLE_EDIT_FILTER_TEXT_ON.
    Q_INVOKABLE void setFilterOn(bool on);
    // "Save filter": STYLE_EDIT_FILTER_TEXT.
    Q_INVOKABLE void saveFilterText(const QString &text);
    // Every installed font, sorted (FontEnum.GetFonts).
    Q_INVOKABLE QStringList allFonts();

    // FontList::SetSelectionByName / SetSelectionByPartialName and the
    // management window's search.
    Q_INVOKABLE int nameIndex(const QStringList &fonts, const QString &name) const;
    Q_INVOKABLE int partialIndex(const QStringList &fonts, const QString &partial) const;
    Q_INVOKABLE int catalogListIndex(const QStringList &fonts, const QString &partial) const;
    // The management window's first selection: the first font equal to the
    // Style's ignoring case, else 0 (GenerateList).
    Q_INVOKABLE int styleFontIndex(const QStringList &fonts, const QString &font) const;

    // FindCatalogByFont and IsFontInCatalog.
    Q_INVOKABLE QString catalogOf(const QString &font) const;
    Q_INVOKABLE bool isFontInCatalog(const QString &catalog, const QString &font) const;
    Q_INVOKABLE bool catalogExists(const QString &name) const;

    // The management window's Add (AddCatalog: autosave) and the "Add fonts
    // from subtitles" dialog's Add (no autosave). False when it existed.
    Q_INVOKABLE bool addCatalog(const QString &name, bool autosave = true);
    // ChangeCatalogName; `answer` to "Catalog named \"%s\" already exists.
    // What to do?": 0 Merge, 1 Delete, 2 Cancel (asked only when it exists).
    Q_INVOKABLE bool renameCatalog(const QString &oldName, const QString &newName, int answer);
    Q_INVOKABLE void removeCatalog(const QString &name);
    // CatalogList's menu: the fonts join or leave the catalog (AddCatalogFont /
    // RemoveCatalogFont, with autosave).
    Q_INVOKABLE void setFontsInCatalog(const QString &catalog, const QStringList &fonts, bool add);
    // AddToCatalog's menu: one catalog's check changed for the font, then
    // SaveCatalogs.
    Q_INVOKABLE void toggleFontInCatalog(const QString &catalog, const QString &font, bool add);
    // "Load": LoadCatalogs(path), merged into the catalogs.
    Q_INVOKABLE void loadCatalogsFrom(const QUrl &file);
    // SaveCatalogs (the windows' closing, CATALOG_CHANGED).
    Q_INVOKABLE void save();
    // A "Manage font catalogs" window was made (new FontCatalogList): from
    // then on ~FontCatalogList's SaveCatalogs runs when the controller goes
    // at the application's end, keeping edits made while a window is open.
    Q_INVOKABLE void windowMade() { m_windowMade = true; }
    // GetFontsFromASSDialog's OK: CollectFontsFromSubtitles then SaveCatalogs.
    Q_INVOKABLE bool collectFromSubtitles(const QString &catalog, bool clear, bool allTabs);
    // AddToCatalog with no catalog: legacy logged where to create one.
    Q_INVOKABLE void noCatalogToAddTo();

    // The management window's preview: the "FontPreview" Style with `font`
    // (StylePreview::DrawPreview), as "image://fontcatalogpreview/<key>".
    Q_INVOKABLE void renderPreview(const QString &font, int width, int height);

    // F47-refresh: the installed and external fonts are read again now.
    Q_INVOKABLE void refreshFonts();
    // What the renderer selected for `family` (the font dialog's report):
    // {kind: "requested"|"substituted"|"fallback"|"missing", family, file,
    // emboldened, italicized}; resolutionReady(requestId, result) answers.
    // One worker thread resolves the latest request; a request still waiting
    // when a newer one comes is replaced and never answered.
    Q_INVOKABLE int resolveFamily(const QString &family, bool bold, bool italic);

    // The external fonts' bytes, for renderer contexts.
    std::vector<application::FontLease> externalFontLeases() const;
    const application::FontEnvironment &environment() const { return m_families->environment(); }

    // Tests: another font service; waiting for the latest resolution; the catalog
    // file's folder; the autosave and change delays.
    void setFontService(std::unique_ptr<application::FontServicePort> service);
    bool waitResolved(int ms = 30000);
    std::filesystem::path catalogDir() const { return m_dir; }
    int autosaveInterval() const { return m_autosave.interval(); }
    void setAutosaveInterval(int ms) { m_autosave.setInterval(ms); }
    void setWatchDelay(int ms) { m_watchDelay.setInterval(ms); }

signals:
    void catalogsChanged();
    void filterChanged();
    void autosaveStatusChanged();
    // The font lists changed (RefreshClientsFonts): windows take their lists again.
    void fontsChanged();
    void previewChanged();
    void resolutionReady(int requestId, const QVariantMap &result);

private:
    void ensureLoaded();
    void startAutosave(); // FontCatalogList::StartEditionTimer(saveInterval)
    void autosaveNow();
    void loadExternalFonts();
    void relist();
    void watchFolders();
    void settingChanged(const QString &id);
    struct ResolveJob {
        int request = 0;
        std::shared_ptr<application::FontServicePort> service;
        application::FontEnvironment environment;
        application::FontRequest want;
    };
    void resolverLoop();
    void stopResolver();

    ui::SettingsStore &m_settings;
    std::filesystem::path m_dir;
    Hooks m_hooks;
    application::FontCatalogs m_catalogs;
    bool m_loaded = false; // isInit
    // Shared with the resolver thread, which keeps the service it was given.
    std::shared_ptr<application::FontServicePort> m_service;
    std::unique_ptr<application::FontFamilies> m_families;
    std::uint64_t m_generation = 1;
    QTimer m_autosave, m_autosaveLabel, m_watchDelay;
    int m_autosaveIndex = 0;
    QString m_autosaveStatus;
    QFileSystemWatcher m_watcher;
    backends::LibassRenderer m_renderer;
    QImage m_preview;
    int m_previewKey = 0;
    std::thread m_resolver; // started with the first request
    std::mutex m_resolveMutex;
    std::condition_variable m_resolveWake;
    std::optional<ResolveJob> m_resolveJob; // the latest request not yet taken
    bool m_resolverStop = false;
    int m_resolveRequest = 0;  // the latest request made (GUI thread)
    int m_resolveAnswered = 0; // the latest request answered (GUI thread)
    bool m_windowMade = false; // a FontCatalogList exists
};

// "image://fontcatalogpreview/<key>": the management window's preview.
void attachFontCatalogPreview(QQmlEngine &engine, FontCatalogsController &controller);

} // namespace hikari::app
