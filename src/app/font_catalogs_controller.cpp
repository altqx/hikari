#include "hikari/app/font_catalogs_controller.h"

#include "hikari/app/style_manager_controller.h"
#include "hikari/backends/libass_font_service.h"
#include "hikari/core/style.h"
#include "hikari/core/text_projection.h"
#include "list_files.h"
#include "settings_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QMetaObject>
#include <QQuickImageProvider>
#include <QRegularExpression>

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

char16_t lowerUnit(char16_t c)
{
    return QChar(c).toLower().unicode();
}

const application::FontNameLower kLower = lowerUnit;

// A word joiner after each backslash keeps libass from reading an escape.
QString rendererText(QString plain)
{
    plain.replace(QLatin1Char('\\'), QStringLiteral("\\\u2060"));
    plain.replace(QLatin1Char('{'), QStringLiteral("\\{")).replace(QLatin1Char('}'), QStringLiteral("\\}"));
    return plain.trimmed();
}

// A Style line is comma-separated, so a family with a comma goes in an \fn override.
QString withFamily(core::StyleValues &style, const QString &family, const QString &text)
{
    if (family.contains(QLatin1Char(',')))
        return QStringLiteral("{\\fn") + family + QLatin1Char('}') + text;
    style.fontname = core::toUtf8(u16(family));
    return text;
}

constexpr char8_t kFontPreviewStyle[] = u8"Style: FontPreview,Arial,80,&H00FFFFFF,&HFF0000FF,&H00301946,&H7A301946,-1,0,"
                                        u8"0,0,100,100,1.13208,0,1,3.5,0,2,120,120,40,1";

std::filesystem::path catalogPath(const std::filesystem::path &dir, std::u16string_view name)
{
    return dir / std::filesystem::path(std::u16string(name));
}

QString defaultPreviewText()
{
    return QString::fromStdString(std::get<std::string>(application::findSetting("styles.previewText")->defaultValue));
}

} // namespace

FontCatalogsController::FontCatalogsController(ui::SettingsStore &settings, std::filesystem::path catalogDir, Hooks hooks,
                                               QObject *parent)
    : QObject(parent), m_settings(settings), m_dir(std::move(catalogDir)), m_hooks(std::move(hooks)),
      m_catalogs(kLower), m_service(std::make_shared<backends::LibassFontService>()),
      m_families(std::make_unique<application::FontFamilies>(*m_service, kLower))
{
    const ui::ListFile::Report report{m_hooks.fileConverted, m_hooks.fileProblem, m_hooks.fileReplaced};
    const auto convert = [](const std::filesystem::path &legacy) -> std::optional<toml::array> {
        if (const auto catalogs = ui::readLegacyFontCatalogs(legacy))
            return ui::fontCatalogsToToml(*catalogs);
        return std::nullopt;
    };
    const auto file = [&](std::u16string_view name, std::u16string_view legacy) {
        return ui::ListFile::make(QString::fromStdU16String(catalogPath(m_dir, name).u16string()),
                                  QString::fromStdU16String(catalogPath(m_dir, legacy).u16string()),
                                  ui::kFontCatalogsFormat, convert, report);
    };
    m_file = file(u"fonts.toml", application::kFontCatalogsFile);
    for (int n = 0; n < 3; ++n)
        m_autosaveFiles[n] = file(u"fonts.autosave-" + std::u16string(1, char16_t(u'0' + n)) + u".toml",
                                  application::fontCatalogsAutosaveFile(n));
    m_autosave.setSingleShot(true);
    m_autosave.setInterval(20000);
    connect(&m_autosave, &QTimer::timeout, this, &FontCatalogsController::autosaveNow);
    m_autosaveLabel.setSingleShot(true);
    m_autosaveLabel.setInterval(10000);
    connect(&m_autosaveLabel, &QTimer::timeout, this, [this] {
        m_autosaveStatus.clear();
        emit autosaveStatusChanged();
    });
    m_watchDelay.setSingleShot(true);
    m_watchDelay.setInterval(1000);
    connect(&m_watchDelay, &QTimer::timeout, this, [this] {
        loadExternalFonts();
        relist();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_watchDelay.start(); });
    connect(&settings, &ui::SettingsStore::changed, this, &FontCatalogsController::settingChanged);
    loadExternalFonts();
    watchFolders();
}

FontCatalogsController::~FontCatalogsController()
{
    if (m_windowMade || m_unsaved)
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
        m_resolver.join();
}

void FontCatalogsController::setFontService(std::unique_ptr<application::FontServicePort> service)
{
    application::FontEnvironment environment = m_families->environment();
    m_service = std::shared_ptr<application::FontServicePort>(std::move(service));
    m_families = std::make_unique<application::FontFamilies>(*m_service, kLower);
    m_families->setEnvironment(std::move(environment));
    watchFolders();
    emit fontsChanged();
}

void FontCatalogsController::ensureLoaded()
{
    if (m_loaded)
        return;
    m_loaded = true;
    if (const auto items = m_file->read())
        m_catalogs.load(std::get<std::vector<application::FontCatalogs::Catalog>>(ui::fontCatalogsFromToml(*items)));
    if (m_catalogs.find(u""))
        m_catalogs.remove(u"");
}

void FontCatalogsController::convertFiles()
{
    m_file->convert();
    for (const auto &file : m_autosaveFiles)
        file->convert();
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
    const QString text = m_settings.text("styles.previewText");
    return text.trimmed().isEmpty() ? defaultPreviewText() : text;
}

QStringList FontCatalogsController::catalogChoices() const
{
    const_cast<FontCatalogsController *>(this)->ensureLoaded();
    return list(application::catalogChoices(m_catalogs.names(), u16(QCoreApplication::translate("HikariSub.Fonts", "All fonts")),
                                            u16(QCoreApplication::translate("HikariSub.Fonts", "Without catalog"))));
}

QStringList FontCatalogsController::fontList(int selection, const QString &value, bool filterOn, const QString &filterText)
{
    ensureLoaded();
    const std::vector<std::u16string> fonts =
        filterText.isEmpty() || !filterOn ? m_families->fonts() : m_families->filteredFonts(u16(filterText));
    return list(application::catalogView(selection, u16(value), fonts, m_catalogs));
}

void FontCatalogsController::setFilterOn(bool on)
{
    m_settings.set("styles.editFilterTextOn", on);
}

void FontCatalogsController::setPreviewText(const QString &text)
{
    if (text != previewText())
        m_settings.set("styles.previewText", text);
}

void FontCatalogsController::saveFilterText(const QString &text)
{
    m_settings.set("styles.editFilterText", text);
}

void FontCatalogsController::applyFilterText(const QString &text)
{
    ensureLoaded();
    saveFilterText(text);
    m_families->refilter(u16(text));
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

QStringList FontCatalogsController::catalogsOf(const QString &font) const
{
    const_cast<FontCatalogsController *>(this)->ensureLoaded();
    QStringList out;
    for (const auto &c : m_catalogs.catalogs())
        if (std::find(c.fonts.begin(), c.fonts.end(), u16(font)) != c.fonts.end())
            out << qs(c.name);
    return out;
}

int FontCatalogsController::catalogSize(const QString &name) const
{
    const_cast<FontCatalogsController *>(this)->ensureLoaded();
    const auto *c = m_catalogs.find(u16(name));
    return c ? int(c->fonts.size()) : 0;
}

QString FontCatalogsController::existingName(const QString &name) const
{
    const_cast<FontCatalogsController *>(this)->ensureLoaded();
    const auto *c = m_catalogs.findNoCase(u16(name));
    return c ? qs(c->name) : QString();
}

int FontCatalogsController::sameNameIndex(const QStringList &names, const QString &name) const
{
    const std::u16string key = u16(name);
    for (int i = 0; i < names.size(); ++i)
        if (application::sameNoCase(u16(names[i]), key, kLower))
            return i;
    return -1;
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
    if (autosave)
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
    save();
    emit catalogsChanged();
}

QVariantMap FontCatalogsController::reviewCatalogFile(const QUrl &file)
{
    ensureLoaded();
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    m_reviewed.clear();
    // Anything but fonts.toml text is read as a HikariSub legacy FontCatalogs.txt.
    const std::filesystem::path chosen(path.toStdU16String());
    if (const auto bytes = application::readOwnedFile(chosen)) {
        if (auto catalogs = ui::fontCatalogsFromText(*bytes))
            m_reviewed = std::move(*catalogs);
        else if (const auto text = application::readFontCatalogFile(chosen))
            m_reviewed = application::FontCatalogs::parse(*text);
    }
    std::erase_if(m_reviewed, [](const auto &c) { return c.name.empty(); });
    std::vector<application::FontCatalogs::Catalog> one;
    for (auto &c : m_reviewed) {
        const auto it = std::find_if(one.begin(), one.end(), [&](const auto &o) { return application::sameNoCase(o.name, c.name, kLower); });
        if (it == one.end()) {
            one.push_back(std::move(c));
            continue;
        }
        for (auto &font : c.fonts)
            if (std::find(it->fonts.begin(), it->fonts.end(), font) == it->fonts.end())
                it->fonts.push_back(std::move(font));
    }
    m_reviewed = std::move(one);
    QVariantList rows;
    QStringList names;
    int same = 0;
    for (const auto &c : m_reviewed) {
        names.append(qs(c.name));
        const auto *here = m_catalogs.findNoCase(c.name);
        if (!here)
            continue;
        int added = 0;
        for (const auto &font : c.fonts)
            if (std::find(here->fonts.begin(), here->fonts.end(), font) == here->fonts.end())
                ++added;
        if (added == 0 && here->fonts.size() == c.fonts.size()) {
            ++same;
            continue;
        }
        rows.append(QVariantMap{{QStringLiteral("name"), qs(c.name)},
                                {QStringLiteral("here"), int(here->fonts.size())},
                                {QStringLiteral("there"), int(c.fonts.size())},
                                {QStringLiteral("added"), added}});
    }
    return {{QStringLiteral("ok"), !m_reviewed.empty()},
            {QStringLiteral("file"), QFileInfo(path).fileName()},
            {QStringLiteral("total"), int(m_reviewed.size())},
            {QStringLiteral("rows"), rows},
            {QStringLiteral("names"), names},
            {QStringLiteral("same"), same}};
}

QVariantMap FontCatalogsController::loadReviewed(const QVariantList &choices)
{
    ensureLoaded();
    using Action = application::FontCatalogs::LoadAction;
    std::vector<application::FontCatalogs::LoadChoice> chosen(m_reviewed.size());
    int added = 0, merged = 0, replaced = 0, renamed = 0, skipped = 0, same = 0;
    std::vector<std::u16string> addedNames;
    const auto isNew = [&](const std::u16string &name) {
        if (m_catalogs.findNoCase(name))
            return false;
        for (const auto &earlier : addedNames)
            if (application::sameNoCase(earlier, name, kLower))
                return false;
        addedNames.push_back(name);
        return true;
    };
    for (std::size_t i = 0; i < m_reviewed.size(); ++i) {
        bool reviewed = false;
        for (const QVariant &v : choices) {
            const QVariantMap c = v.toMap();
            if (u16(c.value(QStringLiteral("name")).toString()) != m_reviewed[i].name)
                continue;
            reviewed = true;
            const QString action = c.value(QStringLiteral("action")).toString();
            chosen[i].action = action == QLatin1String("replace") ? Action::Replace
                             : action == QLatin1String("rename")  ? Action::Rename
                             : action == QLatin1String("skip")    ? Action::Skip
                                                                  : Action::Merge;
            chosen[i].newName = u16(c.value(QStringLiteral("newName")).toString());
        }
        if (!reviewed && m_catalogs.findNoCase(m_reviewed[i].name)) {
            chosen[i].action = Action::Skip;
            ++same;
            continue;
        }
        // MUST KILL: these counts mirror FontCatalogs::apply()'s rules.
        switch (chosen[i].action) {
        case Action::Skip: ++skipped; break;
        case Action::Replace: ++replaced; break;
        case Action::Rename: ++(isNew(chosen[i].newName) ? renamed : merged); break;
        case Action::Merge: ++(isNew(m_reviewed[i].name) ? added : merged); break;
        }
    }
    m_catalogs.apply(m_reviewed, chosen);
    const int loaded = int(m_reviewed.size()) - skipped - same;
    m_reviewed.clear();
    emit catalogsChanged();
    return {{QStringLiteral("loaded"), loaded}, {QStringLiteral("added"), added}, {QStringLiteral("merged"), merged},
            {QStringLiteral("replaced"), replaced}, {QStringLiteral("renamed"), renamed}, {QStringLiteral("skipped"), skipped}};
}

bool FontCatalogsController::save()
{
    ensureLoaded();
    const bool written = m_file->write(ui::fontCatalogsToToml(m_catalogs.catalogs())) != ui::ListFile::Write::Failed;
    m_unsaved = !written;
    return written;
}

QVariantMap FontCatalogsController::collectFromSubtitles(const QString &catalog, bool clear, bool allTabs)
{
    ensureLoaded();
    std::vector<std::shared_ptr<const core::Document>> documents;
    if (m_hooks.documents)
        documents = m_hooks.documents(allTabs);
    std::vector<const core::Document *> raw;
    for (const auto &d : documents)
        raw.push_back(d.get());
    const bool done = m_catalogs.collect(u16(catalog), clear, raw);
    const bool saved = done && save();
    emit catalogsChanged();
    return {{QStringLiteral("done"), done}, {QStringLiteral("saved"), saved}};
}

void FontCatalogsController::startAutosave()
{
    if (!m_autosave.isActive())
        m_autosave.start();
}

void FontCatalogsController::autosaveNow()
{
    const bool written = m_autosaveFiles[m_autosaveIndex]->write(ui::fontCatalogsToToml(m_catalogs.catalogs())) !=
                         ui::ListFile::Write::Failed;
    m_autosaveStatus = written ? QCoreApplication::translate("HikariSub.Fonts", "Autosave") : QString();
    m_autosaveFailed = !written;
    emit autosaveStatusChanged();
    if (++m_autosaveIndex > 2)
        m_autosaveIndex = 0;
    m_autosaveLabel.start();
}

void FontCatalogsController::renderPreview(const QString &font, int width, int height, qreal scale)
{
    if (width < 1 || height < 1)
        return;
    core::StyleValues style = core::legacy::decodeStyle(kFontPreviewStyle, false);
    const QString plain = samplePlain(previewText(), true);
    // libass sizes a face by its height, not its em: Qt's bold advance at 80 px is at least
    // libass's at size 80.
    QFont face(font);
    face.setPixelSize(80);
    face.setBold(true);
    const qreal wide = QFontMetricsF(face).horizontalAdvance(plain);
    const qreal room = width - 240.0;
    QString fit;
    if (room > 0 && wide > room)
        fit = QStringLiteral("{\\fscx%1\\fscy%1}").arg(std::max(1, int(100 * room / wide)));
    const QString text = withFamily(style, font, fit + rendererText(plain));
    m_preview = renderStylePreview(m_renderer, style, width, height, text, externalFontLeases(), m_atScreenSize ? scale : 1);
    ++m_previewKey;
    emit previewChanged();
}

QString FontCatalogsController::samplePlain(const QString &text, bool ass, int from, int to) const
{
    constexpr qsizetype kMaxSample = 1000;
    QString out;
    out.reserve(std::min(text.size(), kMaxSample));
    const auto put = [&](qsizetype at, QChar c) {
        if (to < from || (at >= from && at < to))
            out += c;
    };
    bool drawing = false;
    // MUST KILL: -2 means not searched yet, -1 no '}' left.
    qsizetype nextClose = -2;
    for (qsizetype i = 0; i < text.size() && out.size() < kMaxSample; ++i) {
        const QChar c = text[i];
        if (c == QLatin1Char('\n')) {
            if (!drawing)
                put(i, QLatin1Char(' '));
            continue;
        }
        if (!ass) {
            put(i, c);
            continue;
        }
        if (c == QLatin1Char('{')) {
            if (nextClose != -1 && nextClose <= i)
                nextClose = text.indexOf(QLatin1Char('}'), i + 1);
            const qsizetype close = nextClose;
            if (close >= 0) {
                // libass switches drawing mode only on a top-level \pN, not one inside parentheses.
                const QString block = text.mid(i + 1, close - i - 1);
                int depth = 0;
                for (qsizetype k = 0; k < block.size(); ++k) {
                    const QChar b = block[k];
                    if (b == QLatin1Char('('))
                        ++depth;
                    else if (b == QLatin1Char(')'))
                        depth = std::max(0, depth - 1);
                    else if (depth == 0 && b == QLatin1Char('\\') && k + 1 < block.size() && block[k + 1] == QLatin1Char('p')) {
                        qsizetype digits = k + 2;
                        if (digits < block.size() && block[digits] == QLatin1Char('-'))
                            ++digits;
                        qsizetype end = digits;
                        while (end < block.size() && block[end].isDigit())
                            ++end;
                        if (end > digits)
                            drawing = block.mid(k + 2, end - k - 2).toInt() > 0;
                    }
                }
                i = close;
                continue;
            }
        }
        if (drawing)
            continue;
        if (c == QLatin1Char('\\') && i + 1 < text.size()) {
            const QChar next = text[i + 1];
            if (next == QLatin1Char('N') || next == QLatin1Char('n') || next == QLatin1Char('h')
                || next == QLatin1Char('{') || next == QLatin1Char('}')) {
                put(i, next == QLatin1Char('h') ? QChar(0x00A0) : next == QLatin1Char('N') || next == QLatin1Char('n') ? QChar(u' ') : next);
                ++i;
                continue;
            }
        }
        put(i, c);
    }
    return out.trimmed();
}

void FontCatalogsController::renderSample(const QVariantMap &font, const QString &text, bool ass, int width, int height,
                                         qreal scale)
{
    if (width < 1 || height < 1)
        return;
    core::StyleValues style = core::legacy::decodeStyle(kFontPreviewStyle, false);
    const double size = std::clamp(font.value(QStringLiteral("size")).toString().toDouble(), 6.0, 64.0);
    style.fontsize = core::toUtf8(u16(QString::number(size)));
    style.bold = font.value(QStringLiteral("bold")).toBool();
    style.italic = font.value(QStringLiteral("italic")).toBool();
    style.underline = font.value(QStringLiteral("underline")).toBool();
    style.strikeOut = font.value(QStringLiteral("strikeOut")).toBool();
    const QString plain = withFamily(style, font.value(QStringLiteral("name")).toString(), rendererText(samplePlain(text, ass)));
    std::vector<application::FontLease> fonts = m_hooks.documentFonts ? m_hooks.documentFonts() : std::vector<application::FontLease>();
    for (auto &font : externalFontLeases())
        fonts.push_back(std::move(font));
    m_sample = renderStylePreview(m_renderer, style, width, height, plain, std::move(fonts), m_atScreenSize ? scale : 1);
    ++m_sampleKey;
    emit sampleChanged();
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
    application::FontEnvironment environment = m_families->environment();
    const QString path = m_settings.text("fonts.externalDirectory");
    environment.externalFonts.clear();
    if (!path.isEmpty()) {
        auto load = application::loadExternalFonts(u16(path), kLower);
        if (!load.opened) {
            if (m_hooks.log)
                m_hooks.log(QCoreApplication::translate("HikariSub.Fonts", "Cannot load external font folder"));
        } else {
            environment.externalFonts = std::move(load.fonts);
        }
    }
    m_families->setEnvironment(std::move(environment));
    for (const auto &file : m_families->unreadableExternalFonts())
        if (m_hooks.log)
            m_hooks.log(QCoreApplication::translate("HikariSub.Fonts", "Cannot add external font file %1.")
                            .arg(QFileInfo(QString::fromStdString(file)).fileName()));
    if (m_hooks.fontsChanged)
        m_hooks.fontsChanged();
}

void FontCatalogsController::relist()
{
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
        refreshFonts();
    } else if (id.startsWith(QLatin1String("styles.editFilterText")) || id == QLatin1String("styles.previewText")) {
        emit filterChanged();
    }
}

int FontCatalogsController::resolveFamily(const QString &family, bool bold, bool italic, bool documentFonts,
                                          const QString &text)
{
    application::FontEnvironment environment = m_families->environment();
    if (documentFonts && m_hooks.documentFonts)
        for (auto &font : m_hooks.documentFonts())
            environment.attachments.push_back({std::move(font.family), std::move(font.bytes)});
    const int request = ++m_resolveRequest;
    {
        std::lock_guard lock(m_resolveMutex);
        m_resolveJob = ResolveJob{request, m_service, std::move(environment),
                                  application::FontRequest{family.toStdString(), bold, italic,
                                                           (text.trimmed().isEmpty() ? defaultPreviewText() : text).toStdString()}};
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
        // MUST KILL: captures this; safe only because the destructor joins this thread.
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
    QImage requestImage(const QString &id, QSize *size, const QSize &) override
    {
        const QImage image = id.startsWith(QLatin1String("sample/")) ? m_controller.sample() : m_controller.preview();
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
