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
    // STYLE_PREVIEW_TEXT, the text of every style and font sample (PV1
    // #398: the Font catalog's Sample column and preview, the font picker
    // without a selection, the Style editor's preview, the Styles panel);
    // empty, its default "光 Hikari 日本".
    Q_PROPERTY(QString previewText READ previewText NOTIFY filterChanged)
    // The management window's status bar field 0: "Autosave" for 10 s after
    // each safety copy.
    Q_PROPERTY(QString autosaveStatus READ autosaveStatus NOTIFY autosaveStatusChanged)
    // The last safety copy could not be written (PU3: the footer says so).
    Q_PROPERTY(bool autosaveFailed READ autosaveFailed NOTIFY autosaveStatusChanged)
    // The font environment generation: each re-listing makes a new one.
    Q_PROPERTY(int generation READ generation NOTIFY fontsChanged)
    Q_PROPERTY(int previewKey READ previewKey NOTIFY previewChanged)
    // SV1: the device pixel ratio the preview and the sample were drawn at.
    Q_PROPERTY(qreal previewScale READ previewScale NOTIFY previewChanged)
    Q_PROPERTY(qreal sampleScale READ sampleScale NOTIFY sampleChanged)
    // SV1: whether they take the screen's pixels; open windows draw again when it changes.
    Q_PROPERTY(bool previewAtScreenSize READ previewAtScreenSize NOTIFY previewAtScreenSizeChanged)
    // PU3: the font picker's preview, "image://fontcatalogpreview/sample/<key>".
    Q_PROPERTY(int sampleKey READ sampleKey NOTIFY sampleChanged)
public:
    struct Hooks {
        // CollectFontsFromSubtitles' Documents: every tab's from the first
        // (`all`), else the active tab's.
        std::function<std::vector<std::shared_ptr<const core::Document>>(bool all)> documents;
        std::function<void(const QString &)> log; // HikariLog
        // The external fonts changed (renderers take them on their next context).
        std::function<void()> fontsChanged;
        // The editing target's attached fonts, which its renderer takes
        // before the external ones (the picker's preview and its note).
        std::function<std::vector<application::FontLease>()> documentFonts;
    };

    FontCatalogsController(ui::SettingsStore &settings, std::filesystem::path catalogDir, Hooks hooks,
                           QObject *parent = nullptr);
    ~FontCatalogsController() override;

    QStringList catalogNames() const;
    QString filterText() const;
    bool filterOn() const;
    QString previewText() const;
    QString autosaveStatus() const { return m_autosaveStatus; }
    bool autosaveFailed() const { return m_autosaveFailed; }
    int generation() const { return int(m_generation); }
    int previewKey() const { return m_previewKey; }
    qreal previewScale() const { return m_preview.devicePixelRatio(); }
    qreal sampleScale() const { return m_sample.devicePixelRatio(); }
    QImage preview() const { return m_preview; }
    int sampleKey() const { return m_sampleKey; }
    QImage sample() const { return m_sample; }

    // The catalog choice of the font dialog and the Style editor: "All
    // fonts" and "Without catalog" inserted at 0 and 1 (HikariChoice::Insert).
    Q_INVOKABLE QStringList catalogChoices() const;
    // ChangeCatalog's list (FontDialog.cpp:656-695, StyleChange.cpp:626-658)
    // for the choice's selection and value. The filtered list is used when
    // `filterOn` and the filter text is not empty (GetFontsTable);
    // `filterText` is the text the window read (the Style editor reads it
    // once when it opens). "Without catalog" removes the catalogs' fonts
    // exactly in both windows (Y6-without-catalog).
    Q_INVOKABLE QStringList fontList(int selection, const QString &value, bool filterOn, const QString &filterText);
    // GetFontsTable(save = true): the Filter toggle saves STYLE_EDIT_FILTER_TEXT_ON.
    Q_INVOKABLE void setFilterOn(bool on);
    // SY3: the Style editor's sample line edited (StylePreview's
    // Options.SetString(STYLE_PREVIEW_TEXT)).
    Q_INVOKABLE void setPreviewText(const QString &text);
    // "Save filter": STYLE_EDIT_FILTER_TEXT.
    Q_INVOKABLE void saveFilterText(const QString &text);
    // The picker's characters chip: saved, and the filtered list made with
    // it now (legacy's keeps the first until the fonts are read again).
    Q_INVOKABLE void applyFilterText(const QString &text);
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
    // PU3: every catalog holding `font` (map order); a catalog's size; the
    // name of the catalog called `name` ignoring case ("" for none), which
    // the windows treat as taken.
    Q_INVOKABLE QStringList catalogsOf(const QString &font) const;
    Q_INVOKABLE int catalogSize(const QString &name) const;
    Q_INVOKABLE QString existingName(const QString &name) const;
    // The index of `name` in `names` ignoring case as the catalogs do (-1:
    // none); the load review's taken names.
    Q_INVOKABLE int sameNameIndex(const QStringList &names, const QString &name) const;

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
    // "Load" (PU3-load-review): the catalogs of `file`, kept until
    // loadReviewed(): {ok, file (its name), total, rows: [{name, here,
    // there, added}] for each catalog that exists here with other fonts,
    // names: every catalog's name in the file, same: the count that exists
    // with the same fonts}. ok is false when
    // the file cannot be read or holds no catalog.
    Q_INVOKABLE QVariantMap reviewCatalogFile(const QUrl &file);
    // The reviewed file loaded: `choices` [{name, action: "merge" |
    // "replace" | "rename" | "skip", newName}] for the rows, every other
    // catalog merged (legacy LoadCatalogs). {loaded, added, merged,
    // replaced, renamed, skipped}: catalogs written, of them new ones, and
    // so on.
    Q_INVOKABLE QVariantMap loadReviewed(const QVariantList &choices);
    // SaveCatalogs (the windows' closing, CATALOG_CHANGED); false when
    // FontCatalogs.txt could not be written (tried again at the end).
    Q_INVOKABLE bool save();
    // A "Manage font catalogs" window was made (new FontCatalogList): from
    // then on ~FontCatalogList's SaveCatalogs runs when the controller goes
    // at the application's end, keeping edits made while a window is open.
    Q_INVOKABLE void windowMade() { m_windowMade = true; }
    // GetFontsFromASSDialog's OK: CollectFontsFromSubtitles then SaveCatalogs.
    // {done: the catalog existed and took the fonts, saved: FontCatalogs.txt
    // was written}.
    Q_INVOKABLE QVariantMap collectFromSubtitles(const QString &catalog, bool clear, bool allTabs);

    // The management window's preview: the "FontPreview" Style with `font`
    // (StylePreview::DrawPreview), as "image://fontcatalogpreview/<key>".
    Q_INVOKABLE void renderPreview(const QString &font, int width, int height, qreal scale = 1);
    // PU3: the font picker's preview: `text` (a Line's text; when `ass`,
    // override blocks dropped and \N and \n as spaces, otherwise every
    // character as written) in the picker's font {name, size, bold,
    // italic, underline, strikeOut} on the preview Style, drawn by the
    // subtitle renderer at `width` x `height`.
    // SV1: `scale` (the window's device pixel ratio) is used with Screen size.
    Q_INVOKABLE void renderSample(const QVariantMap &font, const QString &text, bool ass, int width, int height,
                                  qreal scale = 1);
    // SV1-screen-size-subtitles: previews drawn at the screen's pixels.
    void setPreviewAtScreenSize(bool on)
    {
        if (on == m_atScreenSize)
            return;
        m_atScreenSize = on;
        emit previewAtScreenSizeChanged();
    }
    bool previewAtScreenSize() const { return m_atScreenSize; }
    // The characters renderSample draws for `text`, on one line: when `ass`,
    // without override blocks and what \p drawing mode draws, \N and \n
    // as spaces, \h as a no-break space, \{ and \} as braces; otherwise
    // as written.
    // With `to` at or past `from`, only what [from, to) of `text` draws
    // (the Line editor's raw selection: drawing mode from the start).
    Q_INVOKABLE QString samplePlain(const QString &text, bool ass, int from = 0, int to = -1) const;

    // F47-refresh: the installed and external fonts are read again now.
    Q_INVOKABLE void refreshFonts();
    // What the renderer selected for `family` (the font dialog's report):
    // {kind: "requested"|"substituted"|"fallback"|"missing", family, file,
    // emboldened, italicized}; resolutionReady(requestId, result) answers.
    // One worker thread resolves the latest request; a request still waiting
    // when a newer one comes is replaced and never answered. With
    // `documentFonts`, the editing target's attached fonts come first, as
    // its renderer has them.
    // `text`: the characters that need glyphs (the preview's); empty for
    // the preview text's default.
    Q_INVOKABLE int resolveFamily(const QString &family, bool bold, bool italic, bool documentFonts = false,
                                  const QString &text = QString());

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
    void sampleChanged();
    void previewAtScreenSizeChanged();
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
    bool m_autosaveFailed = false;
    QFileSystemWatcher m_watcher;
    backends::LibassRenderer m_renderer;
    QImage m_preview;
    bool m_atScreenSize = false;
    int m_previewKey = 0;
    QImage m_sample;
    int m_sampleKey = 0;
    std::vector<application::FontCatalogs::Catalog> m_reviewed; // reviewCatalogFile's catalogs
    std::thread m_resolver; // started with the first request
    std::mutex m_resolveMutex;
    std::condition_variable m_resolveWake;
    std::optional<ResolveJob> m_resolveJob; // the latest request not yet taken
    bool m_resolverStop = false;
    int m_resolveRequest = 0;  // the latest request made (GUI thread)
    int m_resolveAnswered = 0; // the latest request answered (GUI thread)
    bool m_windowMade = false; // a FontCatalogList exists
    bool m_unsaved = false;    // the last save could not write FontCatalogs.txt
};

// "image://fontcatalogpreview/<key>": the management window's preview.
void attachFontCatalogPreview(QQmlEngine &engine, FontCatalogsController &controller);

} // namespace hikari::app
