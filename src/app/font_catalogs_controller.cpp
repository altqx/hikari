#include "hikari/app/font_catalogs_controller.h"

#include "hikari/app/style_manager_controller.h"
#include "hikari/backends/libass_font_service.h"
#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QQuickImageProvider>

#include <algorithm>

namespace hikari::app {

namespace {

std::u16string u16(const QString &s)
{
    return s.toStdU16String();
}

QString qs(std::u16string_view s)
{
    return QString::fromUtf16(s.data(), qsizetype(s.size()));
}

QStringList list(const std::vector<std::u16string> &v)
{
    QStringList out;
    out.reserve(qsizetype(v.size()));
    for (const auto &s : v)
        out << qs(s);
    return out;
}

std::vector<std::u16string> vec(const QStringList &l)
{
    std::vector<std::u16string> out;
    out.reserve(std::size_t(l.size()));
    for (const auto &s : l)
        out.push_back(u16(s));
    return out;
}

// U1-unicode-case: every letter folds (towlower per UTF-16 unit in legacy).
char16_t lowerUnit(char16_t c)
{
    return QChar(c).toLower().unicode();
}

const application::FontNameLower kLower = lowerUnit;

// FontCatalogList's preview Style (FontCatalogList.cpp:191).
constexpr char8_t kFontPreviewStyle[] = u8"Style: FontPreview,Arial,80,&H00FFFFFF,&HFF0000FF,&H00301946,&H7A301946,-1,0,"
                                        u8"0,0,100,100,1.13208,0,1,3.5,0,2,120,120,40,1";

std::filesystem::path catalogPath(const std::filesystem::path &dir, std::u16string_view name)
{
    return dir / std::filesystem::path(std::u16string(name));
}

} // namespace

FontCatalogsController::FontCatalogsController(ui::SettingsStore &settings, std::filesystem::path catalogDir, Hooks hooks,
                                               QObject *parent)
    : QObject(parent), m_settings(settings), m_dir(std::move(catalogDir)), m_hooks(std::move(hooks)),
      m_catalogs(kLower), m_service(std::make_shared<backends::LibassFontService>()),
      m_families(std::make_unique<application::FontFamilies>(*m_service, kLower))
{
    // FontCatalogManagement::saveInterval; the label goes after 10 s.
    m_autosave.setSingleShot(true);
    m_autosave.setInterval(20000);
    connect(&m_autosave, &QTimer::timeout, this, &FontCatalogsController::autosaveNow);
    m_autosaveLabel.setSingleShot(true);
    m_autosaveLabel.setInterval(10000);
    connect(&m_autosaveLabel, &QTimer::timeout, this, [this] {
        m_autosaveStatus.clear();
        emit autosaveStatusChanged();
    });
    // CheckFontsProc: a burst of changes settles for a second, then the
    // fonts are listed again (and the external folder read again).
    m_watchDelay.setSingleShot(true);
    m_watchDelay.setInterval(1000);
    connect(&m_watchDelay, &QTimer::timeout, this, [this] {
        loadExternalFonts();
        relist();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_watchDelay.start(); });
    connect(&settings, &ui::SettingsStore::changed, this, &FontCatalogsController::settingChanged);
    // hikarisubApp: the external folder loads at start, then the watcher starts.
    loadExternalFonts();
    watchFolders();
}

FontCatalogsController::~FontCatalogsController()
{
    // ~FontCatalogList: FCManagement.SaveCatalogs() (FontCatalogList.cpp:
    // 218-221 at 20d647c4). A window that was made lived until the end, so
    // the catalogs as they are then are written to FontCatalogs.txt.
    if (m_windowMade)
        save();
    stopResolver();
}

void FontCatalogsController::stopResolver()
{
    {
        std::lock_guard lock(m_resolveMutex);
        m_resolverStop = true;
        m_resolveJob.reset();
    }
    m_resolveWake.notify_all();
    if (m_resolver.joinable())
        m_resolver.join(); // a resolution under way finishes first
}

void FontCatalogsController::setFontService(std::unique_ptr<application::FontServicePort> service)
{
    // A resolution under way keeps the service it was given (shared).
    application::FontEnvironment environment = m_families->environment();
    m_service = std::shared_ptr<application::FontServicePort>(std::move(service));
    m_families = std::make_unique<application::FontFamilies>(*m_service, kLower);
    m_families->setEnvironment(std::move(environment));
    watchFolders();
    emit fontsChanged();
}

void FontCatalogsController::ensureLoaded()
{
    // FCManagement.LoadCatalogs(): Config/FontCatalogs.txt, once.
    if (m_loaded)
        return;
    m_loaded = true;
    if (const auto text = application::readFontCatalogFile(catalogPath(m_dir, application::kFontCatalogsFile)))
        m_catalogs.load(*text);
}

QStringList FontCatalogsController::catalogNames() const
{
    const_cast<FontCatalogsController *>(this)->ensureLoaded();
    return list(m_catalogs.names());
}

QString FontCatalogsController::filterText() const
{
    return m_settings.text("styles.editFilterText");
}

bool FontCatalogsController::filterOn() const
{
    return m_settings.boolean("styles.editFilterTextOn");
}

QString FontCatalogsController::previewText() const
{
    return m_settings.text("styles.previewText");
}

QStringList FontCatalogsController::catalogChoices() const
{
    const_cast<FontCatalogsController *>(this)->ensureLoaded();
    return list(application::catalogChoices(m_catalogs.names(), u16(tr("All fonts")), u16(tr("Without catalog"))));
}

QStringList FontCatalogsController::fontList(int selection, const QString &value, bool filterOn, const QString &filterText)
{
    ensureLoaded();
    // GetFontsTable: the filtered fonts when the filter is on and has text.
    const std::vector<std::u16string> fonts =
        filterText.isEmpty() || !filterOn ? m_families->fonts() : m_families->filteredFonts(u16(filterText));
    return list(application::catalogView(selection, u16(value), fonts, m_catalogs));
}

void FontCatalogsController::setFilterOn(bool on)
{
    m_settings.set("styles.editFilterTextOn", on);
}

void FontCatalogsController::saveFilterText(const QString &text)
{
    m_settings.set("styles.editFilterText", text);
}

QStringList FontCatalogsController::allFonts()
{
    return list(m_families->fonts());
}

int FontCatalogsController::nameIndex(const QStringList &fonts, const QString &name) const
{
    return application::fontListNameIndex(vec(fonts), u16(name), kLower);
}

int FontCatalogsController::partialIndex(const QStringList &fonts, const QString &partial) const
{
    return application::fontListPartialIndex(vec(fonts), u16(partial), kLower);
}

int FontCatalogsController::catalogListIndex(const QStringList &fonts, const QString &partial) const
{
    return application::catalogListPartialIndex(vec(fonts), u16(partial), kLower);
}

int FontCatalogsController::styleFontIndex(const QStringList &fonts, const QString &font) const
{
    // GenerateList: `if (!sel && font.CmpNoCase(styleFont) == 0) sel = row`.
    const std::u16string want = u16(font);
    for (qsizetype i = 0; i < fonts.size(); ++i)
        if (application::compareNoCase(u16(fonts[i]), want, kLower) == 0)
            return int(i);
    return 0;
}

QString FontCatalogsController::catalogOf(const QString &font) const
{
    const_cast<FontCatalogsController *>(this)->ensureLoaded();
    return qs(m_catalogs.catalogOf(u16(font)));
}

bool FontCatalogsController::isFontInCatalog(const QString &catalog, const QString &font) const
{
    const_cast<FontCatalogsController *>(this)->ensureLoaded();
    return m_catalogs.contains(u16(catalog), u16(font));
}

bool FontCatalogsController::catalogExists(const QString &name) const
{
    const_cast<FontCatalogsController *>(this)->ensureLoaded();
    return m_catalogs.find(u16(name)) != nullptr;
}

bool FontCatalogsController::addCatalog(const QString &name, bool autosave)
{
    ensureLoaded();
    const bool added = m_catalogs.addCatalog(u16(name));
    if (autosave) // AddCatalog(catalog, nullptr, setTimer)
        startAutosave();
    emit catalogsChanged();
    return added;
}

bool FontCatalogsController::renameCatalog(const QString &oldName, const QString &newName, int answer)
{
    ensureLoaded();
    using Clash = application::FontCatalogs::Clash;
    const bool done = m_catalogs.rename(u16(oldName), u16(newName), [answer] {
        return answer == 0 ? Clash::Merge : answer == 1 ? Clash::Delete : Clash::Cancel;
    });
    if (done) {
        startAutosave();
        emit catalogsChanged();
    }
    return done;
}

void FontCatalogsController::removeCatalog(const QString &name)
{
    ensureLoaded();
    m_catalogs.remove(u16(name));
    startAutosave();
    emit catalogsChanged();
}

void FontCatalogsController::setFontsInCatalog(const QString &catalog, const QStringList &fonts, bool add)
{
    ensureLoaded();
    for (const auto &font : fonts) {
        if (add)
            m_catalogs.addFont(u16(catalog), u16(font));
        else
            m_catalogs.removeFont(u16(catalog), u16(font));
        startAutosave();
    }
    emit catalogsChanged();
}

void FontCatalogsController::toggleFontInCatalog(const QString &catalog, const QString &font, bool add)
{
    ensureLoaded();
    if (add)
        m_catalogs.addFont(u16(catalog), u16(font));
    else
        m_catalogs.removeFont(u16(catalog), u16(font));
    save(); // AddToCatalog: `if (changed) SaveCatalogs()`
    emit catalogsChanged();
}

void FontCatalogsController::loadCatalogsFrom(const QUrl &file)
{
    ensureLoaded();
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (const auto text = application::readFontCatalogFile(std::filesystem::path(path.toStdU16String())))
        m_catalogs.load(*text);
    emit catalogsChanged();
}

void FontCatalogsController::save()
{
    ensureLoaded();
    application::writeFontCatalogFile(catalogPath(m_dir, application::kFontCatalogsFile), m_catalogs.serialize());
}

bool FontCatalogsController::collectFromSubtitles(const QString &catalog, bool clear, bool allTabs)
{
    ensureLoaded();
    std::vector<std::shared_ptr<const core::Document>> documents;
    if (m_hooks.documents)
        documents = m_hooks.documents(allTabs);
    std::vector<const core::Document *> raw;
    for (const auto &d : documents)
        raw.push_back(d.get());
    const bool done = m_catalogs.collect(u16(catalog), clear, raw);
    if (done) // the catalog's absence returned before SaveCatalogs
        save();
    emit catalogsChanged();
    return done;
}

void FontCatalogsController::noCatalogToAddTo()
{
    if (m_hooks.log)
        m_hooks.log(tr("For adding fonts to catalog,\nclick \"Manage\" button first,\nto create new catalog."));
}

void FontCatalogsController::startAutosave()
{
    if (!m_autosave.isActive())
        m_autosave.start();
}

void FontCatalogsController::autosaveNow()
{
    // The edition timer: "Autosave", Config/FontCatalogsAutosave<i>.txt with i
    // going 0, 1, 2 and round again.
    m_autosaveStatus = tr("Autosave");
    emit autosaveStatusChanged();
    application::writeFontCatalogFile(catalogPath(m_dir, application::fontCatalogsAutosaveFile(m_autosaveIndex)),
                                      m_catalogs.serialize());
    if (++m_autosaveIndex > 2)
        m_autosaveIndex = 0;
    m_autosaveLabel.start();
}

void FontCatalogsController::renderPreview(const QString &font, int width, int height)
{
    if (width < 1 || height < 1)
        return;
    core::StyleValues style = core::legacy::decodeStyle(kFontPreviewStyle, false);
    style.fontname = core::toUtf8(u16(font));
    m_preview = renderStylePreview(m_renderer, style, width, height, previewText(), externalFontLeases());
    ++m_previewKey;
    emit previewChanged();
}

std::vector<application::FontLease> FontCatalogsController::externalFontLeases() const
{
    std::vector<application::FontLease> out;
    for (const auto &f : m_families->environment().externalFonts)
        out.push_back({f.name, f.bytes});
    return out;
}

void FontCatalogsController::loadExternalFonts()
{
    // LoadExternalFontsToProcess(EXTERNAL_FONTS_DIRECTORY): nothing when empty.
    application::FontEnvironment environment = m_families->environment();
    const QString path = m_settings.text("fonts.externalDirectory");
    environment.externalFonts.clear();
    if (!path.isEmpty()) {
        auto load = application::loadExternalFonts(u16(path), kLower);
        if (!load.opened) {
            if (m_hooks.log)
                m_hooks.log(tr("Cannot load external font folder"));
        } else {
            environment.externalFonts = std::move(load.fonts);
        }
    }
    m_families->setEnvironment(std::move(environment));
    // "Cannot add external font file %s." (HikariLogSilent) for the files
    // the provider reads no face from.
    for (const auto &file : m_families->unreadableExternalFonts())
        if (m_hooks.log)
            m_hooks.log(tr("Cannot add external font file %1.").arg(QFileInfo(QString::fromStdString(file)).fileName()));
    if (m_hooks.fontsChanged)
        m_hooks.fontsChanged();
}

void FontCatalogsController::relist()
{
    // EnumerateFonts(true) then RefreshClientsFonts, in a new generation.
    m_service->refresh();
    application::FontEnvironment environment = m_families->environment();
    environment.generation = ++m_generation;
    m_families->setEnvironment(std::move(environment));
    m_families->enumerate();
    watchFolders();
    emit fontsChanged();
}

void FontCatalogsController::refreshFonts()
{
    m_watchDelay.stop();
    loadExternalFonts();
    relist();
}

void FontCatalogsController::watchFolders()
{
    // The Windows font folders or fontconfig's, and the external folder.
    QStringList folders;
    for (const auto &dir : m_service->fontDirectories())
        if (QFileInfo(QString::fromStdString(dir)).isDir())
            folders << QString::fromStdString(dir);
    const QString external = m_settings.text("fonts.externalDirectory");
    if (!external.isEmpty() && QFileInfo(external).isDir())
        folders << external;
    const QStringList watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    if (!folders.isEmpty())
        m_watcher.addPaths(folders);
}

void FontCatalogsController::settingChanged(const QString &id)
{
    if (id == QLatin1String("fonts.externalDirectory")) {
        // ReloadExternalFontsToProcess: the old folder's fonts go, the new
        // folder's load, the lists are made again.
        refreshFonts();
    } else if (id.startsWith(QLatin1String("styles.editFilterText")) || id == QLatin1String("styles.previewText")) {
        emit filterChanged();
    }
}

int FontCatalogsController::resolveFamily(const QString &family, bool bold, bool italic)
{
    // Each resolution builds a libass library over fontconfig, so it runs on
    // the worker; the GUI thread only hands over the latest request.
    const int request = ++m_resolveRequest;
    {
        std::lock_guard lock(m_resolveMutex);
        m_resolveJob = ResolveJob{request, m_service, m_families->environment(),
                                  application::FontRequest{family.toStdString(), bold, italic, "AaBbCcDdEeFfGg 0123456789"}};
    }
    m_resolveWake.notify_one();
    if (!m_resolver.joinable())
        m_resolver = std::thread([this] { resolverLoop(); });
    return request;
}

void FontCatalogsController::resolverLoop()
{
    for (;;) {
        ResolveJob job;
        {
            std::unique_lock lock(m_resolveMutex);
            m_resolveWake.wait(lock, [this] { return m_resolverStop || m_resolveJob.has_value(); });
            if (m_resolverStop)
                return;
            job = std::move(*m_resolveJob);
            m_resolveJob.reset();
        }
        const auto report = job.service->resolve(job.environment, {job.want});
        QVariantMap result;
        if (report) {
            const auto r = application::pickerResolution(*report);
            using K = application::PickerResolution::Kind;
            result[QStringLiteral("kind")] = r.kind == K::Requested     ? QStringLiteral("requested")
                                             : r.kind == K::Substituted ? QStringLiteral("substituted")
                                             : r.kind == K::Fallback    ? QStringLiteral("fallback")
                                                                        : QStringLiteral("missing");
            result[QStringLiteral("family")] = QString::fromStdString(r.family);
            result[QStringLiteral("file")] = QString::fromStdString(r.file);
            result[QStringLiteral("emboldened")] = r.emboldened;
            result[QStringLiteral("italicized")] = r.italicized;
        } else {
            result[QStringLiteral("kind")] = QStringLiteral("unavailable");
        }
        const int request = job.request;
        // The controller outlives this thread (its destructor joins before
        // ~QObject), so the queued call runs on it or is dropped with it.
        QMetaObject::invokeMethod(
            this,
            [this, request, result] {
                m_resolveAnswered = std::max(m_resolveAnswered, request);
                emit resolutionReady(request, result);
            },
            Qt::QueuedConnection);
    }
}

bool FontCatalogsController::waitResolved(int ms)
{
    QElapsedTimer t;
    t.start();
    while (m_resolveAnswered < m_resolveRequest && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return m_resolveAnswered >= m_resolveRequest;
}

namespace {

class FontCatalogPreviewProvider : public QQuickImageProvider {
public:
    explicit FontCatalogPreviewProvider(FontCatalogsController &controller)
        : QQuickImageProvider(QQuickImageProvider::Image), m_controller(controller)
    {
    }
    QImage requestImage(const QString &, QSize *size, const QSize &) override
    {
        const QImage image = m_controller.preview();
        if (size)
            *size = image.size();
        return image;
    }

private:
    FontCatalogsController &m_controller;
};

} // namespace

void attachFontCatalogPreview(QQmlEngine &engine, FontCatalogsController &controller)
{
    engine.addImageProvider(QStringLiteral("fontcatalogpreview"), new FontCatalogPreviewProvider(controller));
}

} // namespace hikari::app
