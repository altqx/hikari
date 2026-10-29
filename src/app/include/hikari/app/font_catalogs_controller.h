#pragma once

#include "hikari/application/font_catalogs.h"
#include "hikari/application/font_families.h"
#include "hikari/application/owned_file.h"
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
class ListFile;
class SettingsStore;
}

namespace hikari::app {

class FontCatalogsController : public QObject {
    Q_OBJECT
    // In the order the catalogs were added.
    Q_PROPERTY(QStringList catalogNames READ catalogNames NOTIFY catalogsChanged)
    Q_PROPERTY(QString filterText READ filterText NOTIFY filterChanged)
    Q_PROPERTY(bool filterOn READ filterOn NOTIFY filterChanged)
    Q_PROPERTY(QString previewText READ previewText NOTIFY filterChanged)
    // "Autosave" for 10 s after each safety copy, else empty.
    Q_PROPERTY(QString autosaveStatus READ autosaveStatus NOTIFY autosaveStatusChanged)
    Q_PROPERTY(bool autosaveFailed READ autosaveFailed NOTIFY autosaveStatusChanged)
    Q_PROPERTY(int generation READ generation NOTIFY fontsChanged)
    Q_PROPERTY(int previewKey READ previewKey NOTIFY previewChanged)
    // The device pixel ratio the preview and the sample were drawn at.
    Q_PROPERTY(qreal previewScale READ previewScale NOTIFY previewChanged)
    Q_PROPERTY(qreal sampleScale READ sampleScale NOTIFY sampleChanged)
    Q_PROPERTY(bool previewAtScreenSize READ previewAtScreenSize NOTIFY previewAtScreenSizeChanged)
    // The font picker's preview is "image://fontcatalogpreview/sample/<key>".
    Q_PROPERTY(int sampleKey READ sampleKey NOTIFY sampleChanged)
public:
    struct Hooks {
        // Every tab's Documents when `all`, else the active tab's.
        std::function<std::vector<std::shared_ptr<const core::Document>>(bool all)> documents;
        std::function<void(const QString &)> log;
        std::function<void()> fontsChanged;
        // The editing target's attached fonts.
        std::function<std::vector<application::FontLease>()> documentFonts;
        // The catalog files' ui::ListFile::Report.
        std::function<void(const std::filesystem::path &legacy, const std::filesystem::path &file)> fileConverted;
        std::function<void(const application::FileProblem &)> fileProblem;
        std::function<void(const application::FileProblem &)> fileReplaced;
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

    // "All fonts" and "Without catalog" at 0 and 1, then the catalogs.
    Q_INVOKABLE QStringList catalogChoices() const;
    // `selection` and `value` from catalogChoices(); filtered when `filterOn` and `filterText` is
    // not empty.
    Q_INVOKABLE QStringList fontList(int selection, const QString &value, bool filterOn, const QString &filterText);
    Q_INVOKABLE void setFilterOn(bool on);
    Q_INVOKABLE void setPreviewText(const QString &text);
    Q_INVOKABLE void saveFilterText(const QString &text);
    Q_INVOKABLE void applyFilterText(const QString &text);
    // Sorted.
    Q_INVOKABLE QStringList allFonts();

    Q_INVOKABLE int nameIndex(const QStringList &fonts, const QString &name) const;
    Q_INVOKABLE int partialIndex(const QStringList &fonts, const QString &partial) const;
    Q_INVOKABLE int catalogListIndex(const QStringList &fonts, const QString &partial) const;
    // The first of `fonts` equal to `font` ignoring case, else 0.
    Q_INVOKABLE int styleFontIndex(const QStringList &fonts, const QString &font) const;

    Q_INVOKABLE QString catalogOf(const QString &font) const;
    Q_INVOKABLE bool isFontInCatalog(const QString &catalog, const QString &font) const;
    Q_INVOKABLE bool catalogExists(const QString &name) const;
    // catalogsOf in map order; existingName matches ignoring case and returns "" for none.
    Q_INVOKABLE QStringList catalogsOf(const QString &font) const;
    Q_INVOKABLE int catalogSize(const QString &name) const;
    Q_INVOKABLE QString existingName(const QString &name) const;
    // Ignoring case; -1 for none.
    Q_INVOKABLE int sameNameIndex(const QStringList &names, const QString &name) const;

    // False when it exists.
    Q_INVOKABLE bool addCatalog(const QString &name, bool autosave = true);
    // `answer` when `newName` exists: 0 merge, 1 delete, 2 cancel.
    Q_INVOKABLE bool renameCatalog(const QString &oldName, const QString &newName, int answer);
    Q_INVOKABLE void removeCatalog(const QString &name);
    Q_INVOKABLE void setFontsInCatalog(const QString &catalog, const QStringList &fonts, bool add);
    Q_INVOKABLE void toggleFontInCatalog(const QString &catalog, const QString &font, bool add);
    // Reads `file`'s catalogs, kept until loadReviewed(): {ok, file (its name), total, rows:
    // [{name, here, there, added}] for each catalog that exists here with other fonts, names: every
    // catalog's name in the file, same: the count that exists with the same fonts}. ok is false
    // when the file cannot be read or holds no catalog.
    Q_INVOKABLE QVariantMap reviewCatalogFile(const QUrl &file);
    // `choices`: [{name, action: "merge" | "replace" | "rename" | "skip", newName}] for the rows;
    // every other catalog merges. Returns {loaded, added, merged, replaced, renamed, skipped}.
    Q_INVOKABLE QVariantMap loadReviewed(const QVariantList &choices);
    // False when fonts.toml could not be written; the destructor tries again.
    Q_INVOKABLE bool save();
    // From then on the destructor saves the catalogs.
    Q_INVOKABLE void windowMade() { m_windowMade = true; }
    // {done: the catalog existed and took the fonts, saved: fonts.toml was written}.
    Q_INVOKABLE QVariantMap collectFromSubtitles(const QString &catalog, bool clear, bool allTabs);

    // `font` on the "FontPreview" Style, served as "image://fontcatalogpreview/<key>".
    Q_INVOKABLE void renderPreview(const QString &font, int width, int height, qreal scale = 1);
    // `text` (when `ass`, without override blocks and with \N and \n as spaces) in `font`
    // {name, size, bold, italic, underline, strikeOut}. `scale` is used with Screen size.
    Q_INVOKABLE void renderSample(const QVariantMap &font, const QString &text, bool ass, int width, int height,
                                  qreal scale = 1);
    void setPreviewAtScreenSize(bool on)
    {
        if (on == m_atScreenSize)
            return;
        m_atScreenSize = on;
        emit previewAtScreenSizeChanged();
    }
    bool previewAtScreenSize() const { return m_atScreenSize; }
    // What renderSample draws for `text`, on one line. With `to` at or past `from`, only what
    // [from, to) of `text` draws.
    Q_INVOKABLE QString samplePlain(const QString &text, bool ass, int from = 0, int to = -1) const;

    Q_INVOKABLE void refreshFonts();
    // Returns the request id that resolutionReady(requestId, result) answers, result {kind:
    // "requested" | "substituted" | "fallback" | "missing" | "unavailable", family, file,
    // emboldened, italicized}. A request still waiting when a newer one comes is never answered.
    // `documentFonts` puts the editing target's attached fonts first; `text` holds the characters
    // that need glyphs, empty for the preview text's default.
    Q_INVOKABLE int resolveFamily(const QString &family, bool bold, bool italic, bool documentFonts = false,
                                  const QString &text = QString());

    std::vector<application::FontLease> externalFontLeases() const;
    const application::FontEnvironment &environment() const { return m_families->environment(); }

    void setFontService(std::unique_ptr<application::FontServicePort> service);
    bool waitResolved(int ms = 30000);
    std::filesystem::path catalogDir() const { return m_dir; }
    // Converts HikariSub legacy catalog files, once.
    void convertFiles();
    int autosaveInterval() const { return m_autosave.interval(); }
    void setAutosaveInterval(int ms) { m_autosave.setInterval(ms); }
    void setWatchDelay(int ms) { m_watchDelay.setInterval(ms); }

signals:
    void catalogsChanged();
    void filterChanged();
    void autosaveStatusChanged();
    void fontsChanged();
    void previewChanged();
    void sampleChanged();
    void previewAtScreenSizeChanged();
    void resolutionReady(int requestId, const QVariantMap &result);

private:
    void ensureLoaded();
    void startAutosave();
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
    bool m_loaded = false;
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
    std::vector<application::FontCatalogs::Catalog> m_reviewed;
    std::thread m_resolver;
    std::mutex m_resolveMutex;
    std::condition_variable m_resolveWake;
    std::optional<ResolveJob> m_resolveJob;
    bool m_resolverStop = false;
    int m_resolveRequest = 0;
    int m_resolveAnswered = 0;
    bool m_windowMade = false;
    bool m_unsaved = false;
    std::shared_ptr<ui::ListFile> m_file;
    std::shared_ptr<ui::ListFile> m_autosaveFiles[3];
};

// Registers the "fontcatalogpreview" image provider.
void attachFontCatalogPreview(QQmlEngine &engine, FontCatalogsController &controller);

} // namespace hikari::app
