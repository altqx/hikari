#include "hikari/app/application.h"

#include "hikari/backends/legacy_text_file.h"
#include "hikari/backends/portaudio_output.h"
#include "hikari/backends/simulated_output.h"
#include "hikari/application/grid_clipboard.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/grid_filtering.h"
#include "hikari/application/grid_groups.h"
#include "hikari/application/grid_split.h"
#include "hikari/application/grid_translation.h"
#include "hikari/application/script_properties.h"
#include "hikari/application/shift_times.h"
#include "hikari/application/select_lines.h"
#include "hikari/application/resample.h"
#include "hikari/application/spell_checker.h"
#include "hikari/application/legacy_dir.h"
#include "hikari/core/spelling.h"
#include "hikari/core/text_projection.h"
#include "spelling_text.h"
#include "hikari/core/conversion.h"
#include "hikari/application/keyframe_files.h"
#include "hikari/core/line_groups.h"
#include "hikari/core/style.h"
#include "hikari/core/subtitle_load.h"
#include "hikari/application/media_association.h"
#include "hikari/core/ass_save.h"
#include "automation_services_qt.h"
#include "icon_theme.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QFile>
#include <QMetaMethod>
#include <QSet>
#include <QStringDecoder>
#include <QTextBoundaryFinder>
#include <QThread>
#include <QImage>
#include <QVideoFrame>
#include <cstring>
#include <QCollator>
#include <QGuiApplication>
#include <QLocale>
#include <QDir>
#include <QUuid>
#include <QCoreApplication>
#include <QTimer>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <cstdlib>
#include <QFileInfo>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#include <unistd.h>
#endif

namespace hikari::app {

namespace {

// U1-unicode-case: case-insensitive matching folds every letter by Unicode
// simple case folding, which maps one code point to one, so match offsets in
// the folded text are offsets in the original.
std::u16string unicodeFold(std::u16string_view s)
{
    const QString folded = QString(reinterpret_cast<const QChar *>(s.data()), static_cast<qsizetype>(s.size())).toCaseFolded();
    return std::u16string(reinterpret_cast<const char16_t *>(folded.utf16()), static_cast<std::size_t>(folded.size()));
}

std::u8string toU8(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    return std::u8string(reinterpret_cast<const char8_t *>(utf8.constData()), static_cast<std::size_t>(utf8.size()));
}

QString mediaHelperPath(const QString &configured)
{
    if (!configured.isEmpty())
        return configured;
#ifdef _WIN32
    const QString name = QStringLiteral("hikari-media-helper.exe");
#else
    const QString name = QStringLiteral("hikari-media-helper");
#endif
    const QString sibling = QCoreApplication::applicationDirPath() + QLatin1Char('/') + name;
#ifdef HIKARI_BUILD_MEDIA_HELPER
    if (!QFileInfo::exists(sibling))
        return QStringLiteral(HIKARI_BUILD_MEDIA_HELPER); // running from the build tree
#endif
    return sibling;
}

QString luaHelperPath(const QString &configured)
{
    if (!configured.isEmpty())
        return configured;
#ifdef _WIN32
    const QString name = QStringLiteral("hikari-lua-helper.exe");
#else
    const QString name = QStringLiteral("hikari-lua-helper");
#endif
    const QString sibling = QCoreApplication::applicationDirPath() + QLatin1Char('/') + name;
#ifdef HIKARI_BUILD_LUA_HELPER
    if (!QFileInfo::exists(sibling))
        return QStringLiteral(HIKARI_BUILD_LUA_HELPER);
#endif
    return sibling;
}

QString automationPath(const QString &configured)
{
    if (!configured.isEmpty())
        return configured;
    const QString sibling = QCoreApplication::applicationDirPath() + QStringLiteral("/Automation");
#ifdef HIKARI_BUILD_AUTOMATION
    if (!QFileInfo::exists(sibling + QStringLiteral("/automation")))
        return QStringLiteral(HIKARI_BUILD_AUTOMATION);
#endif
    return sibling;
}

} // namespace

// F1: the shell around FindReplace (legacy HikariSubFrame, the Grid, the
// EditBox, HikariMessageBox and the files on disk).
class Application::FindHost : public application::FindReplaceHost {
public:
    explicit FindHost(Application &app) : m_app(app) {}

    static QString qs(std::u8string_view s)
    {
        return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
    }

    std::optional<application::FindTab> tab(application::DocumentId id)
    {
        auto *session = m_app.m_files->session(id);
        const std::string *title = m_app.m_workspace.title(id);
        if (!session)
            return std::nullopt;
        return application::FindTab{id, session, title ? toU8(QString::fromStdString(*title)) : std::u8string()};
    }
    std::optional<application::FindTab> current() override
    {
        const auto target = m_app.m_workspace.editingTarget();
        return target ? tab(*target) : std::nullopt;
    }
    std::vector<application::FindTab> tabs() override
    {
        std::vector<application::FindTab> out;
        for (const auto id : m_app.m_workspace.documents())
            if (auto t = tab(id))
                out.push_back(*t);
        return out;
    }
    application::LineVisible actionLines(application::EditSession &session) override
    {
        return m_app.actionLines(session);
    }
    // HikariMessageBox / HikariMessageDialog without a nested event loop: the
    // box is shown (findQuestion) and the engine resumes when it is answered
    // (answerFindQuestion); a message needs no answer. Tests answer at once.
    void ask(const application::FindQuestion &question, std::function<void(application::FindAnswer)> answer) override
    {
        const QString text = questionText(question);
        const QString title = questionTitle(question);
        const int kind = static_cast<int>(question.kind);
        const auto reply = [&](int code) {
            if (answer)
                answer(static_cast<application::FindAnswer>(std::clamp(code, 0, 3)));
        };
        if (m_app.m_findQuestionHandler) {
            reply(m_app.m_findQuestionHandler(kind, text));
            return;
        }
        if (!m_app.isSignalConnected(QMetaMethod::fromSignal(&Application::findQuestion))) {
            reply(answer ? 3 : 0); // nobody to ask: Cancel
            return;
        }
        const int id = ++m_app.m_findQuestionId;
        if (answer)
            m_app.m_findAnswers.emplace(id, std::move(answer));
        emit m_app.findQuestion(id, kind, text, title);
        emit m_app.findBusyChanged();
    }
    // The legacy texts, translated (the engine's English is the source).
    static QString questionText(const application::FindQuestion &q)
    {
        using K = application::FindQuestion::Kind;
        using I = application::FindQuestion::Info;
        const QString argument = qs(q.argument);
        switch (q.kind) {
        case K::Wrap: return tr("Reached end. Search from the beginning?");
        case K::Styles:
            return tr("Styles named \"%1\" do not exist in the subtitles being searched,\nwhich may significantly reduce "
                      "the number of search results.\nWhat would you like to do?")
                .arg(argument);
        case K::NoStyles:
            return tr("None of the selected styles exist in the subtitles being searched,\nso nothing will be found.\n"
                      "What would you like to do?");
        case K::ConfirmFiles:
            return tr("Are you sure you want to make changes in all subtitle files?\nIf you make a mistake, backups are "
                      "in the 'ReplaceBackup' folder.");
        case K::Message: break;
        }
        switch (q.info) {
        case I::NotFound: return tr("Could not find the specified phrase \"%1\".").arg(argument);
        case I::Replaced: return tr("Replaced %1 times.").arg(argument);
        case I::InvalidPath: return tr("Search path is invalid");
        case I::None: break;
        }
        return qs(q.text);
    }
    static QString questionTitle(const application::FindQuestion &q)
    {
        const QString title = qs(q.title);
        if (title == QLatin1String("Confirmation"))
            return tr("Confirmation");
        if (title == QLatin1String("Info"))
            return tr("Info");
        if (title == QLatin1String("Find and Replace") || title.isEmpty())
            return tr("Find and Replace");
        return title;
    }
    void finished() override { m_app.findFinished(); }
    // The legacy log lines, translated (the engine's English is the source).
    void logLine(const application::FindLog &line) override { m_app.m_log->log(logText(line)); }
    static QString logText(const application::FindLog &line)
    {
        using K = application::FindLog::Kind;
        switch (line.kind) {
        case K::InvalidRegex: return tr("Invalid regular expression '%1': %2").arg(qs(line.first), qs(line.second));
        case K::MatchError: return tr("Failed to find match for regular expression: %1").arg(qs(line.first));
        case K::LineEdited: return tr("Line %1 cannot be replaced,\ncause it was edited.").arg(qs(line.first));
        case K::CannotBackUp: return tr("Cannot back up %1.").arg(qs(line.first));
        case K::OutsideReplaceTab: break; // legacy's untranslated debug text
        }
        return qs(line.text);
    }
    void recentAdded(application::FindReplaceSettings::Tab tab) override { m_app.saveFindRecent(tab); }
    void showLine(application::DocumentId document, core::LineId line, bool keepSelection, int role, int start,
                  int end) override
    {
        if (m_app.m_workspace.editingTarget() != document) {
            if (!m_app.m_workspace.setEditingTarget(document))
                return;
            m_app.refreshViews();
        }
        auto *session = m_app.m_files->session(document);
        if (!session)
            return;
        application::Selection next = session->selection();
        if (!keepSelection)
            next.selected = {line};
        next.active = line;
        next.anchor = line;
        next.extent.reset();
        if (!m_app.applySelection(next))
            return;
        // TextEdit holds the translation in translation mode; TextEditOrig the original.
        if (role == 1)
            m_app.m_editor->selectRaw(application::translationMode(*session) ? 1 : 0, start, end);
        else if (role == 0)
            m_app.m_editor->selectRaw(0, start, end);
    }
    void changed(application::DocumentId) override
    {
        m_app.m_editor->reloadFromSession();
        m_app.refreshViews();
    }
    // wxDir::GetAllFiles: subfolders first, then the files, each in the
    // order the file system lists them (readdir, FindFirstFile; no sorting,
    // R5-per-platform); hidden ones only when asked. Links are followed as
    // wxDir follows them, so a folder reached twice through links is listed
    // twice. Only a real cycle (a folder inside itself) is cut: legacy
    // recursed until the path was too long, listing and replacing the same
    // files dozens of times at once (R3-hang-crash-loss, F1-link-loop).
    std::optional<std::vector<std::u8string>> listFiles(const std::u8string &folder, const std::u8string &filter,
                                                        bool subfolders, bool hidden) override
    {
        const QDir dir(qs(folder));
        if (folder.empty() || !dir.exists())
            return std::nullopt;
        std::vector<std::u8string> out;
        QStringList path;
        collect(dir, qs(filter), subfolders, hidden, path, out);
        return out;
    }
    // OpenWrite::FileOpen (R4-uchardet, R5-per-platform): see legacy_text_file.h.
    std::optional<std::u16string> readFile(const std::u8string &path) override
    {
        return backends::readLegacyTextFile(qs(path));
    }
    bool fileExists(const std::u8string &path) override { return QFileInfo(qs(path)).isFile(); }
    // The legacy copy into ReplaceBackup beside the settings file, the one
    // place written besides the chosen files (none without a settings file).
    bool backupFile(const std::u8string &path) override
    {
        if (m_app.m_replaceBackup.isEmpty())
            return true;
        QDir().mkpath(m_app.m_replaceBackup);
        const QString source = qs(path);
        const QString copy = m_app.m_replaceBackup + QLatin1Char('/') + QFileInfo(source).fileName();
        QFile::remove(copy);
        return QFile::copy(source, copy);
    }
    // OpenWrite::FileWrite: UTF-8 with a BOM, in place (a link writes its
    // target). A failed or short write puts the original bytes back.
    bool writeFile(const std::u8string &path, const std::u16string &text) override
    {
        const QString name = qs(path);
        QByteArray original;
        {
            QFile in(name);
            if (!in.open(QIODevice::ReadOnly)) {
                m_app.m_log->log(tr("Cannot open file %1.").arg(name));
                return false;
            }
            original = in.readAll();
        }
        const QByteArray bytes =
            "\xEF\xBB\xBF" + QString(reinterpret_cast<const QChar *>(text.data()), static_cast<qsizetype>(text.size())).toUtf8();
        const auto write = [&](const QByteArray &data) {
            QFile file(name);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return false;
            const bool all = file.write(data) == data.size() && file.flush();
            file.close();
            return all && file.error() == QFileDevice::NoError;
        };
        if (write(bytes))
            return true;
        m_app.m_log->log(tr("Cannot write file %1; it was left as it was.").arg(name));
        if (!write(original))
            m_app.m_log->log(tr("%1 could not be restored; its backup is in %2.").arg(name, m_app.m_replaceBackup));
        return false;
    }
    // FindReplace::ShowResult for a file: the tab that has it, else legacy
    // InsertTab + OpenFile when the current tab has a file, else OpenFile
    // into the current (Untitled) tab, which asks to save its changes first
    // (the close review, P2's reviewOpen). Legacy then took the result's Line
    // from whatever tab was current: after a cancelled or failed open, the
    // Untitled Document (or the new empty tab).
    void openFile(const std::u8string &path, std::function<void(std::optional<application::DocumentId>)> done) override
    {
        const QString name = qs(path);
        const QString wanted = QFileInfo(name).canonicalFilePath();
        std::optional<application::DocumentId> found;
        for (const auto id : m_app.m_workspace.documents())
            if (const auto destination = m_app.m_files->destination(id);
                destination && QFileInfo(QString::fromStdString(destination->value)).canonicalFilePath() == wanted)
                found = id;
        if (found)
            return done(found);
        const auto target = m_app.m_workspace.editingTarget();
        if (!target || !m_app.targetUntitled()) {
            if (const auto id = m_app.open(name))
                return done(id);
            m_app.newDocument(); // InsertTab came first
            return done(m_app.m_workspace.editingTarget());
        }
        const QVariantMap review = m_app.reviewOpen(name);
        if (!review.value(QStringLiteral("ok")).toBool())
            return done(target);
        const QVariantList rows = review.value(QStringLiteral("rows")).toList();
        if (rows.isEmpty()) {
            m_app.finishClose();
            return done(m_app.m_workspace.editingTarget());
        }
        m_app.m_findOpenDone = [this, target, done = std::move(done)](bool opened) {
            done(opened ? m_app.m_workspace.editingTarget() : std::optional(*target));
        };
        emit m_app.findOpenReview(rows);
        emit m_app.findBusyChanged();
    }
    // Legacy numOfProcessors (findreplace.cpp): GetSystemInfo's
    // dwNumberOfProcessors, 4 when it is 0. The Linux build's GetSystemInfo
    // (platform.h) is sysconf(_SC_NPROCESSORS_ONLN), the online processors
    // whatever the affinity mask, else hardware_concurrency, at least 1.
    int processorCount() override
    {
        if (m_app.m_findProcessors > 0)
            return m_app.m_findProcessors;
#ifdef _WIN32
        SYSTEM_INFO info{};
        ::GetSystemInfo(&info);
        return info.dwNumberOfProcessors > 0 ? static_cast<int>(info.dwNumberOfProcessors) : 4;
#else
        long processors = ::sysconf(_SC_NPROCESSORS_ONLN);
        if (processors < 1)
            processors = static_cast<long>(std::thread::hardware_concurrency());
        return static_cast<int>(std::max<long>(1, processors));
#endif
    }

private:
    static QString tr(const char *text) { return Application::tr(text); }
    // wxDir::Traverse through application::legacy_dir (R5-per-platform):
    // the subfolders (listed with no pattern) walked first, then this
    // folder's own files matching `filter`; on Windows FindFirstFileW with
    // wx's PathMatchSpec check and the entries' own hidden/system
    // attributes, on Linux readdir and wxMatchWild.
    void collect(const QDir &dir, const QString &filter, bool subfolders, bool hidden, QStringList &path,
                 std::vector<std::u8string> &out)
    {
        const QString canonical = dir.canonicalPath();
        if (path.contains(canonical))
            return; // a cycle
        path.push_back(canonical);
        const std::filesystem::path folder = QFileInfo(dir.absolutePath()).filesystemAbsoluteFilePath();
        const int shown = hidden ? application::legacy_dir::Hidden : 0;
        if (subfolders)
            for (const auto &sub : application::legacy_dir::entries(folder, {}, application::legacy_dir::Dirs | shown)
                                       .value_or(std::vector<std::filesystem::path>{}))
                collect(QDir(QFileInfo(sub).absoluteFilePath()), filter, subfolders, hidden, path, out);
        // wxDir takes every entry that is not a folder (broken links, fifos
        // and the like too).
        for (const auto &file : application::legacy_dir::entries(folder, filter.toStdU16String(),
                                                                 application::legacy_dir::Files | shown)
                                    .value_or(std::vector<std::filesystem::path>{}))
            out.push_back(toU8(QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath())));
        path.pop_back();
    }

    Application &m_app;
};

Application::Application(QObject *parent) : Application(Options{}, parent) {}

Application::Application(Options options, QObject *parent) : QObject(parent)
{
    m_reader = backends::makeFileReader();
    // Write outcomes arrive on the writer's thread; publish them on this one.
    m_port = backends::makeFilePort([this](application::PermitId permit, application::WriteOutcome outcome) {
        QMetaObject::invokeMethod(this, [this, permit, outcome] { m_writes->complete(permit, outcome); },
                                  Qt::QueuedConnection);
    });
    m_writes = std::make_unique<application::WriteCoordinator>(*m_port, [this](const application::WriteResult &r) {
        m_files->onWriteResult(r);
        m_editor->writeFinished();
        writeFinished(r);
    });
    m_files = std::make_unique<application::DocumentFiles>(*m_reader, *m_writes);
    m_log = std::make_unique<ui::LogController>();
    m_shell = std::make_unique<ui::ShellController>(m_workspace);
    m_editor = std::make_unique<ui::LineEditorController>(*m_files);
    m_editor->setCommittedListener([this] { refreshViews(); });
    m_mediaSource = std::make_unique<backends::FfmsIndexedSource>(mediaHelperPath(options.mediaHelper));
    m_video = std::make_unique<ui::VideoController>(*m_mediaSource, m_renderer);
    // V1: playback through the general player; its frames are shown with the
    // overlay, and a pause hands back to the exact indexed frame.
    m_generalPlayer = std::make_unique<backends::QtGeneralPlayer>(options.playbackAudio);
    m_video->session().setGeneralPlayer(m_generalPlayer.get());
    connect(m_generalPlayer.get(), &backends::QtGeneralPlayer::frameDelivered, this, [this](const QVideoFrame &frame) {
        const QImage image = frame.toImage().convertToFormat(QImage::Format_RGB32); // BGRA in memory
        if (image.isNull())
            return;
        application::IndexedFrame out;
        out.index = -1; // a player frame, not an indexed one
        out.width = image.width();
        out.height = image.height();
        out.stride = static_cast<int>(image.bytesPerLine());
        out.bgra.resize(static_cast<std::size_t>(image.sizeInBytes()));
        std::memcpy(out.bgra.data(), image.constBits(), out.bgra.size());
        m_video->session().generalFrame(std::move(out), frame.startTime());
        emit m_video->changed();
    }, Qt::QueuedConnection);
    m_automation = std::make_unique<AutomationShell>(
        AutomationShell::Paths{luaHelperPath(options.luaHelper), automationPath(options.automationDir)}, *m_files,
        m_workspace, *m_editor, *m_video, *m_shell, *m_mediaSource);
    m_automation->setDocumentChanged([this] { refreshViews(); });
    if (options.autoload)
        m_automation->autoload();
    m_settingsFile = options.settingsFile;
    // R6-dictionary-location: user dictionaries in the settings folder's
    // Dictionary, bundled ones in the program folder's (legacy's only one).
    if (!m_settingsFile.isEmpty())
        m_dictionaryDirs << QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/Dictionary");
    if (const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/Dictionary");
        !m_dictionaryDirs.contains(bundled))
        m_dictionaryDirs << bundled;
    // O1: the settings registry over the INI file (in memory without one).
    m_settings = std::make_unique<ui::SettingsStore>(m_settingsFile);
    connect(m_settings.get(), &ui::SettingsStore::changed, this, &Application::settingChanged);
    // K1: the icons take their colours from this profile.
    ui::IconTheme::useSettings(m_settings.get());
    m_tagButtons = std::make_unique<ui::TagButtonsController>(*m_settings);
    m_colourPicker = std::make_unique<ui::ColourPickerController>(*m_settings);
    m_shiftTimes = std::make_unique<ui::ShiftTimesController>(*m_settings);
    m_selectOptions = m_settings->integer("selectLines.options");
    // Legacy keeps 20 when the dialog opens.
    m_selectRecent = m_settings->list("selectLines.recentSelections").mid(0, 20);
    // D1: the panel layout beside the settings (none without a settings file).
    m_workspaceLayout = std::make_unique<ui::WorkspaceLayoutController>(
        m_settingsFile.isEmpty() ? QString() : QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/layout.json"));
    m_gridFilter = std::make_unique<ui::GridFilterController>(*m_settings);
    // T1: the visual tools edit the editing target, never the reference.
    m_visualTools = std::make_unique<ui::VisualToolsController>(*m_video, *m_settings, [this] { return targetSession(); });
    m_visualTools->setEdited([this] {
        m_editor->reloadFromSession();
        refreshViews();
    });
    // F1: find and replace. Its options (FIND_REPLACE_OPTIONS,
    // FIND_REPLACE_STYLES) are read from the registry when the tool shows a
    // tab; its recent lists when the tool is first opened (openFindReplace).
    m_findHost = std::make_unique<FindHost>(*this);
    createFindReplace();
    if (!m_settingsFile.isEmpty())
        m_replaceBackup = QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/ReplaceBackup");
    // S4: validation errors go to the log window; the script editor setting.
    m_automation->setLog([this](const QString &line) { m_log->log(line); });
    m_automation->setSettings(m_settings.get());
    m_automationHotkeys = std::make_unique<AutomationHotkeysController>(*m_automation, *m_settings);
    // O2: legacy LoadHkeys at startup, then SetAccels.
    m_hotkeys = std::make_unique<HotkeysController>(*m_automationHotkeys, *m_settings,
                                                    [this](const QString &message) { m_log->log(message); });
    m_updates = std::make_unique<UpdateChecker>(*m_settings, options.updateFeed, QStringLiteral(HIKARI_VERSION));
    {
        const QString catalogDir = !options.catalogDir.isEmpty() ? options.catalogDir
                                   : !m_settingsFile.isEmpty()   ? QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/Catalog")
                                                                 : QDir::temp().filePath(QStringLiteral("hikari-catalogs-%1").arg(QCoreApplication::applicationPid()));
        StyleManagerController::Hooks hooks;
        hooks.target = [this] { return targetSession(); };
        hooks.documents = [this] {
            std::vector<application::EditSession *> out;
            for (const auto id : m_workspace.documents())
                if (auto *session = m_files->session(id))
                    out.push_back(session);
            return out;
        };
        hooks.refresh = [this] {
            m_editor->reloadFromSession();
            refreshViews();
        };
        m_styleManager = std::make_unique<StyleManagerController>(std::filesystem::path(catalogDir.toStdU16String()), std::move(hooks));
    }
    {
        // Y8: the font collector reads every tab's Document (a copy for the
        // run) and goes to a Line or Style from its log.
        FontCollectorController::Hooks hooks;
        hooks.tabs = [this] {
            std::vector<FontCollectorController::Tab> out;
            for (const auto id : m_workspace.tabs()) {
                FontCollectorController::Tab tab;
                if (auto *session = m_files->session(id))
                    tab.document = std::make_shared<core::Document>(session->document());
                if (const auto destination = m_files->destination(id))
                    tab.path = QString::fromStdString(destination->value);
                out.push_back(std::move(tab));
            }
            return out;
        };
        hooks.currentTab = [this] { return currentTab(); };
        const auto goTo = [this](int tab, const std::function<bool(const core::LineRecord &, int)> &match) {
            selectTab(tab);
            if (auto *session = targetSession()) {
                const auto lines = session->document().lines();
                for (int i = 0; i < int(lines.size()); ++i)
                    if (match(*lines[std::size_t(i)], i)) {
                        selectLine(lines[std::size_t(i)]->id.value);
                        return;
                    }
            }
        };
        hooks.goToLine = [goTo](int tab, int line) { goTo(tab, [line](const core::LineRecord &, int i) { return i == line; }); };
        hooks.goToStyle = [goTo](int tab, const QString &style) {
            const std::u16string name = style.toStdU16String();
            goTo(tab, [name](const core::LineRecord &l, int) { return core::toUtf16(l.style) == name; });
        };
        m_fontCollector = std::make_unique<FontCollectorController>(*m_settings, std::move(hooks));
    }
    // P3: this session's lock marks it as running; bundles of sessions whose
    // lock is gone or stale were left by a crash.
    m_recoveryDir = options.recoveryDir;
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const int capacity = m_settings->integer("autosave.maxFiles"); // legacy AUTOSAVE_MAX_FILES
    if (!m_recoveryDir.isEmpty()) {
        QDir().mkpath(m_recoveryDir + QStringLiteral("/sessions"));
        m_sessionLock = std::make_unique<QLockFile>(m_recoveryDir + QStringLiteral("/sessions/") + m_sessionId + QStringLiteral(".lock"));
        m_sessionLock->tryLock(0);
    }
    m_recovery = std::make_unique<application::RecoveryStore>(
        m_recoveryDir.isEmpty() ? std::filesystem::path() : std::filesystem::path(m_recoveryDir.toStdU16String()),
        m_sessionId.toStdString(), m_recoveryDir.isEmpty() ? 0 : capacity);
    m_recovery->prune(std::chrono::system_clock::now());
    // F3: spelling options, the Dictionary folder and the Grid's marks.
    // GetRightCase, IsAllUpperCase and Ignore All's comparison: the case
    // functions of the interface language's C locale (legacyCaseMapping).
    m_spellingText = backends::legacySpellingText(true);
    m_dictionaryDir = options.dictionaryDir;
    if (m_dictionaryDir.isEmpty() && !m_settingsFile.isEmpty())
        m_dictionaryDir = QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/Dictionary");
    m_bundledDictionaryDir = options.bundledDictionaryDir;
    // The Line editor's own switch (legacy TextEditor::SpellCheckerOnOff, from
    // SPELLCHECKER_ON when the editor is built); the Grid and the menus read
    // the option itself.
    m_spellingOn = m_settings->boolean("editor.spellchecker");
    // The user's Dictionary folder first (UserDic.udic lives there), then the
    // bundled one beside the executable (legacy's only folder).
    if (!m_dictionaryDir.isEmpty() && options.spellingBackend) {
        std::vector<std::filesystem::path> folders{std::filesystem::path(m_dictionaryDir.toStdU16String())};
        if (!m_bundledDictionaryDir.isEmpty() && m_bundledDictionaryDir != m_dictionaryDir)
            folders.emplace_back(m_bundledDictionaryDir.toStdU16String());
        m_spellChecker = std::make_unique<application::SpellChecker>(std::move(folders), options.spellingBackend);
    }
    m_shell->setSpelling([this](std::u16string_view text, core::SubtitleFormat format, bool spell) {
        application::SpellChecker *checker = m_spellingStarted ? m_spellChecker.get() : nullptr;
        return core::legacy::checkTextAndBrackets(text, format, m_spellingText.segment,
                                                  spell && m_settings->boolean("editor.spellchecker") && checker ? checker->wordCheck()
                                                                                   : core::legacy::WordCheck{});
    });
    // GRID_HIDE_COLUMNS (G7).
    m_shell->setHiddenColumns(m_settings->integer("grid.hideColumns"));
    connect(m_shell.get(), &ui::ShellController::hiddenColumnsChanged, this,
            [this] { m_settings->set("grid.hideColumns", m_shell->hiddenColumns()); });
    m_recent.set(m_settings->settings().list("recent.subtitles"));
    m_copyColumns = m_settings->integer("grid.copyColumns");
    m_pasteColumns = m_settings->integer("grid.pasteColumns");
    connect(m_editor.get(), &ui::LineEditorController::changed, this, [this] {
        refreshVideo();
        scheduleAutosave();
    });
    // A video that cannot be opened is reported in the log window once.
    connect(m_video.get(), &ui::VideoController::changed, this, [this] {
        const bool failed = m_video->session().state() == application::VideoSession::State::Failed;
        if (failed && !m_videoFailureLogged)
            m_log->log(m_video->status());
        m_videoFailureLogged = failed;
    });
    // Y4: a newly shown video is compared with the editing target's resolution once.
    connect(m_video.get(), &ui::VideoController::changed, this, [this] {
        const auto &video = m_video->session();
        if (video.state() != application::VideoSession::State::Ready || !video.lastFrame())
            return;
        const QString path = QString::fromStdString(video.path());
        if (path == m_resolutionCheckedVideo)
            return;
        m_resolutionCheckedVideo = path;
        checkResolution();
    });
    // A keyframe file opened before the video applies once a video is ready.
    connect(m_video.get(), &ui::VideoController::changed, this, [this] {
        if (m_pendingKeyframes.isEmpty() || m_video->session().state() != application::VideoSession::State::Ready)
            return;
        const QString path = std::exchange(m_pendingKeyframes, QString());
        const QString problem = openKeyframes(QUrl::fromLocalFile(path));
        if (!problem.isEmpty())
            m_log->log(problem);
    });
    // The editor moved the active Line itself (Enter, Ctrl+D, Undo): a plain selection there.
    connect(m_editor.get(), &ui::LineEditorController::lineChanged, this, [this](qulonglong id) {
        const auto target = m_workspace.editingTarget();
        auto *session = target ? m_files->session(*target) : nullptr;
        if (!session)
            return;
        session->setSelection(gridSelection().plain(session->selection(), core::LineId{id}));
        m_shell->setSelection(session->selection());
    });
    // The editor's Start/End difference measures from the frame the Video panel shows.
    m_editor->setVideoTimeSource([this]() -> std::optional<std::int64_t> {
        const auto &session = m_video->session();
        const auto frame = session.shownFrame();
        const auto start = frame ? session.frameStart(*frame) : std::nullopt;
        if (!start)
            return std::nullopt;
        return start->microseconds() / 1000;
    });
    // A1: the audio box reads its audio through a media helper of its own, so
    // caching never waits behind the video's frames; legacy's index files let
    // it open the video's audio without indexing it again.
    m_audioSource = std::make_unique<backends::FfmsIndexedSource>(mediaHelperPath(options.mediaHelper));
    m_audio = std::make_unique<ui::AudioController>(*m_audioSource);
    m_automation->setAudioBox([this]() -> const application::AudioBox * { return m_audio ? &m_audio->box() : nullptr; });
    {
        const QString cacheDir = !options.audioCacheDir.isEmpty() ? options.audioCacheDir
                                 : !m_settingsFile.isEmpty()
                                     ? QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/AudioCache")
                                     : QDir::temp().filePath(QStringLiteral("hikari-audio-cache-%1").arg(QCoreApplication::applicationPid()));
        // legacy Options.pathfull/Indices; without a settings folder (tests) none
        m_indexDir = !options.indexDir.isEmpty() ? options.indexDir
                     : !m_settingsFile.isEmpty() ? QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/Indices")
                                                 : QString();
        m_audioSettings = [this, cacheDir] {
            // Legacy AUDIO_RAM_CACHE, AUDIO_DELAY, AUDIO_CACHE_FILES_LIMIT and
            // ACCEPTED_AUDIO_STREAM, read from the registry when the box uses
            // them (as the legacy provider reads Options), so an Options
            // dialog change reaches the next opening.
            application::AudioCacheSettings settings;
            settings.cacheDir = std::filesystem::path(cacheDir.toStdU16String());
            if (!m_indexDir.isEmpty())
                settings.indexDir = std::filesystem::path(m_indexDir.toStdU16String());
            settings.ram = m_settings->boolean("audio.ramCache");
            settings.delayMs = m_settings->integer("audio.delay");
            settings.cacheFilesLimit = m_settings->integer("audio.cacheFilesLimit");
            settings.acceptedStreams = application::legacyAcceptedStreams(m_settings->settings().text("video.acceptedAudioStream"));
            return settings;
        };
        m_audio->setSettings(m_audioSettings);
    }
    // A2: the box's zoom, volume, link, auto-scroll and spectrum switches
    // start from the registry and are written back as they change.
    m_audio->setSettingsStore(m_settings.get());
    // A4: the box's player through the editor output; PortAudio starts at
    // the first play. Without playbackAudio (tests) an output with no device
    // runs the same clock, so nothing sounds through a desktop's speakers.
    {
        // A4-wasapi-default: on Windows the output's host API is
        // audio.outputHostApi's (WASAPI, or DirectSound); the player asks for
        // the output when it next opens one, so a changed choice takes effect
        // then (reopenOutput: at once while idle, else after playback stops).
        const bool device = options.playbackAudio;
        auto make = options.makeAudioOutput;
        const bool windowsApis = options.outputHostApiSetting;
        m_audioPlayer = std::make_unique<backends::AudioBoxPlayer>(
            [this, device, make, windowsApis]() -> application::AudioOutputPort & {
                backends::PortAudioOutput::Options output;
                output.hostApi = backends::PortAudioOutput::hostApiForSetting(m_settings->integer("audio.outputHostApi"),
                                                                              windowsApis);
                if (!m_audioOutput || output.hostApi != m_audioOutputHostApi) {
                    m_audioOutput.reset(); // the player let it go (closed)
                    if (make)
                        m_audioOutput = make(output);
                    else if (device)
                        m_audioOutput = std::make_unique<backends::PortAudioOutput>(output);
                    else
                        m_audioOutput = std::make_unique<backends::SimulatedOutput>();
                    m_audioOutputHostApi = output.hostApi;
                }
                return *m_audioOutput;
            },
            backends::AudioBoxPlayer::SharedAudio([this]() -> std::shared_ptr<const application::DisplayAudio> {
                return m_audio ? m_audio->box().sharedAudio() : nullptr;
            }));
        m_audio->setPlayer(m_audioPlayer.get());
        // AUDIO_MARK_PLAY_TIME, read at each play (the volume is the
        // slider's: AUDIO_VOLUME when the audio is set, then each move)
        m_audio->setPlaybackSettings([this] { return m_settings->integer("audio.markPlayTime"); });
        m_audio->setVideoPlayback([this] { return m_video->playing(); }, [this] { m_video->pause(); });
        m_audioConnections << connect(m_audioPlayer.get(), &backends::AudioBoxPlayer::failed, this,
                                      [this](const QString &message) { m_log->log(message); });
    }
    m_recentAudio.set(m_settings->settings().list("recent.audio")); // AUDIO_RECENT_FILES
    m_audioConnections << connect(m_audio.get(), &ui::AudioController::opened, this,
                                  [this](const QString &path, bool) { rememberRecentAudio(path); });
    m_audioConnections << connect(m_audio.get(), &ui::AudioController::logged, this, [this](const QString &message, bool debug) {
        if (debug)
            qDebug().noquote() << message; // legacy HikariLogDebug: debug builds only
        else
            m_log->log(message);
    });
    m_audioConnections << connect(m_editor.get(), &ui::LineEditorController::changed, this, &Application::refreshAudio);
    m_audioConnections << connect(m_video.get(), &ui::VideoController::changed, this, &Application::followVideoInAudio);
    m_audioConnections << connect(m_audio.get(), &ui::AudioController::changed, this, &Application::releaseReadIndexHandoff);
    // A3: the box's timing options from the registry, read when used (legacy
    // Options), and what legacy's AudioDisplay did through the edit box, the
    // grid and the video. Playback (A4) is the box's own.
    m_audio->setTimingSettings([this] {
        application::AudioTimingOptions timing;
        timing.autoCommit = m_settings->boolean("audio.autoCommit");
        timing.snapToKeyframes = m_settings->boolean("audio.snapToKeyframes");
        timing.snapToOtherLines = m_settings->boolean("audio.snapToOtherLines");
        timing.startDragSensitivity = m_settings->integer("audio.startDragSensitivity");
        timing.leadIn = m_settings->integer("audio.leadInValue");
        timing.leadOut = m_settings->integer("audio.leadOutValue");
        timing.dontPlayWhenLineChanges = m_settings->boolean("audio.dontPlayWhenLineChanges");
        return timing;
    });
    {
        ui::AudioController::TimingHooks hooks;
        hooks.commit = [this](const application::AudioCommitRequest &request) { commitAudioTimes(request); };
        hooks.setActive = [this](int key) { setAudioActive(key); };
        hooks.seekVideo = [this](int ms) { seekVideoFromAudio(ms); };
        m_audio->setTimingHooks(std::move(hooks));
    }
    // Legacy RendererFFMS2::OpenFile: a file with audio and no video goes to
    // the audio box (as audio from video: SetRecent, the box's own provider),
    // and the open video stays. Otherwise legacy ProviderFFMS2::Init chose the
    // audio track before indexing (ACCEPTED_AUDIO_STREAM or "Choose the
    // track"); a cancelled choice failed the open and the open video stayed
    // ("safe mode"). The video is then indexed with that track alone and its
    // index file, Indices/<name>_<track>.ffindex.
    m_video->setOpenFilter([this](const QString &path, std::function<void(application::IndexRequest)> asVideo) {
        QPointer<Application> self(this);
        m_mediaSource->probe(path.toStdString(), [self, path, asVideo = std::move(asVideo)](
                                                     std::expected<application::MediaProbe, application::AudioFailure> probe) {
            if (!self)
                return;
            if (!probe)
                return asVideo({}); // the video's open reports why the file cannot be read
            if (!probe->hasVideo && !probe->audio.empty())
                return self->m_audio->openAudio(path);
            const auto choice = application::legacyAudioTrackChoice(probe->audio, self->m_audioSettings().acceptedStreams);
            auto openWith = [self, path, asVideo](int track) {
                if (self)
                    asVideo({track, self->indexFile(path, track).toStdString()});
            };
            if (choice.track || choice.rows.empty())
                return openWith(choice.track.value_or(-1));
            self->m_audio->askTrack(choice.rows, [openWith, tracks = choice.rowTracks](std::optional<int> row) {
                if (row && *row >= 0 && *row < static_cast<int>(tracks.size()))
                    openWith(tracks[static_cast<std::size_t>(*row)]);
            });
        });
    });
    trackTabMedia(); // P6
    refreshViews();
}

// A1: legacy AudioDisplay::SetDialogue runs when the active Line changes or
// the editor commits (EditBox::SetLine, Send); every redraw shades the other
// Lines. The controller redraws only when what it shows changed.
void Application::refreshAudio()
{
    if (!m_audio)
        return; // views refreshed while the application is still being composed
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    std::vector<application::AudioLineSpan> lines;
    int active = -1;
    std::optional<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>> key;
    if (session) {
        const auto shown = shownLines();
        const auto activeId = session->selection().active;
        lines.reserve(session->document().lines().size());
        for (const auto *line : session->document().lines()) {
            if (activeId && line->id == *activeId) {
                active = static_cast<int>(lines.size());
                // A5: the Line karaoke splits (legacy's edit box Line: TextTl when set, else Text)
                m_audio->setActiveText(core::toUtf16(line->translation.empty() ? line->text : line->translation));
            }
            lines.push_back({static_cast<int>(line->start.value.microseconds() / 1000),
                             static_cast<int>(line->end.value.microseconds() / 1000), shown(line->id)});
        }
        if (activeId)
            key = std::tuple(target->value, activeId->value, session->revision());
    }
    bool select = key && key != m_audioLine;
    // A3: a box commit on the same Line runs no SetDialogue (legacy CommitChanges)
    if (select && m_audioCommitting && m_audioLine && std::get<0>(*key) == std::get<0>(*m_audioLine) &&
        std::get<1>(*key) == std::get<1>(*m_audioLine))
        select = false;
    m_audioLine = key;
    m_audio->setLines(std::move(lines), active, select);
}

// A1: the video's marks in the box and legacy RendererFFMS2::OpenFile's
// changeAudio. The keyframes are worked out again only when the video or its
// keyframe list changes; the paused frame's time is a lookup, and nothing is
// marked while playing or stopped (legacy draws it only while Paused).
void Application::followVideoInAudio()
{
    const auto &video = m_video->session();
    const auto state = video.state();
    if (state == application::VideoSession::State::Opening) {
        m_audioFollowedVideo.clear();
        m_pendingIndexHandoff.clear(); // the source's new open removed it
    }
    if (state == application::VideoSession::State::Ready && m_audioFollowedVideo != QString::fromStdString(video.path())) {
        // an indexed video brings its track into the box, opened from the
        // index file the video wrote; a video without audio closes the box
        m_audioFollowedVideo = QString::fromStdString(video.path());
        if (keepTabAudio(m_audioFollowedVideo)) {
            // P6: a restored tab's own audio file stays (legacy dontLoadAudio)
        } else if (video.hasAudio()) {
            m_audio->openFromVideo(m_audioFollowedVideo, video.audioTrack(), video.newIndex(),
                                   QString::fromStdString(video.indexHandoff()));
            m_pendingIndexHandoff = video.indexHandoff();
            releaseReadIndexHandoff();
        } else if (m_audio->hasAudio())
            m_audio->closeAudio();
        m_audio->trimCache(); // legacy OpenFile, once the video's provider exists
    }
    if (state != application::VideoSession::State::Ready) {
        if (m_audioTimebase) {
            m_audioTimebase.reset();
            m_audioKeyframes.clear();
            m_audio->setKeyframes({});
            m_audio->setKeyframeSnapTimes({}); // A3
        }
        m_audio->setVideoTime(std::nullopt);
        return;
    }
    if (!m_audioTimebase || m_audioTimebasePath != video.path() || m_audioKeyframes != video.keyframes()) {
        m_audioTimebasePath = video.path();
        m_audioKeyframes = video.keyframes();
        m_audioTimebase = video.legacyTimebase();
        std::vector<int> ms;
        ms.reserve(m_audioKeyframes.size());
        for (const int frame : m_audioKeyframes)
            ms.push_back(m_audioTimebase->msAt(frame));
        // A3: GetBoundarySnap's keyframe times, StartTimeFor(FrameAt(keyframe))
        std::vector<int> snap;
        snap.reserve(ms.size());
        for (const int keyMs : ms)
            snap.push_back(m_audioTimebase->startTimeFor(m_audioTimebase->frameAt(keyMs)));
        m_audio->setKeyframeSnapTimes(std::move(snap));
        m_audio->setKeyframes(std::move(ms));
    }
    std::optional<int> paused; // legacy VideoBox::Tell while Paused
    if (!video.playing() && !video.stopped() && video.shownFrame())
        paused = m_audioTimebase->msAt(*video.shownFrame());
    m_audio->setVideoTime(paused);
}

void Application::rememberRecentAudio(const QString &path)
{
    m_recentAudio.add(path.toStdString());
    m_settings->settings().set("recent.audio", m_recentAudio.entries()); // SetRecent's SetTable
}

// A3: legacy AudioDisplay::CommitChanges' edit-box part.
void Application::commitAudioTimes(const application::AudioCommitRequest &request)
{
    auto *session = targetSession();
    if (!session || !m_editor->editable())
        return;
    const bool before = std::exchange(m_audioCommitting, true);
    const auto outcome = application::commitAudioTimes(*session, request, shownLines());
    m_editor->reloadFromSession();
    if (outcome && outcome->stepped)
        refreshViews();
    m_audioCommitting = before;
    // SubsGrid::NextLine: the next Line, selected alone
    if (outcome && outcome->next)
        applySelection(gridSelection().plain(session->selection(), *outcome->next));
}

// A3: legacy SubsGrid::SetActive from the box (AUDIO_NEXT / AUDIO_PREVIOUS).
void Application::setAudioActive(int key)
{
    auto *session = targetSession();
    if (!session || key < 0)
        return;
    const auto lines = session->document().lines();
    if (key >= static_cast<int>(lines.size()))
        return;
    applySelection(gridSelection().plain(session->selection(), lines[static_cast<std::size_t>(key)]->id));
}

// A3: legacy tab->video->Seek(time) (Ctrl+left or middle click in the box).
void Application::seekVideoFromAudio(int ms)
{
    auto &video = m_video->session();
    if (video.state() != application::VideoSession::State::Ready)
        return;
    if (ms <= 0)
        video.showFrame(0);
    else
        video.seekTo(core::DocumentTime(static_cast<std::int64_t>(ms) * 1000));
}

void Application::setAudioFromVideo(bool mark)
{
    if (!m_audio->hasAudio())
        return;
    // VideoBox::Tell: the shown frame's time, 0 without video
    int time = 0;
    const auto &video = m_video->session();
    if (video.state() == application::VideoSession::State::Ready)
        if (const auto frame = video.shownFrame())
            time = video.legacyTimebase().msAt(*frame);
    m_audio->showTime(time, mark);
}

// Legacy OpenAudioInTab with no path: the video's file (a provider of its own).
void Application::openAudioFromVideo()
{
    const auto &video = m_video->session();
    if (video.state() == application::VideoSession::State::Ready)
        m_audio->openAudio(QString::fromStdString(video.path()));
}

QVariantList Application::recentAudio()
{
    const bool pruned = m_recentAudio.prune([](const std::string &path) {
        return application::isMissingLocalFile(
            path, [](const std::string &p) { return QFileInfo(QString::fromStdString(p)).isFile(); },
            [](char drive) {
#ifdef _WIN32
                const wchar_t root[] = {static_cast<wchar_t>(drive), L':', L'\\', 0};
                return ::GetDriveTypeW(root) == DRIVE_REMOTE;
#else
                (void)drive;
                return false;
#endif
            });
    });
    if (pruned)
        m_settings->settings().set("recent.audio", m_recentAudio.entries());
    QVariantList rows;
    int n = 0;
    for (const auto &entry : m_recentAudio.entries()) {
        const QString path = QString::fromStdString(entry);
        rows << QVariantMap{{QStringLiteral("path"), path},
                            {QStringLiteral("label"), QStringLiteral("%1 %2").arg(++n).arg(QFileInfo(path).fileName())}};
    }
    return rows;
}

QUrl Application::audioDialogFolder() const
{
    // legacy OpenAudioInTab: the video's folder, else the latest recent
    // video's (not yet: the rewrite keeps no recent video list)
    const QString from = QString::fromStdString(m_video->session().path());
    if (from.isEmpty())
        return {};
    return QUrl::fromLocalFile(QFileInfo(from).absolutePath());
}

Application::~Application()
{
    // P6: an orderly end writes the session with "[Close session]" (legacy
    // HikariSubFrame::OnClose); only a crash leaves it without.
    endSession();
    // A1: the audio box goes first, before the video and helpers it reads
    // through; its signal connections go with it.
    for (const auto &connection : std::as_const(m_audioConnections))
        disconnect(connection);
    m_video->setOpenFilter(nullptr);
    m_audio.reset();
    m_audioPlayer.reset(); // A4: the player stops before its output goes
    m_audioOutput.reset();
    m_audioSource.reset();
    m_port->waitIdle(); // no write may outlive the services it reports to
    saveMisspellRules();
}

// F4: MisspellReplacer's destructor saves a non-empty rules list (SaveRules,
// UTF-8 with a BOM as OpenWrite::FileWrite writes it).
void Application::saveMisspellRules()
{
    if (m_misspellRules && !m_misspellRules->empty() && !m_settingsFile.isEmpty()) {
        QFile file(QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/Rules.txt"));
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const auto text = application::writeReplacerRules(*m_misspellRules);
            file.write("\xEF\xBB\xBF");
            file.write(reinterpret_cast<const char *>(text.data()), static_cast<qint64>(text.size()));
        }
    }
}

namespace {

// Legacy SubsGrid::LoadSubtitles: the ASS "Active Line" (or the first Line)
// is active, selected and the anchor, and the editor shows it.
void selectLegacyActiveLine(application::EditSession &session)
{
    const auto lines = session.document().lines();
    if (lines.empty())
        return;
    std::size_t active = 0;
    if (session.document().format() == core::SubtitleFormat::Ass) {
        const auto value = session.document().scriptInfo(u8"Active Line").value_or(std::u8string());
        const std::string text(value.begin(), value.end());
        const long n = std::strtol(text.c_str(), nullptr, 10); // wxAtoi
        if (n > 0 && static_cast<std::size_t>(n) < lines.size())
            active = static_cast<std::size_t>(n);
    }
    const auto line = lines[active]->id;
    session.setSelection(application::Selection{line, {line}, line, {}});
}

} // namespace

std::optional<application::DocumentId> Application::open(const QString &path, bool asReference)
{
    auto staged = m_files->stageOpen({QFileInfo(path).absoluteFilePath().toStdString()});
    if (!staged)
        return std::nullopt;
    return publish(std::move(*staged), path, asReference);
}

std::optional<application::DocumentId> Application::publish(application::StagedOpen staged, const QString &path,
                                                            bool asReference)
{
    auto id = m_files->activate(std::move(staged));
    if (!id)
        return std::nullopt;
    if (auto *session = m_files->session(*id); session && !session->selection().active)
        selectLegacyActiveLine(*session);
    // GRID_FILTER_AFTER_LOAD (legacy LoadSubtitles, ASS only, never by selection alone).
    if (auto *session = m_files->session(*id);
        session && !asReference && session->document().format() == core::SubtitleFormat::Ass && m_gridFilter->afterLoad() &&
        m_gridFilter->filterBy() != 0 && m_gridFilter->filterBy() != application::filter_by::Selections) {
        application::FilterSettings settings{m_gridFilter->filterBy(), {}, m_gridFilter->inverted(),
                                             m_gridFilter->addToFilter()};
        for (const QString &style : m_gridFilter->styles())
            settings.styles.push_back(toU8(style));
        (void)application::filterLines(*session, settings, {}, true);
    }
    m_workspace.add(*id, QFileInfo(path).fileName().toStdString(), asReference);
    recordFileTime(*id);
    if (!asReference)
        rememberRecent(QFileInfo(path).absoluteFilePath().toStdString());
    return *id;
}

bool Application::openFile(const QString &path)
{
    // P6: an untouched empty tab (legacy's new tab, or the one left after
    // closing the last) takes the file, as legacy OpenFile loaded into it.
    const auto previous = m_workspace.editingTarget();
    auto *previousSession = previous ? m_files->session(*previous) : nullptr;
    const bool emptyTab = previousSession && targetUntitled() && !previousSession->isDirty() &&
                          previousSession->historySize() == 1;
    const auto id = open(path);
    if (!id)
        return false;
    if (emptyTab)
        replaceTarget(*id);
    refreshViews();
    checkResolution();
    trimAudioCache();
    saveLastSession(); // P6: legacy OpenFile ends with SaveLastSession
    return true;
}

// A1: the video's index handed over in a temporary file is needed only while
// the box opens from it; once the box has opened (or stopped opening) the
// file goes. The video's source removes it anyway when it opens again or ends.
void Application::releaseReadIndexHandoff()
{
    if (m_pendingIndexHandoff.empty() || (m_audio->hasAudio() && !m_audio->loaded()))
        return; // none, or the box is still opening
    m_mediaSource->releaseIndexHandoff(std::exchange(m_pendingIndexHandoff, std::string()));
}

// A1: legacy OpenFile, OpenFiles and LoadLastSession end with the tab's
// VideoBox::DeleteAudioCache, which acts only with a video open.
void Application::trimAudioCache()
{
    if (m_video->session().state() == application::VideoSession::State::Ready)
        m_audio->trimCache();
}

QString Application::indexFile(const QString &path, int track) const
{
    if (m_indexDir.isEmpty())
        return {};
    return QDir(m_indexDir).filePath(QString::fromStdString(application::legacyIndexName(path.toStdString(), track)));
}

bool Application::openReference(const QString &path)
{
    if (!open(path, true))
        return false;
    refreshViews();
    return true;
}

bool Application::closeEditingTarget()
{
    const auto target = m_workspace.editingTarget();
    if (!target)
        return false;
    closeDocument(*target); // P6: legacy DeletePage (also writes the session)
    return true;
}

void Application::reportGroupBreak()
{
    auto *session = targetSession();
    if (!session || session->groupBreakCount() == m_seenGroupBreaks)
        return;
    m_seenGroupBreaks = session->groupBreakCount();
    // The group the first broken member belongs to now (the refused command changed nothing).
    qulonglong description = 0;
    const auto owners = core::groupOwners(session->document());
    for (const auto id : session->lastGroupBreak())
        if (const auto it = owners.find(id.value); it != owners.end() && it->second) {
            description = it->second->value;
            break;
        }
    emit groupBreakRefused(description, description ? groupTitle(description) : QString());
}

void Application::refreshViews()
{
    // F3: legacy creates the spell checker when the Grid first paints a Line.
    if (m_workspace.editingTarget())
        spellChecker();
    reportGroupBreak();
    scheduleAutosave();
    const auto target = m_workspace.editingTarget();
    const auto reference = m_workspace.reference();
    auto *targetSession = target ? m_files->session(*target) : nullptr;
    auto *referenceSession = reference ? m_files->session(*reference) : nullptr;
    refreshComparison(); // R1: an edited compared Document is compared again
    m_shell->refresh(targetSession ? &targetSession->document() : nullptr,
                     referenceSession ? &referenceSession->document() : nullptr,
                     target ? m_comparison.table(*target) : nullptr,
                     reference ? m_comparison.table(*reference) : nullptr);
    if (targetSession)
        m_shell->setSelection(targetSession->selection());
    // The editor only ever edits the editing target, never the reference.
    if (target != m_editorDocument) {
        m_editorDocument = target;
        m_editor->setDocument(target, target && m_workspace.checkContentCommand(*target).has_value());
        // F1: legacy OnPageChanged calls FR->Reset(): the next search starts over.
        if (m_find)
            m_find->reset();
    }
    refreshVideo();
    refreshAudio();
    emit tabsChanged(); // P6: titles, modified marks and the active tab
}

void Application::refreshVideo()
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (target != m_videoDocument) {
        const auto previous = m_videoDocument;
        leaveTabMedia(previous); // P6: the tab shown so far keeps its video position
        m_videoDocument = target;
        m_videoRevision.reset();
        m_videoLine.reset();
        application::MediaAssociations associations;
        const auto destination = target ? m_files->destination(*target) : std::nullopt;
        if (session && destination) {
#ifdef _WIN32
            constexpr bool windows = true;
#else
            constexpr bool windows = false;
#endif
            associations = application::resolveMediaAssociations(
                session->document(), destination->value,
                [](const std::string &p) { return QFileInfo(QString::fromStdString(p)).isFile(); }, windows);
        }
        m_video->offer(associations);
        enterTabMedia(previous, target); // P6: the tab's own video, audio and keyframes
    }
    m_visualTools->refresh(); // T1: the script resolution, the format and the active Line
    if (!session)
        return;
    if (session->revision() != m_videoRevision) {
        m_videoRevision = session->revision();
        m_video->session().setSubtitles(core::encodeAss(session->document()));
    }
    const auto active = session->selection().active;
    // V2: the times field and the go-to commands follow the active Line.
    std::optional<std::pair<core::DocumentTime, core::DocumentTime>> lineTimes;
    if (active)
        for (const auto *line : session->document().lines())
            if (line->id == *active)
                lineTimes = std::pair(line->start.value, line->end.value);
    m_video->setActiveLineTimes(lineTimes);
    if (active && active != m_videoLine) {
        m_videoLine = active;
        for (const auto *line : session->document().lines())
            if (line->id == *active)
                m_video->session().seekTo(line->start.value);
    }
}

namespace {

QString fileTitle(const std::string &path)
{
    return QFileInfo(QString::fromStdString(path)).fileName();
}

} // namespace

void Application::newDocument()
{
    const auto id = m_files->createNew();
    m_workspace.add(id, tr("Untitled").toStdString());
    m_workspace.setEditingTarget(id);
    refreshViews();
}

void Application::endFindOpen(bool opened)
{
    if (!m_findOpenDone)
        return;
    auto done = std::move(m_findOpenDone);
    m_findOpenDone = nullptr;
    done(opened);
    emit findBusyChanged();
}

QVariantList Application::reviewClose(const QString &then)
{
    endFindOpen(false); // another review replaces the file result's
    m_closeThen = then;
    m_closing.clear();
    if (then != QLatin1String("open")) {
        m_pendingOpen.reset();
        m_pendingOpenPath.clear();
    }
    std::vector<application::DocumentId> scope;
    if (then != QLatin1String("tab"))
        m_closingTab.reset(); // P6
    if (then != QLatin1String("session"))
        m_pendingSession.reset(); // P6
    if (then == QLatin1String("quit") || then == QLatin1String("session")) // P6: a session replaces every tab
        scope = m_workspace.documents();
    else if (then == QLatin1String("tab")) // P6: a middle-clicked tab
        scope = m_closingTab ? std::vector{*m_closingTab} : std::vector<application::DocumentId>{};
    else if (const auto target = m_workspace.editingTarget())
        scope = {*target};
    QVariantList rows;
    for (const auto id : scope) {
        auto *session = m_files->session(id);
        if (!session || !session->isDirty())
            continue;
        const std::string *title = m_workspace.title(id);
        const auto destination = m_files->destination(id);
        rows << QVariantMap{{QStringLiteral("id"), QVariant::fromValue<qulonglong>(id.value)},
                            {QStringLiteral("title"), title ? QString::fromStdString(*title) : QString()},
                            {QStringLiteral("untitled"), !destination || destination->value.empty()}};
    }
    return rows;
}

void Application::resolveClose(const QVariantList &choices)
{
    m_closing.clear();
    for (const QVariant &v : choices) {
        const QVariantMap row = v.toMap();
        const application::DocumentId id{row.value(QStringLiteral("id")).toULongLong()};
        auto *session = m_files->session(id);
        if (!session)
            continue;
        Closing c{id, row.value(QStringLiteral("save")).toBool(), 0, std::nullopt};
        if (c.save) {
            const QString path = row.value(QStringLiteral("path")).toString();
            auto plan = m_files->prepareSave(
                id, path.isEmpty() ? std::nullopt
                                   : std::optional(application::DestinationKey{QFileInfo(path).absoluteFilePath().toStdString()}));
            if (!plan) {
                m_closing.clear();
                emit closeFinished(false, plan.error() == application::SaveRefusal::NoDestination
                                              ? tr("Choose where to save the Untitled Document.")
                                              : tr("A Document could not be saved; nothing was closed."));
                return;
            }
            c.revision = plan->revision;
            auto permit = m_files->startSave(std::move(*plan));
            if (!permit) {
                m_closing.clear();
                emit closeFinished(false, tr("A Document could not be saved; nothing was closed."));
                return;
            }
            c.permit = *permit;
        } else {
            session->discardDraft(); // Discard covers the draft; no automatic commit
            c.revision = session->revision();
        }
        m_closing.push_back(c);
    }
    refreshViews();
    // Without saves to wait for, finish now; otherwise when the last one reports.
    if (std::none_of(m_closing.begin(), m_closing.end(), [](const Closing &c) { return c.permit.has_value(); }))
        finishClose();
}

void Application::writeFinished(const application::WriteResult &result)
{
    // Save As of an Untitled or renamed Document: the title follows the file.
    if (result.outcome == application::WriteOutcome::Written) {
        rememberRecent(result.destination.value); // legacy SetRecent after a save
        if (auto *session = m_files->session(result.document); session && !session->isDirty())
            discardRecovery(result.document); // the work is in the file
        if (const auto destination = m_files->destination(result.document);
            destination && destination->value == result.destination.value)
            recordFileTime(result.document);
    }
    if (result.outcome == application::WriteOutcome::Written)
        if (const std::string *title = m_workspace.title(result.document);
            title && QString::fromStdString(*title) != fileTitle(result.destination.value)) {
            const auto destination = m_files->destination(result.document);
            if (destination && destination->value == result.destination.value) {
                m_workspace.setTitle(result.document, fileTitle(result.destination.value).toStdString());
                refreshViews();
                saveLastSession(); // P6: legacy Save writes the session when the name changed
            }
        }
    if (m_closing.empty())
        return;
    const bool waiting = std::any_of(m_closing.begin(), m_closing.end(), [this](const Closing &c) {
        if (!c.permit)
            return false;
        const auto status = m_files->lastSave(c.document);
        return !status || status->permit.value != c.permit->value || !status->outcome;
    });
    if (!waiting)
        finishClose();
}

void Application::finishClose()
{
    QStringList kept;
    for (const auto &c : m_closing) {
        auto *session = m_files->session(c.document);
        if (!session)
            continue;
        bool ok = true;
        if (c.save) {
            // L58-write-close: only an acknowledged complete write authorizes the close.
            const auto status = m_files->lastSave(c.document);
            ok = status && status->outcome == application::WriteOutcome::Written && !session->isDirty();
        } else {
            // Work done after the Discard choice is not covered by it.
            ok = session->revision() == c.revision && !session->draftLine();
        }
        if (!ok) {
            const std::string *title = m_workspace.title(c.document);
            kept << (title ? QString::fromStdString(*title) : QString());
        }
    }
    const QString then = m_closeThen;
    m_closing.clear();
    m_closeThen.clear();
    if (!kept.isEmpty()) {
        refreshViews();
        emit closeFinished(false, tr("Not closed, because saving failed or there is newer work: %1").arg(kept.join(QStringLiteral(", "))));
        return;
    }
    if (then == QLatin1String("quit")) {
        for (const auto id : m_workspace.documents())
            discardRecovery(id); // every Document reviewed
        m_quitApproved = true;
        emit quitApprovedChanged();
    } else if (then == QLatin1String("new")) {
        // P6: the Untitled Document takes the tab's place (legacy same tab).
        const auto id = m_files->createNew();
        m_workspace.add(id, tr("Untitled").toStdString());
        replaceTarget(id);
    } else if (then == QLatin1String("open")) {
        std::optional<application::DocumentId> id;
        if (m_pendingOpen)
            id = publish(std::move(*m_pendingOpen), m_pendingOpenPath, false);
        if (id) {
            replaceTarget(*id); // P6: loaded into the same tab (legacy OpenFile)
            QTimer::singleShot(0, this, [this] { checkResolution(); });
        } else {
            closeEditingTarget();
        }
        m_pendingOpen.reset();
        m_pendingOpenPath.clear();
        refreshViews();
        saveLastSession(); // legacy OpenFile ends with SaveLastSession
    } else if (then == QLatin1String("session")) {
        applySession(); // P6
    } else if (then == QLatin1String("tab")) {
        if (const auto tab = std::exchange(m_closingTab, std::nullopt))
            closeDocument(*tab); // P6
    } else {
        closeEditingTarget();
    }
    // Before closeFinished: the review window closing on it counts as Cancel.
    endFindOpen(then == QLatin1String("open"));
    emit closeFinished(true, QString());
}

void Application::cancelClose()
{
    // Saves already completed stay completed (accepted quit review).
    m_closing.clear();
    m_closeThen.clear();
    m_pendingOpen.reset();
    m_pendingOpenPath.clear();
    m_pendingSession.reset(); // P6
    m_closingTab.reset();
    endFindOpen(false);
}

bool Application::targetUntitled() const
{
    const auto target = m_workspace.editingTarget();
    const auto destination = target ? m_files->destination(*target) : std::nullopt;
    return target && (!destination || destination->value.empty());
}

namespace {

// wxString::BeforeLast: empty when the character is missing.
QString beforeLast(const QString &s, QChar c)
{
    const auto at = s.lastIndexOf(c);
    return at < 0 ? QString() : s.left(at);
}

QString extensionFor(core::SubtitleFormat format)
{
    return format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::PlainText ? QStringLiteral("ass")
           : format == core::SubtitleFormat::Srt                                          ? QStringLiteral("srt")
                                                                                          : QStringLiteral("txt");
}

// Legacy checks the read-only attribute of an existing file.
bool readOnly(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() && !info.isWritable();
}

} // namespace

QString Application::saveRoute() const
{
    const auto target = m_workspace.editingTarget();
    if (!target)
        return {};
    const auto destination = m_files->destination(*target);
    const QString path = destination ? QString::fromStdString(destination->value) : QString();
    const QString video = m_video->session().state() == application::VideoSession::State::Ready
                              ? QString::fromStdString(m_video->session().path())
                              : QString();
    if (path.isEmpty() || m_formatChanged.contains(target->value) || (saveWithVideoName() && !video.isEmpty() &&
                           beforeLast(QFileInfo(path).fileName(), u'.') != beforeLast(QFileInfo(video).fileName(), u'.')))
        return QStringLiteral("dialog");
    return readOnly(path) ? QStringLiteral("readonly") : QString();
}

QVariantMap Application::saveDialogValues() const
{
    auto *session = targetSession();
    if (!session)
        return {};
    const auto target = m_workspace.editingTarget();
    const auto destination = m_files->destination(*target);
    const QString video = m_video->session().state() == application::VideoSession::State::Ready
                              ? QString::fromStdString(m_video->session().path())
                              : QString();
    const QString path = !video.isEmpty() && saveWithVideoName() ? video
                         : destination                           ? QString::fromStdString(destination->value)
                                                                 : QString();
    const QString ext = extensionFor(session->document().format());
    const QString filter = ext == QStringLiteral("txt") ? tr("Subtitle file ") + QStringLiteral("(*.txt *.sub)")
                                                        : tr("Subtitle file ") + QStringLiteral("(*.%1)").arg(ext);
    const QFileInfo info(path);
    return {{QStringLiteral("folder"), path.isEmpty() ? QUrl() : QUrl::fromLocalFile(info.absolutePath())},
            {QStringLiteral("file"), path.isEmpty() ? QUrl() : QUrl::fromLocalFile(info.absolutePath() + u'/' + info.completeBaseName())},
            {QStringLiteral("filter"), filter},
            {QStringLiteral("extension"), ext}};
}

QString Application::saveChosen(const QUrl &file)
{
    auto *session = targetSession();
    QString path = file.toLocalFile();
    if (!session || path.isEmpty())
        return QStringLiteral("failed");
    if (readOnly(path))
        return QStringLiteral("readonly");
    // Legacy EndsWith(ext), without the dot.
    const QString ext = extensionFor(session->document().format());
    if (!path.endsWith(ext))
        path += u'.' + ext;
    if (readOnly(path))
        return QStringLiteral("readonly");
    if (!saveAs(path))
        return QStringLiteral("failed");
    if (const auto target = m_workspace.editingTarget())
        m_formatChanged.erase(target->value); // legacy originalFormat = subsFormat
    return {};
}

bool Application::saveAll()
{
    bool targetNeedsDialog = false;
    for (const auto id : m_workspace.documents()) {
        auto *session = m_files->session(id);
        if (!session || !session->isDirty())
            continue;
        const auto destination = m_files->destination(id);
        if (!destination || destination->value.empty()) {
            targetNeedsDialog = targetNeedsDialog || m_workspace.editingTarget() == id;
            continue;
        }
        if (auto plan = m_files->prepareSave(id))
            m_files->startSave(std::move(*plan));
    }
    m_editor->reloadFromSession();
    refreshViews();
    return targetNeedsDialog;
}

bool Application::turnOffTranslationMode()
{
    auto *session = targetSession();
    if (!session || !application::turnOffTranslationMode(*session))
        return false;
    m_editor->reloadFromSession();
    refreshViews();
    return true;
}

void Application::setSaveWithVideoName(bool on)
{
    m_settings->set("subtitles.saveWithVideoName", on); // settingChanged announces it
}

void Application::reportIssue()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/altqx/hikari/issues")));
}

QVariantMap Application::reviewOpen(const QString &path)
{
    auto staged = m_files->stageOpen({QFileInfo(path).absoluteFilePath().toStdString()});
    if (!staged) {
        const QString problem = tr("Could not open %1; nothing was changed.").arg(QFileInfo(path).fileName());
        m_log->log(problem);
        return {{QStringLiteral("ok"), false}, {QStringLiteral("problem"), problem}, {QStringLiteral("rows"), QVariantList()}};
    }
    const QVariantList rows = reviewClose(QStringLiteral("open"));
    m_pendingOpen = std::move(*staged);
    m_pendingOpenPath = path;
    return {{QStringLiteral("ok"), true}, {QStringLiteral("problem"), QString()}, {QStringLiteral("rows"), rows}};
}

QString Application::openDropped(const QList<QUrl> &urls)
{
    QStringList files;
    for (const QUrl &url : urls)
        if (url.isLocalFile())
            files << url.toLocalFile();
    // Legacy sorts by the locale's collation.
    QCollator collator;
    std::sort(files.begin(), files.end(), [&](const QString &a, const QString &b) { return collator.compare(a, b) < 0; });
    const bool single = files.size() == 1;
    QString subtitles;
    QString video;
    for (const QString &file : files) {
        switch (application::openKindOf(file.toStdString(), single)) {
        case application::OpenKind::Subtitles:
            if (subtitles.isEmpty())
                subtitles = file; // one editing target until tabs arrive (D1)
            break;
        case application::OpenKind::Script:
            m_automation->loadScript(QUrl::fromLocalFile(file));
            break;
        case application::OpenKind::Video:
            if (video.isEmpty())
                video = file;
            break;
        case application::OpenKind::Keyframes: // keyframes arrive with the video cards
        case application::OpenKind::Refused:
            break;
        }
    }
    if (!video.isEmpty())
        m_video->openVideo(video);
    trimAudioCache(); // legacy OpenFiles
    return subtitles;
}

void Application::rememberRecent(const std::string &path)
{
    m_recent.add(path);
    m_settings->settings().set("recent.subtitles", m_recent.entries());
    emit recentChanged();
}

QVariantList Application::recentSubtitles()
{
    const bool pruned = m_recent.prune([](const std::string &path) {
        return application::isMissingLocalFile(
            path, [](const std::string &p) { return QFileInfo(QString::fromStdString(p)).isFile(); },
            [](char drive) {
#ifdef _WIN32
                const wchar_t root[] = {static_cast<wchar_t>(drive), L':', L'\\', 0};
                return ::GetDriveTypeW(root) == DRIVE_REMOTE;
#else
                (void)drive;
                return false;
#endif
            });
    });
    if (pruned)
        m_settings->settings().set("recent.subtitles", m_recent.entries());
    QVariantList rows;
    int n = 0;
    for (const auto &entry : m_recent.entries()) {
        const QString path = QString::fromStdString(entry);
        rows << QVariantMap{{QStringLiteral("path"), path},
                            {QStringLiteral("label"), QStringLiteral("%1 %2").arg(++n).arg(QFileInfo(path).fileName())}};
    }
    return rows;
}

void Application::recordFileTime(application::DocumentId document)
{
    const auto destination = m_files->destination(document);
    if (!destination || destination->value.empty())
        return;
    m_fileTimes[document.value] = QFileInfo(QString::fromStdString(destination->value)).lastModified();
    m_removedNoticed.erase(document.value);
}

QString Application::externalChange()
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    const auto destination = target ? m_files->destination(*target) : std::nullopt;
    if (!session || !destination || destination->value.empty())
        return {};
    const QFileInfo file(QString::fromStdString(destination->value));
    if (!file.exists()) {
        // Unsaved once, so the next save writes it again.
        if (!m_removedNoticed.insert(target->value).second)
            return {};
        session->markUnsaved();
        refreshViews();
        return QStringLiteral("removed");
    }
    m_removedNoticed.erase(target->value);
    const auto known = m_fileTimes.find(target->value);
    const QDateTime now = file.lastModified();
    if (known != m_fileTimes.end() && now <= known->second)
        return {};
    // Asked once per change, whatever the answer (legacy SetLastSaveTime).
    m_fileTimes[target->value] = now;
    return known == m_fileTimes.end() ? QString() : QStringLiteral("modified");
}

bool Application::reloadTarget()
{
    const auto target = m_workspace.editingTarget();
    if (!target)
        return false;
    auto staged = m_files->stageReload(*target);
    if (!staged) {
        m_log->log(tr("Could not reload the subtitles; nothing was changed."));
        return false;
    }
    if (!m_files->activate(std::move(*staged))) {
        m_log->log(tr("The subtitles changed while reloading; nothing was replaced."));
        return false;
    }
    if (auto *session = m_files->session(*target))
        selectLegacyActiveLine(*session);
    recordFileTime(*target);
    m_videoRevision.reset();
    m_videoLine.reset();
    m_editor->reloadFromSession();
    refreshViews();
    return true;
}

bool Application::saveAs(const QString &path)
{
    const auto target = m_workspace.editingTarget();
    if (!target || path.isEmpty())
        return false;
    auto plan = m_files->prepareSave(*target, application::DestinationKey{QFileInfo(path).absoluteFilePath().toStdString()});
    if (!plan)
        return false;
    return m_files->startSave(std::move(*plan)).has_value();
}

application::GridSelection Application::gridSelection() const
{
    std::vector<core::LineId> document;
    const auto target = m_workspace.editingTarget();
    if (auto *session = target ? m_files->session(*target) : nullptr)
        for (const auto *line : session->document().lines())
            document.push_back(line->id);
    application::GridSelection rules(std::move(document), m_shell->displayedLines());
    rules.setChangeActiveOnSelection(m_settings->boolean("grid.changeActiveOnSelection"));
    return rules;
}

bool Application::applySelection(application::Selection next)
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    if (next.active && next.active != session->selection().active && !m_editor->showLine(next.active->value))
        return false; // e.g. a draft that cannot be committed
    session->setSelection(std::move(next));
    m_shell->setSelection(session->selection());
    refreshVideo();
    return true;
}

void Application::moveActiveLine(qulonglong id)
{
    auto *session = targetSession();
    if (!session || session->selection().active == core::LineId{id})
        return;
    application::Selection next = session->selection();
    next.active = core::LineId{id};
    applySelection(std::move(next));
}

void Application::selectLine(qulonglong id)
{
    const auto target = m_workspace.editingTarget();
    if (auto *session = target ? m_files->session(*target) : nullptr)
        applySelection(gridSelection().plain(session->selection(), core::LineId{id}));
}

void Application::extendSelection(int rows)
{
    const auto target = m_workspace.editingTarget();
    if (auto *session = target ? m_files->session(*target) : nullptr)
        applySelection(gridSelection().shiftKey(session->selection(), rows));
}

void Application::clickLine(qulonglong id, int modifiers)
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return;
    const auto rules = gridSelection();
    const bool ctrl = modifiers & Qt::ControlModifier, shift = modifiers & Qt::ShiftModifier;
    const core::LineId line{id};
    if (shift)
        applySelection(rules.shiftClick(session->selection(), line, ctrl));
    else if (ctrl)
        applySelection(rules.ctrlClick(session->selection(), line));
    else
        applySelection(rules.plain(session->selection(), line));
}

void Application::dragSelection(qulonglong id)
{
    const auto target = m_workspace.editingTarget();
    if (auto *session = target ? m_files->session(*target) : nullptr)
        applySelection(gridSelection().shiftClick(session->selection(), core::LineId{id}, false));
}

void Application::selectAllLines()
{
    const auto target = m_workspace.editingTarget();
    if (auto *session = target ? m_files->session(*target) : nullptr)
        applySelection(gridSelection().selectAll(session->selection()));
}

bool Application::insertLine(bool before, const QString &timing)
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    const auto where = before ? application::InsertWhere::Before : application::InsertWhere::After;
    std::expected<void, application::CommandRefusal> done;
    if (timing.isEmpty()) {
        done = application::insertLine(*session, where);
    } else {
        // Legacy needs video for these: the shown frame's start and the next one's.
        const auto &video = m_video->session();
        const auto frame = video.shownFrame();
        const auto start = frame ? video.frameStart(*frame) : std::nullopt;
        if (!start)
            return false;
        const auto next = video.frameStart(*frame + 1);
        const std::int64_t startMs = start->microseconds() / 1000;
        if (timing == QLatin1String("frame"))
            done = application::insertWithFrameTimes(
                *session, where, {startMs, next ? next->microseconds() / 1000 : startMs});
        else
            done = application::insertLine(*session, where, startMs);
    }
    m_editor->reloadFromSession();
    refreshViews();
    return done.has_value();
}

application::EditSession *Application::targetSession() const
{
    const auto target = m_workspace.editingTarget();
    return target ? m_files->session(*target) : nullptr;
}

application::LineVisible Application::shownLines() const
{
    const auto shown = m_shell->displayedLines();
    const std::set<core::LineId> set(shown.begin(), shown.end());
    return [set](core::LineId id) { return set.contains(id); };
}

namespace {

QString clipboardText()
{
    return QGuiApplication::clipboard()->text();
}

void setClipboardText(const std::u8string &text)
{
    QGuiApplication::clipboard()->setText(
        QString::fromUtf8(reinterpret_cast<const char *>(text.data()), static_cast<qsizetype>(text.size())));
}

} // namespace

bool Application::copyLines()
{
    auto *session = targetSession();
    if (!session || session->selection().selected.empty())
        return false;
    setClipboardText(application::copyRows(*session));
    return true;
}

bool Application::cutLines()
{
    // Legacy GRID_CUT copies, then deletes the selection.
    return copyLines() && deleteLines();
}

bool Application::pasteLines()
{
    auto *session = targetSession();
    if (!session)
        return false;
    const bool done = application::pasteRows(*session, toU8(clipboardText()), shownLines()).has_value();
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

QVariantList Application::columnChoices(bool paste)
{
    using namespace core::column;
    auto *session = targetSession();
    if (!session)
        return {};
    const auto format = session->document().format();
    std::vector<std::pair<QString, int>> rows;
    if (format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::PlainText) {
        rows = {{tr("Layer"), Layer},        {tr("The starting time"), Start}, {tr("End time"), End},
                {tr("Actor"), Actor},        {tr("Styles"), Style},           {tr("Left margin"), MarginLeft},
                {tr("Right margin"), MarginRight}, {tr("Vertical margin"), MarginVertical}, {tr("Effect"), Effect}};
        if (!paste) {
            rows.push_back({tr("Text"), Text});
            rows.push_back({tr("Text without tags"), TextWithoutTags});
        } else if (application::translationMode(*session)) {
            rows.push_back({tr("Text into original"), Text});
            rows.push_back({tr("Text into translation"), Translation});
        } else {
            rows.push_back({tr("Text"), Text});
        }
    } else if (format == core::SubtitleFormat::TMPlayer) {
        rows = {{tr("The starting time"), Start}, {tr("Text"), Text}};
    } else {
        // Legacy labels the end time "The starting time" too.
        rows = {{tr("The starting time"), Start}, {tr("The starting time"), End}, {tr("Text"), Text}};
    }
    const int chosen = paste ? m_pasteColumns : m_copyColumns;
    QVariantList out;
    for (const auto &[label, bit] : rows)
        out << QVariantMap{{QStringLiteral("label"), label},
                           {QStringLiteral("bit"), bit},
                           {QStringLiteral("checked"), (chosen & bit) != 0}};
    return out;
}

void Application::rememberColumns(bool paste, int columns)
{
    (paste ? m_pasteColumns : m_copyColumns) = columns;
    m_settings->set(paste ? "grid.pasteColumns" : "grid.copyColumns", columns);
}

bool Application::copyColumns(int columns)
{
    auto *session = targetSession();
    if (!session || session->selection().selected.empty())
        return false;
    rememberColumns(false, columns);
    setClipboardText(application::copyColumns(*session, columns));
    return true;
}

bool Application::pasteColumns(int columns)
{
    auto *session = targetSession();
    if (!session)
        return false;
    rememberColumns(true, columns);
    const bool done =
        application::pasteColumns(*session, toU8(clipboardText()), columns, shownLines()).has_value();
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

bool Application::setNewFps(const QString &oldFps, const QString &newFps)
{
    auto *session = targetSession();
    bool okOld = false, okNew = false;
    // The dialog only takes digits and '.', so read them in the C locale.
    const double from = QLocale::c().toDouble(oldFps.trimmed(), &okOld);
    const double to = QLocale::c().toDouble(newFps.trimmed(), &okNew);
    if (!session || !okOld || !okNew)
        return false;
    const bool done = application::setNewFps(*session, from, to).has_value();
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

bool Application::setFpsFromVideo()
{
    auto *session = targetSession();
    const auto &video = m_video->session();
    const auto frame = video.shownFrame();
    const auto start = frame ? video.frameStart(*frame) : std::nullopt;
    if (!session || !start)
        return false;
    const bool done = application::setFpsFromVideo(*session, start->microseconds() / 1000, shownLines()).has_value();
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

bool Application::runFilter(
    const std::function<std::expected<void, application::CommandRefusal>(application::EditSession &)> &command)
{
    auto *session = targetSession();
    if (!session)
        return false;
    const bool done = command(*session).has_value();
    // The editor following a new active Line would make the selection plain;
    // filtering keeps it (hidden Lines stay selected).
    const application::Selection kept = session->selection();
    m_editor->reloadFromSession();
    if (kept.active)
        m_editor->showLine(kept.active->value);
    session->setSelection(kept);
    refreshViews();
    return done;
}

bool Application::filterLines()
{
    application::FilterSettings settings{m_gridFilter->filterBy(), {}, m_gridFilter->inverted(), m_gridFilter->addToFilter()};
    for (const QString &style : m_gridFilter->styles())
        settings.styles.push_back(toU8(style));
    const auto shown = shownLines();
    return runFilter([&](application::EditSession &s) { return application::filterLines(s, settings, shown); });
}

bool Application::hideSelectedLines()
{
    const auto shown = shownLines();
    return runFilter([&](application::EditSession &s) { return application::hideSelectedLines(s, shown); });
}

bool Application::turnOffFiltering()
{
    return runFilter([](application::EditSession &s) { return application::turnOffFiltering(s); });
}

bool Application::toggleHiddenBlock(int documentRow)
{
    // R1: legacy FilterPartial (SubsGridFiltering.cpp:102-128).
    m_partialFilter = true;
    const bool done =
        runFilter([&](application::EditSession &s) { return application::toggleHiddenBlock(s, documentRow); });
    m_partialFilter = false;
    return done;
}

QStringList Application::styleNames() const
{
    QStringList out;
    if (auto *session = targetSession())
        for (const auto &style : core::decodeStyles(session->document()))
            out << QString::fromUtf8(reinterpret_cast<const char *>(style.name.data()), static_cast<qsizetype>(style.name.size()));
    return out;
}

std::string Application::recoveryKey(application::DocumentId document) const
{
    return m_sessionId.toStdString() + "-" + std::to_string(document.value);
}

void Application::scheduleAutosave()
{
    if (!m_recovery || !m_recovery->enabled())
        return;
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session || !session->isDirty() || session->isReadOnly())
        return;
    // Legacy: 20 s after the first change since the last autosave.
    QTimer *&timer = m_autosaveTimers[target->value];
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(20'000);
        const auto id = *target;
        connect(timer, &QTimer::timeout, this, [this, id] { autosave(id); });
    }
    if (!timer->isActive())
        timer->start();
}

bool Application::autosaveNow()
{
    const auto target = m_workspace.editingTarget();
    return target && autosave(*target);
}

bool Application::autosave(application::DocumentId document)
{
    auto *session = m_files->session(document);
    if (!session || !m_recovery->enabled())
        return false;
    application::RecoveryContent content;
    content.bytes = core::encodeSubtitle(session->document());
    const auto format = session->document().format();
    content.extension = format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::PlainText ? "ass"
                        : format == core::SubtitleFormat::Srt                                            ? "srt"
                                                                                                         : "txt";
    const std::string *title = m_workspace.title(document);
    content.title = title ? *title : std::string();
    if (const auto destination = m_files->destination(document))
        content.originalPath = destination->value;
    if (const auto draft = session->draftChange()) {
        const auto lines = session->document().lines();
        for (std::size_t row = 0; row < lines.size(); ++row)
            if (lines[row]->id == draft->first)
                content.draftRow = row;
        content.draft = draft->second;
    }
    if (const auto &rate = session->document().frameRate())
        content.frameRate = std::pair(rate->framesPerSecond().numerator(), rate->framesPerSecond().denominator());
    content.writtenMs = QDateTime::currentMSecsSinceEpoch();
    return m_recovery->write(recoveryKey(document), content);
}

void Application::discardRecovery(application::DocumentId document)
{
    if (const auto it = m_autosaveTimers.find(document.value); it != m_autosaveTimers.end() && it->second)
        it->second->stop();
    if (m_recovery)
        m_recovery->discard(recoveryKey(document));
}

namespace {

// A session's lock is gone or stale: it ended without closing cleanly.
bool sessionEnded(const QString &dir, const std::string &session)
{
    const QString path = dir + QStringLiteral("/sessions/") + QString::fromStdString(session) + QStringLiteral(".lock");
    QLockFile lock(path);
    if (!lock.tryLock(0))
        return false; // still running
    lock.unlock();
    return true;
}

} // namespace

QVariantList Application::recoveryBundles() const
{
    QVariantList out;
    if (!m_recovery)
        return out;
    const QString dir = m_recoveryDir;
    for (const auto &bundle : m_recovery->leftovers([&](const std::string &s) { return sessionEnded(dir, s); })) {
        QVariantList generations;
        for (auto it = bundle.generations.rbegin(); it != bundle.generations.rend(); ++it)
            if (const auto content = m_recovery->read(bundle.key, *it))
                generations << QVariantMap{{QStringLiteral("generation"), QVariant::fromValue<qulonglong>(*it)},
                                           {QStringLiteral("written"), QDateTime::fromMSecsSinceEpoch(content->writtenMs).toString(Qt::ISODate)}};
        out << QVariantMap{{QStringLiteral("key"), QString::fromStdString(bundle.key)},
                           {QStringLiteral("title"), QString::fromStdString(bundle.latest.title)},
                           {QStringLiteral("original"), QString::fromStdString(bundle.latest.originalPath)},
                           {QStringLiteral("written"), QDateTime::fromMSecsSinceEpoch(bundle.latest.writtenMs).toString(Qt::ISODate)},
                           {QStringLiteral("generations"), generations}};
    }
    return out;
}

bool Application::recoverBundle(const QString &key, qulonglong generation)
{
    const auto content = m_recovery ? m_recovery->read(key.toStdString(), generation) : std::nullopt;
    if (!content)
        return false;
    const std::u8string extension(content->extension.begin(), content->extension.end());
    auto loaded = core::loadSubtitle(content->bytes, extension);
    if (!loaded)
        return false;
    core::Document document = std::move(loaded->document);
    if (content->frameRate)
        if (const auto rate = core::FrameRate::make(content->frameRate->first, content->frameRate->second))
            document.setFrameRate(*rate);
    // L58-recovery-copy: a new Untitled Document; the original file is untouched.
    const auto id = m_files->createUnsaved(std::move(document));
    auto *session = m_files->session(id);
    selectLegacyActiveLine(*session);
    if (content->draftRow) {
        const auto lines = session->document().lines();
        if (*content->draftRow < lines.size()) {
            const auto line = lines[*content->draftRow]->id;
            session->setSelection(application::Selection{line, {line}, line, {}});
            session->editDraft(line, content->draft); // pending, not committed
        }
    }
    m_workspace.add(id, tr("%1 (recovered)").arg(QString::fromStdString(content->title)).toStdString());
    m_workspace.setEditingTarget(id);
    refreshViews();
    return true;
}

void Application::dismissBundle(const QString &key)
{
    if (m_recovery)
        m_recovery->discard(key.toStdString());
}

int Application::removeAutosavesOlderThan(const QDate &date)
{
    int removed = 0;
    const qint64 cutoff = date.isValid() ? QDateTime(date, QTime(0, 0)).toMSecsSinceEpoch() : -1;
    for (const QVariant &v : recoveryBundles()) {
        const auto bundle = v.toMap();
        const auto written = QDateTime::fromString(bundle.value(QStringLiteral("written")).toString(), Qt::ISODate);
        if (cutoff < 0 || written.toMSecsSinceEpoch() < cutoff) {
            dismissBundle(bundle.value(QStringLiteral("key")).toString());
            ++removed;
        }
    }
    return removed;
}

bool Application::splitLines(const QString &kind)
{
    const auto &video = m_video->session();
    const auto timebase = video.legacyTimebase();
    const auto shown = shownLines();
    if (kind == QLatin1String("videoTime")) {
        const auto frame = video.shownFrame();
        const auto start = frame ? video.frameStart(*frame) : std::nullopt;
        if (!start)
            return false; // legacy needs the video
        const std::int64_t tell = start->microseconds() / 1000;
        return runFilter([&](application::EditSession &s) { return application::splitAtVideoTime(s, timebase, tell, shown); });
    }
    if (kind == QLatin1String("frames"))
        return runFilter([&](application::EditSession &s) { return application::splitIntoFrames(s, timebase, shown); });
    const auto text = kind == QLatin1String("chars")   ? application::SplitText::Characters
                      : kind == QLatin1String("words") ? application::SplitText::Words
                      : kind == QLatin1String("wraps") ? application::SplitText::Wraps
                                                       : std::optional<application::SplitText>();
    if (!text)
        return false;
    // Qt's word boundaries stand in for legacy boost::locale's.
    const application::WordSegments words = [](std::u16string_view view) {
        const QString string = QString::fromUtf16(view.data(), static_cast<qsizetype>(view.size()));
        QTextBoundaryFinder finder(QTextBoundaryFinder::Word, string);
        std::vector<std::pair<std::size_t, bool>> segments;
        qsizetype start = 0;
        for (qsizetype end = finder.toNextBoundary(); end >= 0; end = finder.toNextBoundary()) {
            if (end == start)
                continue;
            bool word = false;
            for (const QChar c : QStringView(string).mid(start, end - start))
                word = word || c.isLetterOrNumber() || c.isSurrogate();
            segments.emplace_back(static_cast<std::size_t>(end), word);
            start = end;
        }
        return segments;
    };
    ui::QtTextMeasurePort measure;
    return runFilter([&](application::EditSession &s) { return application::splitByText(s, *text, measure, words, shown); });
}

bool Application::canPasteTranslation() const
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    if (!session || targetUntitled())
        return false;
    const auto format = session->document().format();
    return format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::PlainText;
}

QUrl Application::targetFolder() const
{
    const auto target = m_workspace.editingTarget();
    const auto destination = target ? m_files->destination(*target) : std::nullopt;
    if (!destination || destination->value.empty())
        return {};
    return QUrl::fromLocalFile(QFileInfo(QString::fromStdString(destination->value)).absolutePath());
}

bool Application::pasteTranslationFile(const QUrl &file)
{
    auto *session = targetSession();
    if (!session || !canPasteTranslation())
        return false;
    const QString path = file.toLocalFile();
    // SubsGrid::OnPasteTextTl reads through OpenWrite::FileOpen (test = true):
    // R4-uchardet's charset and R5-per-platform line ends (CRLF is LF on
    // Windows, the Linux build keeps "\r"). A file it cannot read, decode, or
    // that is empty changes nothing.
    const auto read = backends::readLegacyTextFile(path);
    if (!read) {
        m_log->log(tr("Could not open %1; nothing was changed.").arg(QFileInfo(path).fileName()));
        return false;
    }
    const QString text = QString::fromStdU16String(*read);
    // Legacy compares the extension after the last '.' exactly.
    const QString extension = path.section(QLatin1Char('.'), -1);
    const auto shown = shownLines();
    const bool done = application::pasteTranslation(*session, toU8(text), toU8(extension), shown).has_value();
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

bool Application::canShiftTranslation() const
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    return session && session->document().scriptInfo(u8"TLMode Showtl") == u8"Yes";
}

bool Application::shiftTranslation(int mode)
{
    auto *session = targetSession();
    if (!session || mode < 0 || mode > 5)
        return false;
    const auto shown = shownLines();
    const bool done =
        application::moveTranslation(*session, static_cast<application::TranslationMove>(mode), shown).has_value();
    m_editor->reloadFromSession();
    if (const auto active = session->selection().active)
        m_editor->showLine(active->value);
    refreshViews();
    return done;
}

QString Application::openKeyframes(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (const auto target = m_workspace.editingTarget())
        m_tabMedia[target->value].keyframes = QDir::toNativeSeparators(path); // P6: legacy KeyframesPath
    auto &video = m_video->session();
    if (video.state() != application::VideoSession::State::Ready) {
        m_pendingKeyframes = path; // applied when a video opens
        return {};
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return tr("Invalid keyframes format");
    const QByteArray bytes = f.readAll();
    auto frames = application::parseKeyframes(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (frames.empty())
        return tr("Invalid keyframes format");
    video.setKeyframes(std::move(frames));
    refreshViews();
    return {};
}

bool Application::exactTimebase() const
{
    return m_video->session().state() == application::VideoSession::State::Ready &&
           m_video->session().legacyTimebase().exact();
}

QString Application::shiftTimes()
{
    auto *session = targetSession();
    if (!session)
        return tr("No document open");
    const auto &video = m_video->session();
    const bool hasVideo = video.state() == application::VideoSession::State::Ready;
    const auto timebase = video.legacyTimebase();
    application::ShiftContext context;
    if (hasVideo) {
        context.timebase = &timebase;
        // VideoBox::GetFrameTime: the shown frame's midpoint start and end times.
        context.keyframes = video.keyframes();
        if (const auto frame = video.shownFrame()) {
            context.videoFrame = *frame;
            context.videoFrameStartMs = timebase.startTimeFor(*frame);
            context.videoFrameEndMs = timebase.endTimeFor(*frame);
        }
    }
    // A3: the audio box's mark (legacy ChangeTimes' moveTimeOptions & 8 with ABox->hasMark)
    if (m_audio && m_audio->hasMark())
        context.audioMarkMs = m_audio->markMs();
    const auto result = application::shiftTimes(*session, m_shiftTimes->settings(), context, actionLines(*session));
    m_editor->reloadFromSession();
    refreshViews();
    if (!result) {
        if (const auto *problem = std::get_if<application::ShiftProblem>(&result.error())) {
            switch (*problem) {
            case application::ShiftProblem::NoStylesChosen:
                return tr("No styles selected for time shifting");
            case application::ShiftProblem::NoLinesSelected:
                return tr("No lines selected for shifting");
            case application::ShiftProblem::NoExactTimebase:
                return tr("Video was not loaded using FFMS2");
            }
        }
        return tr("The times could not be shifted.");
    }
    if (result->endCorrectionSkipped)
        m_log->log(tr("Video was not loaded using FFMS2"));
    return {};
}

application::LineVisible Application::actionLines(const application::EditSession &session) const
{
    if (m_gridFilter->ignoreInActions())
        return {};
    std::set<core::LineId> hidden;
    for (const auto *line : session.document().lines())
        if (line->visibility == core::LineVisibility::Hidden)
            hidden.insert(line->id);
    return [hidden](core::LineId id) { return !hidden.contains(id); };
}

namespace {

std::u8string utf8(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return std::u8string(reinterpret_cast<const char8_t *>(b.constData()), static_cast<std::size_t>(b.size()));
}

QString fromUtf8(const std::u8string &s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

application::SelectLinesSettings selectSettings(const QVariantMap &m, int options)
{
    using S = application::SelectLinesSettings;
    S s = application::selectLinesFromOptions(options);
    s.find = utf8(m.value(QStringLiteral("find")).toString());
    s.with = m.value(QStringLiteral("with"), s.with).toBool();
    s.matchCase = m.value(QStringLiteral("matchCase"), s.matchCase).toBool();
    s.regex = m.value(QStringLiteral("regex"), s.regex).toBool();
    s.field = static_cast<S::Field>(std::clamp(m.value(QStringLiteral("field"), int(s.field)).toInt(), 0, 5));
    s.dialogues = m.value(QStringLiteral("dialogues"), s.dialogues).toBool();
    s.comments = m.value(QStringLiteral("comments"), s.comments).toBool();
    s.mode = static_cast<S::Mode>(std::clamp(m.value(QStringLiteral("mode"), int(s.mode)).toInt(), 0, 2));
    s.action = static_cast<S::Action>(std::clamp(m.value(QStringLiteral("action"), int(s.action)).toInt(), 0, 6));
    return s;
}

// wx_regexec's log line for a failed match (defined with the Fix minor errors helpers).
QString regexMatchError(const std::u16string &error);
QString fromUtf16(const std::u16string &s);

} // namespace

QVariantMap Application::selectLinesSettings() const
{
    const auto s = application::selectLinesFromOptions(m_selectOptions);
    return {{QStringLiteral("with"), s.with},
            {QStringLiteral("matchCase"), s.matchCase},
            {QStringLiteral("regex"), s.regex},
            {QStringLiteral("field"), int(s.field)},
            {QStringLiteral("dialogues"), s.dialogues},
            {QStringLiteral("comments"), s.comments},
            {QStringLiteral("mode"), int(s.mode)},
            {QStringLiteral("action"), int(s.action)},
            {QStringLiteral("recent"), m_selectRecent}};
}

void Application::colourPickerOpened()
{
    const auto target = m_workspace.editingTarget();
    m_colourPicker->opened(target ? QString::number(target->value) : QString());
}

QVariantMap Application::openSelectLines()
{
    m_selectLinesOpened = true;
    return selectLinesSettings();
}

void Application::saveSelectLinesSettings(const QVariantMap &settings)
{
    m_selectOptions = application::selectLinesOptions(selectSettings(settings, m_selectOptions));
    m_settings->set("selectLines.options", m_selectOptions);
}

QString Application::selectLines(const QVariantMap &map, bool allTabs)
{
    const auto settings = selectSettings(map, m_selectOptions);
    // Case-insensitive by Unicode case folding for every letter
    // (U1-unicode-case; legacy's English interface folded A-Z only).
    const application::TextFold fold = unicodeFold;
    std::vector<application::EditSession *> sessions;
    if (allTabs) {
        for (const auto id : m_workspace.documents())
            if (auto *session = m_files->session(id))
                sessions.push_back(session);
    } else if (auto *session = targetSession()) {
        sessions.push_back(session);
    }
    int count = 0;
    for (auto *session : sessions) {
        const auto result = application::selectLines(*session, settings, actionLines(*session), fold);
        if (!result)
            continue;
        // wxRegEx's wxLogError calls, per tab as legacy compiled per tab.
        if (result->invalidExpression)
            m_log->log(tr("Invalid regular expression '%1': %2")
                           .arg(fromUtf16(result->invalidExpression->first), fromUtf16(result->invalidExpression->second)));
        for (const auto &error : result->matchErrors)
            m_log->log(regexMatchError(error));
        count += result->count;
        // Each Document's Copy or Cut replaces the clipboard (legacy, per tab).
        if (result->clipboard)
            setClipboardText(*result->clipboard);
    }
    m_editor->reloadFromSession();
    refreshViews();
    // AddRecent, after the run.
    std::vector<std::u8string> recent;
    for (const QString &r : std::as_const(m_selectRecent))
        recent.push_back(utf8(r));
    m_selectRecent.clear();
    for (const auto &r : application::addRecentSelection(std::move(recent), settings.find))
        m_selectRecent << fromUtf8(r);
    m_settings->set("selectLines.recentSelections", m_selectRecent); // AddRecent's SetTable
    saveSelectLinesSettings(map);
    using M = application::SelectLinesSettings::Mode;
    return settings.mode == M::Select           ? tr("%1 lines selected.").arg(count)
           : settings.mode == M::AddToSelection ? tr("%1 lines added to selection.").arg(count)
                                                : tr("%1 lines deselected.").arg(count);
}

QString Application::selectStylesPattern(const QStringList &styles) const
{
    std::vector<std::u8string> names;
    for (const QString &s : styles)
        names.push_back(utf8(s));
    return fromUtf8(application::stylesPattern(names));
}

namespace {

// MisspellReplacer::MoveCase: iswupper, and towlower / towupper through
// wxString MakeLower / MakeUpper and wxToupper, as the interface language's
// C locale has them (backends::legacyCaseMapping: A-Z only in English).
application::ReplacerCase replacerCase(bool everyLetter)
{
    const auto cases = backends::legacyCaseMapping(everyLetter);
    return {cases.isUpper, cases.toLower, cases.toUpper};
}

QString fromUtf16(const std::u16string &s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), static_cast<qsizetype>(s.size()));
}

application::ReplacerRule replacerRule(const QVariantMap &m)
{
    return {utf8(m.value(QStringLiteral("description")).toString()), utf8(m.value(QStringLiteral("find")).toString()),
            utf8(m.value(QStringLiteral("replace")).toString()), m.value(QStringLiteral("options")).toInt(), false};
}

// wxRegEx::Compile's wxLogError for each rule that does not compile, at the
// points MisspellReplacer compiles its rules.
void logRegexErrors(ui::LogController &log, const std::vector<application::ReplacerRule> &rules, bool checkedOnly)
{
    for (const auto &error : application::replacerRegexErrors(rules, checkedOnly))
        log.log(QCoreApplication::translate("hikari::app::Application", "Invalid regular expression '%1': %2")
                    .arg(fromUtf16(error.expression), fromUtf16(error.message)));
}

// wx_regexec's wxLogError for each Matches or Replace that failed with a
// PCRE2 error (the match or heap limit), counted as no match.
QString regexMatchError(const std::u16string &error)
{
    return QCoreApplication::translate("hikari::app::Application", "Failed to find match for regular expression: %1")
        .arg(fromUtf16(error));
}

void logMatchErrors(ui::LogController &log, const application::ReplacerMatchErrors &errors)
{
    for (const auto &error : errors)
        log.log(regexMatchError(error));
}

application::ReplacerScope replacerScope(const QVariantMap &m)
{
    using L = application::ReplacerScope::Lines;
    return {static_cast<L>(std::clamp(m.value(QStringLiteral("lines")).toInt(), 0, 3)),
            utf8(m.value(QStringLiteral("styles")).toString())};
}

QString findQs(std::u8string_view s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

// F1: a dialog tab as QML holds it.
application::FindReplaceSettings findSettings(const QVariantMap &m)
{
    using S = application::FindReplaceSettings;
    S s;
    s.tab = static_cast<S::Tab>(std::clamp(m.value(QStringLiteral("tab")).toInt(), 0, 2));
    s.find = toU8(m.value(QStringLiteral("find")).toString());
    s.replace = toU8(m.value(QStringLiteral("replace")).toString());
    s.styles = toU8(m.value(QStringLiteral("styles")).toString());
    s.filters = toU8(m.value(QStringLiteral("filters")).toString());
    s.folder = toU8(m.value(QStringLiteral("folder")).toString());
    s.field = static_cast<S::Field>(std::clamp(m.value(QStringLiteral("field")).toInt(), 0, 3));
    s.lines = static_cast<S::Lines>(std::clamp(m.value(QStringLiteral("lines")).toInt(), 0, 3));
    s.matchCase = m.value(QStringLiteral("matchCase")).toBool();
    s.regex = m.value(QStringLiteral("regex")).toBool();
    s.startOfText = m.value(QStringLiteral("startOfText")).toBool();
    s.endOfText = m.value(QStringLiteral("endOfText")).toBool();
    s.includeComments = m.value(QStringLiteral("includeComments")).toBool();
    s.skipTags = m.value(QStringLiteral("skipTags")).toBool();
    s.skipText = m.value(QStringLiteral("skipText")).toBool();
    s.subfolders = m.value(QStringLiteral("subfolders")).toBool();
    s.hiddenFolders = m.value(QStringLiteral("hiddenFolders")).toBool();
    return s;
}

QStringList findList(const std::vector<std::u8string> &list)
{
    QStringList out;
    for (const auto &s : list)
        out << findQs(s);
    return out;
}

QVariantMap findMap(const application::FindReplaceSettings &s, const application::FindReplace::Recent &recent)
{
    const auto q = [](const std::u8string &v) { return findQs(v); };
    return {{QStringLiteral("tab"), static_cast<int>(s.tab)},
            {QStringLiteral("find"), q(s.find)},
            {QStringLiteral("replace"), q(s.replace)},
            {QStringLiteral("styles"), q(s.styles)},
            {QStringLiteral("filters"), q(s.filters)},
            {QStringLiteral("folder"), q(s.folder)},
            {QStringLiteral("field"), static_cast<int>(s.field)},
            {QStringLiteral("lines"), static_cast<int>(s.lines)},
            {QStringLiteral("matchCase"), s.matchCase},
            {QStringLiteral("regex"), s.regex},
            {QStringLiteral("startOfText"), s.startOfText},
            {QStringLiteral("endOfText"), s.endOfText},
            {QStringLiteral("includeComments"), s.includeComments},
            {QStringLiteral("skipTags"), s.skipTags},
            {QStringLiteral("skipText"), s.skipText},
            {QStringLiteral("subfolders"), s.subfolders},
            {QStringLiteral("hiddenFolders"), s.hiddenFolders},
            {QStringLiteral("finds"), findList(recent.finds)},
            {QStringLiteral("replacements"), findList(recent.replacements)},
            {QStringLiteral("filterList"), findList(recent.filters)},
            {QStringLiteral("paths"), findList(recent.paths)}};
}

// TabWindow::SetValues and the constructor take the first entry of each list.
void findFirstEntries(application::FindReplaceSettings &s, const application::FindReplace::Recent &recent)
{
    const auto first = [](const std::vector<std::u8string> &l) { return l.empty() ? std::u8string() : l.front(); };
    s.find = first(recent.finds);
    s.replace = first(recent.replacements);
    s.filters = first(recent.filters);
    s.folder = first(recent.paths);
}

} // namespace

// F3: spelling.

namespace {

QString qstr(std::u16string_view s)
{
    return QString::fromUtf16(s.data(), static_cast<qsizetype>(s.size()));
}

// A raw inclusive [start, end] of the editor's text in its hidden-tag
// projection, as [start, end); nothing when it lies in hidden tags.
std::optional<std::pair<int, int>> displayRange(const core::Projection &projection, int start, int end)
{
    auto locate = [&](int raw, bool after) {
        for (const auto &span : projection.spans) {
            if (raw < static_cast<int>(span.rawStart) || raw >= static_cast<int>(span.rawEnd))
                continue;
            if (span.kind == core::SpanKind::Text)
                return static_cast<int>(span.displayStart) + raw - static_cast<int>(span.rawStart) + (after ? 1 : 0);
            if (span.kind == core::SpanKind::HiddenOverride)
                return static_cast<int>(span.displayStart);
            return static_cast<int>(after ? span.displayEnd : span.displayStart);
        }
        return static_cast<int>(projection.text.size());
    };
    const int from = locate(start, false), to = locate(end, true);
    if (to <= from)
        return std::nullopt;
    return std::pair(from, to);
}

application::SpellCheckWalk::Options walkOptions(const QVariantMap &options)
{
    return {options.value(QStringLiteral("ignoreComments")).toBool(),
            options.value(QStringLiteral("ignoreUpperCase")).toBool()};
}

} // namespace

std::vector<application::ReplacerRule> &Application::misspellRuleList()
{
    if (!m_misspellRules) {
        // FillRulesList: Rules.txt in the configuration folder through
        // OpenWrite::FileOpen (R4-uchardet: UTF-8, else uchardet's charset,
        // else wxConvLocal; R5-per-platform line ends), else the shipped rules.
        QByteArray text;
        if (!m_settingsFile.isEmpty())
            if (const auto rules =
                    backends::readLegacyTextFile(QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/Rules.txt")))
                text = QString::fromStdU16String(*rules).toUtf8();
        auto read = application::readReplacerRules(
            std::u8string_view(reinterpret_cast<const char8_t *>(text.constData()), static_cast<std::size_t>(text.size())));
        for (const auto &line : read.invalid)
            m_log->log(tr("Rule \"%1\" is invalid.").arg(fromUtf8(line)));
        m_misspellRules = std::move(read.rules);
    }
    return *m_misspellRules;
}

QVariantList Application::misspellRules()
{
    QVariantList rows;
    for (const auto &rule : misspellRuleList())
        rows << QVariantMap{{QStringLiteral("description"), fromUtf8(rule.description)},
                            {QStringLiteral("find"), fromUtf8(rule.find)},
                            {QStringLiteral("replace"), fromUtf8(rule.replace)},
                            {QStringLiteral("options"), rule.options},
                            {QStringLiteral("checked"), rule.checked}};
    return rows;
}

bool Application::addMisspellRule(const QVariantMap &rule)
{
    if (m_misspellResultsShown) {
        m_log->log(tr("Cannot change rules\nwhen find results window is open"));
        return false;
    }
    misspellRuleList().push_back(replacerRule(rule)); // added unchecked
    return true;
}

bool Application::editMisspellRule(int index, const QVariantMap &rule)
{
    if (m_misspellResultsShown) {
        m_log->log(tr("Cannot change rules\nwhen find results window is open"));
        return false;
    }
    auto &rules = misspellRuleList();
    if (index < 0 || static_cast<std::size_t>(index) >= rules.size()) {
        m_log->log(QStringLiteral("edit rule - bad selection of rule %1 - %2").arg(index).arg(rules.size()));
        return false;
    }
    // The checkbox stays as it was.
    const bool checked = rules[static_cast<std::size_t>(index)].checked;
    rules[static_cast<std::size_t>(index)] = replacerRule(rule);
    rules[static_cast<std::size_t>(index)].checked = checked;
    return true;
}

bool Application::removeMisspellRule(int index)
{
    if (m_misspellResultsShown) {
        m_log->log(tr("Cannot change rules\nwhen find results window is open"));
        return false;
    }
    auto &rules = misspellRuleList();
    if (index < 0 || static_cast<std::size_t>(index) >= rules.size()) {
        m_log->log(QStringLiteral("edit rule - bad selection of rule %1 - %2").arg(index).arg(rules.size()));
        return false;
    }
    rules.erase(rules.begin() + index);
    return true;
}

void Application::checkMisspellRule(int index, bool checked)
{
    auto &rules = misspellRuleList();
    if (index >= 0 && static_cast<std::size_t>(index) < rules.size())
        rules[static_cast<std::size_t>(index)].checked = checked;
}

QVariantList Application::findMisspells(const QVariantMap &scope, bool allTabs)
{
    // SeekOnActualTab / SeekOnAllTabs: the list starts empty each time.
    m_misspellFinds.clear();
    std::vector<application::DocumentId> documents;
    if (allTabs)
        documents = m_workspace.documents();
    else if (const auto target = m_workspace.editingTarget())
        documents.push_back(*target);
    QVariantList rows;
    for (const auto id : documents) {
        auto *session = m_files->session(id);
        if (!session || (m_workspace.reference() && *m_workspace.reference() == id))
            continue;
        logRegexErrors(*m_log, misspellRuleList(), true); // SeekOnTab compiles per tab
        application::ReplacerMatchErrors matchErrors;
        const auto finds = application::findErrors(*session, misspellRuleList(), replacerScope(scope), &matchErrors);
        logMatchErrors(*m_log, matchErrors);
        if (finds.empty())
            continue;
        // The header is the tab's SubsPath ("" for an Untitled Document).
        const auto destination = m_files->destination(id);
        rows << QVariantMap{{QStringLiteral("header"), true},
                            {QStringLiteral("text"), destination ? QDir::toNativeSeparators(QString::fromStdString(destination->value))
                                                                 : QString()}};
        for (const auto &find : finds) {
            rows << QVariantMap{{QStringLiteral("header"), false},
                                {QStringLiteral("find"), static_cast<int>(m_misspellFinds.size())},
                                {QStringLiteral("line"), find.lineNumber},
                                {QStringLiteral("text"), fromUtf16(find.text)},
                                {QStringLiteral("position"), static_cast<int>(find.position)},
                                {QStringLiteral("length"), static_cast<int>(find.length)}};
            m_misspellFinds.emplace_back(id, find);
        }
    }
    return rows;
}

void Application::replaceMisspells(const QVariantMap &scope, bool allTabs)
{
    std::vector<application::DocumentId> documents;
    if (allTabs)
        documents = m_workspace.documents();
    else if (const auto target = m_workspace.editingTarget())
        documents.push_back(*target);
    for (const auto id : documents)
        if (auto *session = m_files->session(id); session && !(m_workspace.reference() && *m_workspace.reference() == id)) {
            logRegexErrors(*m_log, misspellRuleList(), true); // ReplaceOnTab compiles per tab
            application::ReplacerMatchErrors matchErrors;
            (void)application::replaceErrors(*session, misspellRuleList(), replacerScope(scope), replacerCase(true),
                                             &matchErrors);
            logMatchErrors(*m_log, matchErrors);
        }
    m_editor->reloadFromSession();
    refreshViews();
}

void Application::replaceMisspellFinds(const QVariantList &chosen)
{
    // ReplaceChecked: every rule is compiled first (and logged when it does
    // not compile); the checked finds in list order, one run per Document;
    // finds in a Document no longer open are skipped.
    logRegexErrors(*m_log, misspellRuleList(), false);
    std::vector<std::size_t> indices;
    for (const auto &v : chosen)
        if (const int i = v.toInt(); i >= 0 && static_cast<std::size_t>(i) < m_misspellFinds.size())
            indices.push_back(static_cast<std::size_t>(i));
    std::ranges::sort(indices);
    for (std::size_t at = 0; at < indices.size();) {
        const auto document = m_misspellFinds[indices[at]].first;
        std::vector<application::ReplacerFind> finds;
        for (; at < indices.size() && m_misspellFinds[indices[at]].first == document; ++at)
            finds.push_back(m_misspellFinds[indices[at]].second);
        auto *session = m_files->session(document);
        if (!session)
            continue;
        const auto result = application::replaceFinds(*session, misspellRuleList(), finds, replacerCase(true));
        if (!result)
            continue;
        for (const auto &problem : result->problems) {
            using K = application::ReplacerProblem::Kind;
            if (problem.kind == K::Edited)
                m_log->log(tr("Line %1 cannot be replaced,\ncause it was edited.").arg(problem.lineNumber));
            else if (problem.kind == K::MatchError)
                m_log->log(regexMatchError(problem.found));
            else
                m_log->log(tr("Cannot replace \"%1\" to \"%2\", with rule \"%3\" in line %4.")
                               .arg(fromUtf16(problem.found), fromUtf8(problem.replace), fromUtf8(problem.find))
                               .arg(problem.lineNumber));
        }
    }
    m_editor->reloadFromSession();
    refreshViews();
}

void Application::showMisspellFind(int index)
{
    // ShowResult: that tab, the Line alone selected, the find selected in the editor.
    if (index < 0 || static_cast<std::size_t>(index) >= m_misspellFinds.size())
        return;
    const auto &[document, find] = m_misspellFinds[static_cast<std::size_t>(index)];
    auto *session = m_files->session(document);
    if (!session || find.row >= session->document().lines().size())
        return;
    if (m_workspace.editingTarget() != document) {
        m_workspace.setEditingTarget(document);
        refreshViews();
    }
    const auto &record = *session->document().lines()[find.row];
    const auto line = record.id;
    if (!applySelection(application::Selection{line, {line}, line, {}}))
        return;
    // GetEditor(): the Original field in translation mode when the Line has
    // no translation (the text that was searched).
    const int role = m_editor->translationMode() && !record.translation.empty() ? 1 : 0;
    m_editor->selectInField(static_cast<int>(find.position), static_cast<int>(find.position + find.length), role);
}

application::SpellChecker *Application::spellChecker()
{
    if (!m_spellChecker)
        return nullptr;
    if (!m_spellingStarted) {
        m_spellingStarted = true;
        if (m_settings->boolean("editor.spellchecker")) {
            const auto status = m_spellChecker->initialize(dictionaryLanguage().toStdU16String());
            if (status != application::SpellChecker::Status::Ready) {
                // Legacy turns SPELLCHECKER_ON off and says why. The Line
                // editor's switch stays: with no dictionary it marks brackets only.
                m_settings->set("editor.spellchecker", false);
                const QString message =
                    status == application::SpellChecker::Status::NoDictionary
                        // Legacy names "<settings folder>\Dictionary"; the path
                        // takes the platform's separators.
                        ? tr("No dictionary files were found in the \"%1\" folder.\nSpell checking will be disabled")
                              .arg(QDir::toNativeSeparators(QFileInfo(m_dictionaryDir).absoluteFilePath()))
                        : tr("Failed to initialize spell checker.");
                QMetaObject::invokeMethod(this, [this, message] {
                    emit spellingNotice(message);
                    emit spellingChanged();
                }, Qt::QueuedConnection);
            }
        }
    }
    return m_spellChecker.get();
}

void Application::restartSpellChecker()
{
    // SpellChecker::Destroy: the next use creates it again.
    if (m_spellChecker)
        m_spellChecker->clear();
    m_spellingStarted = false;
}

void Application::spellingRefresh()
{
    // EditBox::ClearErrs: the Grid's and the editor's marks are checked again.
    refreshViews();
    emit spellingChanged();
}

bool Application::spellingOn() const
{
    return m_settings->boolean("editor.spellchecker");
}

QString Application::dictionaryLanguage() const
{
    return m_settings->text("editor.dictionaryLanguage");
}

bool Application::suggestionsOnDoubleClick() const
{
    return m_settings->boolean("editor.suggestionsOnDoubleClick");
}

// The editor menu's "Spellchecker" (TextEditor::OnAccelerator): the
// editor's switch and SPELLCHECKER_ON, then ClearErrs(true, on).
void Application::setSpellingOn(bool on)
{
    if (on == m_spellingOn && on == spellingOn())
        return;
    m_spellingOn = on;
    m_settings->set("editor.spellchecker", on);
    spellingRefresh();
}

// A language from the editor menu: DICTIONARY_LANGUAGE, SpellChecker::Destroy
// and ClearErrs, also for the language already chosen.
void Application::setDictionaryLanguage(const QString &symbol)
{
    m_settings->set("editor.dictionaryLanguage", symbol);
    restartSpellChecker();
    spellingRefresh();
}

void Application::setSuggestionsOnDoubleClick(bool on)
{
    if (on == suggestionsOnDoubleClick())
        return;
    m_settings->set("editor.suggestionsOnDoubleClick", on);
    emit spellingChanged();
}

QVariantList Application::dictionaries() const
{
    QVariantList out;
    if (!m_spellChecker)
        return out;
    for (const auto &symbol : application::availableDictionaries(m_spellChecker->folders()))
        out << QVariantMap{{QStringLiteral("symbol"), qstr(symbol)}, {QStringLiteral("name"), ui::dictionaryName(qstr(symbol))}};
    return out;
}

QString Application::dictionaryName(const QString &symbol) const
{
    return ui::dictionaryName(symbol);
}

int Application::spellingRole() const
{
    const auto *session = targetSession();
    return session && application::spellsTranslation(session->document()) ? 1 : 0;
}

std::optional<std::u16string> Application::editorRaw(int role) const
{
    const auto *session = targetSession();
    if (!session || !session->selection().active)
        return std::nullopt;
    const auto active = *session->selection().active;
    std::optional<core::LineRecord> record;
    if (session->draftLine() == active)
        record = session->draftRecord();
    else
        for (const auto *line : session->document().lines())
            if (line->id == active)
                record = *line;
    if (!record)
        return std::nullopt;
    return core::toUtf16(role == 1 ? record->translation : record->text);
}

QVariantList Application::editorSpellingMarks(int role)
{
    QVariantList out;
    if (!m_spellingOn || role != spellingRole())
        return out;
    const auto raw = editorRaw(role);
    if (!raw || raw->empty())
        return out;
    const auto format = targetSession()->document().format();
    auto *checker = spellChecker();
    const auto marks = checker ? application::editorMarks(*raw, format, *checker, m_spellingText)
                               : core::legacy::checkTextAndBrackets(*raw, format, m_spellingText.segment, {});
    const bool hidden = !m_editor->showTags();
    const core::Projection projection = hidden ? core::project(*raw) : core::Projection{};
    for (std::size_t i = 0; i + 1 < marks.errors.size(); i += 2) {
        if (!hidden) {
            out << marks.errors[i] << marks.errors[i + 1] + 1;
        } else if (const auto range = displayRange(projection, marks.errors[i], marks.errors[i + 1])) {
            out << range->first << range->second;
        }
    }
    return out;
}

QVariantMap Application::editorMisspellAt(int role, int position)
{
    if (!m_spellingOn || role != spellingRole())
        return {};
    const auto raw = editorRaw(role);
    auto *checker = spellChecker();
    if (!raw || !checker)
        return {};
    const auto marks = application::editorMarks(*raw, targetSession()->document().format(), *checker, m_spellingText);
    int at = position;
    if (!m_editor->showTags())
        at = static_cast<int>(core::rawOffset(core::project(*raw), static_cast<std::size_t>(std::max(position, 0)), true));
    const auto found = application::misspellAt(marks, at);
    if (!found)
        return {};
    const auto &misspell = marks.misspells[*found];
    QStringList suggestions;
    for (const auto &s : checker->suggestions(misspell.word))
        suggestions << qstr(s);
    return {{QStringLiteral("word"), qstr(misspell.word)},
            {QStringLiteral("start"), misspell.start},
            {QStringLiteral("end"), misspell.end},
            {QStringLiteral("suggestions"), suggestions}};
}

bool Application::replaceEditorMisspell(int role, int position, const QString &replacement)
{
    auto *session = targetSession();
    const QVariantMap found = editorMisspellAt(role, position);
    if (!session || found.isEmpty() || !session->selection().active)
        return false;
    const core::legacy::Misspell misspell{found.value(QStringLiteral("word")).toString().toStdU16String(),
                                          found.value(QStringLiteral("start")).toInt(),
                                          found.value(QStringLiteral("end")).toInt()};
    const auto caret = application::replaceInEditor(*session, *session->selection().active, role == 1, misspell,
                                                    replacement.toStdU16String());
    if (!caret)
        return false;
    m_editor->reloadFromSession();
    // TextEditor::SetSelection(newto, newto).
    int shown = *caret;
    if (!m_editor->showTags())
        if (const auto raw = editorRaw(role))
            shown = static_cast<int>(core::displayOffset(core::project(*raw), static_cast<std::size_t>(*caret)));
    m_editor->selectInField(shown, shown);
    spellingRefresh();
    return true;
}

bool Application::addEditorWord(const QString &word)
{
    auto *checker = spellChecker();
    if (!checker || !checker->addWord(word.toStdU16String()))
        return false;
    spellingRefresh();
    return true;
}

QVariantMap Application::spellCheckState(bool changed, const QString &problem)
{
    if (problem.isEmpty() && m_spellWalk && m_spellWalk->refused())
        return spellCheckState(changed, tr("The edited line cannot be committed."));
    const auto *current = m_spellWalk && m_spellWalk->current() ? &*m_spellWalk->current() : nullptr;
    QStringList suggestions;
    if (current)
        for (const auto &s : current->suggestions)
            suggestions << qstr(s);
    return {{QStringLiteral("found"), current != nullptr},
            {QStringLiteral("word"), current ? qstr(current->word) : QString()},
            {QStringLiteral("suggestions"), suggestions},
            {QStringLiteral("replacement"), suggestions.isEmpty() ? QString() : suggestions.front()},
            {QStringLiteral("changed"), changed},
            {QStringLiteral("problem"), problem}};
}

void Application::showSpellCheckWord()
{
    m_editor->reloadFromSession();
    const auto *current = m_spellWalk ? &m_spellWalk->current() : nullptr;
    if (current && *current) {
        m_editor->showLine((*current)->line.value);
        // The editor selects the word (TextEdit->SetSelection(posStart, posEnd + 1)).
        int from = (*current)->start, to = (*current)->end + 1;
        if (!m_editor->showTags())
            if (const auto raw = editorRaw(spellingRole()))
                if (const auto range = displayRange(core::project(*raw), (*current)->start, (*current)->end)) {
                    from = range->first;
                    to = range->second;
                }
        m_editor->selectInField(from, to);
    }
    spellingRefresh();
}

QVariantMap Application::openSpellChecker(const QVariantMap &options)
{
    auto *session = targetSession();
    auto *checker = spellChecker();
    m_spellWalk.reset();
    if (!session || !checker)
        return spellCheckState(false);
    m_editor->commit(); // the editor is left for the window: its draft commits
    m_spellWalk = std::make_unique<application::SpellCheckWalk>(*checker, m_spellingText);
    m_spellWalkDocument = m_workspace.editingTarget();
    m_spellWalk->next(*session, walkOptions(options));
    showSpellCheckWord();
    return spellCheckState(false);
}

std::optional<QVariantMap> Application::restartStaleSpellCheck(application::EditSession &session,
                                                               const QVariantMap &options)
{
    // The accepted draft policy: leaving the editor for the window commits
    // its draft. When that (or anything else) changed the active Line or its
    // text since the window's word was found, legacy's OnActive would start
    // again; the action does that instead of acting on a word or offsets the
    // window no longer matches.
    m_editor->commit();
    const bool other = m_workspace.editingTarget() != m_spellWalkDocument;
    if (!m_spellWalk->current() || !m_spellWalk->stale(session, other))
        return std::nullopt;
    m_spellWalkDocument = m_workspace.editingTarget();
    m_spellWalk->restart(session, walkOptions(options));
    showSpellCheckWord();
    QVariantMap state = spellCheckState(false);
    state.insert(QStringLiteral("restarted"), true);
    return state;
}

QVariantMap Application::spellCheckerActivated(const QVariantMap &options)
{
    auto *session = targetSession();
    if (!m_spellWalk || !session)
        return spellCheckState(false);
    m_editor->commit();
    const bool other = m_workspace.editingTarget() != m_spellWalkDocument;
    m_spellWalkDocument = m_workspace.editingTarget();
    QVariantMap state;
    if (m_spellWalk->activated(*session, walkOptions(options), other)) {
        showSpellCheckWord();
        state = spellCheckState(false);
        state.insert(QStringLiteral("restarted"), true);
    } else {
        state = spellCheckState(false);
        state.insert(QStringLiteral("restarted"), false);
    }
    return state;
}

namespace {

QString refusalText(application::CommandRefusal refusal)
{
    switch (refusal) {
    case application::CommandRefusal::Protected: return QObject::tr("The reference is protected (read-only).");
    case application::CommandRefusal::ReadOnly: return QObject::tr("A macro is running on this document.");
    case application::CommandRefusal::InvalidDraft: return QObject::tr("The edited line cannot be committed.");
    default: return QObject::tr("The change was refused.");
    }
}

QVariantMap unchangedState()
{
    return {{QStringLiteral("unchanged"), true}};
}

} // namespace

QVariantMap Application::spellCheckerReplace(const QString &replacement, const QVariantMap &options)
{
    auto *session = targetSession();
    if (!m_spellWalk || !session)
        return unchangedState();
    if (auto restarted = restartStaleSpellCheck(*session, options))
        return *restarted;
    const auto result = m_spellWalk->replace(*session, replacement.toStdU16String(), walkOptions(options));
    if (!result)
        return spellCheckState(false, refusalText(result.error()));
    // SpellCheckerDialog::Replace returns before touching the window.
    if (*result == application::SpellCheckWalk::Result::Unchanged)
        return unchangedState();
    showSpellCheckWord();
    return spellCheckState(true);
}

QVariantMap Application::spellCheckerReplaceAll(const QString &misspell, const QString &replacement,
                                                const QVariantMap &options)
{
    auto *session = targetSession();
    if (!m_spellWalk || !session)
        return unchangedState();
    if (auto restarted = restartStaleSpellCheck(*session, options))
        return *restarted;
    const auto result = m_spellWalk->replaceAll(*session, misspell.toStdU16String(), replacement.toStdU16String(),
                                                walkOptions(options));
    if (!result)
        return spellCheckState(false, refusalText(result.error()));
    if (*result == application::SpellCheckWalk::Result::Unchanged)
        return unchangedState();
    showSpellCheckWord();
    return spellCheckState(*result == application::SpellCheckWalk::Result::Replaced);
}

QVariantMap Application::spellCheckerIgnore(const QVariantMap &options)
{
    auto *session = targetSession();
    if (!m_spellWalk || !session)
        return spellCheckState(false);
    if (auto restarted = restartStaleSpellCheck(*session, options))
        return *restarted;
    m_spellWalk->ignore(*session, walkOptions(options));
    showSpellCheckWord();
    return spellCheckState(false);
}

QVariantMap Application::spellCheckerIgnoreAll(const QString &word, const QVariantMap &options)
{
    auto *session = targetSession();
    if (!m_spellWalk || !session)
        return spellCheckState(false);
    if (auto restarted = restartStaleSpellCheck(*session, options))
        return *restarted;
    m_spellWalk->ignoreAll(*session, word.toStdU16String(), walkOptions(options));
    showSpellCheckWord();
    return spellCheckState(false);
}

QVariantMap Application::spellCheckerAddWord(const QString &word, const QVariantMap &options)
{
    auto *session = targetSession();
    if (!m_spellWalk || !session)
        return spellCheckState(false);
    if (auto restarted = restartStaleSpellCheck(*session, options))
        return *restarted;
    m_spellWalk->addWord(*session, word.toStdU16String(), walkOptions(options));
    showSpellCheckWord();
    return spellCheckState(false);
}

void Application::closeSpellChecker()
{
    m_spellWalk.reset();
    m_spellWalkDocument.reset();
}

QStringList Application::addedDictionaryWords() const
{
    QStringList out;
    if (m_spellChecker)
        for (const auto &word : m_spellChecker->addedWords())
            out << qstr(word);
    return out;
}

bool Application::removeDictionaryWords(const QStringList &words)
{
    auto *checker = spellChecker();
    std::vector<std::u16string> list;
    for (const QString &w : words)
        list.push_back(w.toStdU16String());
    if (!checker || !checker->removeWords(list))
        return false;
    spellingRefresh();
    return true;
}

void Application::createFindReplace()
{
    // wxString::Lower, one unit at a time, is towlower: in the "C" locale
    // legacy keeps with an English interface it changes A-Z only (captured on
    // Linux: a plain "łódź" never finds "ŁÓDŹ"); with another language
    // wxLocale set the process locale and every letter folds.
    m_find = std::make_unique<application::FindReplace>(*m_findHost, unicodeFold);
}

QVariantMap Application::openFindReplace(int tab)
{
    // FindReplaceDialog's FindReplace reads the four recent tables when the
    // dialog is created (cut to 20), and keeps them until it is destroyed.
    if (!m_findOpened) {
        const auto list = [this](const char *id) {
            std::vector<std::u8string> out;
            for (const QString &entry : m_settings->list(id))
                out.push_back(toU8(entry));
            return out;
        };
        m_find->setRecent({list("find.recentFinds"), list("find.recentReplacements"), list("findInSubs.recentFilters"),
                           list("findInSubs.recentPaths")});
        m_findOpened = true;
    }
    return findReplaceSettings(tab);
}

QVariantMap Application::findReplaceSettings(int tab) const
{
    // TabWindow's constructor and SetValues read FIND_REPLACE_OPTIONS and
    // FIND_REPLACE_STYLES from the options.
    auto s = application::findReplaceFromOptions(m_settings->integer("find.options"));
    s.tab = static_cast<application::FindReplaceSettings::Tab>(std::clamp(tab, 0, 2));
    findFirstEntries(s, m_find->recent());
    s.styles = toU8(m_settings->text("find.styles"));
    return findMap(s, m_find->recent());
}

void Application::saveFindReplaceSettings(const QVariantMap &settings)
{
    // TabWindow::SaveValues: the Find in subtitles tab keeps the stored
    // option's bits from 512 up; the styles of the other tabs.
    const auto s = findSettings(settings);
    m_settings->set("find.options", application::findReplaceOptions(s, m_settings->integer("find.options")));
    if (s.tab != application::FindReplaceSettings::Tab::FindInFiles)
        m_settings->set("find.styles", findQs(s.styles));
}

QVariantMap Application::switchFindReplaceTab(const QVariantMap &settings, int tab)
{
    saveFindReplaceSettings(settings);
    auto next = application::findReplaceSetValues(m_settings->integer("find.options"), findSettings(settings));
    next.tab = static_cast<application::FindReplaceSettings::Tab>(std::clamp(tab, 0, 2));
    findFirstEntries(next, m_find->recent());
    next.styles = toU8(m_settings->text("find.styles"));
    return findMap(next, m_find->recent());
}

void Application::saveFindRecent(application::FindReplaceSettings::Tab tab)
{
    // FindReplace::AddRecent's SetTable calls, from its own lists (which
    // survive Set default while the tool exists).
    using Tab = application::FindReplaceSettings::Tab;
    const auto &r = m_find->recent();
    m_settings->set("find.recentFinds", findList(r.finds));
    if (tab != Tab::Find)
        m_settings->set("find.recentReplacements", findList(r.replacements));
    if (tab == Tab::FindInFiles) {
        m_settings->set("findInSubs.recentFilters", findList(r.filters));
        m_settings->set("findInSubs.recentPaths", findList(r.paths));
    }
}

void Application::runFindReplace(const QString &action, const QVariantMap &settings)
{
    const auto s = findSettings(settings);
    if (action == QLatin1String("find"))
        m_find->find(s);
    else if (action == QLatin1String("findAllCurrent"))
        m_find->findAllInCurrent(s);
    else if (action == QLatin1String("findAllTabs"))
        m_find->findInAllOpened(s);
    else if (action == QLatin1String("replace"))
        m_find->replace(s);
    else if (action == QLatin1String("replaceAll"))
        m_find->replaceAll(s);
    else if (action == QLatin1String("replaceAllTabs"))
        m_find->replaceInAllOpened(s);
    else if (action == QLatin1String("findInFiles"))
        m_find->findInFiles(s);
    else if (action == QLatin1String("replaceInFiles"))
        m_find->replaceInFiles(s);
}

void Application::findFinished()
{
    emit findResultsChanged();
    emit findBusyChanged();
    emit findFinished(findMap(m_find->window(), m_find->recent()));
}

void Application::findNext()
{
    m_find->findNext();
}

bool Application::findBusy() const
{
    return m_find && m_find->busy();
}

void Application::answerFindQuestion(int id, int answer)
{
    const auto it = m_findAnswers.find(id);
    if (it == m_findAnswers.end())
        return;
    auto resume = std::move(it->second);
    m_findAnswers.erase(it);
    resume(static_cast<application::FindAnswer>(std::clamp(answer, 0, 3)));
    emit findBusyChanged();
}

void Application::setFindQuestionHandler(std::function<int(int, const QString &)> handler)
{
    m_findQuestionHandler = std::move(handler);
}

QString Application::findReplaceActivated(const QString &find)
{
    auto *session = targetSession();
    if (!session || !m_editor->hasLine())
        return find;
    // TextEdit first (the translation in translation mode), then TextEditOrig.
    const bool tl = application::translationMode(*session);
    std::vector<int> roles{tl ? 1 : 0};
    if (tl)
        roles.push_back(0);
    for (const int role : roles) {
        const auto [from, to] = m_editor->fieldSelectionOf(role);
        if (from >= to)
            continue;
        // The last match, compared where the editor shows it (hidden tags
        // move display offsets; legacy compared raw offsets of raw text).
        if (std::pair(from, to) == m_editor->displaySpan(role, m_find->lastStart(), m_find->lastEnd()))
            return find;
        const QString shown = role == 1 ? m_editor->translationText() : m_editor->text();
        const QString selected = shown.mid(from, to - from);
        m_find->selectionAdopted();
        // OnActivate's case-insensitive comparison (U1-unicode-case).
        const auto lower = [](const QString &t) { return t.toCaseFolded(); };
        return lower(selected) != lower(find) ? selected : find;
    }
    return find;
}

void Application::resetFindReplace()
{
    m_find->reset();
}

QVariantList Application::findResults() const
{
    QVariantList rows;
    for (const auto &r : m_find->results()) {
        const QString text = QString(reinterpret_cast<const QChar *>(r.text.data()), static_cast<qsizetype>(r.text.size()));
        QVariantMap row{{QStringLiteral("header"), r.header},
                        {QStringLiteral("text"), text},
                        {QStringLiteral("checked"), r.checked},
                        {QStringLiteral("visible"), r.visible}};
        if (!r.header) {
            // "Line %i: " and the text with the match marked (SeekResults::OnPaint).
            row.insert(QStringLiteral("line"), tr("Line %1: ").arg(r.idLine));
            row.insert(QStringLiteral("before"), text.left(r.start));
            row.insert(QStringLiteral("match"), text.mid(r.start, r.length));
            row.insert(QStringLiteral("after"), text.mid(r.start + r.length));
        }
        rows << row;
    }
    return rows;
}

bool Application::findResultsShown() const
{
    return m_find->resultsShown();
}

bool Application::canReplaceFindResults() const
{
    return m_find->canReplaceChecked();
}

void Application::checkFindResults(bool check)
{
    m_find->checkAll(check);
    emit findResultsChanged();
}

void Application::toggleFindResult(int row)
{
    m_find->toggleChecked(static_cast<std::size_t>(std::max(row, 0)));
    emit findResultsChanged();
}

void Application::toggleFindGroup(int row)
{
    m_find->toggleGroup(static_cast<std::size_t>(std::max(row, 0)));
    emit findResultsChanged();
}

void Application::showFindResult(int row)
{
    m_find->showResult(static_cast<std::size_t>(std::max(row, 0)));
}

void Application::replaceFindResults(const QString &replacement)
{
    m_find->replaceChecked(toU8(replacement));
    emit findResultsChanged();
}

void Application::checkResolution()
{
    auto *session = targetSession();
    const auto &video = m_video->session();
    if (!askForBadResolution() || !session || targetUntitled() ||
        session->document().format() != core::SubtitleFormat::Ass ||
        video.state() != application::VideoSession::State::Ready || !video.lastFrame())
        return;
    const auto subs = application::scriptResolution(session->document());
    const int width = video.lastFrame()->width, height = video.lastFrame()->height;
    if (subs.width == width && subs.height == height)
        return;
    emit resolutionMismatch({{QStringLiteral("subsWidth"), subs.width},
                             {QStringLiteral("subsHeight"), subs.height},
                             {QStringLiteral("videoWidth"), width},
                             {QStringLiteral("videoHeight"), height}});
}

QVariantMap Application::resampleValues() const
{
    auto *session = targetSession();
    if (!session || (session->document().format() != core::SubtitleFormat::Ass &&
                     session->document().format() != core::SubtitleFormat::PlainText))
        return {};
    const auto subs = application::scriptResolution(session->document());
    application::Resolution video = subs;
    if (m_video->session().state() == application::VideoSession::State::Ready && m_video->session().lastFrame())
        video = {m_video->session().lastFrame()->width, m_video->session().lastFrame()->height};
    return {{QStringLiteral("subsWidth"), subs.width},
            {QStringLiteral("subsHeight"), subs.height},
            {QStringLiteral("videoWidth"), video.width},
            {QStringLiteral("videoHeight"), video.height}};
}

bool Application::resample(int subsWidth, int subsHeight, int width, int height, bool stretch)
{
    auto *session = targetSession();
    if (!session || (subsWidth == width && subsHeight == height) || subsWidth < 1 || subsHeight < 1)
        return false;
    const auto warn = [this](int row, const std::u16string &value, const std::u16string &tag) {
        m_log->log(tr("In line %1, value '%2' cannot be scaled\nin tag '%3'")
                       .arg(row)
                       .arg(QString::fromStdU16String(value), QString::fromStdU16String(tag)));
    };
    const bool done = application::changeResolution(*session, {subsWidth, subsHeight}, {width, height}, true, stretch, warn)
                          .has_value();
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

bool Application::matchVideoResolution(int option)
{
    auto *session = targetSession();
    const auto values = resampleValues();
    if (!session || values.isEmpty())
        return false;
    const application::Resolution subs{values.value(QStringLiteral("subsWidth")).toInt(), values.value(QStringLiteral("subsHeight")).toInt()};
    const application::Resolution video{values.value(QStringLiteral("videoWidth")).toInt(), values.value(QStringLiteral("videoHeight")).toInt()};
    const auto warn = [this](int row, const std::u16string &value, const std::u16string &tag) {
        m_log->log(tr("In line %1, value '%2' cannot be scaled\nin tag '%3'")
                       .arg(row)
                       .arg(QString::fromStdU16String(value), QString::fromStdU16String(tag)));
    };
    const bool done = application::changeResolution(*session, subs, video, option != 0, option == 2, warn).has_value();
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

void Application::setAskForBadResolution(bool on)
{
    m_settings->set("video.dontAskForBadResolution", !on); // settingChanged announces it
}

namespace {

// The conversion dialog's options and their CONVERT_* settings.
constexpr std::pair<const char *, const char *> kConversionOptions[] = {
    {"fps", "convert.fps"},
    {"fpsFromVideo", "convert.fpsFromVideo"},
    {"style", "convert.style"},
    {"styleCatalog", "convert.styleCatalog"},
    {"newEndTimes", "convert.newEndTimes"},
    {"timePerCharacter", "convert.timePerCharacter"},
    {"prefix", "convert.assTagsToInsertInLine"},
    {"resolutionWidth", "convert.resolutionWidth"},
    {"resolutionHeight", "convert.resolutionHeight"},
};

std::optional<core::SubtitleFormat> formatNamed(const QString &name)
{
    if (name == QLatin1String("ass"))
        return core::SubtitleFormat::Ass;
    if (name == QLatin1String("srt"))
        return core::SubtitleFormat::Srt;
    if (name == QLatin1String("tmp"))
        return core::SubtitleFormat::TMPlayer;
    if (name == QLatin1String("mdvd"))
        return core::SubtitleFormat::MicroDvd;
    if (name == QLatin1String("mpl2"))
        return core::SubtitleFormat::Mpl2;
    return std::nullopt;
}

} // namespace

QVariantMap Application::conversionOptions() const
{
    QVariantMap out;
    for (const auto &[key, setting] : kConversionOptions) {
        const QVariant v = m_settings->value(QLatin1String(setting));
        out.insert(QLatin1String(key), v.typeId() == QMetaType::LongLong ? QVariant(v.toInt()) : v);
    }
    return out;
}

void Application::setConversionOptions(const QVariantMap &options)
{
    for (const auto &[key, setting] : kConversionOptions)
        if (options.contains(QLatin1String(key)))
            m_settings->set(setting, options.value(QLatin1String(key)));
    m_conversionPlan.reset(); // a changed option invalidates the preview (C02)
}

QString Application::activeLineStyle() const
{
    auto *session = targetSession();
    if (!session || !session->selection().active)
        return {};
    for (const auto *line : session->document().lines())
        if (line->id == *session->selection().active)
            return QString::fromUtf8(reinterpret_cast<const char *>(line->style.data()), qsizetype(line->style.size()));
    return {};
}

QStringList Application::conversionTargets() const
{
    auto *session = targetSession();
    // Legacy converts nothing while translation mode is on.
    if (!session || session->document().scriptInfo(u8"TLMode") == std::optional<std::u8string>(u8"Yes"))
        return {};
    const auto format = session->document().format();
    QStringList out;
    // Legacy enables "Convert to ASS" only for a format after ASS.
    if (format != core::SubtitleFormat::Ass && format != core::SubtitleFormat::PlainText)
        out << QStringLiteral("ass");
    if (format != core::SubtitleFormat::Srt)
        out << QStringLiteral("srt");
    if (format != core::SubtitleFormat::MicroDvd)
        out << QStringLiteral("mdvd");
    if (format != core::SubtitleFormat::Mpl2)
        out << QStringLiteral("mpl2");
    if (format != core::SubtitleFormat::TMPlayer)
        out << QStringLiteral("tmp");
    return out;
}

QVariantMap Application::previewConversion(const QString &targetName)
{
    m_conversionPlan.reset();
    auto *session = targetSession();
    const auto target = formatNamed(targetName);
    if (!session || !target || !conversionTargets().contains(targetName))
        return {{QStringLiteral("ok"), false}, {QStringLiteral("problem"), tr("This Document can't be converted to that format.")}};
    const QVariantMap o = conversionOptions();
    core::ConversionOptions options;
    const auto &video = m_video->session();
    const bool hasVideo = video.state() == application::VideoSession::State::Ready;
    const float frameMs = hasVideo ? video.legacyTimebase().frameDuration() : 0.f;
    // CONVERT_FPS_FROM_VIDEO takes the open video's rate.
    options.fps = o.value(QStringLiteral("fpsFromVideo")).toBool() && hasVideo && frameMs > 0.f
                      ? 1000.0 / frameMs
                      : o.value(QStringLiteral("fps")).toString().toDouble();
    if (options.fps < 1)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("problem"), tr("Invalid FPS. Correct it in options and try again.")}};
    options.videoFps = hasVideo && frameMs > 0.f ? 1000.0 / frameMs : 23.976;
    options.prefix = utf8(o.value(QStringLiteral("prefix")).toString());
    options.newEndTimes = o.value(QStringLiteral("newEndTimes")).toBool();
    options.timePerCharacter = o.value(QStringLiteral("timePerCharacter")).toInt();
    options.resolutionWidth = utf8(o.value(QStringLiteral("resolutionWidth")).toString());
    options.resolutionHeight = utf8(o.value(QStringLiteral("resolutionHeight")).toString());
    // config::GetConversionStyle: CONVERT_STYLE from the CONVERT_STYLE_CATALOG
    // catalog, else legacy Styles() with that name.
    const std::u8string styleName = utf8(o.value(QStringLiteral("style")).toString());
    if (const auto found = m_styleManager->catalogStore().find(utf8(o.value(QStringLiteral("styleCatalog")).toString()), styleName))
        options.style = *found;
    else
        options.style = application::defaultStyle(styleName);
    const QCollator collator;
    auto result = core::convertDocument(session->document(), *target, options, [&](std::u8string_view a, std::u8string_view b) {
        return collator.compare(QString::fromUtf8(reinterpret_cast<const char *>(a.data()), qsizetype(a.size())),
                                QString::fromUtf8(reinterpret_cast<const char *>(b.data()), qsizetype(b.size())));
    });
    if (!result)
        return {{QStringLiteral("ok"), false}, {QStringLiteral("problem"), tr("This Document can't be converted to that format.")}};
    const auto &r = result->report;
    QStringList losses;
    if (r.headerDropped)
        losses << tr("The script properties and styles are removed.");
    if (r.commentsRemoved)
        losses << tr("%n comment line(s) removed.", nullptr, r.commentsRemoved);
    if (r.drawingsCleared)
        losses << tr("%n drawing(s) removed.", nullptr, r.drawingsCleared);
    if (r.emptyRemoved)
        losses << tr("%n line(s) left without text removed.", nullptr, r.emptyRemoved);
    if (r.duplicatesRemoved)
        losses << tr("%n duplicate line(s) removed.", nullptr, r.duplicatesRemoved);
    if (r.reordered)
        losses << tr("Lines are sorted by time.");
    if (r.textChanged)
        losses << tr("Formatting tags converted or removed in %n line(s).", nullptr, r.textChanged);
    if (r.fieldsDropped)
        losses << (*target == core::SubtitleFormat::Ass
                       ? tr("%n line(s) get the conversion style \"%1\".", nullptr, r.fieldsDropped).arg(o.value(QStringLiteral("style")).toString())
                       : tr("Layer, style, actor, margins or effect removed from %n line(s).", nullptr, r.fieldsDropped));
    if (r.markersDropped)
        losses << tr("Bookmarks, hidden lines and groups removed from %n line(s).", nullptr, r.markersDropped);
    if (r.timesChanged)
        losses << tr("Times changed (rounding, frames or new end times) in %n line(s).", nullptr, r.timesChanged);
    m_conversionPlan = std::make_unique<ConversionPlan>(
        ConversionPlan{m_workspace.editingTarget()->value, session->revision(), o, std::move(result->document)});
    return {{QStringLiteral("ok"), true}, {QStringLiteral("losses"), losses}};
}

bool Application::acceptConversion()
{
    auto plan = std::move(m_conversionPlan);
    auto *session = targetSession();
    const auto target = m_workspace.editingTarget();
    // A changed Document or options invalidates the plan (C02).
    if (!plan || !session || !target || target->value != plan->document || session->revision() != plan->revision ||
        conversionOptions() != plan->options)
        return false;
    std::set<core::LineId> touches;
    for (const auto *line : session->document().lines())
        touches.insert(line->id);
    application::Command command{"Subtitles conversion", session->revision(), touches,
                                 [&](core::Document &d) {
                                     d = plan->converted;
                                     return true;
                                 }};
    command.keepGroups = false; // groups are among the reported losses
    if (!session->run(command))
        return false;
    // The active Line stays when it survived; otherwise the first Line.
    const auto lines = session->document().lines();
    application::Selection selection = session->selection();
    std::erase_if(selection.selected, [&](core::LineId id) {
        return std::ranges::none_of(lines, [&](const auto *l) { return l->id == id; });
    });
    if (!selection.active || std::ranges::none_of(lines, [&](const auto *l) { return l->id == *selection.active; }))
        selection.active = lines.empty() ? std::nullopt : std::optional(lines.front()->id);
    selection.anchor = selection.active;
    selection.extent.reset();
    session->setSelection(selection);
    m_formatChanged.insert(target->value);
    m_editor->reloadFromSession();
    refreshViews();
    checkResolution();
    return true;
}

QVariantMap Application::scriptProperties()
{
    auto *session = targetSession();
    if (!session)
        return {};
    const auto format = session->document().format();
    if (format != core::SubtitleFormat::Ass && format != core::SubtitleFormat::PlainText)
        return {};
    const auto p = application::scriptProperties(session->document());
    auto q = [](const std::u8string &s) { return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), qsizetype(s.size())); };
    QStringList matrices;
    for (const auto &m : application::matrixNames())
        matrices << q(m);
    int videoWidth = 0, videoHeight = 0;
    if (const auto frame = m_video->session().lastFrame(); frame && m_video->session().state() == application::VideoSession::State::Ready) {
        videoWidth = frame->width;
        videoHeight = frame->height;
    }
    const bool link = m_settings->boolean("scriptProperties.linkResolutions");
    return {{QStringLiteral("title"), q(p.title)},
            {QStringLiteral("originalScript"), q(p.originalScript)},
            {QStringLiteral("originalTranslation"), q(p.originalTranslation)},
            {QStringLiteral("originalEditing"), q(p.originalEditing)},
            {QStringLiteral("originalTiming"), q(p.originalTiming)},
            {QStringLiteral("updatedBy"), q(p.updatedBy)},
            {QStringLiteral("playResX"), p.playResX},
            {QStringLiteral("playResY"), p.playResY},
            {QStringLiteral("layoutResX"), p.layoutResX},
            {QStringLiteral("layoutResY"), p.layoutResY},
            {QStringLiteral("matrix"), p.matrix},
            {QStringLiteral("matrices"), matrices},
            {QStringLiteral("wrapStyle"), p.wrapStyle},
            {QStringLiteral("reverseCollisions"), p.reverseCollisions},
            {QStringLiteral("scaledBorderAndShadow"), p.scaledBorderAndShadow},
            {QStringLiteral("videoWidth"), videoWidth},
            {QStringLiteral("videoHeight"), videoHeight},
            {QStringLiteral("linkResolutions"), link}};
}

bool Application::applyScriptProperties(const QVariantMap &values, const QVariantMap &edits, bool linkResolutions)
{
    auto *session = targetSession();
    if (!session)
        return false;
    m_settings->set("scriptProperties.linkResolutions", linkResolutions);
    application::ScriptProperties p;
    p.title = toU8(values.value(QStringLiteral("title")).toString());
    p.originalScript = toU8(values.value(QStringLiteral("originalScript")).toString());
    p.originalTranslation = toU8(values.value(QStringLiteral("originalTranslation")).toString());
    p.originalEditing = toU8(values.value(QStringLiteral("originalEditing")).toString());
    p.originalTiming = toU8(values.value(QStringLiteral("originalTiming")).toString());
    p.updatedBy = toU8(values.value(QStringLiteral("updatedBy")).toString());
    p.playResX = values.value(QStringLiteral("playResX")).toInt();
    p.playResY = values.value(QStringLiteral("playResY")).toInt();
    p.layoutResX = values.value(QStringLiteral("layoutResX")).toInt();
    p.layoutResY = values.value(QStringLiteral("layoutResY")).toInt();
    p.matrix = values.value(QStringLiteral("matrix")).toInt();
    p.wrapStyle = values.value(QStringLiteral("wrapStyle")).toInt();
    p.reverseCollisions = values.value(QStringLiteral("reverseCollisions")).toBool();
    p.scaledBorderAndShadow = values.value(QStringLiteral("scaledBorderAndShadow")).toBool();
    application::ScriptPropertiesEdits e;
    auto edited = [&](const char *key) { return edits.value(QLatin1String(key)).toBool(); };
    e.title = edited("title");
    e.originalScript = edited("originalScript");
    e.originalTranslation = edited("originalTranslation");
    e.originalEditing = edited("originalEditing");
    e.originalTiming = edited("originalTiming");
    e.updatedBy = edited("updatedBy");
    e.playResX = edited("playResX");
    e.playResY = edited("playResY");
    e.layoutResX = edited("layoutResX");
    e.layoutResY = edited("layoutResY");
    const bool done = application::applyScriptProperties(*session, p, e, linkResolutions).has_value();
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

bool Application::makeGroups()
{
    const auto shown = shownLines();
    return runFilter([&](application::EditSession &s) { return application::makeGroups(s, shown); });
}

bool Application::toggleGroup(qulonglong description)
{
    // R1: a group description's click is legacy FilterPartial too
    // (SubsGridWindow.cpp:1572).
    m_partialFilter = true;
    const bool done =
        runFilter([&](application::EditSession &s) { return application::toggleGroup(s, core::LineId{description}); });
    m_partialFilter = false;
    return done;
}

bool Application::renameGroup(qulonglong description, const QString &text)
{
    return runFilter([&](application::EditSession &s) {
        return application::renameGroup(s, core::LineId{description}, toU8(text));
    });
}

bool Application::removeGroup(qulonglong description)
{
    return runFilter([&](application::EditSession &s) { return application::removeGroup(s, core::LineId{description}); });
}

bool Application::selectGroup(qulonglong description)
{
    return runFilter([&](application::EditSession &s) { return application::selectGroup(s, core::LineId{description}); });
}

bool Application::addLinesToGroup(qulonglong description)
{
    return runFilter(
        [&](application::EditSession &s) { return application::addLinesToGroup(s, core::LineId{description}); });
}

bool Application::copyGroup(qulonglong description)
{
    auto *session = targetSession();
    if (!session)
        return false;
    const auto text = application::copyGroup(*session, core::LineId{description});
    if (text.empty())
        return false;
    setClipboardText(text);
    return true;
}

QString Application::groupTitle(qulonglong description) const
{
    if (auto *session = targetSession())
        for (const auto *l : session->document().lines())
            if (l->id.value == description)
                return QString::fromUtf8(reinterpret_cast<const char *>(l->text.data()), static_cast<qsizetype>(l->text.size()));
    return {};
}

bool Application::sortLines(const QString &key, bool selectedOnly)
{
    using application::SortKey;
    static const std::pair<const char *, SortKey> keys[] = {{"start", SortKey::Start}, {"end", SortKey::End},
                                                            {"style", SortKey::Style}, {"actor", SortKey::Actor},
                                                            {"effect", SortKey::Effect}, {"layer", SortKey::Layer}};
    auto *session = targetSession();
    const auto *found = std::find_if(std::begin(keys), std::end(keys),
                                     [&](const auto &k) { return key == QLatin1String(k.first); });
    if (!session || found == std::end(keys))
        return false;
    // Legacy compares with the UI locale's std::collate.
    const QCollator collator;
    const bool done = application::sortLines(*session, found->second, selectedOnly,
                                             [&](std::u8string_view a, std::u8string_view b) {
                                                 auto q = [](std::u8string_view s) {
                                                     return QString::fromUtf8(reinterpret_cast<const char *>(s.data()),
                                                                              static_cast<qsizetype>(s.size()));
                                                 };
                                                 return collator.compare(q(a), q(b));
                                             })
                          .has_value();
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

bool Application::duplicateLines()
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    // Legacy SubsGrid::OnDuplicate: GRID_DUPLICATION_DONT_CHANGE_SELECTION.
    const bool done =
        application::duplicateLines(*session, {}, m_settings->boolean("grid.duplicationDontChangeSelection")).has_value();
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

bool Application::deleteLines()
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    const bool done = application::deleteLines(*session).has_value();
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

bool Application::joinLines(const QString &kind)
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    using application::JoinKind;
    const JoinKind k = kind == QLatin1String("previous") ? JoinKind::WithPrevious
                       : kind == QLatin1String("next")   ? JoinKind::WithNext
                       : kind == QLatin1String("first")  ? JoinKind::KeepFirst
                       : kind == QLatin1String("last")   ? JoinKind::KeepLast
                                                         : JoinKind::Join;
    const bool done = application::joinLines(*session, k).has_value();
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

bool Application::swapLines()
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    const bool done = application::swapLines(*session).has_value();
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

bool Application::makeContinuous(bool withPrevious)
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    const bool done = application::makeContinuous(*session, withPrevious).has_value();
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

QVariantMap Application::qmlProperties()
{
    return {{QStringLiteral("shell"), QVariant::fromValue(m_shell.get())},
            {QStringLiteral("editor"), QVariant::fromValue(m_editor.get())},
            {QStringLiteral("video"), QVariant::fromValue(m_video.get())},
            {QStringLiteral("audio"), QVariant::fromValue(m_audio.get())},
            {QStringLiteral("automation"), QVariant::fromValue(static_cast<QObject *>(m_automation.get()))},
            {QStringLiteral("automationManager"), QVariant::fromValue(m_automation->managerController())},
            {QStringLiteral("automationDialogs"), QVariant::fromValue(m_automation->dialogs())},
            {QStringLiteral("automationPicker"), QVariant::fromValue(m_automation->picker())},
            {QStringLiteral("log"), QVariant::fromValue(m_log.get())},
            {QStringLiteral("tagButtons"), QVariant::fromValue(m_tagButtons.get())},
            {QStringLiteral("colourPicker"), QVariant::fromValue(m_colourPicker.get())},
            {QStringLiteral("workspaceLayout"), QVariant::fromValue(m_workspaceLayout.get())},
            {QStringLiteral("shiftTimes"), QVariant::fromValue(m_shiftTimes.get())},
            {QStringLiteral("gridFilter"), QVariant::fromValue(m_gridFilter.get())},
            {QStringLiteral("visualTools"), QVariant::fromValue(m_visualTools.get())},
            {QStringLiteral("automationHotkeys"), QVariant::fromValue(static_cast<QObject *>(m_automationHotkeys.get()))},
            {QStringLiteral("hotkeys"), QVariant::fromValue(static_cast<QObject *>(m_hotkeys.get()))},
            {QStringLiteral("updates"), QVariant::fromValue(static_cast<QObject *>(m_updates.get()))},
            {QStringLiteral("styleManager"), QVariant::fromValue(static_cast<QObject *>(m_styleManager.get()))},
            {QStringLiteral("fontCollector"), QVariant::fromValue(static_cast<QObject *>(m_fontCollector.get()))},
            {QStringLiteral("app"), QVariant::fromValue(static_cast<QObject *>(this))}};
}

void Application::waitForWrites()
{
    m_port->waitIdle();
    QCoreApplication::processEvents();
}

// O1: live application of changed settings, where legacy reads the option
// when it acts (the others are read at their next use).
void Application::settingChanged(const QString &id)
{
    if (id == QLatin1String("audio.outputHostApi") && m_audioPlayer)
        m_audioPlayer->reopenOutput();
    if (id == QLatin1String("subtitles.saveWithVideoName"))
        emit saveWithVideoNameChanged();
    else if (id == QLatin1String("video.dontAskForBadResolution"))
        emit askForBadResolutionChanged();
    else if (id.startsWith(QLatin1String("convert.")))
        m_conversionPlan.reset(); // a changed option invalidates the preview (C02)
    else if (id == QLatin1String("autosave.maxFiles") && !m_recoveryDir.isEmpty() && m_recovery)
        m_recovery->setCapacity(m_settings->integer("autosave.maxFiles")); // SubsGridBase autosave
    else if (id == QLatin1String("grid.hideColumns") && !m_resettingSettings)
        m_shell->setHiddenColumns(m_settings->integer("grid.hideColumns"));
}

namespace {

// Legacy config::FindLanguage: the shipped names, then the locale's own.
QString languageName(const QString &tag)
{
    static const std::map<QString, QString> shipped{{QStringLiteral("en"), QStringLiteral("English")},
                                                    {QStringLiteral("ko"), QStringLiteral("한국어")},
                                                    {QStringLiteral("pl"), QStringLiteral("Polski")},
                                                    {QStringLiteral("ta"), QStringLiteral("தமிழ்")},
                                                    {QStringLiteral("th"), QStringLiteral("ไทย")}};
    auto it = shipped.find(tag);
    if (it == shipped.end())
        it = shipped.find(tag.section(u'_', 0, 0));
    if (it != shipped.end())
        return it->second;
    const QLocale locale(tag);
    const QString name = locale.language() == QLocale::C ? QString() : locale.nativeLanguageName();
    return name.isEmpty() ? tag : name;
}

} // namespace

namespace {

QString qs(std::string_view s)
{
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}

QStringList qList(const std::vector<std::string> &list)
{
    QStringList out;
    for (const auto &entry : list)
        out << QString::fromStdString(entry);
    return out;
}

std::vector<std::string> stdList(const QStringList &list)
{
    std::vector<std::string> out;
    for (const QString &entry : list)
        out.push_back(entry.toStdString());
    return out;
}

// The controls' state crosses to QML by setting id.
QVariantMap toVariant(const application::OptionsState &state)
{
    QVariantMap out;
    for (const auto &[id, value] : state) {
        if (const auto *v = std::get_if<std::int64_t>(&value))
            out.insert(QString::fromStdString(id), int(*v));
        else
            out.insert(QString::fromStdString(id), ui::SettingsStore::toVariant(value));
    }
    return out;
}

application::OptionsState fromVariant(const QVariantMap &values)
{
    using application::OptionsControl;
    application::OptionsState out;
    for (const auto &b : application::optionsBindings()) {
        const QString id = qs(b.setting);
        if (!values.contains(id))
            continue;
        const QVariant v = values.value(id);
        switch (b.control) {
        case OptionsControl::Check:
            out[std::string(b.setting)] = v.toBool();
            break;
        case OptionsControl::Text:
        case OptionsControl::ZoomText:
        case OptionsControl::ComboText:
        case OptionsControl::Font:
            out[std::string(b.setting)] = v.toString().toStdString();
            break;
        default:
            out[std::string(b.setting)] = std::int64_t(v.toLongLong());
            break;
        }
        if (b.control == OptionsControl::Font && values.contains(qs(b.sizeSetting)))
            out[std::string(b.sizeSetting)] = std::int64_t(values.value(qs(b.sizeSetting)).toLongLong());
    }
    return out;
}

} // namespace

QVariantMap Application::openSettingsDialog()
{
    auto &lists = m_optionsLists;
    lists = {};
    // No translation catalogues ship with the rewrite yet: English only.
    lists.languageTags = {"en"};
    lists.languageNames = {"English"};
    lists.findLanguage = [](std::string_view tag) { return languageName(qs(tag)).toStdString(); };
    // U1-unicode-case: every letter, whatever the interface language.
    lists.sameIgnoringCase = [](std::string_view a, std::string_view b) {
            const QString x = qs(a), y = qs(b);
            return x.size() == y.size() && x.compare(y, Qt::CaseInsensitive) == 0;
        };
    // SpellChecker::AvailableDics through F3's availableDictionaries: the
    // i-th .dic with the i-th .aff of each Dictionary folder, in the order
    // wxDir lists it on each platform (R5-per-platform, legacy_dir.h:
    // FindFirstFileW on Windows, readdir on Linux, no sorting); the pairing
    // stops at the shorter list (R3-hang-crash-loss).
    // R6-dictionary-location: the settings folder's, then the program
    // folder's, a symbol listed once.
    {
        std::vector<std::filesystem::path> folders;
        for (const QString &folder : std::as_const(m_dictionaryDirs))
            folders.emplace_back(folder.toStdU16String());
        for (const auto &symbol : application::availableDictionaries(folders)) {
            const QString name = qstr(symbol);
            lists.dictionarySymbols.push_back(name.toStdString());
            lists.dictionaryNames.push_back(languageName(name).toStdString());
        }
    }
    {
        if (lists.dictionaryNames.empty())
            lists.dictionaryNames.push_back(tr("Put files .dic and .aff to \"Dictionary\" folder").toStdString());
    }
    // The conversion catalog: its option's catalog is loaded (LoadStyles), then
    // its Styles are listed.
    lists.catalogs = stdList(m_styleManager->catalogs());
    lists.currentCatalog = m_styleManager->catalog().toStdString();
    if (const auto load = application::optionsCatalogToLoad(m_settings->settings(), lists)) {
        m_styleManager->chooseCatalog(QString::fromStdString(*load));
        lists.currentCatalog = *load;
    }
    lists.styles = stdList(m_styleManager->storeStyles());
#ifdef _WIN32
    lists.pathSeparator = '\\';
    lists.slashesForBackslashes = false;
#endif
    const auto open = application::openOptionsDialog(m_settings->settings(), lists);
    // O2: the Hotkeys page's list (AddHotkeysOnList).
    m_hotkeys->beginOptions();
    QStringList warnings;
    // "The selected %s for conversion does not exist\nand will be changed to the default".
    if (open.catalogMissing)
        warnings << tr("The selected %1 for conversion does not exist\nand will be changed to the default").arg(tr("catalog for style"));
    if (open.styleMissing)
        warnings << tr("The selected %1 for conversion does not exist\nand will be changed to the default").arg(tr("style"));
    auto values = toVariant(open.state);
    addThemeColours(values);
    return {{QStringLiteral("values"), values},
            {QStringLiteral("languages"), qList(lists.languageNames)},
            {QStringLiteral("dictionaries"), qList(lists.dictionaryNames)},
            {QStringLiteral("catalogs"), qList(lists.catalogs)},
            {QStringLiteral("styles"), qList(lists.styles)},
            {QStringLiteral("warnings"), warnings}};
}

// The Themes page's colours (legacy's ID_COLOR_CONFIG list), as far as the
// rewrite keeps theme colours: the audio spectrum's three (A2) and the icon
// colours of each appearance (K1). The Grid's comparison colours are fixed
// per theme (R1, LineTableModel).
namespace {
std::vector<std::string_view> themeColours()
{
    std::vector<std::string_view> ids{application::kSpectrumBackgroundSetting, application::kSpectrumEchoSetting,
                                      application::kSpectrumInnerSetting};
    for (const auto &appearance : application::kIconColourSettings)
        ids.insert(ids.end(), std::begin(appearance), std::end(appearance));
    return ids;
}

bool isIconColour(std::string_view id)
{
    for (const auto &appearance : application::kIconColourSettings)
        if (std::ranges::find(appearance, id) != std::end(appearance))
            return true;
    return false;
}
} // namespace

void Application::addThemeColours(QVariantMap &values) const
{
    for (const auto id : themeColours())
        values.insert(qs(id), qs(m_settings->settings().text(id)));
}

void Application::applySettings(const QVariantMap &values)
{
    // SetOptions' ID_COLOR_CONFIG list: the changed colours are saved and
    // ChangeColors runs (the audio display's ChangeOptions: the spectrum
    // reads its colours again, through settingChanged).
    for (const auto id : themeColours()) {
        const auto found = values.constFind(qs(id));
        if (found == values.cend())
            continue;
        const std::string colour = found->toString().toStdString();
        if (!application::parseSettingColour(colour) || colour == m_settings->settings().text(id))
            continue;
        // K1: an icon colour back at its theme default leaves the profile
        // (it follows the default again); the icons repaint at once. The
        // colours are compared as colours, so "#9cdbc9" or "#9CDBC9FF" is
        // the default "#9CDBC9" too.
        const auto *setting = application::findSetting(id);
        if (isIconColour(id) && setting
            && application::parseSettingColour(std::get<std::string>(setting->defaultValue))
                   == application::parseSettingColour(colour))
            m_settings->reset(qs(id));
        else
            m_settings->settings().set(id, colour);
    }
    // Live effects follow from settingChanged, and from OptionsDialog::SetOptions
    // for the options it acts on itself (below).
    const auto written = application::commitOptionsDialog(m_settings->settings(), m_optionsLists, fromVariant(values));
    const auto wrote = [&](std::string_view id) { return std::ranges::find(written, id) != written.end(); };
    // SPELLCHECKER_ON: EditBox::ClearErrs(true, value), the Line editor's
    // switch follows. DICTIONARY_LANGUAGE: SpellChecker::Destroy and
    // ClearErrs. EDITBOX_SUGGESTIONS_ON_DOUBLE_CLICK is read at the double click.
    if (wrote("editor.spellchecker"))
        m_spellingOn = m_settings->boolean("editor.spellchecker");
    if (wrote("editor.dictionaryLanguage"))
        restartSpellChecker();
    if (wrote("editor.spellchecker") || wrote("editor.dictionaryLanguage"))
        spellingRefresh();
    else if (wrote("editor.suggestionsOnDoubleClick"))
        emit spellingChanged();
    // A changed program font runs HikariSubFrame::DestroyDialogs.
    if (wrote("program.font") || wrote("program.fontSize"))
        destroyDialogs();
    // O2: the Hotkeys page's list, a bound control of SetOptions.
    m_hotkeys->commitOptions();
}

// HikariSubFrame::DestroyDialogs: FindReplaceDialog and SelectLines save
// their options and are destroyed, MisspellReplacer is destroyed (its
// destructor saves the rules); each reads its settings again at its next
// opening.
void Application::destroyDialogs()
{
    // FR->SaveOptions(), FR->Destroy(). The tool saves its tab and closes
    // (findReplaceDestroyed). Legacy's modal boxes kept this from happening
    // while a question waits; here the tool then stays.
    if (m_findOpened && !m_find->busy()) {
        emit findReplaceDestroyed();
        m_findOpened = false;
        createFindReplace();
        emit findResultsChanged();
    }
    if (m_selectLinesOpened) {
        m_settings->set("selectLines.options", m_selectOptions);
        m_selectLinesOpened = false;
        m_selectRecent = m_settings->list("selectLines.recentSelections").mid(0, 20);
        emit selectLinesDestroyed();
    }
    // MR->Destroy(): ~MisspellReplacer saves a non-empty list; the next
    // opening reads Rules.txt again.
    if (m_misspellRules) {
        saveMisspellRules();
        m_misspellRules.reset();
        m_misspellFinds.clear();
        m_misspellResultsShown = false;
        emit misspellReplacerDestroyed();
    }
}

QVariantMap Application::resetSettings(const QVariantMap &values)
{
    // config::ResetDefault resets every option in memory. What long-lived
    // legacy objects hold is not reset, and they write it back later:
    // - HikariSubFrame's recent subtitles, video and audio lists (SetRecent
    //   at the next addition, OnClose at exit), kept here at once;
    // - the hotkey files (Hotkeys.txt, AudioHotkeys.txt): Hkeys.ResetDefaults()
    //   (below) resets the bindings in memory only; they are written when
    //   legacy next saves them.
    auto &store = m_settings->settings();
    std::vector<std::pair<std::string_view, application::SettingValue>> kept;
    // The theme's colours are not options: ResetDefault leaves them.
    std::vector<std::string_view> keep{"recent.subtitles", "recent.video", "recent.audio",
                                       application::kAutomationHotkeysSetting, application::kHotkeysSetting,
                                       application::kAudioHotkeysSetting};
    const auto colours = themeColours();
    keep.insert(keep.end(), colours.begin(), colours.end());
    for (const std::string_view id : keep)
        if (store.isSet(id))
            kept.emplace_back(id, store.value(id));
    m_resettingSettings = true;
    m_settings->resetAll();
    for (const auto &[id, value] : kept)
        store.set(id, value);
    m_resettingSettings = false;
    // Options legacy reads when it acts take the defaults at once: copy and
    // paste columns, the grid filter, the update checker.
    m_copyColumns = m_settings->integer("grid.copyColumns");
    m_pasteColumns = m_settings->integer("grid.pasteColumns");
    m_gridFilter->settingsReset(); // per-grid "ignore filtering" is kept (SubsGrid::ignoreFiltered)
    m_updates->reload();
    // SelectLines, once created, keeps its controls and recent searches:
    // the options go back at its close (legacy: at exit or DestroyDialogs),
    // the recent searches at its next selection. Before that, the dialog
    // reads the defaults when it opens.
    if (!m_selectLinesOpened) {
        m_selectOptions = m_settings->integer("selectLines.options");
        m_selectRecent = m_settings->list("selectLines.recentSelections").mid(0, 20);
    }
    // The Line editor's tag buttons stay as built (EditBox::SetTagButtons);
    // pressing one reads its reset option (TagButtonsController::pressed).
    // DialogColorPicker, once created, keeps its recent colours and writes
    // them at its next colour; before that, AddRecent edits the option.
    m_colourPicker->settingsReset();
    // The shift times panel keeps its values and writes them at its next
    // change; the Grid keeps its hidden columns (SubsGrid::visibleColumns)
    // and writes them at the next toggle.
    // FindReplaceDialog, once created, keeps its controls (written back at
    // the next tab change, run or closing; SaveValues takes the reset
    // option's bits from 512 up) and FindReplace its recent lists (written
    // at the next AddRecent, each list as legacy SetTable writes it); never
    // created, it reads the defaults when it opens (openFindReplace).
    // MisspellReplacer has no options (Rules.txt).
    // Spelling: the Line editor keeps its switch (TextEditor::SpellCheckerOnOff)
    // and the spell checker its dictionary (no SpellChecker::Destroy); the
    // Grid and the menus read the reset options.
    emit spellingChanged();
    // O2: Hkeys.ResetDefaults(), then the Hotkeys list again.
    m_hotkeys->resetDefaults();
    auto refreshed =
        toVariant(application::refreshOptionsDialogAfterReset(m_settings->settings(), m_optionsLists, fromVariant(values)));
    // the colour list keeps what it shows
    for (const auto id : themeColours())
        if (const auto found = values.constFind(qs(id)); found != values.cend())
            refreshed.insert(qs(id), *found);
    return refreshed;
}

QVariantMap Application::chooseSettingsCatalog(const QVariantMap &values, int index)
{
    auto state = fromVariant(values);
    if (index >= 0 && index < qsizetype(m_optionsLists.catalogs.size())) {
        // Options.SaveOptions(false) then LoadStyles: the current catalog is saved first.
        m_styleManager->chooseCatalog(QString::fromStdString(m_optionsLists.catalogs[std::size_t(index)]));
        m_optionsLists.currentCatalog = m_optionsLists.catalogs[std::size_t(index)];
        m_optionsLists.styles = stdList(m_styleManager->storeStyles());
    }
    application::chooseOptionsDialogCatalog(m_optionsLists, state, index);
    return {{QStringLiteral("values"), toVariant(state)}, {QStringLiteral("styles"), qList(m_optionsLists.styles)}};
}

QString Application::settingsFolderPath(const QUrl &url) const
{
    return QDir::toNativeSeparators(url.toLocalFile());
}

} // namespace hikari::app
