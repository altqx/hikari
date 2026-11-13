#include "hikari/app/application.h"

#include "docking.h"

#include "hikari/backends/desktop_handoff.h"
#include "hikari/backends/ffms_matroska.h"
#include "hikari/backends/icu_text.h"
#include "hikari/backends/legacy_text_file.h"
#include "hikari/backends/lyrics_text.h"
#include "hikari/backends/mecab_readings.h"
#include "hikari/backends/libass_font_service.h"
#include "hikari/backends/portaudio_output.h"
#include "hikari/backends/qt_audio_devices.h"
#include "hikari/backends/simulated_output.h"
#include "hikari/backends/video_frame_bgra.h"
#include "hikari/application/hotkeys.h"
#include "hikari/application/getting_started.h"
#include "hikari/application/guide_links.h"
#include "hikari/application/grid_clipboard.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/grid_filtering.h"
#include "hikari/application/fold_groups.h"
#include "hikari/application/grid_groups.h"
#include "hikari/application/grid_split.h"
#include "hikari/application/text_commands.h"
#include "hikari/application/grid_translation.h"
#include "hikari/application/script_properties.h"
#include "hikari/application/shift_times.h"
#include "hikari/application/select_lines.h"
#include "hikari/application/resample.h"
#include "hikari/application/position_commands.h"
#include "hikari/application/visual_transform.h"
#include "hikari/application/spell_checker.h"
#include "hikari/application/legacy_dir.h"
#include "hikari/application/video_matrix.h"
#include "hikari/application/script_resolution.h"
#include "hikari/application/video_properties.h"
#include "hikari/core/spelling.h"
#include "hikari/core/text_projection.h"
#include "hikari/core/conversion.h"
#include "hikari/application/keyframe_files.h"
#include "hikari/core/line_groups.h"
#include "hikari/core/style.h"
#include "hikari/core/subtitle_load.h"
#include "hikari/application/media_association.h"
#include "hikari/application/legacy_autosaves.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/ass_attachments.h"
#include "automation_services_qt.h"
#include "dialog_placement.h"
#include "list_files.h"
#include "project_file.h"
#include "theme.h"
#include "window_frame.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRegularExpression>
#include <QClipboard>
#include <QMimeData>
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
#include <QScreen>
#include <QLocale>
#include <QDir>
#include <QUuid>
#include <QCoreApplication>
#include <QTimer>
#include <QStringList>
#include <QVariantMap>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <QFileInfo>
#include <QBuffer>
#include <QLinearGradient>
#include <QPainter>
#include <QUrl>

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

QString refusalText(application::CommandRefusal refusal);

// QString::toCaseFolded is simple case folding (one code point to one), so offsets carry over.
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

QVariantMap columnWidthsFrom(const QStringList &entries)
{
    QVariantMap widths;
    for (const QString &entry : entries)
        if (const auto parts = entry.split(QLatin1Char(' ')); parts.size() == 2 && parts[1].toInt() > 0)
            widths.insert(parts[0], parts[1].toInt());
    return widths;
}

QStringList columnWidthEntries(const QVariantMap &widths)
{
    QStringList entries;
    for (auto it = widths.begin(); it != widths.end(); ++it)
        entries << it.key() + QLatin1Char(' ') + QString::number(it.value().toInt());
    return entries;
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
        return QStringLiteral(HIKARI_BUILD_MEDIA_HELPER);
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

QString pluginHelperPath(const QString &configured)
{
    if (!configured.isEmpty())
        return configured;
#ifdef _WIN32
    const QString name = QStringLiteral("hikari-plugin-helper.exe");
#else
    const QString name = QStringLiteral("hikari-plugin-helper");
#endif
    const QString sibling = QCoreApplication::applicationDirPath() + QLatin1Char('/') + name;
#ifdef HIKARI_BUILD_PLUGIN_HELPER
    if (!QFileInfo::exists(sibling))
        return QStringLiteral(HIKARI_BUILD_PLUGIN_HELPER);
#endif
    return sibling;
}

QString readingDictionaryPath(const QString &configured)
{
    if (!configured.isEmpty())
        return configured;
    const QString sibling = QCoreApplication::applicationDirPath() + QStringLiteral("/Dictionary/unidic-lite");
#ifdef HIKARI_BUILD_UNIDIC
    if (!QFileInfo::exists(sibling + QStringLiteral("/sys.dic")))
        return QStringLiteral(HIKARI_BUILD_UNIDIC);
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

ui::ListFile::Report listFileReport(ui::LogController &logController)
{
    QPointer<ui::LogController> log(&logController);
    const QString title = QCoreApplication::translate("HikariSub.MainWindow", "Settings");
    const auto name = [](const std::filesystem::path &path) {
        return QString::fromStdString(application::displayPath(path.filename()));
    };
    ui::ListFile::Report report;
    report.converted = [log, title, name](const std::filesystem::path &legacy, const std::filesystem::path &file) {
        if (log)
            log->log(QCoreApplication::translate("HikariSub.MainWindow", "%1 moved to %2. The old file is kept as %3.")
                    .arg(name(legacy), name(file), name(legacy) + QStringLiteral(".old")),
                ui::LogController::Info, title);
    };
    const auto where = [](const application::FileProblem &problem) {
        const QString file = QString::fromStdString(application::displayPath(problem.file));
        return (problem.failure.line > 0 ? QCoreApplication::translate("HikariSub.MainWindow", "%1, line %2, column %3")
                                               .arg(file)
                                               .arg(problem.failure.line)
                                               .arg(problem.failure.column)
                                         : file) +
               QLatin1String(": ") + QString::fromStdString(problem.failure.message);
    };
    report.problem = [log, title, name, where](const application::FileProblem &problem) {
        if (!log)
            return;
        const QString text =
            problem.keptAs.empty()
                ? QCoreApplication::translate("HikariSub.MainWindow",
                                              "%1 could not be read, so its list is at its defaults for now. "
                                              "HikariSub will not write over it.")
                      .arg(name(problem.file))
                : QCoreApplication::translate("HikariSub.MainWindow",
                                              "%1 could not be read, so its list is back at its defaults. The file "
                                              "is kept as %2: fix it while HikariSub is closed and give it its name "
                                              "back to use it again.")
                      .arg(name(problem.file), name(problem.keptAs));
        log->notice(ui::LogController::Warning, title, text, where(problem));
    };
    report.replaced = [log, title, name, where](const application::FileProblem &problem) {
        if (log)
            log->notice(ui::LogController::Warning, title,
                        QCoreApplication::translate("HikariSub.MainWindow",
                                                    "%1 could not be read, so it is kept as %2 and a new one is saved.")
                            .arg(name(problem.file), name(problem.keptAs)),
                        where(problem));
    };
    return report;
}

}

class Application::FindHost : public application::FindReplaceHost {
public:
    FindHost(Application &app, int shellWindow) : m_app(app), m_window(shellWindow) {}

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
        const auto current = m_app.m_workspace.currentTab(m_window);
        return current ? tab(*current) : std::nullopt;
    }
    std::vector<application::FindTab> tabs() override
    {
        m_app.protectReference();
        std::vector<application::FindTab> out;
        for (const auto id : m_app.m_workspace.allTabs())
            if (auto t = tab(id))
                out.push_back(*t);
        return out;
    }
    application::LineVisible actionLines(application::EditSession &session) override
    {
        return m_app.actionLines(session);
    }
    void ask(const application::FindQuestion &question, std::function<void(application::FindAnswer)> answer) override
    {
        const QVariantMap shown = questionMap(question);
        const int kind = static_cast<int>(question.kind);
        const QString kindName = questionKind(question.kind);
        const auto reply = [&](int code) {
            if (answer)
                answer(static_cast<application::FindAnswer>(std::clamp(code, 0, 3)));
        };
        if (m_app.m_findQuestionHandler) {
            reply(m_app.m_findQuestionHandler(kind, shown.value(QStringLiteral("text")).toString()));
            return;
        }
        if (!m_app.isSignalConnected(QMetaMethod::fromSignal(&Application::findQuestion))) {
            // MUST KILL: 3 is FindAnswer::Cancel passed as a raw int
            reply(answer ? 3 : 0);
            return;
        }
        if (question.kind == application::FindQuestion::Kind::Message) {
            m_app.m_log->notice(shown.value(QStringLiteral("severity")) == QLatin1String("warning")
                                    ? ui::LogController::Warning
                                    : ui::LogController::Info,
                                shown.value(QStringLiteral("title")).toString(),
                                shown.value(QStringLiteral("text")).toString(), {}, m_window);
            reply(0);
            return;
        }
        const int id = ++m_app.m_findQuestionId;
        if (answer)
            m_app.m_findAnswers.emplace(id, std::move(answer));
        emit m_app.findQuestion(id, kindName, shown, m_window);
        emit m_app.findBusyChanged();
    }
    static QString questionKind(application::FindQuestion::Kind kind)
    {
        using K = application::FindQuestion::Kind;
        switch (kind) {
        case K::Styles: return QStringLiteral("styles");
        case K::NoStyles: return QStringLiteral("noStyles");
        case K::ConfirmFiles: return QStringLiteral("confirmFiles");
        case K::Message: break;
        }
        return QStringLiteral("message");
    }
    static QVariantMap questionMap(const application::FindQuestion &q)
    {
        using K = application::FindQuestion::Kind;
        using I = application::FindQuestion::Info;
        const QString document = qs(q.document);
        QString title = QCoreApplication::translate("HikariSub.Search", "Find"), text, detail,
                severity = QStringLiteral("info");
        switch (q.kind) {
        case K::Styles: {
            QStringList quoted;
            for (const auto &name : q.missing)
                quoted << QCoreApplication::translate("HikariSub.Search", "“%1”").arg(qs(name));
            QString names = quoted.isEmpty() ? QString() : quoted.takeLast();
            if (!quoted.isEmpty())
                names = QCoreApplication::translate("HikariSub.Search", "%1 or %2")
                            .arg(quoted.join(QCoreApplication::translate("HikariSub.Search", ", ")), names);
            text = QCoreApplication::translate("HikariSub.Search", "%1 has no style named %2.").arg(document, names);
            detail = QCoreApplication::translate("HikariSub.Search", "The other chosen styles are there. Searching with "
                                                                     "the missing ones finds fewer lines.");
            severity = QStringLiteral("warning");
            break;
        }
        case K::NoStyles:
            text = QCoreApplication::translate("HikariSub.Search", "%1 has none of the chosen styles.").arg(document);
            detail = QCoreApplication::translate("HikariSub.Search", "Searching only these styles would find nothing.");
            severity = QStringLiteral("warning");
            break;
        case K::ConfirmFiles:
            title = QCoreApplication::translate("HikariSub.Search", "Replace in files");
            text = QCoreApplication::translate("HikariSub.Search", "Replace in every matching file in “%1”?")
                       .arg(qs(q.folder));
            detail = QCoreApplication::translate("HikariSub.Search",
                                                 "The files are changed on disk; Undo cannot bring them back. A copy "
                                                 "of each file goes to the ReplaceBackup folder first.");
            severity = QStringLiteral("warning");
            break;
        case K::Message:
            switch (q.info) {
            case I::NotFound:
                title = QCoreApplication::translate("HikariSub.Search", "Find");
                text = QCoreApplication::translate("HikariSub.Search", "Could not find “%1”.").arg(qs(q.argument));
                break;
            case I::Replaced:
                title = QCoreApplication::translate("HikariSub.Search", "Replace all");
                text = QCoreApplication::translate("HikariSub.Search", "Replaced %1 times.").arg(qs(q.argument));
                break;
            case I::InvalidPath:
                title = QCoreApplication::translate("HikariSub.Search", "Find");
                text = QCoreApplication::translate("HikariSub.Search", "The search folder “%1” does not exist.")
                           .arg(qs(q.folder));
                severity = QStringLiteral("warning");
                break;
            case I::None: text = qs(q.text); break;
            }
            break;
        }
        return {{QStringLiteral("title"), title}, {QStringLiteral("text"), text},
                {QStringLiteral("detail"), detail}, {QStringLiteral("severity"), severity}};
    }
    void finished() override { m_app.findFinished(m_window); }
    void logLine(const application::FindLog &line) override
    {
        using K = application::FindLog::Kind;
        const bool error = line.kind == K::InvalidRegex || line.kind == K::ReplaceHidden;
        m_app.m_log->notice(error ? ui::LogController::Error : ui::LogController::Warning,
                            QCoreApplication::translate("HikariSub.Search", "Find"), logText(line), {}, m_window);
    }
    static QString logText(const application::FindLog &line)
    {
        using K = application::FindLog::Kind;
        switch (line.kind) {
        case K::InvalidRegex: return QCoreApplication::translate("HikariSub.Search", "Invalid regular expression “%1”: %2")
                                         .arg(qs(line.first), qs(line.second));
        case K::MatchError: return QCoreApplication::translate("HikariSub.Search", "Failed to find match for regular expression: %1")
                                       .arg(qs(line.first));
        case K::LineEdited:
            return QCoreApplication::translate("HikariSub.Search", "Line %1 cannot be replaced,\n"
                                                               "because it was edited.").arg(qs(line.first));
        case K::CannotBackUp: return QCoreApplication::translate("HikariSub.Search", "Cannot back up %1.").arg(qs(line.first));
        case K::ReplaceHidden: break;
        }
        return qs(line.text);
    }
    void recentAdded(const application::FindReplaceSettings &window) override { m_app.saveFindRecent(m_window, window); }
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
        if (role == 1)
            m_app.editor().selectRaw(application::translationMode(*session) ? 1 : 0, start, end);
        else if (role == 0)
            m_app.editor().selectRaw(0, start, end);
        else if (role == 2 || role == 3)
            m_app.editor().selectInBox(role, start, end);
    }
    void changed(application::DocumentId) override
    {
        m_app.editor().reloadFromSession();
        m_app.refreshViews();
    }
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
    std::optional<std::u16string> readFile(const std::u8string &path) override
    {
        return backends::readLegacyTextFile(qs(path));
    }
    bool fileExists(const std::u8string &path) override { return QFileInfo(qs(path)).isFile(); }
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
    bool writeFile(const std::u8string &path, const std::u16string &text) override
    {
        const QString name = qs(path);
        QByteArray original;
        {
            QFile in(name);
            if (!in.open(QIODevice::ReadOnly)) {
                m_app.m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Search", "Replace in files"),
                                    QCoreApplication::translate("HikariSub.Search", "Cannot open file %1.").arg(name), {}, m_window);
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
        const QString title = QCoreApplication::translate("HikariSub.Search", "Replace in files");
        m_app.m_log->notice(ui::LogController::Error, title,
                            QCoreApplication::translate("HikariSub.Search", "Cannot write file %1; it was left as it was.").arg(name), {},
                            m_window);
        if (!write(original))
            m_app.m_log->notice(ui::LogController::Error, title,
                                QCoreApplication::translate("HikariSub.Search", "%1 could not be restored; its backup is in %2.")
                                    .arg(name, m_app.m_replaceBackup),
                                {}, m_window);
        return false;
    }
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
            if (const auto id = m_app.open(name)) {
                if (!target)
                    m_app.refreshViews();
                return done(id);
            }
            m_app.newDocument();
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
    void collect(const QDir &dir, const QString &filter, bool subfolders, bool hidden, QStringList &path,
                 std::vector<std::u8string> &out)
    {
        const QString canonical = dir.canonicalPath();
        if (path.contains(canonical))
            return;
        path.push_back(canonical);
        const std::filesystem::path folder = QFileInfo(dir.absolutePath()).filesystemAbsoluteFilePath();
        const int shown = hidden ? application::legacy_dir::Hidden : 0;
        if (subfolders)
            for (const auto &sub : application::legacy_dir::entries(folder, {}, application::legacy_dir::Dirs | shown)
                                       .value_or(std::vector<std::filesystem::path>{}))
                collect(QDir(QFileInfo(sub).absoluteFilePath()), filter, subfolders, hidden, path, out);
        for (const auto &file : application::legacy_dir::entries(folder, filter.toStdU16String(),
                                                                 application::legacy_dir::Files | shown)
                                    .value_or(std::vector<std::filesystem::path>{}))
            out.push_back(toU8(QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath())));
        path.pop_back();
    }

    Application &m_app;
    int m_window;
};

Application::Application(QObject *parent) : Application(Options{}, parent) {}

Application::Application(Options options, QObject *parent) : QObject(parent)
{
    // MUST KILL: read before anything below may write a settings file
    const bool firstStart = !options.settingsFile.isEmpty() && !ui::SettingsStore::filesExist(options.settingsFile);
    m_reader = backends::makeFileReader();
    m_port = backends::makeFilePort([this](application::PermitId permit, application::WriteOutcome outcome) {
        QMetaObject::invokeMethod(this, [this, permit, outcome] { m_writes->complete(permit, outcome); },
                                  Qt::QueuedConnection);
    });
    m_writes = std::make_unique<application::WriteCoordinator>(*m_port, [this](const application::WriteResult &r) {
        m_files->onWriteResult(r);
        for (auto *views : allViews())
            views->editor->writeFinished();
        writeFinished(r);
    });
    m_files = std::make_unique<application::DocumentFiles>(*m_reader, *m_writes);
    m_files->setLyricsDecoder([](std::span<const std::byte> bytes) {
        return backends::lyricsUtf8(bytes, backends::detectLyricsEncoding(bytes));
    });
    m_log = std::make_unique<ui::LogController>();
    m_log->setActiveWindow([this] { return m_workspace.activeWindow(); });
    m_mediaHelper = mediaHelperPath(options.mediaHelper);
    m_playbackAudio = options.playbackAudio;
    m_renderer.setCsriFolder(std::filesystem::path(QCoreApplication::applicationDirPath().toStdU16String()) / u"Csri");
    m_renderer.setLog([this](const std::string &text) { m_log->log(QString::fromStdString(text)); });
    m_mainMedia = std::make_unique<WindowMedia>();
    m_mainViews.media = m_mainMedia.get();
    makeVideo(*m_mainMedia);
    m_luaHelper = luaHelperPath(options.luaHelper);
    m_automation = std::make_unique<AutomationShell>(
        AutomationShell::Paths{m_luaHelper, automationPath(options.automationDir),
                               options.userAutomationDir, options.compileCacheDir},
        *m_files,
        m_workspace, [this]() -> ui::LineEditorController & { return editor(); }, [this]() -> ui::VideoController & { return video(); },
        [this]() -> ui::ShellController & { return shell(); }, *activeMedia().videoHelper);
    m_automation->setDocumentChanged([this] { refreshViews(); });
    m_automation->setBeforeRun([this] { commitTemplates(); });
    m_files->setBeforeSave([this](application::DocumentId document) {
        commitTemplates();
        applyScriptInfoOnSave(document);
    });
    connect(m_automation.get(), &AutomationShell::karaokeApplied, this,
            [this](quint64 document, const QByteArray &report) {
                const application::DocumentId id{document};
                if (!m_files->session(id))
                    return;
                for (auto *views : allViews())
                    views->templateEditor->applied(
                        report.toStdString(), ui::TemplateEditorController::DocumentKey{document, m_files->generation(id)});
            });
    m_settingsFile = options.settingsFile;
    m_userFolder = options.userFolder;
    m_readingDictionary = readingDictionaryPath(options.readingDictionaryDir);
    // MUST KILL: must run before anything writes the session
    rememberStartSession();
    m_dictionaryDir = options.dictionaryDir;
    if (m_dictionaryDir.isEmpty() && !m_settingsFile.isEmpty())
        m_dictionaryDir = QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/Dictionary");
    if (!m_dictionaryDir.isEmpty())
        m_dictionaryDirs << m_dictionaryDir;
    if (const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/Dictionary");
        !m_dictionaryDirs.contains(bundled))
        m_dictionaryDirs << bundled;
    m_settings = std::make_unique<ui::SettingsStore>(m_settingsFile);
    if (!m_settingsFile.isEmpty()) {
        m_importStore = std::make_unique<SettingsImportStore>(*m_settings, QFileInfo(m_settingsFile).absolutePath());
        if (!options.dictionaryDir.isEmpty())
            m_importStore->setDictionaryFolder(m_dictionaryDir);
        m_importStore->macroProblem = [this](std::string_view alias) -> std::string {
            const auto r = m_automation->manager().registry().resolve(std::string(alias));
            if (!r.problem)
                return {};
            switch (*r.problem) {
            case application::AliasProblem::MissingScript:
                return "no loaded script has this file name";
            case application::AliasProblem::BasenameCollision:
                return "several loaded scripts have this file name";
            case application::AliasProblem::OrdinalOutOfRange:
                return "the script has no macro of this ordinal";
            default:
                return "the script's registration differs";
            }
        };
        m_importStore->rendererInstalled = [this](std::string_view name) {
            return std::ranges::contains(m_renderer.providers(), name);
        };
        noteRemovedRules();
        m_importStore->recover();
    }
    m_settingsImport = std::make_unique<SettingsImportController>(m_importStore.get());
    if (firstStart && Localisation::firstStartLanguage(options.systemUiLanguages) == u"pl") {
        m_settings->set("program.language", QStringLiteral("pl"));
        m_settings->set("editor.dictionaryLanguage", QStringLiteral("pl"));
    }
    if (firstStart && options.gettingStarted)
        m_settings->set("workspace.gettingStarted", QStringLiteral("due"));
    m_askVideoProperties = options.askVideoProperties;
    connect(m_settings.get(), &ui::SettingsStore::changed, this, &Application::settingChanged);
    m_automation->manager().setTraceLevel(m_settings->integer("automation.traceLevel"));
    m_automation->setSettings(m_settings.get());
    m_automation->setWindowProjects([this] {
        std::map<int, application::WindowProject> projects;
        if (m_projects)
            for (const int window : sessionWindows()) {
                const QString root = m_projects->root(window);
                projects[window] = {QDir::toNativeSeparators(root).toStdString(), !root.isEmpty() && projectTrusted(root)};
            }
        return projects;
    });
    if (options.autoload)
        m_automation->autoload();
    m_renderer.select(m_settings->text("video.subtitleProvider").toStdString());
    m_renderer.countCost(m_settings->boolean("video.renderCost"));
    ui::theme::useSettings(m_settings.get());
    m_tagButtons = std::make_unique<ui::TagButtonsController>(*m_settings);
    m_colourPicker = std::make_unique<ui::ColourPickerController>(*m_settings);
    m_shiftTimes = std::make_unique<ui::ShiftTimesController>(*m_settings);
    m_selectOptions = m_settings->integer("selectLines.options");
    m_selectRecent = m_settings->list("selectLines.recentSelections").mid(0, 20);
    m_workspaceLayout = std::make_unique<ui::WorkspaceLayoutController>(
        m_settingsFile.isEmpty() ? QString() : QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/layout.json"),
        options.presetsDir.isEmpty() ? QString() : options.presetsDir + QStringLiteral("/layouts.json"));
    m_gridFilter = std::make_unique<ui::GridFilterController>(*m_settings);
    makeViews(m_mainViews, 0);
    choosePlayer();
    setUpTranslationControls();
    setUpFileArrangements();
    if (!m_settingsFile.isEmpty())
        m_replaceBackup = QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/ReplaceBackup");
    m_automation->setLog([this](const QString &line, AutomationShell::LogLine kind) {
        const QString title = QCoreApplication::translate("HikariSub.Automation", "Automation");
        switch (kind) {
        case AutomationShell::LogLine::Notice: m_log->notice(ui::LogController::Error, title, line); break;
        case AutomationShell::LogLine::Quiet: m_log->log(line); break;
        case AutomationShell::LogLine::QuietError: m_log->log(line, ui::LogController::Error, title); break;
        }
    });
    m_automationHotkeys = std::make_unique<AutomationHotkeysController>(*m_automation, *m_settings);
    m_hotkeys = std::make_unique<HotkeysController>(*m_automationHotkeys, *m_settings,
                                                    [this](const QString &message, bool warning) {
                                                        m_log->notice(warning ? ui::LogController::Warning : ui::LogController::Info,
                                                                      QCoreApplication::translate("HikariSub.Options", "Shortcuts"), message);
                                                    });
    m_aiScripts = std::make_unique<AiScripts>(
        *m_automation, m_userFolder.isEmpty() ? QString() : m_userFolder + QStringLiteral("/ai/scripts"),
        [this] { return m_workspace.editingTarget().has_value(); });
    m_aiKaraoke = std::make_unique<AiKaraoke>(AiKaraoke::Wiring{
        [this] { return activeMedia().audio ? activeMedia().audio->box().sharedAudio() : nullptr; },
        [folder = m_readingDictionary]() -> application::ReadingAnalyzer {
            std::shared_ptr<const backends::MecabReadings> readings =
                backends::MecabReadings::open(std::filesystem::path(folder.toStdU16String()));
            return readings ? backends::MecabReadings::analyzer(std::move(readings)) : application::ReadingAnalyzer{};
        },
        [this] { return &m_automation->templater(); }, QDir::tempPath()});
    m_apiHost = std::make_unique<ApiHost>(ApiHost::Wiring{
        m_workspace, *m_files, &m_settings->settings(), &m_automation->media(),
        [this](application::Selection next) { return applySelection(std::move(next)); },
        [this](application::DocumentId document) {
            for (WindowViews *v : allViews())
                if (v->editor && v->editorDocument == document)
                    v->editor->reloadFromSession();
            refreshViews();
        },
        [this](std::string_view command) {
            const QString accel = m_hotkeys->accelOf(
                QString::fromUtf8(command.data(), qsizetype(command.size())),
                application::hotkeyType(application::hotkeyIdOf(command)));
            return accel.isEmpty() ? std::vector<std::string>{} : std::vector<std::string>{accel.toStdString()};
        },
        [this]() -> const application::VideoSession * { return activeMedia().video ? &activeMedia().video->session() : nullptr; },
        [this](const core::Document &document) { return rendererScript(document); },
        [this](application::DocumentId document) { return subtitleFonts(document); },
        [this] { return m_settings->text("video.subtitleProvider").toStdString(); },
        m_userFolder.isEmpty() ? std::filesystem::path()
                               : std::filesystem::path((m_userFolder + QStringLiteral("/ai/skills")).toStdU16String()),
        m_aiScripts.get(),
        [this] { return perspectiveMeasure(); }, [this] { return activeProject(); },
        [this](application::DocumentId document) { return honyaFolderOf(document); },
        m_aiKaraoke.get(),
        [this] {
            application::NodeEffectOptions o;
            o.measure = perspectiveMeasure().port;
            o.lua = &luaNodes();
            o.audio = m_automation->media().peakAudio();
            return o;
        }});
    AiFeatures::Ports aiPorts = options.aiPorts ? std::move(*options.aiPorts)
                                                : AiFeatures::defaultPorts(m_userFolder, &m_apiHost->api(),
                                                                           options.appVersion.isEmpty()
                                                                               ? QStringLiteral(HIKARI_VERSION)
                                                                               : options.appVersion);
    if (!options.externalAgents) {
        aiPorts.agents = {};
        aiPorts.agentOnPath = {};
        aiPorts.findAgent = {};
        aiPorts.agentLatest = {};
    }
    aiPorts.reportRollback = [this](const QString &version, const QString &reason) {
        if (m_analytics)
            m_analytics->aiComponentRolledBack(version, reason, m_ai->versionProperties());
        return m_crashReports && m_crashReports->aiComponentFailed(version, reason);
    };
    m_ai = std::make_unique<AiFeatures>(*m_settings, std::move(aiPorts));
    m_aiMcp = std::make_unique<AiMcp>(*m_settings, *m_ai,
                                      m_userFolder.isEmpty() ? QString() : backends::ai::mcpEndpointFile(m_userFolder));
    m_aiMcpServers = std::make_unique<AiMcpServers>(*m_settings, *m_ai);
    m_aiSettings = std::make_unique<AiSettings>(*m_ai, m_aiMcp.get(), m_aiMcpServers.get());
    m_review = std::make_unique<ReviewController>(ReviewController::Wiring{
        m_apiHost->review(), *m_files, m_workspace, [this] { return m_ai->on(); },
        [this](application::Selection next) { return applySelection(std::move(next)); },
        [this](application::DocumentId document) { m_apiHost->committed(document); },
        [this](application::CommandRefusal refusal) {
            m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Review", "Review"),
                          refusalText(refusal));
        },
        [this] { refreshViews(); }});
    m_agent = std::make_unique<AgentController>(AgentController::Wiring{
        *m_ai, *m_settings, m_apiHost->api(), m_apiHost->review(), m_workspace, *m_files, m_userFolder, {},
        m_aiScripts.get(), m_automation.get(),
        [this] { return m_projects ? m_projects->root(m_workspace.activeWindow()) : QString(); }});
    m_skillsSettings = std::make_unique<SkillsSettings>(
        m_userFolder, [this](int window) { return m_projects ? m_projects->root(window) : QString(); });
    m_ai->setProjectKeys([this](const std::string &project, const std::string &providerId) {
        return projectAiKey(QString::fromStdString(project), providerId);
    });
    m_inlineAi = std::make_unique<InlineAiController>(InlineAiController::Wiring{
        *m_ai, *m_settings, m_apiHost->api(), m_apiHost->review(), m_workspace, *m_files, *m_agent,
        [this](application::DocumentId document, core::LineId line) {
            if (m_workspace.editingTarget() == document)
                applySelection(application::Selection{line, {line}, line, {}});
        },
        [this](application::DocumentId document) {
            const WindowSettings &window =
                *views(m_workspace.window(document).value_or(m_workspace.activeWindow())).settings;
            return InlineAiController::ProjectModels{
                QString::fromStdString(window.settings().text("translation.translator")),
                QString::fromStdString(window.settings().text("translation.reviewer")),
                window.waitsForTrust("translation.translator") || window.waitsForTrust("translation.reviewer")};
        },
        [this](application::DocumentId document) {
            return views(m_workspace.window(document).value_or(m_workspace.activeWindow())).settings->root();
        }});
    const auto hideAiCommands = [this] {
        m_hotkeys->setHidden([this](int id) { return !m_ai->on() && application::aiCommand(id); });
    };
    hideAiCommands();
    connect(m_ai.get(), &AiFeatures::switched, this, [this, hideAiCommands] {
        hideAiCommands();
        refreshViews();
    });
    m_pluginManager = std::make_unique<PluginManager>(options.plugins);
    m_pluginMarketplace = std::make_unique<PluginMarketplace>(
        *m_pluginManager, m_settings.get(), options.marketplace,
        options.plugins.registry.isEmpty() ? QString()
                                           : QFileInfo(options.plugins.registry).dir().filePath(QStringLiteral("marketplace.json")),
        options.appVersion.isEmpty() ? QStringLiteral(HIKARI_VERSION) : options.appVersion, options.marketplaceNetwork);
    PluginRuntime::Wiring pluginWiring{pluginHelperPath(options.pluginHelper), options.plugins, &m_apiHost->api(), m_log.get(),
                                       [this] { return m_localisation ? m_localisation->language() : QString(); }};
    pluginWiring.networkAllowed = [this] { return m_settings->boolean("plugins.network"); };
    pluginWiring.project = [this] { return activeProject(); };
    pluginWiring.sandbox = true;
    pluginWiring.setSetting = [this](const QString &id, const QString &setting, const QString &json) {
        return m_pluginManager->setSetting(id, setting, json);
    };
    pluginWiring.windowPlugins = [this] { return windowPlugins(); };
    pluginWiring.activeWindow = [this] { return m_workspace.activeWindow(); };
    m_pluginRuntime = std::make_unique<PluginRuntime>(std::move(pluginWiring));
    connect(m_pluginManager.get(), &PluginManager::folderForgotten, m_pluginRuntime.get(), &PluginRuntime::endFileAccess);
    connect(m_pluginManager.get(), &PluginManager::pluginsChanged, m_pluginRuntime.get(), &PluginRuntime::sync);
    connect(m_pluginManager.get(), &PluginManager::settingChanged, m_pluginRuntime.get(), &PluginRuntime::settingChanged);
    m_automationHotkeys->setPluginRunner([this](const QString &command) { return m_pluginRuntime->run(command); });
    connect(m_pluginRuntime.get(), &PluginRuntime::commandsChanged, m_automationHotkeys.get(),
            [this] { m_automationHotkeys->setPluginCommands(m_pluginRuntime->commands()); });
    m_pluginMarketplace->setBusy([this](const QString &id) { return m_pluginRuntime->busy(id); });
    connect(m_pluginRuntime.get(), &PluginRuntime::idle, m_pluginMarketplace.get(), &PluginMarketplace::pluginIdle);
    QMetaObject::invokeMethod(m_pluginRuntime.get(), &PluginRuntime::sync, Qt::QueuedConnection);
    m_analytics = std::make_unique<Analytics>(*m_settings, options.analytics);
    m_analytics->setAiVersions([this] { return m_ai->versionProperties(); });
    m_crashReports = std::make_unique<CrashReports>(*m_settings, *m_analytics, *m_log, options.crashReports);
    m_presence = std::make_unique<DiscordPresence>(*m_settings, options.presence);
    m_presence->setMode(m_workspaceLayout->preset());
    connect(m_workspaceLayout.get(), &ui::WorkspaceLayoutController::changed, m_presence.get(),
            [this] { m_presence->setMode(m_workspaceLayout->preset()); });
    connect(this, &Application::tabsChanged, m_presence.get(), [this] {
        const auto target = m_workspace.editingTarget();
        const std::string *title = target ? m_workspace.title(*target) : nullptr;
        m_presence->setFileName(title ? QString::fromStdString(*title) : QString());
    });
    m_crashReports->setKnown(
        [this] {
            QStringList names;
            for (const auto id : m_workspace.documents())
                if (const std::string *title = m_workspace.title(id))
                    names << QString::fromStdString(*title);
            for (const auto &[id, media] : m_tabMedia)
                names << media.video << media.audio << media.keyframes << media.timecodes;
            names.removeAll(QString());
            return names;
        },
        [this] {
            static const QRegularExpression tags(QStringLiteral("\\{[^}]*\\}"));
            QStringList texts;
            for (const auto id : m_workspace.documents())
                if (const auto *session = m_files->session(id))
                    for (const auto *line : session->document().lines())
                        for (const std::u8string *body : {&line->text, &line->translation}) {
                            const QString text = QString::fromUtf8(reinterpret_cast<const char *>(body->data()),
                                                                   qsizetype(body->size()));
                            texts << text;
                            if (const QString plain = QString(text).remove(tags); plain != text)
                                texts << plain;
                        }
            return texts;
        });
    m_crashReports->setCurrentFile([this] {
        const auto target = m_workspace.editingTarget();
        const auto destination = target ? m_files->destination(*target) : std::nullopt;
        return destination ? QString::fromStdString(destination->value) : QString();
    });
    connect(m_hotkeys.get(), &HotkeysController::ran, m_analytics.get(), [this](const QString &symbol, bool handled) {
        if (handled && !m_hotkeys->rehearsing())
            m_analytics->commandUsed(symbol, QStringLiteral("key"));
    });
    ui::DialogPlacement::setOpenedHook([analytics = m_analytics.get()](const QString &id) { analytics->dialogOpened(id); });
    m_renderer.observeCost([analytics = m_analytics.get()](double ms) { analytics->frameRendered(ms); });
    {
        using Outcome = backends::LuaScriptHost::RunOutcome;
        connect(&m_automation->manager(), &backends::AutomationManager::runFinished, m_analytics.get(),
                [this](const QString &path, Outcome outcome, const QString &) {
                    const QString kind = outcome == Outcome::Failed         ? QStringLiteral("failed")
                                         : outcome == Outcome::HelperLost   ? QStringLiteral("lost")
                                         : outcome == Outcome::ForceStopped ? QStringLiteral("stopped")
                                                                            : QString();
                    if (!kind.isEmpty() && m_analytics->active())
                        m_analytics->automationError(kind, bundledScriptName(path));
                });
        connect(&m_automation->manager(), &backends::AutomationManager::loadFailed, m_analytics.get(),
                [this](const QString &path) {
                    if (m_analytics->active())
                        m_analytics->automationError(QStringLiteral("load_failed"), bundledScriptName(path));
                });
    }
    m_updates = std::make_unique<UpdateChecker>(
        *m_settings, options.updateFeed,
        options.appVersion.isEmpty() ? QStringLiteral(HIKARI_VERSION) : options.appVersion, options.updateEnvironment);
    m_whatsNew = std::make_unique<WhatsNew>(*m_updates, WhatsNew::cacheFileIn(options.updateEnvironment));
    StarPrompt::Config starPrompt = options.starPrompt;
    if (!starPrompt.editing)
        starPrompt.editing = [this] {
            return QGuiApplication::applicationState() == Qt::ApplicationActive && !home();
        };
    m_starPrompt = std::make_unique<StarPrompt>(*m_settings, *m_analytics, std::move(starPrompt));
    if (gettingStartedDue())
        m_starPrompt->quietRun();
    connect(m_whatsNew.get(), &WhatsNew::changed, m_starPrompt.get(), [this] {
        if (m_whatsNew->updated())
            m_starPrompt->quietRun();
    });
    {
        const QString catalogDir = !options.catalogDir.isEmpty() ? options.catalogDir
                                   : !m_settingsFile.isEmpty()   ? QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/Catalog")
                                                                 : QDir::temp().filePath(QStringLiteral("hikari-catalogs-%1").arg(QCoreApplication::applicationPid()));
        StyleManagerController::Hooks hooks;
        hooks.target = [this] { return targetSession(); };
        hooks.windowTarget = [this](int shellWindow) { return windowSession(shellWindow); };
        hooks.targetDocument = [this] { return m_workspace.editingTarget(); };
        hooks.session = [this](application::DocumentId id) -> application::EditSession * {
            return id == m_workspace.reference() ? nullptr : m_files->session(id);
        };
        hooks.documents = [this] {
            std::vector<application::EditSession *> out;
            for (const auto id : m_workspace.documents())
                if (auto *session = m_files->session(id); session && id != m_workspace.reference())
                    out.push_back(session);
            return out;
        };
        hooks.refresh = [this] {
            editor().reloadFromSession();
            refreshViews();
        };
        hooks.fonts = [this] {
            return subtitleFonts(m_workspace.editingTarget());
        };
        hooks.lastCatalog = [this] { return m_settings->text("styles.lastCatalog"); };
        hooks.catalogChosen = [this](const QString &name) { m_settings->set("styles.lastCatalog", name); };
        hooks.projectRoot = [this] { return m_projects ? m_projects->root(m_workspace.activeWindow()) : QString(); };
        m_styleManager = std::make_unique<StyleManagerController>(std::filesystem::path(catalogDir.toStdU16String()), std::move(hooks));
    }
    {
        AlignToVideoController::Hooks hooks;
        hooks.video = [this] { return videoState() == application::VideoState::None ? nullptr : &activeMedia().video->session(); };
        hooks.target = [this] { return targetSession(); };
        hooks.pause = [this] {
            if (activeMedia().video->playing())
                activeMedia().video->pause();
        };
        hooks.refresh = [this] {
            editor().reloadFromSession();
            refreshViews();
        };
        m_alignToVideo = std::make_unique<AlignToVideoController>(*m_settings, std::move(hooks));
    }
    {
        VideoAnalysisController::Hooks hooks;
        hooks.video = [this] { return videoState() == application::VideoState::None ? nullptr : &activeMedia().video->session(); };
        hooks.target = [this] { return targetSession(); };
        hooks.pause = [this] {
            if (activeMedia().video->playing())
                activeMedia().video->pause();
        };
        hooks.refresh = [this] {
            editor().reloadFromSession();
            refreshViews();
        };
        hooks.clipboard = [] { return QGuiApplication::clipboard()->text(); };
        m_videoAnalysis = std::make_unique<VideoAnalysisController>(std::move(hooks));
        connect(m_videoAnalysis.get(), &VideoAnalysisController::previewChanged, this, [this] {
            if (activeMedia().videoShowsTemplate || m_videoAnalysis->videoPreview())
                showVideoSubtitles();
        });
    }
    {
        LyricsImportController::Hooks hooks;
        hooks.target = [this] { return targetSession(); };
        hooks.refresh = [this] {
            editor().reloadFromSession();
            refreshViews();
        };
        hooks.openNew = [this](core::LoadResult load, const QString &path) {
            application::StagedOpen staged{{}, std::move(load), std::nullopt, 0, 0};
            staged.imported = true;
            (void)publish(std::move(staged), path, false);
        };
        hooks.describe = [this](const QString &path, const core::LyricsProblem &problem) {
            return openProblem(path, application::StageFailure{application::OpenError::MalformedLyrics, problem});
        };
        m_lyricsImport = std::make_unique<LyricsImportController>(*m_settings, std::move(hooks));
    }
    {
        QcNotesController::Hooks hooks;
        hooks.target = [this] { return targetSession(); };
        hooks.editable = [this] {
            const auto target = m_workspace.editingTarget();
            return target && m_workspace.checkContentCommand(*target).has_value();
        };
        hooks.refresh = [this] {
            editor().reloadFromSession();
            refreshViews();
        };
        hooks.selectLine = [this](core::LineId line) { selectLine(line.value); };
        hooks.seek = [this](int ms) { return activeMedia().video->hasVideo() && activeMedia().video->session().seekToMs(ms); };
        hooks.videoTime = [this]() -> std::optional<int> {
            return activeMedia().video->hasVideo() ? std::optional(activeMedia().video->session().tellMs()) : std::nullopt;
        };
        hooks.marks = [this](QcNotesController::Marks marks, std::optional<core::LineId> current) {
            shell().setQcMarks(std::move(marks), current);
        };
        m_qcNotes = std::make_unique<QcNotesController>(*m_settings, std::move(hooks));
    }
    {
        FontCollectorController::Hooks hooks;
        hooks.projectFontFolder = [this] { return projectFontFolder(m_workspace.activeWindow()); };
        hooks.folderFonts = [this] {
            auto fonts = projectFonts(std::nullopt);
            if (m_fontCatalogs)
                std::ranges::move(m_fontCatalogs->externalFontLeases(), std::back_inserter(fonts));
            return fonts;
        };
        hooks.tabs = [this] {
            std::vector<FontCollectorController::Tab> out;
            for (const auto id : m_workspace.allTabs()) {
                FontCollectorController::Tab tab;
                if (auto *session = m_files->session(id))
                    tab.document = std::make_shared<core::Document>(session->document());
                if (const auto destination = m_files->destination(id))
                    tab.path = QString::fromStdString(destination->value);
                if (const auto media = m_tabMedia.find(id.value); media != m_tabMedia.end())
                    tab.video = media->second.video;
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
        m_fontCollector->setDisplayRenderer(QString::fromStdString(m_renderer.provider()));
        m_fontCollector->setMatroskaPort(
            std::make_unique<backends::FfmsMatroska>(mediaHelperPath(options.mediaHelper)));
    }
    {
        MatroskaController::Hooks hooks;
        hooks.videoPath = [this] { return targetVideoPath(); };
        hooks.modified = [this] {
            auto *session = targetSession();
            return session && session->isDirty();
        };
        hooks.targetHasSubtitlesPath = [this] {
            const auto target = m_workspace.editingTarget();
            return target && (!targetUntitled() || m_importPaths.contains(target->value));
        };
        hooks.apply = [this](application::MatroskaLoaded loaded, bool newTab) {
            applyMatroska(std::move(loaded), newTab);
        };
        hooks.started = [this] {
            m_matroskaTarget = m_workspace.editingTarget();
            auto *session = targetSession();
            m_matroskaRevision = session ? session->revision() : 0;
            m_matroskaDraftLine = session ? session->draftLine() : std::nullopt;
            m_matroskaDraft = session ? session->draftText() : std::nullopt;
            m_matroskaReloads = m_reloads;
            m_matroskaVideo = targetVideoPath();
            m_matroskaRequested = activeMedia().video->requestedVideo();
            m_matroskaFromHome = !m_matroskaTarget;
        };
        hooks.log = [this](const QString &text) {
            m_log->notice(ui::LogController::Error,
                          QCoreApplication::translate("HikariSub.Matroska", "Load subtitles from an MKV/OGM file"), text);
        };
        m_matroska = std::make_unique<MatroskaController>(
            std::make_unique<backends::FfmsMatroska>(mediaHelperPath(options.mediaHelper)), std::move(hooks));
        connect(this, &Application::tabsChanged, m_matroska.get(), &MatroskaController::refresh);
    }
    {
        QString dir = options.fontCatalogDir;
        if (dir.isEmpty() && !m_settingsFile.isEmpty())
            dir = QFileInfo(m_settingsFile).absolutePath();
        if (dir.isEmpty()) {
            m_fontCatalogTemp = std::make_unique<QTemporaryDir>();
            dir = m_fontCatalogTemp->path();
        }
        FontCatalogsController::Hooks hooks;
        hooks.documents = [this](bool all) {
            std::vector<std::shared_ptr<const core::Document>> out;
            const auto tabs = m_workspace.tabs();
            const int current = currentTab();
            for (int i = 0; i < int(tabs.size()); ++i)
                if (all || i == current)
                    if (auto *session = m_files->session(tabs[std::size_t(i)]))
                        out.push_back(std::make_shared<core::Document>(session->document()));
            return out;
        };
        hooks.log = [this](const QString &text) {
            m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Fonts", "Font catalogues"), text);
        };
        hooks.documentFonts = [this] {
            auto fonts = attachedFontLeases(m_workspace.editingTarget());
            for (auto &font : projectFonts(m_workspace.editingTarget()))
                fonts.push_back(std::move(font));
            return fonts;
        };
        hooks.projectFonts = [this](int shellWindow) {
            const auto found = m_projectFonts.find(shellWindow);
            return found == m_projectFonts.end() ? std::vector<application::FontLease>() : found->second.fonts;
        };
        const auto report = listFileReport(*m_log);
        hooks.fileConverted = report.converted;
        hooks.fileProblem = report.problem;
        hooks.fileReplaced = report.replaced;
        hooks.fontsChanged = [this] { subtitleFontsChanged(); };
        m_fontCatalogs = std::make_unique<FontCatalogsController>(*m_settings, std::filesystem::path(dir.toStdU16String()),
                                                                  std::move(hooks));
        setVideoFonts(std::nullopt);
    }
    applySubtitlesAt();
    m_recoveryDir = options.recoveryDir;
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const int capacity = m_settings->integer("autosave.maxFiles");
    if (!m_recoveryDir.isEmpty()) {
        QDir().mkpath(m_recoveryDir + QStringLiteral("/sessions"));
        m_sessionLock = std::make_unique<QLockFile>(m_recoveryDir + QStringLiteral("/sessions/") + m_sessionId + QStringLiteral(".lock"));
        m_sessionLock->tryLock(0);
    }
    m_recovery = std::make_unique<application::RecoveryStore>(
        m_recoveryDir.isEmpty() ? std::filesystem::path() : std::filesystem::path(m_recoveryDir.toStdU16String()),
        m_sessionId.toStdString(), m_recoveryDir.isEmpty() ? 0 : capacity);
    m_recovery->prune(std::chrono::system_clock::now());
    m_spellingText = backends::legacySpellingText(true);
    m_bundledDictionaryDir = options.bundledDictionaryDir;
    m_spellingOn = m_settings->boolean("editor.spellchecker");
    if (!m_dictionaryDir.isEmpty() && options.spellingBackend) {
        m_spellingFolders.emplace_back(m_dictionaryDir.toStdU16String());
        if (!m_bundledDictionaryDir.isEmpty() && m_bundledDictionaryDir != m_dictionaryDir)
            m_spellingFolders.emplace_back(m_bundledDictionaryDir.toStdU16String());
    }
    setUpGrammar(options);
    m_spellingLoader = options.spellingBackend;
    spellSlot(m_settings->text("editor.dictionaryLanguage"));
    m_recent.set(m_settings->settings().list("recent.subtitles"));
    m_recent.setOpenedRecords(m_settings->settings().list("recent.subtitlesOpened"));
    m_projects = std::make_unique<ProjectWindows>(
        *m_settings, m_settingsFile.isEmpty() ? QString() : QFileInfo(m_settingsFile).absolutePath());
    connect(m_projects.get(), &ProjectWindows::changed, this, &Application::applyWindowProject);
    connect(m_projects.get(), &ProjectWindows::changed, this, &Application::projectsChanged);
    connect(m_projects.get(), &ProjectWindows::changed, m_agent.get(), &AgentController::projectChanged);
    connect(m_projects.get(), &ProjectWindows::recentChanged, this, &Application::recentProjectsChanged);
    m_copyColumns = int(m_settings->integer("grid.copyColumns") | (m_settings->integer("grid.copyColumnTags") & core::column::Tags));
    m_pasteColumns = int(m_settings->integer("grid.pasteColumns") | (m_settings->integer("grid.pasteColumnTags") & core::column::Tags));
    m_editRefresh = new QTimer(this);
    m_editRefresh->setSingleShot(true);
    m_editRefresh->setInterval(0);
    connect(m_editRefresh, &QTimer::timeout, this, &Application::refreshVideo);
    m_automation->setAudioBox([this]() -> const application::AudioBox * { return activeMedia().audio ? &activeMedia().audio->box() : nullptr; });
    m_automation->setProjectSources(
        [this] {
            const auto target = m_workspace.editingTarget();
            const auto media = target ? m_tabMedia.find(target->value) : m_tabMedia.end();
            return media == m_tabMedia.end() ? std::string() : media->second.keyframes.toStdString();
        },
        [this] { return timecodesFile().toStdString(); },
        [this] { return activeViews().visualTools ? visualTools().videoView().displayScale() : 1.0; });
    {
        const QString cacheDir = !options.audioCacheDir.isEmpty() ? options.audioCacheDir
                                 : !m_settingsFile.isEmpty()
                                     ? QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/AudioCache")
                                     : QDir::temp().filePath(QStringLiteral("hikari-audio-cache-%1").arg(QCoreApplication::applicationPid()));
        m_indexDir = !options.indexDir.isEmpty() ? options.indexDir
                     : !m_settingsFile.isEmpty() ? QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/Indices")
                                                 : QString();
        m_audioSettings = [this, cacheDir] {
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
    }
    {
        auto make = options.makeAudioOutput;
        const bool windowsApis = options.outputHostApiSetting;
        m_sharedOutput = [this, make, windowsApis]() -> application::AudioOutputPort & {
            backends::PortAudioOutput::Options output;
            output.hostApi = backends::PortAudioOutput::hostApiForSetting(m_settings->integer("audio.outputHostApi"),
                                                                          windowsApis);
            if (!m_audioOutput || output.hostApi != m_audioOutputHostApi) {
                m_audioOutput.reset();
                if (make)
                    m_audioOutput = make(output);
                else if (m_playbackAudio)
                    m_audioOutput = std::make_unique<backends::PortAudioOutput>(output);
                else
                    m_audioOutput = std::make_unique<backends::SimulatedOutput>();
                m_audioOutputHostApi = output.hostApi;
            }
            return *m_audioOutput;
        };
    }
    makeAudio(*m_mainMedia);
    if (options.makeAudioDevices)
        m_audioDevices = options.makeAudioDevices();
    else if (options.playbackAudio)
        m_audioDevices = std::make_unique<backends::QtAudioDevices>();
    else
        m_audioDevices = std::make_unique<application::NoAudioDevices>();
    m_recentAudio.set(m_settings->settings().list("recent.audio"));
    if (const int chars = m_settings->integer("program.tabTextMaxChars"); chars > 19)
        m_tabTextMax = chars;
    m_recentVideo.set(m_settings->settings().list("recent.video"));
    m_recentKeyframes.set(m_settings->settings().list("recent.keyframes"));
    m_recentTimecodes.set(m_settings->settings().list("recent.timecodes"));
    startLocalisation();
    setupTabMenu(options);
    wireMedia(*m_mainMedia);
    reportUserFolder(options.userFolderMove, options.userFolderSeed);
    reportSettingsFiles();
    convertListFiles();
    refreshViews();
}

void Application::makeVideo(WindowMedia &media)
{
    media.videoHelper = std::make_unique<backends::FfmsIndexedSource>(m_mediaHelper);
    media.videoSource = std::make_unique<application::DummyVideoSource>(*media.videoHelper);
    media.video = std::make_unique<ui::VideoController>(*media.videoSource, m_renderer);
    media.video->setMediaInfo(media.videoHelper.get(), media.videoHelper.get());
    media.generalPlayer = std::make_unique<backends::QtGeneralPlayer>(m_playbackAudio);
    media.video->session().setGeneralPlayer(media.generalPlayer.get());
    media.video->session().setBeforePlay([this, &media] { takePlayback(media); });
    media.video->session().setFrameWorker([this](std::function<void()> task) {
        QMetaObject::invokeMethod(this, std::move(task), Qt::QueuedConnection);
    });
    connect(media.generalPlayer.get(), &backends::QtGeneralPlayer::frameDelivered, this, [&media](const QVideoFrame &frame) {
        const qint64 end = frame.endTime();
        const auto colour = media.video->session().playerColour();
        const bool planes = media.video->session().presenterConvertsYcbcr();
        media.video->session().submitGeneralFrame(
            frame.startTime(), end > frame.startTime() ? std::optional<std::int64_t>(end) : std::nullopt,
            [frame, colour, planes](application::FramePool &pool) -> std::shared_ptr<application::IndexedFrame> {
                auto out = pool.acquire();
                const bool made = planes ? backends::videoFrameToYcbcr(frame, *out, colour)
                                         : backends::videoFrameToBgra(frame, *out, colour);
                return made ? out : nullptr;
            });
    });
    media.video->session().setLog([this](const std::string &) {
        m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Options", "Video"),
                      QCoreApplication::translate("HikariSub.Video", "Cannot change YCbCr matrix"));
    });
}

void Application::makeAudio(WindowMedia &media)
{
    media.audioHelper = std::make_unique<backends::FfmsIndexedSource>(m_mediaHelper);
    media.audio = std::make_unique<ui::AudioController>(*media.audioHelper);
    media.audio->setSettings(m_audioSettings);
    media.audio->setSettingsStore(m_settings.get());
    media.audioPlayer = std::make_unique<backends::AudioBoxPlayer>(
        m_sharedOutput,
        backends::AudioBoxPlayer::SharedAudio([&media]() -> std::shared_ptr<const application::DisplayAudio> {
            return media.audio ? media.audio->box().sharedAudio() : nullptr;
        }));
    media.audio->setPlayer(media.audioPlayer.get());
    media.audioPlayer->setMono(m_settings->boolean("audio.playMono"));
    media.audio->setPlaybackSettings([this] { return m_settings->integer("audio.markPlayTime"); });
    media.audio->setVideoPlayback([&media] { return media.video->playing(); }, [&media] { media.video->pause(); });
    media.audio->setBeforePlay([this, &media] { takePlayback(media); });
    media.audioConnections << connect(media.audioPlayer.get(), &backends::AudioBoxPlayer::failed, this,
                                  [this](const QString &message) {
                                      m_log->notice(ui::LogController::Error,
                                                    QCoreApplication::translate("HikariSub.Options", "Audio"), message);
                                  });
}

void Application::wireMedia(WindowMedia &media)
{
    connect(media.video.get(), &ui::VideoController::changed, this, [this, &media] {
        if (!m_audioRouting && !media.video->dummy() && media.video->session().state() != application::VideoSession::State::Closed)
            ensureAudioRoute();
    });
    media.audioConnections << connect(media.audio.get(), &ui::AudioController::changed, this, [this, &media] {
        if (!m_audioRouting && media.audio->box().state() != application::AudioBox::State::Closed)
            ensureAudioRoute();
    });
    connect(media.video.get(), &ui::VideoController::changed, this, [this, &media] {
        if (directShowPlayback() && holdsDirectShow(media))
            media.video->session().preparePlayer();
    });
    connect(media.video.get(), &ui::VideoController::changed, m_analytics.get(), [this, &media] {
        using State = application::VideoSession::State;
        const State state = media.video->session().state();
        const State before = std::exchange(media.videoUsageState, state);
        if (state == before)
            return;
        if (state == State::Opening)
            media.videoOpenClock.start();
        else if (state == State::Ready || state == State::Failed)
            m_analytics->mediaOpened(true, before == State::Opening ? media.videoOpenClock.elapsed() : 0, state == State::Ready,
                                     media.video->dummy() ? QStringLiteral("dummy") : QStringLiteral("file"));
    });
    connect(media.video.get(), &ui::VideoController::changed, this, [this, &media] {
        const auto &video = media.video->session();
        const bool ready = video.state() == application::VideoSession::State::Ready;
        const auto timedBy = ready || video.replacedFrameTimes()
                                 ? std::optional(std::pair(ready ? video.path() : std::string(), video.frameTimesSerial()))
                                 : std::nullopt;
        if (timedBy == media.editorTimebaseFor)
            return;
        media.editorTimebaseFor = timedBy;
        views(media.window).editor->setVideoTimebase(timedBy ? std::optional(video.legacyTimebase()) : std::nullopt);
    });
    connect(media.video.get(), &ui::VideoController::changed, this, [this, &media] { logVideoFailure(media); });
    connect(media.video.get(), &ui::VideoController::changed, this, [this, &media] { logPlaybackFailure(media); });
    connect(media.video.get(), &ui::VideoController::changed, this, [this, &media] {
        const auto &video = media.video->session();
        if (video.state() == application::VideoSession::State::Opening)
            media.resolutionCheckedVideo.clear();
        if (&media != &activeMedia() || video.state() != application::VideoSession::State::Ready || !video.lastFrame())
            return;
        const QString path = QString::fromStdString(video.path());
        if (path == media.resolutionCheckedVideo)
            return;
        media.resolutionCheckedVideo = path;
        checkVideoMatrix();
        checkResolution();
        checkLayoutResolution();
    });
    connect(media.video.get(), &ui::VideoController::changed, this, [this, &media] {
        if (&media != &activeMedia() || media.pendingKeyframes.isEmpty()
            || media.video->session().state() != application::VideoSession::State::Ready)
            return;
        const QString path = std::exchange(media.pendingKeyframes, QString());
        const QString problem = openKeyframes(QUrl::fromLocalFile(path));
        if (!problem.isEmpty())
            m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.MainWindow", "Open keyframes"),
                          problem);
    });
    media.audioConnections << connect(media.audio.get(), &ui::AudioController::changed, this, [this, &media] {
        using State = application::AudioBox::State;
        const application::AudioBox &box = media.audio->box();
        const State state = box.state();
        const State before = std::exchange(media.audioUsageState, state);
        if (state == before)
            return;
        if (state == State::Opening || state == State::Loading) {
            if (state == State::Opening || !media.audioOpenClock.isValid())
                media.audioOpenClock.start();
            return;
        }
        const qint64 elapsed = media.audioOpenClock.isValid() ? media.audioOpenClock.elapsed() : 0;
        const bool opening = media.audioOpenClock.isValid() || state == State::Ready;
        media.audioOpenClock.invalidate();
        const bool failed = box.error() && *box.error() != application::SourceError::Cancelled;
        if (!opening || (state == State::Closed && !failed))
            return;
        m_analytics->mediaOpened(false, elapsed, state == State::Ready,
                                 box.fromVideo()                                    ? QStringLiteral("from_video")
                                 : application::AudioBox::isDummyName(box.path()) ? QStringLiteral("dummy")
                                                                                   : QStringLiteral("file"));
    });
    media.audioConnections << connect(media.audio.get(), &ui::AudioController::opened, this,
                                  [this](const QString &path, bool) { rememberRecentAudio(path); });
    media.audioConnections << connect(media.audio.get(), &ui::AudioController::logged, this, [this](const QString &message, bool debug, bool error) {
        if (debug)
            qDebug().noquote() << message;
        else
            m_log->notice(error ? ui::LogController::Error : ui::LogController::Warning,
                          QCoreApplication::translate("HikariSub.MainWindow", "Open audio"), message);
    });
    media.audioConnections << connect(media.video.get(), &ui::VideoController::changed, this, [this, &media] { followVideoInAudio(media); });
    media.audioConnections << connect(media.audio.get(), &ui::AudioController::changed, this, [this, &media] { releaseReadIndexHandoff(media); });
    media.audioConnections << connect(media.audio.get(), &ui::AudioController::findingOnsetsChanged, this,
                                  &Application::noticeOnsets);
    media.audioConnections << connect(media.audio.get(), &ui::AudioController::changed, this, [this, &media] {
        if (!media.video->playing())
            for (auto *views : allViews())
                views->templateEditor->hostChanged();
    });
    media.audio->setTimingSettings([this] {
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
        hooks.commit = [this](const application::AudioCommitRequest &request) { return commitAudioTimes(request); };
        hooks.setActive = [this](int key) { setAudioActive(key); };
        hooks.seekVideo = [this](int ms) { seekVideoFromAudio(ms); };
        hooks.fieldTime = [this](int ms) {
            const auto *session = targetSession();
            return session ? application::committedAudioTime(session->document(), ms) : ms;
        };
        hooks.severalSelected = [this] {
            const auto *session = targetSession();
            return session && session->selection().selected.size() > 1;
        };
        media.audio->setTimingHooks(std::move(hooks));
    }
    media.audio->setLineText([this](int key) -> QString {
        const auto target = m_workspace.editingTarget();
        auto *session = target ? m_files->session(*target) : nullptr;
        if (!session)
            return {};
        const auto &lines = session->document().lines();
        if (key < 0 || key >= static_cast<int>(lines.size()))
            return {};
        const auto *line = lines[static_cast<std::size_t>(key)];
        return QString::fromStdU16String(core::toUtf16(line->translation.empty() ? line->text : line->translation));
    });
    media.audio->setFollowAudio([this, &media](int ms) { showVideoAtAudio(media, ms); });
    media.video->setOpenFilter([this, &media](const QString &path, std::function<void(application::IndexRequest)> asVideo) {
        QPointer<Application> self(this);
        QPointer<ui::AudioController> audio(media.audio.get());
        media.videoHelper->probe(path.toStdString(), [self, audio, path, asVideo = std::move(asVideo)](
                                                     std::expected<application::MediaProbe, application::AudioFailure> probe) {
            if (!self || !audio)
                return;
            if (!probe)
                return asVideo({});
            if (!probe->hasVideo && !probe->audio.empty())
                return audio->openAudio(path);
            const auto choice = application::legacyAudioTrackChoice(probe->audio, self->m_audioSettings().acceptedStreams);
            auto openWith = [self, path, asVideo](int track) {
                if (self)
                    asVideo({track, self->indexFile(path, track).toStdString()});
            };
            if (choice.track || choice.rows.empty())
                return openWith(choice.track.value_or(-1));
            audio->askTrack(choice.rows, [openWith, tracks = choice.rowTracks](std::optional<int> row) {
                if (row && *row >= 0 && *row < static_cast<int>(tracks.size()))
                    openWith(tracks[static_cast<std::size_t>(*row)]);
            });
        });
    });
    const auto documentForMedia = [this] {
        if (home())
            newDocument();
    };
    media.video->setBeforeOpen([this, &media, documentForMedia] {
        documentForMedia();
        if (!media.enteringTabMedia)
            closeTimecodes();
    });
    media.audio->setBeforeOpen(documentForMedia);
    trackTabMedia(media);
    trackVideoSources(media);
    trackVideoFollow(media);
    trackOffers(media);
}

void Application::reportUserFolder(const UserFolder::MoveReport &moved, const UserFolder::SeedReport &seeded)
{
    const QString title = QCoreApplication::translate("HikariSub.MainWindow", "User folder");
    const QString folder = QDir::toNativeSeparators(m_userFolder);
    if (!moved.from.isEmpty()) {
        QString text = QCoreApplication::translate("HikariSub.MainWindow",
                                                   "Your settings and files moved to %1. Help > Open user folder opens it.")
                           .arg(folder);
        if (!moved.leftOver.isEmpty())
            text += QLatin1Char(' ') + QCoreApplication::translate("HikariSub.MainWindow",
                                                                   "%n item(s) stayed where they were, because the user folder already had them.",
                                                                   nullptr, int(moved.leftOver.size()));
        if (!moved.keptBack.isEmpty())
            text += QLatin1Char(' ') + QCoreApplication::translate("HikariSub.MainWindow",
                                                                   "The old DependencyControl stayed behind: HikariSub brings a newer one.");
        QString details =
            QCoreApplication::translate("HikariSub.MainWindow", "Moved from: %1").arg(moved.from.join(QStringLiteral(", ")));
        if (!moved.leftOver.isEmpty())
            details += QLatin1Char('\n') + QCoreApplication::translate("HikariSub.MainWindow", "Not moved: %1")
                                               .arg(moved.leftOver.join(QStringLiteral(", ")));
        if (!moved.keptBack.isEmpty())
            details += QLatin1Char('\n') + QCoreApplication::translate("HikariSub.MainWindow", "Left behind: %1")
                                               .arg(moved.keptBack.join(QStringLiteral(", ")));
        m_log->notice(ui::LogController::Info, title, text, details);
    }
    if (!moved.earlierSettings.isEmpty())
        m_log->notice(ui::LogController::Info, title,
                      QCoreApplication::translate("HikariSub.MainWindow",
                                                  "This portable copy keeps its settings in %1. An earlier version’s "
                                                  "settings are still in %2: to use them, quit HikariSub, copy "
                                                  "hikari.ini from there and delete settings.toml here.")
                          .arg(folder, moved.earlierSettings));
    for (const QString &path : seeded.kept)
        m_log->log(QCoreApplication::translate("HikariSub.MainWindow",
                                               "Kept your changed %1; the new bundled version was not copied over it.")
                       .arg(QDir::toNativeSeparators(path)),
                   ui::LogController::Info, title);
    for (const QString &path : seeded.failed)
        m_log->log(QCoreApplication::translate("HikariSub.MainWindow", "Could not copy the bundled %1 into the user folder.")
                       .arg(QDir::toNativeSeparators(path)),
                   ui::LogController::Warning, title);
    if (!seeded.removed.isEmpty())
        m_log->log(QCoreApplication::translate("HikariSub.MainWindow",
                                               "Removed %n bundled file(s) this version no longer brings.", nullptr,
                                               int(seeded.removed.size())),
                   ui::LogController::Info, title);
    if (const qsizetype count = seeded.copied.size() + seeded.updated.size(); count > 0)
        m_log->log(QCoreApplication::translate("HikariSub.MainWindow", "Copied %n bundled file(s) into the user folder.", nullptr,
                                               int(count)),
                   ui::LogController::Info, title);
}

void Application::reportSettingsFiles()
{
    const QString title = QCoreApplication::translate("HikariSub.MainWindow", "Settings");
    if (m_settings->converted())
        m_log->log(QCoreApplication::translate("HikariSub.MainWindow",
                                               "Your settings moved from hikari.ini to settings.toml, hotkeys.toml and "
                                               "state.json. The old file is kept as hikari.ini.old."),
                   ui::LogController::Info, title);
    for (const auto &problem : m_settings->problems()) {
        const QString file = QString::fromStdString(application::displayPath(problem.file));
        const QString where = problem.failure.line > 0
                                  ? QCoreApplication::translate("HikariSub.MainWindow", "%1, line %2, column %3")
                                        .arg(file)
                                        .arg(problem.failure.line)
                                        .arg(problem.failure.column)
                                  : file;
        const QString text =
            problem.keptAs.empty()
                ? QCoreApplication::translate("HikariSub.MainWindow",
                                              "%1 could not be read, so its settings are at their defaults for now. "
                                              "HikariSub will not write over it.")
                      .arg(QFileInfo(file).fileName())
                : QCoreApplication::translate("HikariSub.MainWindow",
                                              "%1 could not be read, so its settings are back at their defaults. "
                                              "The file is kept as %2: fix it while HikariSub is closed and give it "
                                              "its name back to use it again.")
                      .arg(QFileInfo(file).fileName(),
                           QString::fromStdString(application::displayPath(problem.keptAs.filename())));
        m_log->notice(ui::LogController::Warning, title, text,
                      where + QLatin1String(": ") + QString::fromStdString(problem.failure.message));
    }
    for (const auto &ignored : m_settings->ignored())
        m_log->log(QCoreApplication::translate("HikariSub.MainWindow", "hotkeys.toml: %1")
                       .arg(QString::fromStdString(ignored)),
                   ui::LogController::Info, title);
}

void Application::ensureAudioRoute()
{
    if (m_audioRouting)
        return;
    m_audioRouting = std::make_unique<application::AudioOutputRouting>(
        *m_audioDevices, [this] { return m_settings->settings().text("audio.outputDevice"); },
        [this](const application::OutputRoute &route) { routeAudio(route, true); });
    routeAudio(m_audioRouting->route(), false);
}

void Application::routeAudio(const application::OutputRoute &route, bool move)
{
    activeMedia().generalPlayer->setOutputDevice(route.id);
    activeMedia().audioPlayer->setOutputDevice(route.systemDefault ? std::string() : route.name);
    if (move)
        activeMedia().audioPlayer->switchOutput();
    noticeAudioRoute(std::exchange(m_noticedRoute, route), route);
}

void Application::refreshAudio()
{
    if (!activeMedia().audio)
        return;
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
                activeMedia().audio->setActiveText(core::toUtf16(line->translation.empty() ? line->text : line->translation));
            }
            lines.push_back({static_cast<int>(line->start.value.microseconds() / 1000),
                             static_cast<int>(line->end.value.microseconds() / 1000), shown(line->id)});
        }
        if (activeId)
            key = std::tuple(target->value, activeId->value, session->revision());
    }
    bool select = key && key != activeMedia().audioLine;
    if (select && m_audioCommitting && activeMedia().audioLine && std::get<0>(*key) == std::get<0>(*activeMedia().audioLine) &&
        std::get<1>(*key) == std::get<1>(*activeMedia().audioLine))
        select = false;
    activeMedia().audioLine = key;
    activeMedia().audio->setLines(std::move(lines), active, select);
}

void Application::followVideoInAudio(WindowMedia &media)
{
    const auto &video = media.video->session();
    const auto state = video.state();
    if (state == application::VideoSession::State::Opening) {
        media.audioFollowedVideo.clear();
        media.pendingIndexHandoff.clear();
    } else if (state == application::VideoSession::State::Closed) {
        media.audioFollowedVideo.clear();
    }
    if (state == application::VideoSession::State::Ready && media.audioFollowedVideo != QString::fromStdString(video.path())) {
        media.audioFollowedVideo = QString::fromStdString(video.path());
        if (keepTabAudio(media, media.audioFollowedVideo)) {
        } else if (video.dummy()) {
            if (const QString own = dummyVideoAudio(media.audioFollowedVideo);
                !own.isEmpty() && !(media.audio->hasAudio() && media.audio->path() == own))
                media.audio->openAudio(own);
        } else if (video.hasAudio()) {
            media.audio->openFromVideo(media.audioFollowedVideo, video.audioTrack(), video.newIndex(),
                                   QString::fromStdString(video.indexHandoff()));
            media.pendingIndexHandoff = video.indexHandoff();
            releaseReadIndexHandoff(media);
        } else if (media.audio->hasAudio())
            media.audio->closeAudio();
        media.audio->trimCache();
    }
    if (state != application::VideoSession::State::Ready) {
        if (media.audioTimebase) {
            media.audioTimebase.reset();
            media.audioKeyframes.clear();
            media.audio->setKeyframes({});
            media.audio->setKeyframeSnapTimes({});
        }
        media.audio->setVideoFrame(std::nullopt);
        syncAudioWithVideo(media);
        return;
    }
    if (!media.audioTimebase || media.audioTimebasePath != video.path() || media.audioTimebaseSerial != video.frameTimesSerial()
        || media.audioKeyframes != video.keyframes()) {
        media.audioTimebasePath = video.path();
        media.audioTimebaseSerial = video.frameTimesSerial();
        media.audioKeyframes = video.keyframes();
        media.audioTimebase = video.legacyTimebase();
        std::vector<int> ms;
        ms.reserve(media.audioKeyframes.size());
        for (const int frame : media.audioKeyframes)
            ms.push_back(media.audioTimebase->msAt(frame));
        std::vector<int> snap;
        snap.reserve(ms.size());
        for (const int keyMs : ms)
            snap.push_back(media.audioTimebase->startTimeFor(media.audioTimebase->frameAt(keyMs)));
        media.audio->setKeyframeSnapTimes(std::move(snap));
        media.audio->setKeyframes(std::move(ms));
    }
    std::optional<application::AudioVideoFrame> paused;
    if (!video.playing() && !video.stopped())
        if (const auto frame = video.reportedFrame())
            paused = application::audioVideoFrame(*media.audioTimebase, *frame);
    media.audio->setVideoFrame(paused);
    syncAudioWithVideo(media);
}

void Application::rememberRecentAudio(const QString &path)
{
    if (application::AudioBox::isDummyName(path.toStdString()))
        return;
    m_recentAudio.add(path.toStdString());
    m_settings->settings().set("recent.audio", m_recentAudio.entries());
}

bool Application::commitAudioTimes(const application::AudioCommitRequest &request)
{
    auto *session = targetSession();
    if (!session || !editor().editable())
        return false;
    const bool before = std::exchange(m_audioCommitting, true);
    editor().timesFromAudio(request.startMs, request.endMs, request.duration);
    const auto outcome = application::commitAudioTimes(*session, request, shownLines());
    if (request.save)
        editor().audioSent();
    editor().reloadFromSession();
    if (outcome && outcome->stepped)
        refreshViews();
    m_audioCommitting = before;
    if (outcome && outcome->next) {
        m_lineChangeOrigin = LineChangeOrigin{false, true};
        applySelection(gridSelection().plain(session->selection(), *outcome->next));
        m_lineChangeOrigin.reset();
    }
    return outcome.has_value();
}

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

void Application::seekVideoFromAudio(int ms)
{
    auto &video = activeMedia().video->session();
    if (video.state() != application::VideoSession::State::Ready)
        return;
    if (ms <= 0)
        video.showFrame(0);
    else
        video.seekTo(core::DocumentTime(static_cast<std::int64_t>(ms) * 1000));
}

void Application::setAudioFromVideo(bool mark)
{
    if (!activeMedia().audio->hasAudio())
        return;
    int time = 0;
    const auto &video = activeMedia().video->session();
    if (video.state() == application::VideoSession::State::Ready)
        if (const auto frame = video.reportedFrame())
            time = video.legacyTimebase().msAt(*frame);
    activeMedia().audio->showTime(time, mark);
}

void Application::openAudioFromVideo()
{
    const auto &video = activeMedia().video->session();
    if (video.state() == application::VideoSession::State::Ready) {
        if (const QString own = dummyVideoAudio(QString::fromStdString(video.path())); !own.isEmpty())
            return activeMedia().audio->openAudio(own);
        activeMedia().audio->openAudio(QString::fromStdString(video.path()));
        if (const auto target = m_workspace.editingTarget())
            m_tabMedia[target->value].audioFromVideo = true;
    }
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
    QString from = QString::fromStdString(activeMedia().video->session().path());
    if (from.isEmpty() && !m_recentVideo.entries().empty())
        from = QString::fromStdString(m_recentVideo.entries().front());
    if (from.isEmpty() || application::isDummyVideo(from.toStdString()))
        return {};
    return QUrl::fromLocalFile(QFileInfo(from).absolutePath());
}

void Application::makeViews(WindowViews &v, int shellWindow)
{
    v.settings = std::make_unique<WindowSettings>(m_settings->storage());
    if (m_projects)
        if (const QString root = m_projects->root(shellWindow); !root.isEmpty())
            v.settings->setProject(root, projectTrusted(root));
    const auto active = [this, &v] { return &v == &activeViews(); };
    v.shell = std::make_unique<ui::ShellController>(m_workspace);
    v.shell->setShellWindow(shellWindow);
    v.editor = std::make_unique<ui::LineEditorController>(*m_files);
    v.editor->setCommittedListener([this] { refreshViews(); });
    v.editor->setOptionsSource([this] {
        ui::LineEditorController::Options o;
        o.liveEditing = !m_settings->boolean("video.disableLiveEditing");
        o.dontAdvanceOnTimes = m_settings->boolean("editor.dontGoToNextLineOnTimesEdit");
        o.allCharsForCps = m_settings->boolean("grid.calcSpacesAndPunctuationForCps");
        o.allCharsForWraps = m_settings->boolean("grid.calcSpacesAndPunctuationForWraps");
        o.softLineBreak = m_settings->boolean("editor.softLineBreak");
        return o;
    });
    v.editor->setShowFramesSetting(m_settings->boolean("editor.timesToFramesSwitch"),
                                   [this](bool on) { m_settings->set("editor.timesToFramesSwitch", on); });
    v.editor->setShowTagsSetting(!m_settings->boolean("editor.hideTags"),
                                 [this](bool show) { m_settings->set("editor.hideTags", !show); });
    v.editor->setMoveTags(v.settings->settings().boolean("translation.autoMoveTagsFromOriginal"));
    v.shell->setRunMarks(v.settings->settings().boolean("translation.autoMoveTagsFromOriginal"));
    connect(v.editor.get(), &ui::LineEditorController::frameDisplayChanged, this, [&v] {
        const auto *frames = v.editor->frameTimebase();
        v.shell->setFrameTimebase(frames ? std::optional(*frames) : std::nullopt);
    });
    const auto &video = v.media->video->session();
    if (video.state() == application::VideoSession::State::Ready || video.replacedFrameTimes())
        v.editor->setVideoTimebase(video.legacyTimebase());
    v.editor->setVideoTimeSource([media = v.media]() -> std::optional<std::int64_t> {
        const auto &session = media->video->session();
        const auto frame = session.reportedFrame();
        const auto start = frame ? session.frameStart(*frame) : std::nullopt;
        if (!start)
            return std::nullopt;
        return start->microseconds() / 1000;
    });
    connect(v.editor.get(), &ui::LineEditorController::changed, this, [this, active, &v] {
        if (!active())
            return;
        if (const auto *session = targetSession())
            shell().setDraft(session->draftRecord());
        m_editRefresh->start();
        scheduleAutosave();
        if (v.templateEditor->shown())
            v.templateEditor->refresh();
        if (m_grammarDelay)
            scheduleGrammar();
        refreshAudio();
    });
    connect(v.editor.get(), &ui::LineEditorController::leftLineCommitted, this, [this, active](qulonglong id) {
        if (active())
            m_leftEditedLine = core::LineId{id};
    });
    connect(v.editor.get(), &ui::LineEditorController::lineChanged, this, [this, active, &v](qulonglong id) {
        const auto target = m_workspace.editingTarget();
        auto *session = target ? m_files->session(*target) : nullptr;
        if (!session || !active())
            return;
        session->setSelection(gridSelection().plain(session->selection(), core::LineId{id}));
        shell().setSelection(session->selection());
        editor().selectionChanged();
        followEditingLine();
        v.templateEditor->refresh();
    });
    v.shell->setSpelling([this, shellWindow](std::u16string_view text, core::SubtitleFormat format, bool spell,
                                             int replaceTagsLen) {
        application::SpellChecker *checker = startedSpellChecker(shellWindow);
        const bool on = spell && windowSettings(shellWindow).boolean("editor.spellchecker") && checker;
        return core::legacy::checkTextAndBrackets(text, format, m_spellingText.segment,
                                                  on ? checker->wordCheck() : core::legacy::WordCheck{}, replaceTagsLen);
    });
    v.shell->setChangeStates(
        [this, shellWindow](const core::LineRecord &line) {
            const auto *session = windowSession(shellWindow);
            return session ? session->changeState(line) : 0;
        },
        [this](const core::LineRecord &line) {
            const auto reference = m_workspace.reference();
            const auto *session = reference ? m_files->session(*reference) : nullptr;
            return session ? session->changeState(line) : 0;
        });
    const auto reviewOf = [this](std::optional<application::DocumentId> document) {
        return [this, document](core::LineId line) -> std::optional<ui::LineTableModel::ReviewMark> {
            const ReviewController::Mark *mark = document ? m_review->mark(*document, line) : nullptr;
            if (!mark)
                return std::nullopt;
            return ui::LineTableModel::ReviewMark{mark->editedSince, mark->before};
        };
    };
    v.shell->setReview(
        [this, shellWindow, reviewOf](core::LineId line) { return reviewOf(m_workspace.currentTab(shellWindow))(line); },
        [this, reviewOf](core::LineId line) { return reviewOf(m_workspace.reference())(line); });
    connect(v.editor.get(), &ui::LineEditorController::changed, m_review.get(), &ReviewController::currentChanged);
    v.shell->setHideTags(m_settings->boolean("grid.hideTags"), m_settings->text("grid.tagsSwapCharacter"));
    v.editor->tagList()->setOptionsStore([this] { return m_settings->integer("textEditor.tagListOptions"); },
                                         [this](int options) { m_settings->set("textEditor.tagListOptions", options); });
    v.shell->setHiddenColumns(m_settings->integer("grid.hideColumns"));
    connect(v.shell.get(), &ui::ShellController::hiddenColumnsChanged, this,
            [this, &v] { m_settings->set("grid.hideColumns", v.shell->hiddenColumns()); });
    v.shell->setGridColumnWidths(columnWidthsFrom(m_settings->list("grid.columnWidths")));
    connect(v.shell.get(), &ui::ShellController::gridColumnWidthsChanged, this,
            [this, &v] { m_settings->set("grid.columnWidths", columnWidthEntries(v.shell->gridColumnWidths())); });
    v.shell->setAutoHideColumns(m_settings->boolean("grid.autoHideColumns"));
    connect(v.shell.get(), &ui::ShellController::autoHideColumnsChanged, this, [this, &v] {
        // MUST KILL: the guard keeps Set default's re-sync from setting the key
        if (m_settings->boolean("grid.autoHideColumns") != v.shell->autoHideColumns())
            m_settings->set("grid.autoHideColumns", v.shell->autoHideColumns());
    });
    v.shell->setFoldGenerated(m_settings->boolean("grid.foldGenerated"));
    connect(v.shell.get(), &ui::ShellController::foldGeneratedChanged, this, [this, &v, active] {
        if (active())
            selectFoldedRunsWhole();
        if (m_settings->boolean("grid.foldGenerated") != v.shell->foldGenerated())
            m_settings->set("grid.foldGenerated", v.shell->foldGenerated());
    });
    v.visualTools = std::make_unique<ui::VisualToolsController>(*v.media->video, *m_settings,
                                                                [this, shellWindow] { return windowSession(shellWindow); });
    v.visualTools->setShownLines([this] { return shownLines(); });
    v.visualTools->setEditorHotkey([this](int id) {
        if (!m_hotkeys)
            return application::defaultHotkey({id, application::EditorHotkey});
        return m_hotkeys->accelOf(QString::fromUtf8(std::string(application::hotkeySymbol(id))), application::EditorHotkey)
            .toStdString();
    });
    v.visualTools->setEdited([this] {
        editor().reloadFromSession();
        refreshViews();
    });
    v.visualTools->setPreview([this, media = v.media](const core::Document *preview) {
        auto *session = targetSession();
        if (!session)
            return;
        setVideoFonts(m_workspace.editingTarget());
        media->video->session().setSubtitles(rendererScript(preview ? *preview : session->document()));
        media->videoScript.clear();
    });
    v.visualTools->setLog([this, &v](const QString &text) {
        m_log->notice(ui::LogController::Warning, v.visualTools->toolName(), text);
    });
    v.visualTools->setIgnoreFiltered([this] { return m_gridFilter->ignoreInActions(); });
    const auto visualRole = [this, shellWindow] {
        int role = 0;
        if (const auto *s = windowSession(shellWindow); s && application::translationMode(*s) && s->selection().active) {
            const auto draft = s->draftRecord();
            for (const auto *line : s->document().lines())
                if (line->id == *s->selection().active)
                    role = !((draft && draft->id == line->id) ? draft->translation : line->translation).empty() ? 1 : 0;
        }
        return role;
    };
    v.visualTools->setEditorSelection([&v, visualRole]() { return v.editor->rawFieldSelection(visualRole()); });
    v.visualTools->setEditorSelectionPlacer([&v, visualRole](long from, long to) {
        v.editor->selectRaw(visualRole(), static_cast<int>(from), static_cast<int>(to));
    });
    v.editor->setVisualToolActive([&v] { return v.visualTools && v.visualTools->activeFamily() != 0; });
    if (m_mainViews.visualTools) {
        v.visualTools->setShapesFile(m_mainViews.visualTools->shapesFile());
        v.visualTools->setAllTagsFile(m_mainViews.visualTools->allTagsFile());
        std::vector<std::string> names;
        for (const auto &[bytes, own] : m_leasedFontNames)
            names.insert(names.end(), own.begin(), own.end());
        v.visualTools->setLeasedFonts(names);
    }
    v.videoView = std::make_unique<ui::VideoViewController>(*v.media->video, *v.visualTools, *m_settings);
    v.videoView->setPlayer(v.media->generalPlayer.get());
    v.fullscreen = std::make_unique<ui::VideoFullscreenController>(*v.media->video, *v.videoView, *m_settings);
    v.statusBar = std::make_unique<ui::StatusBarController>(*v.media->video, *v.visualTools,
                                                            [this, shellWindow]() -> const core::Document * {
                                                                auto *session = windowSession(shellWindow);
                                                                return session ? &session->document() : nullptr;
                                                            });
    const auto showTimecodes = [this, &v, shellWindow] {
        v.statusBar->setTimecodesFile(tabTimecodes(m_workspace.currentTab(shellWindow)));
    };
    showTimecodes();
    connect(this, &Application::timecodesChanged, v.statusBar.get(), showTimecodes);
    makeTemplateEditor(v, shellWindow);
    makeNodeEffects(v, shellWindow);
    makeKanjiTimer(v, shellWindow);
    createFindReplace(v, shellWindow);
}

void Application::makeTemplateEditor(WindowViews &v, int shellWindow)
{
    using DocumentKey = ui::TemplateEditorController::DocumentKey;
    const auto active = [this, &v] { return &v == &activeViews(); };
    v.templateEditor = std::make_unique<ui::TemplateEditorController>();
    auto *editor = v.templateEditor.get();
    editor->setDocumentKey([this, shellWindow]() -> DocumentKey {
        const auto tab = m_workspace.currentTab(shellWindow);
        return tab ? DocumentKey{tab->value, m_files->generation(*tab)} : DocumentKey{};
    });
    editor->setSessionSource([this, shellWindow] { return windowSession(shellWindow); });
    editor->setSessionOf([this](DocumentKey key) -> application::EditSession * {
        const application::DocumentId id{key.first};
        auto *session = m_files->session(id);
        return session && m_files->generation(id) == key.second ? session : nullptr;
    });
    editor->setTemplater(&m_automation->templater());
    editor->setHostState([this] { return m_automation->hostState(); });
    connect(v.media->video.get(), &ui::VideoController::changed, editor, [this, editor] {
        if (!activeMedia().video->playing())
            editor->hostChanged();
    });
    connect(editor, &ui::TemplateEditorController::documentEdited, this, [this] { refreshViews(); });
    connect(editor, &ui::TemplateEditorController::otherDocumentEdited, this,
            [this](std::pair<std::uint64_t, std::uint64_t> document) {
                scheduleAutosave(application::DocumentId{document.first});
            });
    connect(editor, &ui::TemplateEditorController::videoChanged, this, [this, active, editor] {
        if (active() && (activeMedia().videoShowsTemplate || editor->videoPreview()))
            showVideoSubtitles();
    });
    connect(v.media->video.get(), &ui::VideoController::changed, editor, [this, active, editor] {
        if (!active() || !editor->videoPreview())
            return;
        const auto &video = activeMedia().video->session();
        const auto frame = video.reportedFrame();
        const auto at = frame ? video.frameStart(*frame) : std::nullopt;
        const auto window = editor->videoWindow(at ? at->microseconds() / 1000 : 0, video.playing());
        if (activeMedia().templateWindow != std::pair(window.fromMs, window.toMs))
            showVideoSubtitles();
    });
    connect(m_automation.get(), &AutomationShell::progressChanged, editor, [editor] { editor->refresh(); });
}

void Application::makeNodeEffects(WindowViews &v, int shellWindow)
{
    const auto active = [this, &v] { return &v == &activeViews(); };
    v.nodeEffects = std::make_unique<ui::NodeEffectsController>();
    auto *effects = v.nodeEffects.get();
    effects->setSessionSource([this, shellWindow] { return windowSession(shellWindow); });
    effects->setLibraryFolder(std::filesystem::path(m_userFolder.toStdU16String()) / u"node-effects");
    effects->setSettings(&m_settings->settings());
    connect(effects, &ui::NodeEffectsController::documentEdited, this, [this] { refreshViews(); });
    connect(effects, &ui::NodeEffectsController::videoChanged, this, [this, active, effects] {
        if (active() && (activeMedia().videoShowsTemplate || effects->videoPreview()))
            showVideoSubtitles();
    });
    effects->shareTrustWith(v.templateEditor.get());
    effects->setLuaNodes([this] { return &luaNodes(); });
    effects->setGlyphs([this] { return &glyphOutlines(); });
    effects->setAudio([this]() -> std::optional<const application::DisplayAudio *> {
        ui::AudioController *audio = activeMedia().audio.get();
        if (!audio || !audio->box().isOpen())
            return std::nullopt;
        return audio->box().audio();
    });
}

void Application::makeKanjiTimer(WindowViews &v, int shellWindow)
{
    KanjiTimerController::Hooks hooks;
    hooks.target = [this, shellWindow] { return windowSession(shellWindow); };
    hooks.targetId = [this, shellWindow] {
        const auto tab = m_workspace.currentTab(shellWindow);
        return tab ? tab->value : 0;
    };
    hooks.refresh = [this] {
        editor().reloadFromSession();
        refreshViews();
    };
    hooks.dictionary = [folder = m_readingDictionary]() -> application::ReadingAnalyzer {
        std::shared_ptr<const backends::MecabReadings> readings =
            backends::MecabReadings::open(std::filesystem::path(folder.toStdU16String()));
        return readings ? backends::MecabReadings::analyzer(std::move(readings)) : application::ReadingAnalyzer{};
    };
    v.kanjiTimer = std::make_unique<KanjiTimerController>(*m_settings, std::move(hooks));
}

bool Application::kanjiPending(application::DocumentId document)
{
    return std::ranges::any_of(allViews(), [&](const WindowViews *views) {
        return views->kanjiTimer->pendingFor(document.value) > 0;
    });
}

bool Application::applyKanji(application::DocumentId document, application::EditSession &session)
{
    return std::ranges::all_of(allViews(),
                               [&](WindowViews *views) { return views->kanjiTimer->applyTo(document.value, session); });
}

void Application::commitTemplates()
{
    for (auto *views : allViews()) {
        views->templateEditor->commit();
        views->nodeEffects->commit();
    }
}

Application::WindowViews::WindowViews() = default;
Application::WindowViews::~WindowViews() = default;

Application::WindowViews &Application::views(int shellWindow)
{
    if (const auto found = m_shellWindows.find(shellWindow); found != m_shellWindows.end() && found->second.views)
        return *found->second.views;
    return m_mainViews;
}

const Application::WindowViews &Application::views(int shellWindow) const
{
    if (const auto found = m_shellWindows.find(shellWindow); found != m_shellWindows.end() && found->second.views)
        return *found->second.views;
    return m_mainViews;
}

std::vector<Application::WindowViews *> Application::allViews()
{
    std::vector<WindowViews *> out;
    if (m_mainViews.shell)
        out.push_back(&m_mainViews);
    for (auto &[id, shell] : m_shellWindows)
        if (shell.views)
            out.push_back(shell.views.get());
    return out;
}

std::vector<Application::WindowMedia *> Application::allMedia()
{
    std::vector<WindowMedia *> out;
    if (m_mainMedia)
        out.push_back(m_mainMedia.get());
    for (auto &[id, shell] : m_shellWindows)
        if (shell.media)
            out.push_back(shell.media.get());
    return out;
}

application::EditSession *Application::windowSession(int shellWindow) const
{
    const auto tab = m_workspace.currentTab(shellWindow);
    return tab ? m_files->session(*tab) : nullptr;
}

Application::~Application()
{
    ui::DialogPlacement::setOpenedHook(nullptr);
    m_renderer.observeCost(nullptr);
    endSession();
    // MUST KILL: AutomationShell's teardown refreshes the Template editors, so they go first
    for (auto *views : allViews())
        views->templateEditor.reset();
    // MUST KILL: teardown order: the audio side before the video and helpers it reads through
    for (auto *media : allMedia())
        releaseAudio(*media);
    m_audioRouting.reset();
    for (auto *media : allMedia())
        media->audioPlayer.reset();
    m_audioOutput.reset();
    for (auto *media : allMedia())
        media->audioHelper.reset();
    m_port->waitIdle();
    if (m_localisation)
        QGuiApplication::setFont(m_startFont);
}

void Application::releaseAudio(WindowMedia &media)
{
    for (const auto &connection : std::as_const(media.audioConnections))
        disconnect(connection);
    media.video->setOpenFilter(nullptr);
    media.video->setBeforeOpen(nullptr);
    media.audio.reset();
}

application::LuaNodePort &Application::luaNodes()
{
    if (!m_luaNodes)
        m_luaNodes = std::make_unique<backends::LuaNodeHelper>(
            backends::LuaNodeHelper::Options{m_luaHelper, 10'000, backends::luaNodeProfile(m_luaHelper)});
    return *m_luaNodes;
}

application::shape::GlyphOutlinePort &Application::glyphOutlines()
{
    if (!m_glyphOutlines) {
        m_glyphFonts = std::make_unique<backends::LibassFontService>();
        m_glyphOutlines = std::make_unique<backends::FreetypeGlyphOutlines>(*m_glyphFonts);
    }
    return *m_glyphOutlines;
}

namespace {

void selectLegacyActiveLine(application::EditSession &session)
{
    const auto lines = session.document().lines();
    if (lines.empty())
        return;
    std::size_t active = 0;
    if (session.document().format() == core::SubtitleFormat::Ass) {
        const auto value = session.document().scriptInfo(u8"Active Line").value_or(std::u8string());
        const std::string text(value.begin(), value.end());
        const long n = std::strtol(text.c_str(), nullptr, 10);
        if (n > 0 && static_cast<std::size_t>(n) < lines.size())
            active = static_cast<std::size_t>(n);
    }
    const auto line = lines[active]->id;
    session.setSelection(application::Selection{line, {line}, line, {}});
}

}

std::optional<application::DocumentId> Application::open(const QString &path, bool asReference)
{
    auto staged = m_files->stageOpen({QFileInfo(path).absoluteFilePath().toStdString()});
    if (!staged) {
        if (staged.error().lyrics || staged.error().srv3)
            m_log->log(openProblem(path, staged.error()));
        return std::nullopt;
    }
    return publish(std::move(*staged), path, asReference);
}

QString Application::openProblem(const QString &path, const application::StageFailure &failure)
{
    const QString name = QFileInfo(path).fileName();
    if (failure.srv3) {
        QString reason = QString::fromStdString(*failure.srv3);
        if (!reason.endsWith(QLatin1Char('.')))
            reason += QLatin1Char('.');
        return QCoreApplication::translate("HikariSub.Documents", "Could not import %1 as YouTube timed text. %2 Nothing was imported.")
            .arg(name, reason);
    }
    if (!failure.lyrics)
        return QCoreApplication::translate("HikariSub.Documents", "Could not open %1; nothing was changed.").arg(name);
    using Kind = core::LyricsProblem::Kind;
    const core::LyricsProblem &problem = *failure.lyrics;
    QString excerpt = QString::fromUtf8(reinterpret_cast<const char *>(problem.excerpt.data()),
                                        static_cast<qsizetype>(problem.excerpt.size()));
    if (excerpt.size() > 60) {
        const qsizetype cut = excerpt.at(58).isHighSurrogate() ? 58 : 59;
        excerpt = excerpt.left(cut) + QChar(0x2026);
    }

    QString what;
    switch (problem.kind) {
    case Kind::NotUtf8: what = QCoreApplication::translate("HikariSub.Documents", "the text is not UTF-8"); break;
    case Kind::UnreadableTime:
        what = QCoreApplication::translate("HikariSub.Documents", "“%1” is not a time").arg(excerpt);
        break;
    case Kind::TimeGoesBack:
        what = QCoreApplication::translate("HikariSub.Documents", "the word at %1 starts before the word before it").arg(excerpt);
        break;
    case Kind::EndBeforeBegin:
        what = QCoreApplication::translate("HikariSub.Documents", "the end %1 is earlier than the begin").arg(excerpt);
        break;
    case Kind::OutsideParent:
        what = QCoreApplication::translate("HikariSub.Documents", "the time %1 is outside the times of the element around it")
                   .arg(excerpt);
        break;
    case Kind::MissingTime:
        what = QCoreApplication::translate("HikariSub.Documents", "“%1” has no time").arg(excerpt);
        break;
    case Kind::RepeatedWordLine:
        what = QCoreApplication::translate("HikariSub.Documents", "a line with word times can have only one line time");
        break;
    case Kind::NotWellFormed:
        what = QCoreApplication::translate("HikariSub.Documents", "the XML is broken at “%1”").arg(excerpt);
        break;
    case Kind::NotTtml:
        what = QCoreApplication::translate("HikariSub.Documents", "%1 is not a TTML document").arg(excerpt);
        break;
    case Kind::TooDeep:
        what = QCoreApplication::translate("HikariSub.Documents", "the elements are nested more than %1 deep at “%2”")
                   .arg(QString::number(core::kMaxTtmlDepth), excerpt);
        break;
    case Kind::TooLarge:
        what = QCoreApplication::translate("HikariSub.Documents", "it is too large for a lyrics file");
        break;
    case Kind::NoLyrics: what = QCoreApplication::translate("HikariSub.Documents", "it has no timed lyrics"); break;
    case Kind::Encrypted:
        what = QCoreApplication::translate("HikariSub.Documents",
                                           "it is encrypted. Decode it to text with your own tool first");
        break;
    }
    return problem.line > 0
               ? QCoreApplication::translate("HikariSub.Documents", "Could not import %1: line %2: %3. Nothing was imported.")
                     .arg(name, QString::number(problem.line), what)
               : QCoreApplication::translate("HikariSub.Documents", "Could not import %1: %2. Nothing was imported.").arg(name, what);
}

std::optional<application::DocumentId> Application::publish(application::StagedOpen staged, const QString &path,
                                                            bool asReference)
{
    const bool imported = staged.imported;
    auto id = m_files->activate(std::move(staged));
    if (!id)
        return std::nullopt;
    if (auto *session = m_files->session(*id); session && !session->selection().active)
        selectLegacyActiveLine(*session);
    if (auto *session = m_files->session(*id);
        session && !asReference && session->document().format() == core::SubtitleFormat::Ass && m_gridFilter->afterLoad() &&
        m_gridFilter->filterBy() != 0 && m_gridFilter->filterBy() != application::filter_by::Selections) {
        application::FilterSettings settings{m_gridFilter->filterBy(), {}, m_gridFilter->inverted(),
                                             m_gridFilter->addToFilter()};
        for (const QString &style : m_gridFilter->styles())
            settings.styles.push_back(toU8(style));
        (void)application::filterLines(*session, settings, {}, true);
    }
    const QFileInfo file(path);
    const QString saveAs = QDir::toNativeSeparators(file.absolutePath() + u'/' + file.completeBaseName() + QStringLiteral(".ass"));
    if (imported) {
        m_importPaths[id->value] = saveAs;
        m_lyricsImports.insert(id->value);
    }
    m_workspace.add(*id, (imported ? QFileInfo(saveAs).fileName() : file.fileName()).toStdString(), asReference);
    takeFileArrangement(*id);
    recordFileTime(*id);
    if (!asReference) {
        rememberRecent(QFileInfo(path).absoluteFilePath().toStdString());
        offerProjectFor(*id, path);
    }
    if (const auto *session = m_files->session(*id); session && !asReference)
        if (const auto folds = application::foldConversion(session->document()).converted)
            emit aegisubFoldsFound(id->value, static_cast<int>(folds));
    return *id;
}

bool Application::openFile(const QString &path)
{
    const auto previous = m_workspace.editingTarget();
    auto *previousSession = previous ? m_files->session(*previous) : nullptr;
    const bool emptyTab = previousSession && targetUntitled() && !previousSession->isDirty() &&
                          previousSession->historySize() == 1;
    const auto id = open(path);
    if (!id)
        return false;
    ++m_openBatch;
    if (emptyTab)
        replaceTarget(*id);
    offerAssociations(*id);
    showAssociationOffer();
    refreshViews();
    checkResolution();
    trimAudioCache();
    saveLastSession();
    return true;
}

void Application::releaseReadIndexHandoff(WindowMedia &media)
{
    if (media.pendingIndexHandoff.empty() || (media.audio->hasAudio() && !media.audio->loaded()))
        return;
    media.videoHelper->releaseIndexHandoff(std::exchange(media.pendingIndexHandoff, std::string()));
}

void Application::trimAudioCache()
{
    if (activeMedia().video->session().state() == application::VideoSession::State::Ready)
        activeMedia().audio->trimCache();
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
    closeDocument(*target);
    return true;
}

void Application::reportGroupBreak()
{
    auto *session = targetSession();
    if (!session || session->groupBreakCount() == m_seenGroupBreaks)
        return;
    m_seenGroupBreaks = session->groupBreakCount();
    qulonglong description = 0;
    const auto owners = core::groupOwners(session->document());
    for (const auto id : session->lastGroupBreak())
        if (const auto it = owners.find(id.value); it != owners.end() && it->second) {
            description = it->second->value;
            break;
        }
    emit groupBreakRefused(description, description ? groupTitle(description) : QString());
}

void Application::protectReference()
{
    const auto reference = m_workspace.reference();
    for (const auto id : m_workspace.documents())
        if (auto *session = m_files->session(id))
            session->setProtected(id == reference);
}

void Application::refreshViews()
{
    noteLastUsedWindow();
    noteCrashWindows();
    protectReference();
    showTabArrangement(m_workspace.activeWindow());
    for (const int window : sessionWindows())
        if (m_workspace.currentTab(window))
            spellChecker(window);
    reportGroupBreak();
    scheduleAutosave();
    if (m_styleManager)
        m_styleManager->targetRefreshed();
    const auto target = m_workspace.editingTarget();
    const auto reference = m_workspace.reference();
    auto *targetSession = target ? m_files->session(*target) : nullptr;
    auto *referenceSession = reference ? m_files->session(*reference) : nullptr;
    refreshComparison();
    if (m_review)
        m_review->refreshed();
    if (m_agent)
        m_agent->refreshed();
    if (m_inlineAi)
        m_inlineAi->refreshed();
    shell().setShowOriginal(target && showOriginal(*target), reference && showOriginal(*reference));
    shell().refresh(targetSession ? &targetSession->document() : nullptr,
                     referenceSession ? &referenceSession->document() : nullptr,
                     target ? m_comparison.table(*target) : nullptr,
                     reference ? m_comparison.table(*reference) : nullptr);
    if (targetSession) {
        shell().setDraft(targetSession->draftRecord());
        shell().setSelection(targetSession->selection());
        editor().selectionChanged();
    }
    if (referenceSession)
        shell().setReferenceSelection(referenceSession->selection());
    followEditingLine();
    templateEditor().setEditable(target && m_workspace.checkContentCommand(*target).has_value());
    templateEditor().refresh();
    nodeEffects().setEditable(target && m_workspace.checkContentCommand(*target).has_value());
    nodeEffects().refresh();
    if (auto &active = activeViews(); target != active.editorDocument) {
        active.editorDocument = target;
        editor().setDocument(target, target && m_workspace.checkContentCommand(*target).has_value());
    }
    if (auto &active = activeViews(); target != std::exchange(active.findDocument, target))
        active.find->reset();
    // MUST KILL: must run after editor().setDocument above
    if (m_styleManager)
        m_styleManager->documentEdited();
    kanjiTimer().documentEdited();
    if (m_qcNotes)
        m_qcNotes->documentEdited();
    refreshVideo();
    refreshAudio();
    if (activeViews().statusBar)
        statusBar().refresh();
    const int left = std::exchange(m_viewsWindow, m_workspace.activeWindow());
    if (m_projects && left != m_viewsWindow && m_projects->root(left) != m_projects->root(m_viewsWindow)) {
        applyGrammarSettings();
        emit spellingChanged();
    }
    for (const auto &[shellWindow, shell] : m_shellWindows)
        refreshInactiveViews(shellWindow, left);
    refreshInactiveViews(0, left);
    emit tabsChanged();
    if (m_apiHost)
        m_apiHost->observe();
}

void Application::refreshInactiveViews(int shellWindow, int left)
{
    if (shellWindow == m_workspace.activeWindow())
        return;
    const auto &v = views(shellWindow);
    const auto *session = windowSession(shellWindow);
    if (shellWindow == left || m_workspace.currentTab(shellWindow) != v.editorDocument ||
        (session ? session->revision() : 0) != v.shownRevision)
        refreshWindowViews(shellWindow);
}

void Application::refreshWindowViews(int shellWindow)
{
    auto &v = views(shellWindow);
    const auto tab = m_workspace.currentTab(shellWindow);
    auto *session = windowSession(shellWindow);
    v.shownRevision = session ? session->revision() : 0;
    v.shell->setShowOriginal(tab && showOriginal(*tab), false);
    v.shell->refresh(session ? &session->document() : nullptr, nullptr, tab ? m_comparison.table(*tab) : nullptr,
                     nullptr);
    if (session) {
        v.shell->setDraft(session->draftRecord());
        v.shell->setSelection(session->selection());
    }
    if (tab != v.editorDocument) {
        v.editorDocument = tab;
        v.editor->setDocument(tab, tab && m_workspace.checkContentCommand(*tab).has_value());
    } else {
        v.editor->reloadFromSession();
    }
    if (tab != std::exchange(v.findDocument, tab))
        v.find->reset();
    v.templateEditor->setEditable(tab && m_workspace.checkContentCommand(*tab).has_value());
    v.templateEditor->refresh();
    v.nodeEffects->setEditable(tab && m_workspace.checkContentCommand(*tab).has_value());
    v.nodeEffects->refresh();
    v.kanjiTimer->documentEdited();
    if (tab != v.media->videoDocument)
        switchTabMedia(*v.media, tab);
    v.visualTools->refresh();
    v.statusBar->setTimecodesFile(tabTimecodes(tab));
    v.statusBar->refresh();
}

bool Application::showVideoSubtitles()
{
    const auto target = activeMedia().videoDocument;
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    if (setVideoFonts(target))
        activeMedia().videoScript.clear();
    bool edited = false;
    std::optional<core::Document> draft;
    if (!m_settings->boolean("video.disableLiveEditing") && activeViews().editorDocument == target && activeMedia().video->hasVideo())
        draft = editor().draftDocument();
    std::optional<core::Document> templated;
    if (m_videoAnalysis && m_videoAnalysis->videoPreview()) {
        templated = m_videoAnalysis->videoDocument(draft ? *draft : session->document());
    } else if (m_layerPreview) {
        templated = layerPreviewDocument(draft ? *draft : session->document(), session->selection().selected);
    } else if (nodeEffects().videoPreview()) {
        templated = nodeEffects().videoDocument(draft ? *draft : session->document());
    } else if (templateEditor().videoPreview()) {
        const auto &video = activeMedia().video->session();
        const auto frame = video.reportedFrame();
        const auto at = frame ? video.frameStart(*frame) : std::nullopt;
        const std::int64_t ms = at ? at->microseconds() / 1000 : 0;
        templated = templateEditor().videoDocument(draft ? *draft : session->document(), ms, video.playing());
        const auto window = templateEditor().videoWindow(ms, video.playing());
        activeMedia().templateWindow = std::pair(window.fromMs, window.toMs);
    } else if (!m_interpolationPreview.empty()) {
        templated = draft ? *draft : session->document();
        for (const auto &[id, text] : m_interpolationPreview)
            templated->editLine(id, [&](core::LineRecord &l) { l.text = text; });
    }
    if (draft || templated || activeMedia().videoShowsDraft || activeMedia().videoShowsTemplate || session->revision() != activeMedia().videoRevision) {
        if (session->revision() != activeMedia().videoRevision)
            edited = activeMedia().videoRevision.has_value();
        activeMedia().videoRevision = session->revision();
        activeMedia().videoShowsDraft = draft.has_value();
        activeMedia().videoShowsTemplate = templated.has_value();
        auto script = visualTools().subtitles(templated ? *templated : draft ? *draft : session->document(),
                                               [this](const core::Document &d) { return rendererScript(d); });
        if (script != activeMedia().videoScript) {
            activeMedia().videoScript = script;
            activeMedia().video->session().setSubtitles(std::move(script));
        }
    }
    return edited;
}

std::vector<application::FontLease> Application::attachedFontLeases(std::optional<application::DocumentId> document)
{
    auto *session = document ? m_files->session(*document) : nullptr;
    if (!session) {
        if (document)
            m_attachedFonts.erase(document->value);
        return {};
    }
    const core::Document &doc = session->document();
    const auto generation = m_files->generation(*document);
    const auto key = core::attachedFontsKey(doc);
    auto &entry = m_attachedFonts[document->value];
    if (entry.generation != generation || entry.key != key) {
        entry = AttachedFontsEntry{generation, key, {}};
        for (auto &font : core::attachedFonts(doc))
            entry.leases.push_back({font.name, std::make_shared<const std::vector<std::byte>>(std::move(font.bytes))});
    }
    return entry.leases;
}

void Application::subtitleFontsChanged()
{
    application::reloadSubtitleFonts();
    if (!m_fontCatalogs)
        return;
    setVideoFonts(activeMedia().videoDocument);
    if (activeMedia().videoDocument) {
        activeMedia().videoRevision.reset();
        activeMedia().videoScript.clear();
        refreshVideo();
    }
}

std::vector<application::FontLease> Application::projectFonts(std::optional<application::DocumentId> document) const
{
    const int window = (document ? m_workspace.window(*document) : std::nullopt).value_or(m_workspace.activeWindow());
    const auto found = m_projectFonts.find(window);
    return found == m_projectFonts.end() ? std::vector<application::FontLease>() : found->second.fonts;
}

std::vector<application::FontLease> Application::subtitleFonts(std::optional<application::DocumentId> document)
{
    auto fonts = attachedFontLeases(document);
    for (auto &font : projectFonts(document))
        fonts.push_back(std::move(font));
    if (m_fontCatalogs)
        for (auto &font : m_fontCatalogs->externalFontLeases())
            fonts.push_back(std::move(font));
    return fonts;
}

bool Application::setVideoFonts(std::optional<application::DocumentId> document)
{
    auto fonts = subtitleFonts(document);
    const auto same = [](const application::FontLease &a, const application::FontLease &b) {
        return a.family == b.family && a.bytes == b.bytes;
    };
    if (std::ranges::equal(fonts, activeMedia().videoFonts, same))
        return false;
    activeMedia().videoFonts = fonts;
    if (m_mainViews.visualTools) {
        decltype(m_leasedFontNames) read;
        std::vector<std::string> names;
        for (const auto &font : activeMedia().videoFonts) {
            if (!font.bytes)
                continue;
            const auto known = std::ranges::find(m_leasedFontNames, font.bytes, &decltype(read)::value_type::first);
            auto own = known != m_leasedFontNames.end() ? known->second : backends::fontNames(*font.bytes);
            names.insert(names.end(), own.begin(), own.end());
            read.emplace_back(font.bytes, std::move(own));
        }
        m_leasedFontNames = std::move(read);
        for (auto *views : allViews())
            views->visualTools->setLeasedFonts(names);
    }
    activeMedia().video->session().setFonts(std::move(fonts));
    return true;
}

void Application::switchTabMedia(WindowMedia &media, std::optional<application::DocumentId> tab)
{
    const auto previous = media.videoDocument;
    leaveTabMedia(media, previous);
    media.videoDocument = tab;
    media.videoRevision.reset();
    media.videoScript.clear();
    media.videoLine.reset();
    if (!media.keepVideoOnEnter) {
        media.video->resetForTarget();
        if (&media == &activeMedia())
            showAssociationOffer();
    }
    enterTabMedia(media, previous, tab);
}

void Application::refreshVideo()
{
    if (m_holdVideoRefresh)
        return;
    if (m_editRefresh)
        m_editRefresh->stop();
    const auto left = std::exchange(m_leftEditedLine, std::nullopt);
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (target != activeMedia().videoDocument)
        switchTabMedia(activeMedia(), target);
    visualTools().refresh();
    if (!session)
        return;
    const bool edited = showVideoSubtitles();
    activeMedia().video->session().setMatrix(application::sessionVideoMatrix(*session));
#ifdef _WIN32
    if (m_directShow) // W1: legacy RendererDirectShow::SetColorSpace
        m_directShow->setColorSpace(activeMedia().video->session().matrix());
#endif
    const auto active = session->selection().active;
    std::optional<std::pair<core::DocumentTime, core::DocumentTime>> lineTimes;
    if (active)
        for (const auto *line : session->document().lines())
            if (line->id == *active)
                lineTimes = std::pair(line->start.value, line->end.value);
    activeMedia().video->setActiveLineTimes(lineTimes);
    bool rowChanged = false;
    if (active && active != activeMedia().videoLine) {
        rowChanged = activeMedia().videoLine.has_value();
        activeMedia().videoLine = active;
    }
    followActiveLine(rowChanged, edited, left);
}

namespace {

QString fileTitle(const std::string &path)
{
    return QFileInfo(QString::fromStdString(path)).fileName();
}

}

void Application::newDocument()
{
    const auto id = m_files->createNew();
    m_workspace.add(id, QCoreApplication::translate("HikariSub.Documents", "Untitled").toStdString());
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
    commitTemplates();
    endFindOpen(false);
    const bool quitReplaced = m_closeThen == QLatin1String("quit");
    m_closeThen = then;
    if (quitReplaced)
        emit quitCancelled();
    if (then == QLatin1String("quit"))
        ++m_quitReviews;
    m_closing.clear();
    m_closeApproved.reset();
    if (then != QLatin1String("window"))
        m_closingWindow.reset();
    if (then != QLatin1String("open")) {
        m_pendingOpen.reset();
        m_pendingOpenPath.clear();
        m_videoAfterOpen.clear();
        m_videoAfterOpenFullscreen = false;
        m_openFromVideo = false;
    }
    if (then != QLatin1String("files"))
        m_pendingFiles.reset();
    std::vector<application::DocumentId> scope;
    if (then != QLatin1String("tab")) {
        m_closingTabs.clear();
        m_tabAfterClose.reset();
    }
    if (then != QLatin1String("project"))
        m_projectClosing.reset();
    m_closeTarget = m_workspace.editingTarget();
    if (then != QLatin1String("session"))
        m_pendingSession.reset();
    if (then == QLatin1String("quit") || then == QLatin1String("session"))
        scope = m_workspace.documents();
    else if (then == QLatin1String("tab"))
        scope = m_closingTabs;
    else if (then == QLatin1String("all"))
        scope = m_workspace.allTabs();
    else if (then == QLatin1String("project") && m_projectClosing)
        scope = m_workspace.tabs(*m_projectClosing);
    else if (then == QLatin1String("window"))
        std::ranges::copy_if(m_workspace.documents(), std::back_inserter(scope),
                             [&](application::DocumentId id) { return m_workspace.window(id) == m_closingWindow; });
    else if (const auto target = m_workspace.editingTarget())
        scope = {*target};
    QVariantList rows;
    for (const auto id : scope) {
        auto *session = m_files->session(id);
        if (!session || (!session->isDirty() && !kanjiPending(id)))
            continue;
        const std::string *title = m_workspace.title(id);
        const auto destination = m_files->destination(id);
        const bool untitled = !destination || destination->value.empty();
        rows << QVariantMap{{QStringLiteral("id"), QVariant::fromValue<qulonglong>(id.value)},
                            {QStringLiteral("title"), title ? QString::fromStdString(*title) : QString()},
                            {QStringLiteral("untitled"), untitled},
                            {QStringLiteral("folder"), untitled ? QString()
                                                                : QDir::toNativeSeparators(QFileInfo(QString::fromStdString(destination->value)).absolutePath())}};
    }
    return rows;
}

namespace {
QString extensionFor(core::SubtitleFormat format);
bool savesAsAss(core::SubtitleFormat format, const QString &path, const QString &chosen);
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
        if (c.save && !applyKanji(id, *session)) {
            m_closing.clear();
            emit closeFinished(false, QCoreApplication::translate("HikariSub.Documents",
                                                                  "A document could not be saved; nothing was closed."));
            return;
        }
        if (c.save) {
            QString path = row.value(QStringLiteral("path")).toString();
            const bool toAss = !path.isEmpty() && savesAsAss(session->document().format(), path,
                                                              row.value(QStringLiteral("format")).toString());
            if (const QString ext = toAss ? QStringLiteral("ass") : extensionFor(session->document().format());
                !path.isEmpty() && QFileInfo(path).suffix().compare(ext, Qt::CaseInsensitive) != 0)
                path += u'.' + ext;
            if (toAss && !convertToAss(id)) {
                m_closing.clear();
                emit closeFinished(false, QCoreApplication::translate("HikariSub.Documents",
                                                                      "A document could not be saved; nothing was closed."));
                return;
            }
            auto plan = m_files->prepareSave(
                id, path.isEmpty() ? std::nullopt
                                   : std::optional(application::DestinationKey{QFileInfo(path).absoluteFilePath().toStdString()}));
            if (!plan) {
                m_closing.clear();
                emit closeFinished(false, plan.error() == application::SaveRefusal::NoDestination
                                              ? QCoreApplication::translate("HikariSub.Documents",
                                                                            "Choose where to save the untitled document.")
                                              : QCoreApplication::translate("HikariSub.Documents",
                                                                            "A document could not be saved; nothing was closed."));
                return;
            }
            c.revision = plan->revision;
            auto permit = m_files->startSave(std::move(*plan));
            if (!permit) {
                m_closing.clear();
                emit closeFinished(false, QCoreApplication::translate("HikariSub.Documents",
                                                                      "A document could not be saved; nothing was closed."));
                return;
            }
            c.permit = *permit;
        } else {
            session->discardDraft();
            c.revision = session->revision();
        }
        m_closing.push_back(c);
    }
    refreshViews();
    if (std::none_of(m_closing.begin(), m_closing.end(), [](const Closing &c) { return c.permit.has_value(); }))
        finishClose();
}

void Application::writeFinished(const application::WriteResult &result)
{
    if ((result.outcome == application::WriteOutcome::Written ||
         result.outcome == application::WriteOutcome::DurabilityUncertain) &&
        (m_workspace.editingTarget() == result.document || m_workspace.reference() == result.document))
        refreshViews();
    if (result.outcome == application::WriteOutcome::Written ||
        result.outcome == application::WriteOutcome::DurabilityUncertain) {
        if (const auto destination = m_files->destination(result.document);
            destination && destination->value == result.destination.value)
            recordFileTime(result.document);
        emit documentWritten(result.document.value);
    } else if (result.outcome == application::WriteOutcome::Failed ||
               result.outcome == application::WriteOutcome::Cancelled) {
        if (result.outcome == application::WriteOutcome::Failed)
            m_log->notice(ui::LogController::Error, fileTitle(result.destination.value),
                          QCoreApplication::translate("HikariSub.Editor", "Save failed; the file on disk is unchanged"),
                          QDir::toNativeSeparators(QString::fromStdString(result.destination.value)),
                          windowOfTab(result.document.value));
        emit documentWriteFailed(result.document.value);
    }
    if (result.outcome == application::WriteOutcome::Written) {
        rememberRecent(result.destination.value, false);
        if (auto *session = m_files->session(result.document); session && !session->isDirty())
            discardRecovery(result.document);
    }
    if (result.outcome == application::WriteOutcome::Written)
        if (const std::string *title = m_workspace.title(result.document);
            title && QString::fromStdString(*title) != fileTitle(result.destination.value)) {
            const auto destination = m_files->destination(result.document);
            if (destination && destination->value == result.destination.value) {
                m_workspace.setTitle(result.document, fileTitle(result.destination.value).toStdString());
                refreshViews();
                saveLastSession();
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
            const auto status = m_files->lastSave(c.document);
            ok = status && status->outcome == application::WriteOutcome::Written && !session->isDirty();
        } else {
            ok = session->revision() == c.revision && !session->draftLine();
        }
        if (!ok) {
            const std::string *title = m_workspace.title(c.document);
            kept << (title ? QString::fromStdString(*title) : QString());
        }
    }
    const QString then = m_closeThen;
    const auto reviewed = std::exchange(m_closeTarget, std::nullopt);
    m_closing.clear();
    m_closeThen.clear();
    if (!kept.isEmpty()) {
        m_closeThen = then;
        m_closeTarget = reviewed;
        if (then == QLatin1String("quit"))
            emit quitCancelled();
        refreshViews();
        emit closeFinished(false, QCoreApplication::translate("HikariSub.Documents",
                                                              "Not closed, because saving failed or there is newer work: %1")
                                      .arg(kept.join(QStringLiteral(", "))));
        return;
    }
    const bool onTarget = then != QLatin1String("quit") && then != QLatin1String("session") && then != QLatin1String("tab")
        && then != QLatin1String("all") && then != QLatin1String("window") && then != QLatin1String("project");
    if (onTarget && reviewed && m_workspace.editingTarget() != reviewed) {
        if (!m_workspace.setEditingTarget(*reviewed)) {
            m_closeThen = then;
            cancelClose();
            emit closeFinished(true, QString());
            return;
        }
        refreshViews();
    }
    if (then == QLatin1String("quit")) {
        for (const auto id : m_workspace.documents())
            discardRecovery(id);
        saveLayoutForQuit();
        m_quitApproved = true;
        emit quitApprovedChanged();
    } else if (then == QLatin1String("new")) {
        const auto id = m_files->createNew();
        m_workspace.add(id, QCoreApplication::translate("HikariSub.Documents", "Untitled").toStdString());
        replaceTarget(id);
    } else if (then == QLatin1String("open")) {
        std::optional<application::DocumentId> id;
        if (m_pendingOpen)
            id = publish(std::move(*m_pendingOpen), m_pendingOpenPath, false);
        const QString videoAfter = std::exchange(m_videoAfterOpen, QString());
        const bool fullscreenAfter = std::exchange(m_videoAfterOpenFullscreen, false);
        const bool fromVideo = std::exchange(m_openFromVideo, false);
        if (id) {
            replaceTarget(*id);
            ++m_openBatch;
            if (!fromVideo) {
                offerAssociations(*id);
                showAssociationOffer();
            } else if (!videoAfter.isEmpty()) {
                if (fullscreenAfter)
                    videoFullscreen().enterWhenShown(videoAfter);
                activeMedia().video->openVideo(videoAfter);
            }
            QTimer::singleShot(0, this, [this] { checkResolution(); });
        } else {
            closeEditingTarget();
        }
        m_pendingOpen.reset();
        m_pendingOpenPath.clear();
        refreshViews();
        saveLastSession();
    } else if (then == QLatin1String("session")) {
        applySession();
    } else if (then == QLatin1String("tab")) {
        const auto target = m_workspace.editingTarget();
        const auto closing = std::exchange(m_closingTabs, {});
        const auto after = std::exchange(m_tabAfterClose, std::nullopt);
        for (const auto tab : closing)
            closeDocument(tab);
        if (after && target && std::ranges::find(closing, *target) != closing.end() &&
            m_workspace.setEditingTarget(*after))
            refreshViews();
    } else if (then == QLatin1String("all")) {
        closeAllTabs();
    } else if (then == QLatin1String("project")) {
        if (const auto window = std::exchange(m_projectClosing, std::nullopt))
            closeProject(*window);
    } else if (then == QLatin1String("window")) {
        m_closeApproved = std::exchange(m_closingWindow, std::nullopt);
    } else if (then == QLatin1String("files")) {
        if (auto files = std::exchange(m_pendingFiles, std::nullopt))
            applyFiles(std::move(*files), false);
    } else {
        closeEditingTarget();
    }
    // MUST KILL: must run before closeFinished is emitted
    endFindOpen(then == QLatin1String("open"));
    emit closeFinished(true, QString());
}

void Application::cancelClose()
{
    m_closing.clear();
    const bool quit = m_closeThen == QLatin1String("quit");
    m_closeThen.clear();
    m_closingWindow.reset();
    if (quit)
        emit quitCancelled();
    m_pendingOpen.reset();
    m_pendingOpenPath.clear();
    m_pendingSession.reset();
    m_closingTabs.clear();
    m_tabAfterClose.reset();
    m_videoAfterOpen.clear();
    m_videoAfterOpenFullscreen = false;
    m_openFromVideo = false;
    endFindOpen(false);
    if (auto files = std::exchange(m_pendingFiles, std::nullopt))
        applyFiles(std::move(*files), true);
}

qulonglong Application::editingTargetId() const
{
    const auto target = m_workspace.editingTarget();
    return target ? target->value : 0;
}

bool Application::targetUntitled() const
{
    const auto target = m_workspace.editingTarget();
    const auto destination = target ? m_files->destination(*target) : std::nullopt;
    return target && (!destination || destination->value.empty());
}

namespace {

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

bool savesAsAss(core::SubtitleFormat format, const QString &path, const QString &chosen)
{
    return extensionFor(format) != QLatin1String("ass")
           && (chosen == QLatin1String("ass") || QFileInfo(path).suffix().compare(u"ass", Qt::CaseInsensitive) == 0);
}

bool readOnly(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() && !info.isWritable();
}

}

QString Application::saveRoute() const
{
    return saveRouteFor(0);
}

static QString readyVideo(const application::VideoSession &video)
{
    return video.state() == application::VideoSession::State::Ready ? QString::fromStdString(video.path()) : QString();
}

QString Application::saveRouteFor(qulonglong id) const
{
    const auto document = documentOf(id);
    if (!document)
        return {};
    const auto destination = m_files->destination(*document);
    const QString path = destination ? QString::fromStdString(destination->value) : QString();
    const QString video = *document == m_workspace.editingTarget() ? readyVideo(activeMedia().video->session()) : tabVideo(*document);
    if (path.isEmpty() || (saveWithVideoName() && !video.isEmpty() &&
                           beforeLast(QFileInfo(path).fileName(), u'.') != beforeLast(QFileInfo(video).fileName(), u'.')))
        return QStringLiteral("dialog");
    return readOnly(path) ? QStringLiteral("readonly") : QString();
}

QVariantMap Application::saveDialogValues() const
{
    return saveDialogValuesFor(0);
}

QVariantMap Application::saveDialogValuesFor(qulonglong id) const
{
    const auto document = documentOf(id);
    auto *session = document ? m_files->session(*document) : nullptr;
    if (!session)
        return {};
    const auto destination = m_files->destination(*document);
    const QString video = *document == m_workspace.editingTarget() ? readyVideo(activeMedia().video->session()) : tabVideo(*document);
    const auto importPath = m_importPaths.find(document->value);
    const QString path = !video.isEmpty() && saveWithVideoName() ? video
                         : destination && !destination->value.empty() ? QString::fromStdString(destination->value)
                         : importPath != m_importPaths.end()            ? importPath->second
                                                                       : QString();
    const QString ext = extensionFor(session->document().format());
    const QString filter = QCoreApplication::translate("HikariSub.Documents", "Subtitle file (%1)")
                               .arg(ext == QStringLiteral("txt") ? QStringLiteral("*.txt *.sub") : QStringLiteral("*.%1").arg(ext));
    QStringList filters{filter};
    QStringList formats{QString()};
    if (ext != QLatin1String("ass")) {
        filters << QCoreApplication::translate("HikariSub.Documents", "ASS subtitles (*.ass)");
        formats << QStringLiteral("ass");
    }
    const QFileInfo info(path);
    return {{QStringLiteral("folder"), path.isEmpty() ? QUrl() : QUrl::fromLocalFile(info.absolutePath())},
            {QStringLiteral("file"), path.isEmpty() ? QUrl() : QUrl::fromLocalFile(info.absolutePath() + u'/' + info.completeBaseName())},
            {QStringLiteral("filter"), filter},
            {QStringLiteral("filters"), filters},
            {QStringLiteral("formats"), formats},
            {QStringLiteral("extension"), ext}};
}

QString Application::saveChosen(const QUrl &file)
{
    return saveChosenFor(0, file);
}

QString Application::saveChosenFor(qulonglong id, const QUrl &file, const QString &format)
{
    const auto document = documentOf(id);
    auto *session = document ? m_files->session(*document) : nullptr;
    QString path = file.toLocalFile();
    if (!session || path.isEmpty())
        return QStringLiteral("failed");
    if (readOnly(path))
        return QStringLiteral("readonly");
    const bool toAss = savesAsAss(session->document().format(), path, format);
    const QString ext = toAss ? QStringLiteral("ass") : extensionFor(session->document().format());
    if (QFileInfo(path).suffix().compare(ext, Qt::CaseInsensitive) != 0)
        path += u'.' + ext;
    if (readOnly(path))
        return QStringLiteral("readonly");
    if (toAss && !convertToAss(*document))
        return QStringLiteral("failed");
    auto plan = m_files->prepareSave(*document, application::DestinationKey{QFileInfo(path).absoluteFilePath().toStdString()});
    if (!plan || !m_files->startSave(std::move(*plan)))
        return QStringLiteral("failed");
    if (*document == m_workspace.editingTarget())
        editor().reloadFromSession();
    refreshViews();
    if (toAss)
        checkResolution();
    return {};
}

bool Application::saveDocument(qulonglong id)
{
    const auto document = documentOf(id);
    if (!document)
        return false;
    if (*document == m_workspace.editingTarget())
        return editor().save();
    auto plan = m_files->prepareSave(*document);
    const std::string *title = m_workspace.title(*document);
    const QString name = title ? QString::fromStdString(*title) : QString();
    if (!plan || !m_files->startSave(std::move(*plan))) {
        m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Documents", "Save"),
                      QCoreApplication::translate("HikariSub.Documents", "%1 could not be saved.").arg(name), {},
                      windowOfTab(id));
        return false;
    }
    refreshViews();
    return true;
}

QVariantList Application::saveAll()
{
    commitTemplates();
    QVariantList later;
    for (const auto id : m_workspace.allTabs()) {
        auto *session = m_files->session(id);
        if (!session || !session->isDirty())
            continue;
        QString route = saveRouteFor(id.value);
        if (route.isEmpty() && !scriptInfoQuestion(id.value).isEmpty())
            route = QStringLiteral("scriptInfo");
        if (!route.isEmpty()) {
            const std::string *title = m_workspace.title(id);
            later << QVariantMap{{QStringLiteral("id"), QVariant::fromValue<qulonglong>(id.value)},
                                 {QStringLiteral("route"), route},
                                 {QStringLiteral("title"), title ? QString::fromStdString(*title) : QString()}};
            continue;
        }
        if (auto plan = m_files->prepareSave(id))
            m_files->startSave(std::move(*plan));
    }
    editor().reloadFromSession();
    refreshViews();
    return later;
}

bool Application::turnOffTranslationMode()
{
    auto *session = targetSession();
    if (!session)
        return false;
    showOriginal(*m_workspace.editingTarget());
    if (!application::turnOffTranslationMode(*session))
        return false;
    originalColumns(*m_workspace.editingTarget()).turnedOff(session->document());
    editor().reloadFromSession();
    refreshViews();
    return true;
}

void Application::setSaveWithVideoName(bool on)
{
    m_settings->set("subtitles.saveWithVideoName", on);
}

void Application::openWebsite()
{
    backends::desktop::openUrl(QUrl(QString::fromUtf8(application::kSiteUrl)));
}

void Application::openUserGuide(const QString &kind, const QString &id)
{
    using application::GuideSurface;
    const std::string surface = kind == QLatin1String("panel") || kind == QLatin1String("window") ? id.toStdString() : "";
    const auto url = application::guideUrl(kind == QLatin1String("panel") ? GuideSurface::Panel : GuideSurface::Window,
                                           surface);
    backends::desktop::openUrl(QUrl(QString::fromStdString(url)));
}

QString Application::userFolder() const
{
    return QDir::toNativeSeparators(m_userFolder);
}

QString Application::bundledScriptName(const QString &path) const
{
    return Analytics::bundledScript(path, QCoreApplication::applicationDirPath() + QLatin1Char('/') +
                                              UserFolder::manifestName());
}

void Application::openWhatIsSent()
{
    backends::desktop::openUrl(Analytics::whatIsSentUrl());
}

void Application::openUserFolder()
{
    if (m_userFolder.isEmpty())
        return;
    QDir().mkpath(m_userFolder);
    backends::desktop::openUrl(QUrl::fromLocalFile(m_userFolder));
}

QVariantMap Application::reviewOpen(const QString &path)
{
    auto staged = m_files->stageOpen({QFileInfo(path).absoluteFilePath().toStdString()});
    if (!staged) {
        const QString problem = openProblem(path, staged.error());
        m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Video", "Open subtitles"), problem);
        return {{QStringLiteral("ok"), false}, {QStringLiteral("problem"), problem}, {QStringLiteral("rows"), QVariantList()}};
    }
    if (!m_openFromVideo && m_settings->boolean("subtitles.openInNewTab") && !targetUntitled()) {
        const bool opened = openInNewTab(std::move(*staged), path);
        return {{QStringLiteral("ok"), opened}, {QStringLiteral("problem"), QString()},
                {QStringLiteral("rows"), QVariantList()}, {QStringLiteral("done"), true}};
    }
    const QVariantList rows = reviewClose(QStringLiteral("open"));
    m_pendingOpen = std::move(*staged);
    m_pendingOpenPath = path;
    return {{QStringLiteral("ok"), true}, {QStringLiteral("problem"), QString()}, {QStringLiteral("rows"), rows}};
}

namespace {

bool recentMissing(const std::string &path)
{
    return recentPathMissing(path, false);
}

}

void Application::rememberRecent(const std::string &path, bool opened)
{
    const auto kept = opened ? std::nullopt : m_recent.opened(path);
    m_recent.add(path, kept ? *kept : QDateTime::currentMSecsSinceEpoch());
    storeRecent();
}

void Application::storeRecent()
{
    m_settings->settings().set("recent.subtitles", m_recent.entries());
    m_settings->settings().set("recent.subtitlesOpened", m_recent.openedRecords());
    emit recentChanged();
}

QVariantList Application::recentSubtitles()
{
    QVariantList rows;
    int n = 0;
    for (const auto &entry : m_recent.entries()) {
        if (recentMissing(entry))
            continue;
        const QString path = QString::fromStdString(entry);
        rows << QVariantMap{{QStringLiteral("path"), path},
                            {QStringLiteral("label"), QStringLiteral("%1 %2").arg(++n).arg(QFileInfo(path).fileName())}};
    }
    return rows;
}

QVariantList Application::homeRecent()
{
    QVariantList rows;
    for (const auto &entry : m_recent.entries()) {
        const QString path = QString::fromStdString(entry);
        const QFileInfo info(path);
        const auto opened = m_recent.opened(entry);
        rows << QVariantMap{{QStringLiteral("path"), QDir::toNativeSeparators(path)},
                            {QStringLiteral("name"), info.fileName()},
                            {QStringLiteral("folder"), QDir::toNativeSeparators(info.path())},
                            {QStringLiteral("opened"), opened ? QDateTime::fromMSecsSinceEpoch(*opened) : QDateTime()},
                            {QStringLiteral("missing"), recentMissing(entry)}};
    }
    return rows;
}

void Application::removeRecent(const QString &path)
{
    for (const auto &entry : m_recent.entries())
        if (QDir::toNativeSeparators(QString::fromStdString(entry)) == path) {
            m_recent.remove(entry);
            storeRecent();
            return;
        }
}

void Application::removeMissingRecent()
{
    if (m_recent.prune(recentMissing))
        storeRecent();
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
    m_fileTimes[target->value] = now;
    return known == m_fileTimes.end() ? QString() : QStringLiteral("modified");
}

bool Application::acceptFileOnDisk(qulonglong id)
{
    const auto document = documentOf(id);
    if (!document || !m_files->acceptExternalChange(*document))
        return false;
    recordFileTime(*document);
    return true;
}

bool Application::reloadTarget()
{
    const auto target = m_workspace.editingTarget();
    if (!target)
        return false;
    auto staged = m_files->stageReload(*target);
    if (!staged) {
        m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Documents", "Reload subtitles"),
                      QCoreApplication::translate("HikariSub.Documents", "Could not reload the subtitles; nothing was changed."));
        return false;
    }
    if (!m_files->activate(std::move(*staged))) {
        m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Documents", "Reload subtitles"),
                      QCoreApplication::translate("HikariSub.Documents", "The subtitles changed while reloading; nothing was replaced."));
        return false;
    }
    if (auto *session = m_files->session(*target))
        selectLegacyActiveLine(*session);
    recordFileTime(*target);
    ++m_reloads;
    activeMedia().videoRevision.reset();
    activeMedia().videoScript.clear();
    activeMedia().videoLine.reset();
    m_comparedRevisions.erase(target->value);
    refreshComparison();
    editor().reloadFromSession();
    refreshViews();
    return true;
}

bool Application::compareWithFileOnDisk()
{
    const auto target = m_workspace.editingTarget();
    const auto destination = target ? m_files->destination(*target) : std::nullopt;
    if (!destination || destination->value.empty())
        return false;
    if (const auto reference = m_workspace.reference(); reference && !m_workspace.referenceIsTab())
        if (const auto held = m_files->destination(*reference); held && held->value == destination->value)
            closeDocument(*reference);
    if (!openReference(QString::fromStdString(destination->value)))
        return false;
    return compareWithReference();
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
    application::GridSelection rules(std::move(document), shell().displayedLines());
    rules.setChangeActiveOnSelection(m_settings->boolean("grid.changeActiveOnSelection"));
    return rules;
}

bool Application::applySelection(application::Selection next)
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    if (next.active && next.active != session->selection().active && !editor().showLine(next.active->value))
        return false;
    session->setSelection(std::move(next));
    shell().setSelection(session->selection());
    editor().selectionChanged();
    templateEditor().refresh();
    refreshVideo();
    followEditingLine();
    if (m_apiHost)
        m_apiHost->observe();
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
        applySelection(shell().withFoldedRuns(gridSelection().plain(session->selection(), core::LineId{id})));
}

void Application::extendSelection(int rows)
{
    const auto target = m_workspace.editingTarget();
    if (auto *session = target ? m_files->session(*target) : nullptr)
        applySelection(shell().withFoldedRuns(gridSelection().shiftKey(session->selection(), rows)));
}

void Application::toggleFold(qulonglong first)
{
    const core::LineId id{first};
    if (!shell().foldGenerated() || shell().runFirst(id) != id)
        return;
    const bool fold = !shell().runFolded(id);
    if (auto *session = targetSession(); fold && session) {
        const application::Selection &current = session->selection();
        application::Selection next = shell().foldedWhole(current, id);
        if ((next.active != current.active || next.selected != current.selected) && !applySelection(std::move(next)))
            return;
    }
    shell().setRunFolded(id, fold);
}

void Application::selectFoldedRunsWhole()
{
    auto *session = targetSession();
    if (!session || !shell().foldGenerated())
        return;
    const application::Selection &current = session->selection();
    application::Selection next = shell().foldedWhole(current);
    if (next.active != current.active || next.selected != current.selected)
        applySelection(std::move(next));
}

void Application::clickLine(qulonglong id, int modifiers, bool endColumn, bool doubleClick)
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return;
    const auto rules = gridSelection();
    const bool ctrl = modifiers & Qt::ControlModifier, shift = modifiers & Qt::ShiftModifier;
    const core::LineId line{id};
    const auto before = session->selection().active;
    std::optional<LineChangeOrigin> origin;
    if (!shift && (doubleClick ? ctrl : (m_settings->boolean("grid.changeActiveOnSelection") || !ctrl)))
        origin = LineChangeOrigin{true, !ctrl};
    m_lineChangeOrigin = origin;
    if (doubleClick) {
        if (ctrl && !shift)
            applySelection(shell().withFoldedRuns(rules.plain(session->selection(), line)));
    } else if (shift)
        applySelection(shell().withFoldedRuns(rules.shiftClick(session->selection(), line, ctrl)));
    else if (ctrl)
        applySelection(shell().withFoldedRuns(rules.ctrlClick(session->selection(), line)));
    else
        applySelection(shell().withFoldedRuns(rules.plain(session->selection(), line)));
    if (origin && m_lineChangeOrigin && session->selection().active == before)
        followShownLine(false, *origin);
    m_lineChangeOrigin.reset();
    if (!shift)
        followGridPress(before, line, modifiers, endColumn, doubleClick);
}

void Application::dragSelection(qulonglong id)
{
    const auto target = m_workspace.editingTarget();
    if (auto *session = target ? m_files->session(*target) : nullptr)
        applySelection(shell().withFoldedRuns(gridSelection().shiftClick(session->selection(), core::LineId{id}, false)));
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
        const auto &video = activeMedia().video->session();
        const auto frame = video.reportedFrame();
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
    editor().reloadFromSession();
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
    const auto shown = shell().actionLines();
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

const QString kExtradataMime = QStringLiteral("application/x-hikarisub-extradata");

QByteArray clipboardBytes(const std::u8string &text)
{
    return QByteArray(reinterpret_cast<const char *>(text.data()), static_cast<qsizetype>(text.size()));
}

void setClipboardRows(const std::u8string &text, const std::u8string &extradata)
{
    auto *data = new QMimeData;
    data->setText(QString::fromUtf8(clipboardBytes(text)));
    if (!extradata.empty())
        data->setData(kExtradataMime, clipboardBytes(extradata));
    QGuiApplication::clipboard()->setMimeData(data);
}

std::u8string clipboardExtradata()
{
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    const QByteArray bytes = data ? data->data(kExtradataMime) : QByteArray();
    return std::u8string(reinterpret_cast<const char8_t *>(bytes.constData()), static_cast<std::size_t>(bytes.size()));
}

}

bool Application::copyLines()
{
    auto *session = targetSession();
    if (!session || session->selection().selected.empty())
        return false;
    setClipboardRows(application::copyRows(*session), application::copyRowsExtradata(*session));
    return true;
}

bool Application::cutLines()
{
    return copyLines() && deleteLines();
}

bool Application::pasteLines()
{
    auto *session = targetSession();
    if (!session)
        return false;
    const bool done =
        application::pasteRows(*session, toU8(clipboardText()), shownLines(), {}, clipboardExtradata()).has_value();
    editor().reloadFromSession();
    refreshViews();
    return done;
}

int Application::selectedLineCount(bool shownOnly) const
{
    auto *session = targetSession();
    if (!session)
        return 0;
    const auto &selected = session->selection().selected;
    if (!shownOnly)
        return int(selected.size());
    const auto shown = shownLines();
    return int(std::ranges::count_if(selected, [&](core::LineId id) { return shown(id); }));
}

QVariantList Application::columnChoices(bool paste)
{
    using namespace core::column;
    auto *session = targetSession();
    if (!session)
        return {};
    const auto format = session->document().format();
    std::vector<std::pair<const char *, int>> rows;
    if (format == core::SubtitleFormat::Ass || format == core::SubtitleFormat::PlainText) {
        rows = {{QT_TRANSLATE_NOOP("HikariSub.Grid", "Layer"), Layer},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "Start time"), Start},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "End time"), End},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "Actor"), Actor},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "Style"), Style},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "Left margin"), MarginLeft},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "Right margin"), MarginRight},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "Vertical margin"), MarginVertical},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "Effect"), Effect}};
        if (!paste) {
            rows.push_back({QT_TRANSLATE_NOOP("HikariSub.Grid", "Text"), Text});
            rows.push_back({QT_TRANSLATE_NOOP("HikariSub.Grid", "Text without tags"), TextWithoutTags});
        } else if (application::translationMode(*session)) {
            rows.push_back({QT_TRANSLATE_NOOP("HikariSub.Grid", "Text into original"), Text});
            rows.push_back({QT_TRANSLATE_NOOP("HikariSub.Grid", "Text into translation"), Translation});
        } else {
            rows.push_back({QT_TRANSLATE_NOOP("HikariSub.Grid", "Text"), Text});
        }
        rows.push_back({QT_TRANSLATE_NOOP("HikariSub.Grid", "Start tag block"), StartBlock});
        rows.push_back({QT_TRANSLATE_NOOP("HikariSub.Grid", "Clip (\\clip or \\iclip)"), Clip});
        rows.push_back({QT_TRANSLATE_NOOP("HikariSub.Grid", "Only these tags"), NamedTags});
    } else if (format == core::SubtitleFormat::TMPlayer) {
        rows = {{QT_TRANSLATE_NOOP("HikariSub.Grid", "Start time"), Start},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "Text"), Text}};
    } else {
        // MUST KILL: the End row is labelled "Start time" on purpose
        rows = {{QT_TRANSLATE_NOOP("HikariSub.Grid", "Start time"), Start},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "Start time"), End},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "Text"), Text}};
    }
    const int chosen = paste ? m_pasteColumns : m_copyColumns;
    QVariantList out;
    for (const auto &[label, bit] : rows)
        out << QVariantMap{{QStringLiteral("label"), QCoreApplication::translate("HikariSub.Grid", label)},
                           {QStringLiteral("bit"), bit},
                           {QStringLiteral("checked"), (chosen & bit) != 0},
                           {QStringLiteral("tags"), (bit & Tags) != 0},
                           {QStringLiteral("named"), bit == NamedTags}};
    return out;
}

QString Application::columnTagNames() const
{
    return m_settings->text("grid.columnTagNames");
}

void Application::rememberColumns(bool paste, int columns, const QString &tagNames)
{
    (paste ? m_pasteColumns : m_copyColumns) = columns;
    m_settings->set(paste ? "grid.pasteColumns" : "grid.copyColumns", columns & ~core::column::Tags);
    m_settings->set(paste ? "grid.pasteColumnTags" : "grid.copyColumnTags", columns & core::column::Tags);
    if (columns & core::column::NamedTags)
        m_settings->set("grid.columnTagNames", tagNames);
}

namespace {

std::vector<std::u8string> tagNameList(const QString &names)
{
    std::vector<std::u8string> out;
    for (const QString &name : names.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        QString n = name.trimmed();
        while (n.startsWith(QLatin1Char('\\')))
            n.remove(0, 1);
        if (!n.isEmpty())
            out.push_back(toU8(n));
    }
    return out;
}

}

bool Application::copyColumns(int columns, const QString &tagNames)
{
    auto *session = targetSession();
    if (!session || session->selection().selected.empty())
        return false;
    rememberColumns(false, columns, tagNames);
    setClipboardText(application::copyColumns(*session, columns, tagNameList(tagNames)));
    return true;
}

bool Application::pasteColumns(int columns, const QString &tagNames)
{
    auto *session = targetSession();
    if (!session)
        return false;
    rememberColumns(true, columns, tagNames);
    const bool done = application::pasteColumns(*session, toU8(clipboardText()), columns, shownLines(), {},
                                                tagNameList(tagNames))
                          .has_value();
    editor().reloadFromSession();
    refreshViews();
    return done;
}

bool Application::setNewFps(const QString &oldFps, const QString &newFps)
{
    auto *session = targetSession();
    bool okOld = false, okNew = false;
    const double from = QLocale::c().toDouble(oldFps.trimmed(), &okOld);
    const double to = QLocale::c().toDouble(newFps.trimmed(), &okNew);
    if (!session || !okOld || !okNew)
        return false;
    const bool done = application::setNewFps(*session, from, to).has_value();
    editor().reloadFromSession();
    refreshViews();
    return done;
}

bool Application::setFpsFromVideo()
{
    auto *session = targetSession();
    const auto &video = activeMedia().video->session();
    const auto frame = video.reportedFrame();
    const auto start = frame ? video.frameStart(*frame) : std::nullopt;
    if (!session || !start)
        return false;
    const bool done = application::setFpsFromVideo(*session, start->microseconds() / 1000, shownLines()).has_value();
    editor().reloadFromSession();
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
    const application::Selection kept = session->selection();
    editor().reloadFromSession();
    if (kept.active)
        editor().showLine(kept.active->value);
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

QVariantMap Application::styleLineCounts() const
{
    QVariantMap out;
    if (auto *session = targetSession()) {
        for (const auto &style : core::decodeStyles(session->document()))
            out.insert(QString::fromUtf8(reinterpret_cast<const char *>(style.name.data()), static_cast<qsizetype>(style.name.size())), 0);
        for (const auto *line : session->document().lines()) {
            const QString name = QString::fromUtf8(reinterpret_cast<const char *>(line->style.data()), static_cast<qsizetype>(line->style.size()));
            out.insert(name, out.value(name).toInt() + 1);
        }
    }
    return out;
}

bool Application::selectLinesWithStyle(const QString &style)
{
    auto *session = targetSession();
    if (!session)
        return false;
    const QByteArray bytes = style.toUtf8();
    const std::u8string name(reinterpret_cast<const char8_t *>(bytes.constData()), static_cast<std::size_t>(bytes.size()));
    const application::LineVisible shown = m_gridFilter->ignoreInActions() ? application::LineVisible{} : shownLines();
    std::vector<core::LineId> found;
    for (const auto *line : session->document().lines())
        if (line->style == name && (!shown || shown(line->id)))
            found.push_back(line->id);
    const auto current = session->selection().active;
    if (found.empty()) {
        applySelection(application::Selection{current, {}, current, {}});
        return false;
    }
    std::set<core::LineId> selected(found.begin(), found.end());
    const core::LineId active = current && selected.contains(*current) ? *current : found.front();
    return applySelection(application::Selection{active, std::move(selected), active, {}});
}

std::string Application::recoveryKey(application::DocumentId document) const
{
    return m_sessionId.toStdString() + "-" + std::to_string(document.value);
}

void Application::scheduleAutosave(std::optional<application::DocumentId> document)
{
    if (!m_recovery || !m_recovery->enabled())
        return;
    const auto target = document ? document : m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session || !session->isDirty() || session->isReadOnly())
        return;
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

bool Application::autosavePending(application::DocumentId document) const
{
    const auto it = m_autosaveTimers.find(document.value);
    return it != m_autosaveTimers.end() && it->second && it->second->isActive();
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
    shell().setStatusText(QCoreApplication::translate("HikariSub.Session", "Autosave"));
    // Qt::CoarseTimer may fire up to 5 % early.
    QTimer::singleShot(5000, Qt::PreciseTimer, this, [this] { shell().setStatusText(QString()); });
    application::RecoveryContent content;
    const auto activeLine = application::activeLineToSave(*session);
    const auto layout = m_files->layoutToSave(document, session->document());
    core::Document saved = application::withActiveLine(session->document(), activeLine);
    if (layout)
        saved.setHikariProperty(application::kLayoutKey, *layout);
    m_files->applyProjectGarbage(document, m_files->destination(document).value_or(application::DestinationKey{}),
                                 saved);
    content.bytes = core::encodeSubtitle(saved, m_files->saveOptions());
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
    if (!m_recovery->write(recoveryKey(document), content))
        return false;
    if (activeLine)
        session->amendScriptInfo(application::kActiveLineKey, *activeLine);
    return true;
}

void Application::discardRecovery(application::DocumentId document)
{
    if (const auto it = m_autosaveTimers.find(document.value); it != m_autosaveTimers.end() && it->second)
        it->second->stop();
    if (m_recovery)
        m_recovery->discard(recoveryKey(document));
}

namespace {

bool sessionEnded(const QString &dir, const std::string &session)
{
    const QString path = dir + QStringLiteral("/sessions/") + QString::fromStdString(session) + QStringLiteral(".lock");
    QLockFile lock(path);
    if (!lock.tryLock(0))
        return false;
    lock.unlock();
    return true;
}

}

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

QString Application::targetVideoPath() const
{
    const auto target = m_workspace.editingTarget();
    const auto it = target ? m_tabMedia.find(target->value) : m_tabMedia.end();
    return it == m_tabMedia.end() ? QString() : it->second.video;
}

void Application::applyMatroska(application::MatroskaLoaded loaded, bool newTab)
{
    if (!newTab && m_matroskaTarget) {
        auto *session = m_files->session(*m_matroskaTarget);
        newTab = m_workspace.editingTarget() != m_matroskaTarget || !session || session->revision() != m_matroskaRevision
            || session->draftLine() != m_matroskaDraftLine || session->draftText() != m_matroskaDraft
            || m_reloads != m_matroskaReloads
            || targetVideoPath() != m_matroskaVideo
            || activeMedia().video->requestedVideo() != m_matroskaRequested;
    } else if (!newTab && m_matroskaFromHome) {
        newTab = m_workspace.editingTarget().has_value();
    }
    m_matroskaTarget.reset();
    m_matroskaFromHome = false;
    if (newTab) {
        const QString subsPath = QDir::toNativeSeparators(QString::fromStdString(loaded.subtitlePath));
        const auto id = m_files->createUnsaved(std::move(loaded.document));
        if (auto *session = m_files->session(id)) {
            selectLegacyActiveLine(*session);
            selectRow(id, 0);
        }
        m_workspace.add(id, QFileInfo(subsPath).fileName().toStdString());
        m_importPaths[id.value] = subsPath;
        // MUST KILL: operator[] alone, to create the tab's empty media entry
        m_tabMedia[id.value];
        m_workspace.setEditingTarget(id);
        refreshViews();
        QTimer::singleShot(0, this, [this] { checkResolution(); });
        return;
    }
    const auto old = m_workspace.editingTarget();
    int row = 0;
    if (auto *session = old ? m_files->session(*old) : nullptr)
        if (const auto active = session->selection().active) {
            const auto lines = session->document().lines();
            for (int i = 0; i < int(lines.size()); ++i)
                if (lines[std::size_t(i)]->id == *active)
                    row = i;
        }
    const QString subsPath = QDir::toNativeSeparators(QString::fromStdString(loaded.subtitlePath));
    const auto id = m_files->createUnsaved(std::move(loaded.document));
    if (auto *session = m_files->session(id)) {
        selectLegacyActiveLine(*session);
        selectRow(id, row);
    }
    if (!old) {
        TabMedia media;
        media.video = QDir::toNativeSeparators(readyVideo(activeMedia().video->session()));
        media.position = targetVideoPosition();
        media.audio = activeMedia().audio->hasAudio() ? QDir::toNativeSeparators(activeMedia().audio->path()) : QString();
        activeMedia().keepVideoOnEnter = !media.video.isEmpty();
        m_tabMedia[id.value] = media;
    }
    m_workspace.add(id, QFileInfo(subsPath).fileName().toStdString());
    m_importPaths[id.value] = subsPath;
    replaceTarget(id, true);
    QTimer::singleShot(0, this, [this] { checkResolution(); });
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
    const auto id = m_files->createUnsaved(std::move(document));
    auto *session = m_files->session(id);
    selectLegacyActiveLine(*session);
    if (content->draftRow) {
        const auto lines = session->document().lines();
        if (*content->draftRow < lines.size()) {
            const auto line = lines[*content->draftRow]->id;
            session->setSelection(application::Selection{line, {line}, line, {}});
            session->editDraft(line, content->draft);
        }
    }
    m_workspace.add(id, QCoreApplication::translate("HikariSub.Session", "%1 (recovered)")
                            .arg(QString::fromStdString(content->title)).toStdString());
    takeFileArrangement(id);
    m_workspace.setEditingTarget(id);
    refreshViews();
    return true;
}

QVariantList Application::legacyAutosaves()
{
    QVariantList rows;
    QString folder = m_legacyAutosaveDir;
    if (folder.isEmpty() && !m_settingsFile.isEmpty()) {
        QStringList roots;
        if (const auto receipt = m_importStore ? m_importStore->activeReceipt() : std::nullopt;
            receipt && !receipt->root.empty())
            roots << QString::fromStdString(receipt->root);
        for (const auto &root : SettingsImportStore::discoverRoots(SettingsImportStore::defaultCandidates()))
            roots << root.path;
        for (const QString &root : std::as_const(roots))
            if (QFileInfo(root + QStringLiteral("/Subs")).isDir()) {
                folder = root + QStringLiteral("/Subs");
                break;
            }
    }
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return rows;
    const auto files = application::listLegacyAutosaves(std::filesystem::path(folder.toStdU16String()),
                                                        QCoreApplication::translate("HikariSub.Documents", "Untitled").toStdU16String());
    if (files.empty()) {
        // FindFirstFileW always finds "." and ".."; elsewhere an empty folder counts as unreadable.
#ifdef _WIN32
        m_log->notice(ui::LogController::Info, QCoreApplication::translate("HikariSub.Session", "Open autosave"),
                      QCoreApplication::translate("HikariSub.Session", "The autosave folder is empty."));
#else
        m_log->notice(ui::LogController::Info, QCoreApplication::translate("HikariSub.Session", "Open autosave"), QDir(folder).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)
                       ? QCoreApplication::translate("HikariSub.Session", "The autosave folder cannot be opened.")
                       : QCoreApplication::translate("HikariSub.Session", "The autosave folder is empty."));
#endif
        return rows;
    }
    for (const auto &file : files) {
        QVariantList versions;
        for (const auto &version : file.versions)
        {
            const QString path = QString::fromStdU16String(version.file.u16string());
            versions << QVariantMap{{QStringLiteral("written"), QString::fromStdString(version.written)},
                                    {QStringLiteral("time"), QFileInfo(path).lastModified().toString(Qt::ISODate)},
                                    {QStringLiteral("file"), path}};
        }
        rows << QVariantMap{{QStringLiteral("name"), QString::fromStdU16String(file.name)},
                            {QStringLiteral("versions"), versions}};
    }
    return rows;
}

QVariantList Application::filterLegacyAutosaves(const QVariantList &files, const QString &query, bool allWords) const
{
    QVariantList shown;
    const QStringList tokens = query.split(u' ', Qt::SkipEmptyParts);
    for (int k = 0; k < files.size(); ++k) {
        const QString name = files[k].toMap().value(QStringLiteral("name")).toString().toLower();
        bool found = allWords;
        for (const QString &token : tokens) {
            const bool has = name.contains(token.toLower());
            if (allWords && !has) {
                found = false;
                break;
            }
            if (!allWords && has) {
                found = true;
                break;
            }
        }
        if (query.isEmpty() || found)
            shown << k;
    }
    return shown;
}

bool Application::openLegacyAutosave(const QString &file)
{
    auto staged = m_files->stageOpen({QFileInfo(file).absoluteFilePath().toStdString()});
    if (!staged) {
        m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Session", "Open autosave"),
                      QCoreApplication::translate("HikariSub.Session", "Failed to load autosave"));
        return false;
    }
    const auto id = m_files->createUnsaved(std::move(staged->load.document));
    if (auto *session = m_files->session(id))
        selectLegacyActiveLine(*session);
    m_workspace.add(id, QCoreApplication::translate("HikariSub.Session", "%1 (recovered)").arg(QFileInfo(file).fileName()).toStdString());
    takeFileArrangement(id);
    // MUST KILL: operator[] alone, to create the tab's empty media entry
    m_tabMedia[id.value];
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
    const auto &video = activeMedia().video->session();
    const auto timebase = video.legacyTimebase();
    const auto shown = shownLines();
    if (kind == QLatin1String("videoTime")) {
        const auto frame = video.reportedFrame();
        const auto start = frame ? video.frameStart(*frame) : std::nullopt;
        if (!start)
            return false;
        const std::int64_t tell = start->microseconds() / 1000;
        return runFilter([&](application::EditSession &s) { return application::splitAtVideoTime(s, timebase, tell, shown); });
    }
    if (kind == QLatin1String("frames")) {
        const auto animations = m_settings->boolean("grid.splitFramesBakeAnimations") ? application::FrameAnimations::Bake
                                                                                       : application::FrameAnimations::Keep;
        return runFilter([&](application::EditSession &s) { return application::splitIntoFrames(s, timebase, animations, shown); });
    }
    const auto text = kind == QLatin1String("chars")   ? application::SplitText::Characters
                      : kind == QLatin1String("words") ? application::SplitText::Words
                      : kind == QLatin1String("wraps") ? application::SplitText::Wraps
                      : kind == QLatin1String("tagBlocks") ? application::SplitText::TagBlocks
                                                           : std::optional<application::SplitText>();
    if (!text)
        return false;
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

bool Application::splitEvery(int count, bool seconds)
{
    const auto timebase = activeMedia().video->session().legacyTimebase();
    const auto unit = seconds ? application::SplitUnit::Seconds : application::SplitUnit::Frames;
    return runFilter([&](application::EditSession &s) { return application::splitEvery(s, timebase, count, unit, shownLines()); });
}

bool Application::canPasteTranslation() const
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    if (!session || targetUntitled())
        return false;
    return session->document().format() == core::SubtitleFormat::Ass;
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
    const auto read = backends::readLegacyTextFile(path);
    if (!read) {
        m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Translation", "Paste translation"),
                      QCoreApplication::translate("HikariSub.Translation", "Could not open %1; nothing was changed.")
                          .arg(QFileInfo(path).fileName()));
        return false;
    }
    const QString text = QString::fromStdU16String(*read);
    const QString extension = path.section(QLatin1Char('.'), -1);
    const auto shown = shownLines();
    const bool done = application::pasteTranslation(*session, toU8(text), toU8(extension), shown).has_value();
    if (done)
        originalColumns(*m_workspace.editingTarget()).pasted(session->document());
    editor().reloadFromSession();
    refreshViews();
    return done;
}

bool Application::canShiftTranslation() const
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    return session && session->document().scriptInfo(u8"TLMode Showtl") == u8"Yes";
}

namespace {

using PositionCommand = std::function<std::expected<void, application::CommandRefusal>(const application::position::Geometry &)>;

std::expected<void, application::CommandRefusal> withGeometry(const application::EditSession &session,
                                                              const PositionCommand &command)
{
    namespace transform = application::visual::transform;
    ui::QtTextMeasurePort measure;
    const auto resolution = application::scriptResolution(session.document());
    transform::Context context;
    context.document = &session.document();
    context.styles = core::decodeStyles(session.document());
    context.width = resolution.width;
    context.height = resolution.height;
    context.measure = &measure;
    application::position::Geometry geometry;
    geometry.width = resolution.width;
    geometry.height = resolution.height;
    geometry.style = [&](const core::LineRecord &line) { return transform::lineStyle(context, line.style); };
    geometry.size = [&](const core::LineRecord &line) -> std::optional<application::position::Size> {
        const auto style = transform::lineStyle(context, line.style);
        const auto size = transform::textSize(context, line, &style, false).size;
        if (size.x <= 0 && size.y <= 0)
            return std::nullopt;
        return application::position::Size{size.x, size.y};
    };
    return command(geometry);
}

}

bool Application::changeAlignment(int an)
{
    const auto shown = shownLines();
    return runFilter([&](application::EditSession &s) {
        return withGeometry(s, [&](const auto &g) { return application::position::changeAlignment(s, an, g, shown); });
    });
}

bool Application::alignLines(int edge)
{
    if (edge < 0 || edge > 5)
        return false;
    const auto to = m_settings->boolean("grid.alignToVideoFrame") ? application::position::RelativeTo::VideoFrame
                                                                  : application::position::RelativeTo::ActiveLine;
    const auto shown = shownLines();
    return runFilter([&](application::EditSession &s) {
        return withGeometry(s, [&](const auto &g) {
            return application::position::align(s, static_cast<application::position::Edge>(edge), to, g, shown);
        });
    });
}

bool Application::distributeLines(bool vertical)
{
    const auto axis = vertical ? application::position::Axis::Vertical : application::position::Axis::Horizontal;
    const auto shown = shownLines();
    return runFilter([&](application::EditSession &s) {
        return withGeometry(s, [&](const auto &g) { return application::position::distribute(s, axis, g, shown); });
    });
}

bool Application::positionToMargins()
{
    const auto shown = shownLines();
    return runFilter([&](application::EditSession &s) {
        return withGeometry(s, [&](const auto &g) { return application::position::positionToMargins(s, g, shown); });
    });
}

bool Application::marginsToPosition()
{
    const auto shown = shownLines();
    return runFilter([&](application::EditSession &s) {
        return withGeometry(s, [&](const auto &g) { return application::position::marginsToPosition(s, g, shown); });
    });
}

bool Application::phantom(bool keepEnd)
{
    auto *session = targetSession();
    if (!session || !session->selection().active)
        return false;
    std::optional<std::pair<std::size_t, std::size_t>> selection;
    for (const auto *line : session->document().lines()) {
        if (line->id != *session->selection().active)
            continue;
        const bool editsTranslation = !line->translation.empty();
        if (editsTranslation == application::translationMode(*session)) {
            const auto [from, to] = editor().rawFieldSelection(editsTranslation ? 1 : 0);
            if (from >= 0 && to > from)
                selection = std::pair{static_cast<std::size_t>(from), static_cast<std::size_t>(to)};
        }
    }
    const auto keep = keepEnd ? application::position::Keep::End : application::position::Keep::Start;
    return runFilter([&](application::EditSession &s) { return application::position::phantom(s, keep, selection); });
}

bool Application::cycleTag(int tag, bool back)
{
    if (tag < 0 || tag > 5)
        return false;
    const auto which = static_cast<application::position::CycleTag>(tag);
    const auto name = application::position::cycleTagName(which);
    std::vector<std::u8string> values;
    const std::string setting = "editing.cycleValues." + std::string(name.begin(), name.end());
    for (const QString &v : m_settings->text(setting.c_str()).split(QLatin1Char(','), Qt::SkipEmptyParts))
        if (const QString trimmed = v.trimmed(); !trimmed.isEmpty())
            values.push_back(toU8(trimmed));
    if (values.empty())
        values = application::position::defaultCycleValues(which);
    const auto shown = shownLines();
    return runFilter([&](application::EditSession &s) { return application::position::cycle(s, which, values, back, shown); });
}

bool Application::shiftTranslation(int mode)
{
    auto *session = targetSession();
    if (!session || mode < 0 || mode > 5)
        return false;
    if (!showOriginal(*m_workspace.editingTarget()))
        return false;
    const auto shown = shownLines();
    const bool done =
        application::moveTranslation(*session, static_cast<application::TranslationMove>(mode), shown).has_value();
    editor().reloadFromSession();
    if (const auto active = session->selection().active)
        editor().showLine(active->value);
    refreshViews();
    return done;
}

QString Application::openKeyframes(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (const auto target = m_workspace.editingTarget())
        m_tabMedia[target->value].keyframes = QDir::toNativeSeparators(path);
    auto &video = activeMedia().video->session();
    if (video.state() != application::VideoSession::State::Ready) {
        if (QString problem; keyframesWithoutVideo(path, problem))
            return problem;
        activeMedia().pendingKeyframes = path;
        return {};
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QCoreApplication::translate("HikariSub.Video", "Invalid keyframes format");
    const QByteArray bytes = f.readAll();
    auto frames = application::parseKeyframes(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (frames.empty())
        return QCoreApplication::translate("HikariSub.Video", "Invalid keyframes format");
    video.setKeyframes(std::move(frames));
    refreshViews();
    return {};
}

bool Application::exactTimebase() const
{
    const auto &video = activeMedia().video->session();
    return (video.state() == application::VideoSession::State::Ready || video.replacedFrameTimes()) &&
           video.legacyTimebase().exact();
}

application::ShiftContext Application::shiftContext(application::LegacyTimebase &timebase) const
{
    const auto &video = activeMedia().video->session();
    application::ShiftContext context;
    const bool hasVideo = video.state() == application::VideoSession::State::Ready;
    if (hasVideo || video.replacedFrameTimes()) {
        timebase = video.legacyTimebase();
        context.timebase = &timebase;
    }
    if (hasVideo) {
        context.keyframes = video.keyframes();
        if (const auto frame = video.reportedFrame()) {
            context.videoFrame = *frame;
            context.videoFrameStartMs = timebase.startTimeFor(*frame);
            context.videoFrameEndMs = timebase.endTimeFor(*frame);
        }
    }
    if (activeMedia().audio && activeMedia().audio->hasMark())
        context.audioMarkMs = activeMedia().audio->markMs();
    return context;
}

application::ShiftTimesSettings Application::shiftRunSettings(const QString &tool) const
{
    auto settings = m_shiftTimes->runSettings(tool == QLatin1String("postprocessor")
                                                  ? ui::ShiftTimesController::Tool::Postprocessor
                                                  : ui::ShiftTimesController::Tool::Shift);
    settings.timePerCharacter = m_settings->integer("convert.timePerCharacter");
    return settings;
}

namespace {

QString shiftFailureText(const application::ShiftFailure &failure)
{
    if (const auto *problem = std::get_if<application::ShiftProblem>(&failure)) {
        switch (*problem) {
        case application::ShiftProblem::NoStylesChosen:
            return QCoreApplication::translate("HikariSub.TimeShift", "No styles selected for time shifting");
        case application::ShiftProblem::NoLinesSelected:
            return QCoreApplication::translate("HikariSub.TimeShift", "No lines selected for shifting");
        case application::ShiftProblem::NoExactTimebase:
            return QCoreApplication::translate("HikariSub.TimeShift", "Needs a video.");
        }
    }
    return QCoreApplication::translate("HikariSub.TimeShift", "The times could not be shifted.");
}

}

QString Application::applyShift(const QString &tool)
{
    auto *session = targetSession();
    if (!session)
        return QCoreApplication::translate("HikariSub.TimeShift", "No document open");
    const bool postprocessor = tool == QLatin1String("postprocessor");
    application::LegacyTimebase timebase;
    const auto context = shiftContext(timebase);
    const auto settings = shiftRunSettings(tool);
    const auto visible = actionLines(*session);
    if (const auto plan = application::planShift(*session, settings, context, visible);
        plan && std::ranges::none_of(plan->lines, &application::ShiftedLine::changed))
        return {};
    const std::string step = postprocessor ? "Timing postprocessor" : "Shift times";
    const auto result = application::shiftTimes(*session, settings, context, visible, step);
    editor().reloadFromSession();
    refreshViews();
    if (!result)
        return shiftFailureText(result.error());
    if (result->endCorrectionSkipped)
        m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.TimeShift", "Shift times"),
                      QCoreApplication::translate("HikariSub.TimeShift", "Video was not loaded using FFMS2"));
    if (result->centeringSkipped)
        m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.TimeShift", "Shift times"),
                      QCoreApplication::translate("HikariSub.TimeShift", "Times were not centred within frames: no video is open"));
    return {};
}

QVariantMap Application::shiftPreview(const QString &tool, int shellWindow)
{
    constexpr qsizetype kRows = 8;
    QVariantMap out{{QStringLiteral("problem"), QString()}, {QStringLiteral("changed"), 0},
                    {QStringLiteral("total"), 0},          {QStringLiteral("amount"), 0},
                    {QStringLiteral("targetMs"), -1},      {QStringLiteral("rows"), QVariantList()},
                    {QStringLiteral("more"), 0},           {QStringLiteral("ruleCounts"), QVariantMap()}};
    auto *session = windowSession(shellWindow);
    if (!session) {
        out.insert(QStringLiteral("problem"), QCoreApplication::translate("HikariSub.TimeShift", "No document open"));
        return out;
    }
    application::LegacyTimebase timebase;
    const auto context = shiftContext(timebase);
    const auto settings = shiftRunSettings(tool);
    if (settings.moveToVideoTime && context.videoFrame) {
        const auto at = settings.fromStartTime ? context.videoFrameStartMs : context.videoFrameEndMs;
        out.insert(QStringLiteral("targetMs"), at ? *at : -1);
    } else if (settings.moveToAudioTime && context.audioMarkMs) {
        out.insert(QStringLiteral("targetMs"), *context.audioMarkMs);
    }
    const auto plan = application::planShift(*session, settings, context, actionLines(*session));
    if (!plan) {
        out.insert(QStringLiteral("problem"), shiftFailureText(plan.error()));
        return out;
    }
    const auto &selected = session->selection().selected;
    const auto lines = session->document().lines();
    int changed = 0, leadIn = 0, leadOut = 0, gaps = 0, snapped = 0;
    std::vector<const application::ShiftedLine *> shown;
    for (const auto &l : plan->lines) {
        if (!l.changed())
            continue;
        ++changed;
        leadIn += (l.rules & application::LeadIn) != 0;
        leadOut += (l.rules & application::LeadOut) != 0;
        gaps += (l.rules & application::GapClosed) != 0;
        snapped += (l.rules & (application::SnappedStart | application::SnappedEnd)) != 0;
    }
    for (const auto &l : plan->lines)
        if (selected.contains(l.id) && std::ssize(shown) < kRows)
            shown.push_back(&l);
    for (const auto &l : plan->lines)
        if (!selected.contains(l.id) && l.changed() && std::ssize(shown) < kRows)
            shown.push_back(&l);
    std::ranges::sort(shown, {}, &application::ShiftedLine::row);
    QVariantList rows;
    int shownChanged = 0;
    for (const auto *l : shown) {
        shownChanged += l->changed();
        QStringList rules;
        if (l->rules & application::LeadIn)
            rules << QStringLiteral("leadIn");
        if (l->rules & application::GapClosed)
            rules << QStringLiteral("gapClosed");
        if (l->rules & application::SnappedStart)
            rules << QStringLiteral("snappedStart");
        if (l->rules & application::LeadOut)
            rules << QStringLiteral("leadOut");
        if (l->rules & application::SnappedEnd)
            rules << QStringLiteral("snappedEnd");
        const auto &text = l->row < lines.size() ? lines[l->row]->text : std::u8string();
        rows << QVariantMap{{QStringLiteral("line"), int(l->row) + 1},
                            {QStringLiteral("startBefore"), l->startBefore},
                            {QStringLiteral("endBefore"), l->endBefore},
                            {QStringLiteral("start"), l->start},
                            {QStringLiteral("end"), l->end},
                            {QStringLiteral("changed"), l->changed()},
                            {QStringLiteral("text"), QString::fromUtf8(reinterpret_cast<const char *>(text.data()),
                                                                       qsizetype(text.size()))},
                            {QStringLiteral("rules"), rules}};
    }
    out.insert(QStringLiteral("changed"), changed);
    out.insert(QStringLiteral("total"), int(plan->lines.size()));
    out.insert(QStringLiteral("amount"), plan->amount);
    out.insert(QStringLiteral("frames"), settings.byFrames);
    out.insert(QStringLiteral("rows"), rows);
    out.insert(QStringLiteral("more"), changed - shownChanged);
    out.insert(QStringLiteral("ruleCounts"), QVariantMap{{QStringLiteral("leadIn"), leadIn},
                                                         {QStringLiteral("leadOut"), leadOut},
                                                         {QStringLiteral("gapClosed"), gaps},
                                                         {QStringLiteral("snapped"), snapped}});
    return out;
}

bool Application::hasKeyframes() const
{
    return activeMedia().video->session().state() == application::VideoSession::State::Ready &&
           !activeMedia().video->session().keyframes().empty();
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
    s.field = static_cast<S::Field>(std::clamp(m.value(QStringLiteral("field"), int(s.field)).toInt(), 0,
                                               int(S::Field::Overlaps)));
    s.dialogues = m.value(QStringLiteral("dialogues"), s.dialogues).toBool();
    s.comments = m.value(QStringLiteral("comments"), s.comments).toBool();
    s.mode = static_cast<S::Mode>(std::clamp(m.value(QStringLiteral("mode"), int(s.mode)).toInt(), 0, 2));
    s.action = static_cast<S::Action>(std::clamp(m.value(QStringLiteral("action"), int(s.action)).toInt(), 0, 6));
    return s;
}

QString regexMatchError(const std::u16string &error);
QString fromUtf16(const std::u16string &s);

}

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
    const application::TextFold fold = unicodeFold;
    std::vector<application::EditSession *> sessions;
    if (allTabs) {
        protectReference();
        for (const auto id : m_workspace.allTabs())
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
        if (result->invalidExpression)
            m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.SelectLines", "Select lines"),
                          QCoreApplication::translate("HikariSub.SelectLines", "Invalid regular expression “%1”: %2")
                              .arg(fromUtf16(result->invalidExpression->first), fromUtf16(result->invalidExpression->second)));
        for (const auto &error : result->matchErrors)
            m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.SelectLines", "Select lines"),
                          regexMatchError(error));
        count += result->count;
        if (result->clipboard)
            setClipboardText(*result->clipboard);
    }
    editor().reloadFromSession();
    refreshViews();
    std::vector<std::u8string> recent;
    for (const QString &r : std::as_const(m_selectRecent))
        recent.push_back(utf8(r));
    m_selectRecent.clear();
    for (const auto &r : application::addRecentSelection(std::move(recent), settings.find))
        m_selectRecent << fromUtf8(r);
    m_settings->set("selectLines.recentSelections", m_selectRecent);
    saveSelectLinesSettings(map);
    using M = application::SelectLinesSettings::Mode;
    return settings.mode == M::Select           ? QCoreApplication::translate("HikariSub.SelectLines", "%1 lines selected.").arg(count)
           : settings.mode == M::AddToSelection ? QCoreApplication::translate("HikariSub.SelectLines",
                                                                              "%1 lines added to selection.").arg(count)
                                                : QCoreApplication::translate("HikariSub.SelectLines", "%1 lines deselected.").arg(count);
}

QString Application::selectStylesPattern(const QStringList &styles) const
{
    std::vector<std::u8string> names;
    for (const QString &s : styles)
        names.push_back(utf8(s));
    return fromUtf8(application::stylesPattern(names));
}

namespace {

QString fromUtf16(const std::u16string &s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), static_cast<qsizetype>(s.size()));
}

QString unscaledValue(int row, const std::u16string &value, const std::u16string &tag)
{
    return QCoreApplication::translate("HikariSub.Conversion", "In line %1, value “%2” cannot be scaled in tag “%3”.")
        .arg(row)
        .arg(QString::fromStdU16String(value), QString::fromStdU16String(tag))
        .replace(QLatin1Char('\n'), QLatin1Char(' '));
}

void noticeUnscaled(ui::LogController &log, const QStringList &values)
{
    if (values.isEmpty())
        return;
    log.notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Conversion", "Resample subtitles"),
               QCoreApplication::translate("HikariSub.Conversion", "%n value(s) could not be scaled.", nullptr,
                                           int(values.size())),
               values.join(QLatin1Char('\n')));
}

QString unadjustedRotation(int row)
{
    return QCoreApplication::translate("HikariSub.Conversion",
                                       "Line %1 was only scaled: it moves, changes shape over time or cannot be measured.")
        .arg(row);
}

void noticeUnadjusted(ui::LogController &log, const QStringList &rows)
{
    if (rows.isEmpty())
        return;
    log.notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Conversion", "Resample subtitles"),
               QCoreApplication::translate("HikariSub.Conversion", "%n rotated line(s) could not keep their shape.",
                                           nullptr, int(rows.size())),
               rows.join(QLatin1Char('\n')));
}

QString regexMatchError(const std::u16string &error)
{
    return QCoreApplication::translate("HikariSub.SelectLines", "Failed to find match for regular expression: %1")
        .arg(fromUtf16(error));
}

QString findQs(std::u8string_view s)
{
    return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), static_cast<qsizetype>(s.size()));
}

template <typename E, std::size_t N>
E findChoice(const QVariantMap &m, const char *key, const std::array<const char *, N> &names)
{
    const QString value = m.value(QLatin1String(key)).toString();
    for (std::size_t i = 0; i < N; ++i)
        if (value == QLatin1String(names[i]))
            return static_cast<E>(i);
    return E{};
}

template <typename E, std::size_t N>
QString findChoiceName(E value, const std::array<const char *, N> &names)
{
    return QLatin1String(names[static_cast<std::size_t>(value)]);
}

constexpr std::array<const char *, 3> kFindScopes{"document", "openDocuments", "folder"};
constexpr std::array<const char *, 4> kFindFields{"text", "style", "actor", "effect"};
constexpr std::array<const char *, 3> kFindLines{"all", "selected", "fromSelection"};
constexpr std::array<const char *, 3> kFindInText{"all", "outsideTags", "insideTags"};
constexpr std::array<const char *, 3> kFindMatchAt{"anywhere", "start", "end"};

application::FindReplaceSettings findSettings(const QVariantMap &m)
{
    using S = application::FindReplaceSettings;
    S s;
    s.scope = findChoice<S::Scope>(m, "scope", kFindScopes);
    s.replaceShown = m.value(QStringLiteral("replaceShown")).toBool();
    s.find = toU8(m.value(QStringLiteral("find")).toString());
    s.replace = toU8(m.value(QStringLiteral("replace")).toString());
    s.styles = toU8(m.value(QStringLiteral("styles")).toString());
    s.filters = toU8(m.value(QStringLiteral("filters")).toString());
    s.folder = toU8(m.value(QStringLiteral("folder")).toString());
    s.field = findChoice<S::Field>(m, "field", kFindFields);
    s.lines = findChoice<S::Lines>(m, "lines", kFindLines);
    s.matchCase = m.value(QStringLiteral("matchCase")).toBool();
    s.regex = m.value(QStringLiteral("regex")).toBool();
    s.matchAt = findChoice<S::MatchAt>(m, "matchAt", kFindMatchAt);
    s.inText = findChoice<S::InText>(m, "inText", kFindInText);
    s.includeComments = m.value(QStringLiteral("includeComments")).toBool();
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
    return {{QStringLiteral("scope"), findChoiceName(s.scope, kFindScopes)},
            {QStringLiteral("replaceShown"), s.replaceShown},
            {QStringLiteral("find"), q(s.find)},
            {QStringLiteral("replace"), q(s.replace)},
            {QStringLiteral("styles"), q(s.styles)},
            {QStringLiteral("filters"), q(s.filters)},
            {QStringLiteral("folder"), q(s.folder)},
            {QStringLiteral("field"), findChoiceName(s.field, kFindFields)},
            {QStringLiteral("lines"), findChoiceName(s.lines, kFindLines)},
            {QStringLiteral("matchCase"), s.matchCase},
            {QStringLiteral("regex"), s.regex},
            {QStringLiteral("matchAt"), findChoiceName(s.matchAt, kFindMatchAt)},
            {QStringLiteral("inText"), findChoiceName(s.inText, kFindInText)},
            {QStringLiteral("includeComments"), s.includeComments},
            {QStringLiteral("subfolders"), s.subfolders},
            {QStringLiteral("hiddenFolders"), s.hiddenFolders},
            {QStringLiteral("finds"), findList(recent.finds)},
            {QStringLiteral("replacements"), findList(recent.replacements)},
            {QStringLiteral("filterList"), findList(recent.filters)},
            {QStringLiteral("paths"), findList(recent.paths)}};
}

void findFirstEntries(application::FindReplaceSettings &s, const application::FindReplace::Recent &recent)
{
    const auto first = [](const std::vector<std::u8string> &l) { return l.empty() ? std::u8string() : l.front(); };
    s.find = first(recent.finds);
    s.replace = first(recent.replacements);
    s.filters = first(recent.filters);
    s.folder = first(recent.paths);
}

}

namespace {

QString qstr(std::u16string_view s)
{
    return QString::fromUtf16(s.data(), static_cast<qsizetype>(s.size()));
}

// MUST KILL: takes an inclusive end, returns an exclusive one
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

}

void Application::noteRemovedRules()
{
    const QString rules = QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/rules.toml");
    if (QFileInfo::exists(rules))
        m_log->log(QCoreApplication::translate("HikariSub.MainWindow",
                                               "Fix minor errors was removed. Your rules stay in %1, unused.")
                       .arg(QDir::toNativeSeparators(rules)));
}

void Application::convertListFiles()
{
    m_fontCatalogs->convertFiles();
    if (m_settingsFile.isEmpty())
        return;
    const QString folder = QFileInfo(m_settingsFile).absolutePath();
    auto shapes = ui::ListFile::make(folder + QStringLiteral("/shapes.toml"), folder + QStringLiteral("/ShapesSettings.txt"),
                                     ui::kShapesFormat,
                                     [](const std::filesystem::path &legacy) -> std::optional<toml::array> {
                                         if (const auto shapes = ui::readLegacyShapes(legacy))
                                             return ui::shapesToToml(*shapes);
                                         return std::nullopt;
                                     },
                                     listFileReport(*m_log));
    auto tags = ui::ListFile::make(folder + QStringLiteral("/tags.toml"), folder + QStringLiteral("/AllTagsSettings.txt"),
                                   ui::kTagsFormat,
                                   [](const std::filesystem::path &legacy) -> std::optional<toml::array> {
                                       if (const auto tags = ui::readLegacyTags(legacy))
                                           return ui::tagsToToml(*tags);
                                       return std::nullopt;
                                   },
                                   listFileReport(*m_log));
    for (const auto &file : {shapes, tags})
        file->convert();
    for (auto *views : allViews()) {
        views->visualTools->setShapesFile(shapes);
        views->visualTools->setAllTagsFile(tags);
    }
}

Application::SpellSlot *Application::spellSlot(const QString &language)
{
    if (m_spellingFolders.empty() || !m_spellingLoader)
        return nullptr;
    auto &slot = m_spellCheckers[language];
    if (!slot.checker) {
        slot.checker = std::make_unique<application::SpellChecker>(m_spellingFolders, m_spellingLoader);
        slot.checker->setLoadedHandler(
            [this] { QMetaObject::invokeMethod(this, [this] { spellingRefresh(); }, Qt::QueuedConnection); });
    }
    return &slot;
}

application::SpellChecker *Application::spellChecker(int shellWindow)
{
    const auto &settings = windowSettings(shellWindow);
    const QString language = QString::fromStdString(settings.text("editor.dictionaryLanguage"));
    auto *slot = spellSlot(language);
    if (!slot)
        return nullptr;
    if (!slot->started) {
        slot->started = true;
        if (settings.boolean("editor.spellchecker")) {
            const auto status = slot->checker->initialize(language.toStdU16String());
            if (status != application::SpellChecker::Status::Ready) {
                if (!views(shellWindow).settings->overridden("editor.dictionaryLanguage"))
                    m_settings->set("editor.spellchecker", false);
                const bool noDictionary = status == application::SpellChecker::Status::NoDictionary;
                const QString message =
                    status == application::SpellChecker::Status::NoDictionary
                        ? QCoreApplication::translate("HikariSub.Spelling",
                                                      "No dictionary files were found in the “%1” folder. Spell checking will be disabled.")
                              .arg(QDir::toNativeSeparators(QFileInfo(m_dictionaryDir).absoluteFilePath()))
                        : QCoreApplication::translate("HikariSub.Spelling", "Failed to initialise the spellchecker.");
                QMetaObject::invokeMethod(this, [this, message, noDictionary] {
                    m_log->notice(noDictionary ? ui::LogController::Warning : ui::LogController::Error,
                                  QCoreApplication::translate("HikariSub.Spelling", "Spellchecker"),
                                  QString(message).replace(QLatin1Char('\n'), QLatin1Char(' ')));
                    emit spellingChanged();
                }, Qt::QueuedConnection);
            }
        }
    }
    return slot->checker.get();
}

application::SpellChecker *Application::startedSpellChecker(int shellWindow)
{
    const auto slot =
        m_spellCheckers.find(QString::fromStdString(windowSettings(shellWindow).text("editor.dictionaryLanguage")));
    return slot != m_spellCheckers.end() && slot->second.started ? slot->second.checker.get() : nullptr;
}

application::SpellChecker *Application::anySpellChecker() const
{
    return m_spellCheckers.empty() ? nullptr : m_spellCheckers.begin()->second.checker.get();
}

void Application::userWordsChanged(const application::SpellChecker *by)
{
    for (auto &[language, slot] : m_spellCheckers)
        if (slot.checker.get() != by) {
            slot.checker->clear();
            slot.started = false;
        }
}

void Application::restartSpellChecker()
{
    for (auto &[language, slot] : m_spellCheckers) {
        slot.checker->clear();
        slot.started = false;
    }
    emit spellCheckerRestarted();
}

void Application::spellingRefresh()
{
    refreshViews();
    emit spellingChanged();
}

bool Application::spellingOn() const
{
    return windowSettings(m_workspace.activeWindow()).boolean("editor.spellchecker");
}

bool Application::editorSpellingOn() const
{
    const int window = m_workspace.activeWindow();
    return views(window).settings->overridden("editor.spellchecker") ? spellingOn() : m_spellingOn;
}

QString Application::dictionaryLanguage() const
{
    return QString::fromStdString(windowSettings(m_workspace.activeWindow()).text("editor.dictionaryLanguage"));
}

bool Application::suggestionsOnDoubleClick() const
{
    return m_settings->boolean("editor.suggestionsOnDoubleClick");
}

void Application::setSpellingOn(bool on)
{
    if (on == m_spellingOn && on == spellingOn())
        return;
    m_spellingOn = on;
    m_settings->set("editor.spellchecker", on);
    spellingRefresh();
}

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

int Application::spellingRole() const
{
    const auto *session = targetSession();
    return session && application::spellsTranslation(session->document()) ? 1 : 0;
}

namespace {

std::optional<std::u16string> activeLineRaw(const application::EditSession *session, int role)
{
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

int spellingRoleOf(const application::EditSession *session)
{
    return session && application::spellsTranslation(session->document()) ? 1 : 0;
}

}

std::optional<std::u16string> Application::editorRaw(int role) const
{
    return activeLineRaw(targetSession(), role);
}

std::optional<std::u16string> Application::windowLineRaw(int shellWindow, int role) const
{
    return activeLineRaw(windowSession(shellWindow), role);
}

QVariantList Application::editorSpellingMarks(int role)
{
    QVariantList out;
    if (!editorSpellingOn() || role != spellingRole() || !core::syntax::spellChecked(editor().syntaxState()))
        return out;
    const auto raw = editorRaw(role);
    if (!raw || raw->empty())
        return out;
    const auto format = targetSession()->document().format();
    auto *checker = spellChecker();
    const auto marks = checker ? application::editorMarks(*raw, format, *checker, m_spellingText)
                               : core::legacy::checkTextAndBrackets(*raw, format, m_spellingText.segment, {});
    const bool hidden = !editor().showTags();
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
    if (!editorSpellingOn() || role != spellingRole() || !core::syntax::spellChecked(editor().syntaxState()))
        return {};
    const auto raw = editorRaw(role);
    auto *checker = spellChecker();
    if (!raw || !checker)
        return {};
    const auto format = targetSession()->document().format();
    const auto marks = application::editorMarks(*raw, format, *checker, m_spellingText, true);
    int at = position;
    if (!editor().showTags())
        at = static_cast<int>(core::rawOffset(core::project(*raw), static_cast<std::size_t>(std::max(position, 0)), true));
    const auto found = application::misspellAt(marks, at);
    if (!found)
        return {};
    const auto &misspell = marks.misspells[*found];
    QStringList suggestions;
    for (const auto &s : checker->suggestions(misspell.word, application::lineWords(*raw, format, m_spellingText)))
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
    editor().reloadFromSession();
    int shown = *caret;
    if (!editor().showTags())
        if (const auto raw = editorRaw(role))
            shown = static_cast<int>(core::displayOffset(core::project(*raw), static_cast<std::size_t>(*caret)));
    editor().selectInField(shown, shown);
    spellingRefresh();
    return true;
}

bool Application::addEditorWord(const QString &word)
{
    auto *checker = spellChecker();
    if (!checker || !checker->addWord(word.toStdU16String()))
        return false;
    userWordsChanged(checker);
    spellingRefresh();
    return true;
}

QVariantMap Application::spellCheckState(int shellWindow, bool changed, const QString &problem)
{
    const auto &walk = views(shellWindow).spellWalk;
    const auto &editor = *views(shellWindow).editor;
    if (problem.isEmpty() && walk && walk->refused())
        return spellCheckState(shellWindow, changed,
                               QCoreApplication::translate("HikariSub.Spelling", "The edited line cannot be committed."));
    const auto *current = walk && walk->current() ? &*walk->current() : nullptr;
    QStringList suggestions;
    if (current)
        for (const auto &s : current->suggestions)
            suggestions << qstr(s);
    int row = 0;
    QString before, after;
    if (const auto *session = current ? windowSession(shellWindow) : nullptr) {
        const auto lines = session->document().lines();
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (lines[i]->id == current->line)
                row = static_cast<int>(i) + 1;
        const auto raw = session->selection().active == current->line ? activeLineRaw(session, spellingRoleOf(session))
                                                                        : std::nullopt;
        if (raw) {
            QString text = qstr(*raw);
            int from = current->start, to = current->end + 1;
            if (!editor.showTags()) {
                const auto projection = core::project(*raw);
                if (const auto range = displayRange(projection, current->start, current->end)) {
                    text = QString::fromStdU16String(projection.text);
                    from = range->first;
                    to = range->second;
                }
            }
            if (from >= 0 && to <= text.size() && from <= to) {
                constexpr std::size_t kContext = 40;
                const auto plain = [](QString s) {
                    return s.replace(QLatin1String("\\N"), QLatin1String(" ")).replace(QLatin1String("\\n"), QLatin1String(" "));
                };
                before = qstr(core::grammar::contextBefore(plain(text.left(from)).toStdU16String(), kContext));
                after = qstr(core::grammar::contextAfter(plain(text.mid(to)).toStdU16String(), kContext));
            }
        }
    }
    return {{QStringLiteral("found"), current != nullptr},
            {QStringLiteral("available"), startedSpellChecker(shellWindow) && startedSpellChecker(shellWindow)->ready()},
            {QStringLiteral("line"), row},
            {QStringLiteral("before"), before},
            {QStringLiteral("after"), after},
            {QStringLiteral("word"), current ? qstr(current->word) : QString()},
            {QStringLiteral("suggestions"), suggestions},
            {QStringLiteral("replacement"), suggestions.isEmpty() ? QString() : suggestions.front()},
            {QStringLiteral("changed"), changed},
            {QStringLiteral("problem"), problem}};
}

void Application::showSpellCheckWord(int shellWindow)
{
    auto &v = views(shellWindow);
    v.editor->reloadFromSession();
    const auto *current = v.spellWalk ? &v.spellWalk->current() : nullptr;
    if (current && *current) {
        v.editor->showLine((*current)->line.value);
        int from = (*current)->start, to = (*current)->end + 1;
        const auto *session = windowSession(shellWindow);
        if (!v.editor->showTags())
            if (const auto raw = activeLineRaw(session, spellingRoleOf(session)))
                if (const auto range = displayRange(core::project(*raw), (*current)->start, (*current)->end)) {
                    from = range->first;
                    to = range->second;
                }
        v.editor->selectInField(from, to);
    }
    spellingRefresh();
}

QVariantMap Application::openSpellChecker(const QVariantMap &options, int shellWindow)
{
    auto &v = views(shellWindow);
    auto *session = windowSession(shellWindow);
    auto *checker = spellChecker();
    v.spellWalk.reset();
    if (!session || !checker)
        return spellCheckState(shellWindow, false);
    v.editor->commit();
    v.spellWalk = std::make_unique<application::SpellCheckWalk>(*checker, m_spellingText);
    v.spellWalkDocument = m_workspace.currentTab(shellWindow);
    v.spellWalk->next(*session, walkOptions(options));
    showSpellCheckWord(shellWindow);
    return spellCheckState(shellWindow, false);
}

std::optional<QVariantMap> Application::restartStaleSpellCheck(application::EditSession &session,
                                                               const QVariantMap &options)
{
    auto &v = activeViews();
    const int shellWindow = m_workspace.activeWindow();
    editor().commit();
    const bool other = m_workspace.editingTarget() != v.spellWalkDocument;
    if (!v.spellWalk->current() || !v.spellWalk->stale(session, other))
        return std::nullopt;
    v.spellWalkDocument = m_workspace.editingTarget();
    v.spellWalk->restart(session, walkOptions(options));
    showSpellCheckWord(shellWindow);
    QVariantMap state = spellCheckState(shellWindow, false);
    state.insert(QStringLiteral("restarted"), true);
    return state;
}

QVariantMap Application::spellCheckerActivated(const QVariantMap &options)
{
    auto &v = activeViews();
    const int shellWindow = m_workspace.activeWindow();
    auto *session = targetSession();
    if (!v.spellWalk || !session)
        return spellCheckState(shellWindow, false);
    editor().commit();
    const bool other = m_workspace.editingTarget() != v.spellWalkDocument;
    v.spellWalkDocument = m_workspace.editingTarget();
    QVariantMap state;
    if (v.spellWalk->activated(*session, walkOptions(options), other)) {
        showSpellCheckWord(shellWindow);
        state = spellCheckState(shellWindow, false);
        state.insert(QStringLiteral("restarted"), true);
    } else {
        state = spellCheckState(shellWindow, false);
        state.insert(QStringLiteral("restarted"), false);
    }
    return state;
}

namespace {

QString refusalText(application::CommandRefusal refusal)
{
    switch (refusal) {
    case application::CommandRefusal::Protected:
        return QCoreApplication::translate("HikariSub.Documents", "The reference is protected (read-only).");
    case application::CommandRefusal::ReadOnly:
        return QCoreApplication::translate("HikariSub.Documents", "A macro is running on this document.");
    case application::CommandRefusal::InvalidDraft:
        return QCoreApplication::translate("HikariSub.Documents", "The edited line cannot be committed.");
    default: return QCoreApplication::translate("HikariSub.Documents", "The change was refused.");
    }
}

QVariantMap unchangedState()
{
    return {{QStringLiteral("unchanged"), true}};
}

}

QVariantMap Application::spellCheckerReplace(const QString &replacement, const QVariantMap &options)
{
    auto &walk = activeViews().spellWalk;
    const int shellWindow = m_workspace.activeWindow();
    auto *session = targetSession();
    if (!walk || !session)
        return unchangedState();
    if (auto restarted = restartStaleSpellCheck(*session, options))
        return *restarted;
    const auto result = walk->replace(*session, replacement.toStdU16String(), walkOptions(options));
    if (!result)
        return spellCheckState(shellWindow, false, refusalText(result.error()));
    if (*result == application::SpellCheckWalk::Result::Unchanged)
        return unchangedState();
    showSpellCheckWord(shellWindow);
    return spellCheckState(shellWindow, true);
}

QVariantMap Application::spellCheckerReplaceAll(const QString &misspell, const QString &replacement,
                                                const QVariantMap &options)
{
    auto &walk = activeViews().spellWalk;
    const int shellWindow = m_workspace.activeWindow();
    auto *session = targetSession();
    if (!walk || !session)
        return unchangedState();
    if (auto restarted = restartStaleSpellCheck(*session, options))
        return *restarted;
    const auto result = walk->replaceAll(*session, misspell.toStdU16String(), replacement.toStdU16String(),
                                                walkOptions(options));
    if (!result)
        return spellCheckState(shellWindow, false, refusalText(result.error()));
    if (*result == application::SpellCheckWalk::Result::Unchanged)
        return unchangedState();
    showSpellCheckWord(shellWindow);
    return spellCheckState(shellWindow, *result == application::SpellCheckWalk::Result::Replaced);
}

QVariantMap Application::spellCheckerIgnore(const QVariantMap &options)
{
    auto &walk = activeViews().spellWalk;
    const int shellWindow = m_workspace.activeWindow();
    auto *session = targetSession();
    if (!walk || !session)
        return spellCheckState(shellWindow, false);
    if (auto restarted = restartStaleSpellCheck(*session, options))
        return *restarted;
    walk->ignore(*session, walkOptions(options));
    showSpellCheckWord(shellWindow);
    return spellCheckState(shellWindow, false);
}

QVariantMap Application::spellCheckerIgnoreAll(const QString &word, const QVariantMap &options)
{
    auto &walk = activeViews().spellWalk;
    const int shellWindow = m_workspace.activeWindow();
    auto *session = targetSession();
    if (!walk || !session)
        return spellCheckState(shellWindow, false);
    if (auto restarted = restartStaleSpellCheck(*session, options))
        return *restarted;
    walk->ignoreAll(*session, word.toStdU16String(), walkOptions(options));
    showSpellCheckWord(shellWindow);
    return spellCheckState(shellWindow, false);
}

QVariantMap Application::spellCheckerAddWord(const QString &word, const QVariantMap &options)
{
    auto &walk = activeViews().spellWalk;
    const int shellWindow = m_workspace.activeWindow();
    auto *session = targetSession();
    if (!walk || !session)
        return spellCheckState(shellWindow, false);
    if (auto restarted = restartStaleSpellCheck(*session, options))
        return *restarted;
    walk->addWord(*session, word.toStdU16String(), walkOptions(options));
    showSpellCheckWord(shellWindow);
    return spellCheckState(shellWindow, false);
}

void Application::closeSpellChecker(int shellWindow)
{
    auto &v = views(shellWindow);
    v.spellWalk.reset();
    v.spellWalkDocument.reset();
}

QStringList Application::addedDictionaryWords() const
{
    QStringList out;
    if (const auto *checker = anySpellChecker())
        for (const auto &word : checker->addedWords())
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
    userWordsChanged(checker);
    spellingRefresh();
    return true;
}

void Application::createFindReplace(WindowViews &views, int shellWindow)
{
    views.find.reset();
    views.findHost = std::make_unique<FindHost>(*this, shellWindow);
    views.find = std::make_unique<application::FindReplace>(*views.findHost, unicodeFold);
    if (m_findOpened)
        setFindRecent(views);
}

void Application::setFindRecent(WindowViews &views)
{
    const auto list = [this](const char *id) {
        std::vector<std::u8string> out;
        for (const QString &entry : m_settings->list(id))
            out.push_back(toU8(entry));
        return out;
    };
    views.find->setRecent({list("find.recentFinds"), list("find.recentReplacements"), list("findInSubs.recentFilters"),
                           list("findInSubs.recentPaths")});
}

QVariantMap Application::openFindReplace()
{
    if (!m_findOpened) {
        m_findOpened = true;
        for (auto *views : allViews())
            setFindRecent(*views);
    }
    return findReplaceSettings();
}

QVariantMap Application::findReplaceSettings() const
{
    auto s = application::findReplaceFromOptions(m_settings->integer("find.options"));
    findFirstEntries(s, finder().recent());
    s.styles = toU8(m_settings->text("find.styles"));
    return findMap(s, finder().recent());
}

void Application::saveFindReplaceSettings(const QVariantMap &settings)
{
    const auto s = findSettings(settings);
    m_settings->set("find.options", application::findReplaceOptions(s));
    m_settings->set("find.styles", findQs(s.styles));
}

void Application::saveFindRecent(int shellWindow, const application::FindReplaceSettings &window)
{
    const auto &r = views(shellWindow).find->recent();
    for (auto *other : allViews())
        if (other != &views(shellWindow))
            other->find->setRecent(r);
    m_settings->set("find.recentFinds", findList(r.finds));
    if (window.replaceShown)
        m_settings->set("find.recentReplacements", findList(r.replacements));
    if (window.scope == application::FindReplaceSettings::Scope::Folder) {
        m_settings->set("findInSubs.recentFilters", findList(r.filters));
        m_settings->set("findInSubs.recentPaths", findList(r.paths));
    }
}

void Application::runFindReplace(const QString &action, const QVariantMap &settings)
{
    using Scope = application::FindReplaceSettings::Scope;
    const auto s = findSettings(settings);
    auto &find = finder();
    if (action == QLatin1String("find")) {
        find.find(s);
    } else if (action == QLatin1String("findPrevious")) {
        find.findPrevious(s);
    } else if (action == QLatin1String("findAll")) {
        switch (s.scope) {
        case Scope::Document: find.findAllInCurrent(s); break;
        case Scope::OpenDocuments: find.findInAllOpened(s); break;
        case Scope::Folder: find.findInFiles(s); break;
        }
    } else if (action == QLatin1String("replace")) {
        find.replace(s);
    } else if (action == QLatin1String("replaceAll")) {
        switch (s.scope) {
        case Scope::Document: find.replaceAll(s); break;
        case Scope::OpenDocuments: find.replaceInAllOpened(s); break;
        case Scope::Folder: find.replaceInFiles(s); break;
        }
    }
}

QVariantMap Application::findCount(const QVariantMap &settings, int shellWindow)
{
    const auto count = views(shellWindow).find->count(findSettings(settings));
    return {{QStringLiteral("total"), count.total}, {QStringLiteral("current"), count.current}};
}

QString Application::findPatternError(const QVariantMap &settings) const
{
    const std::u16string error = finder().patternError(findSettings(settings));
    QString text = QString::fromStdU16String(error);
    if (!text.isEmpty())
        text[0] = text[0].toUpper();
    return text;
}

void Application::findFinished(int shellWindow)
{
    const auto &find = *views(shellWindow).find;
    emit findResultsChanged();
    emit findBusyChanged();
    emit findFinished(findMap(find.window(), find.recent()));
}

void Application::findNext()
{
    finder().findNext();
}

void Application::findPrevious()
{
    finder().findPrevious();
}

bool Application::findBusy() const
{
    const auto &find = activeViews().find;
    return find && find->busy();
}

void Application::answerFindQuestion(int id, const QString &answer)
{
    using A = application::FindAnswer;
    const auto it = m_findAnswers.find(id);
    if (it == m_findAnswers.end())
        return;
    auto resume = std::move(it->second);
    m_findAnswers.erase(it);
    const A chosen = answer == QLatin1String("ok")    ? A::Ok
                     : answer == QLatin1String("yes") ? A::Yes
                     : answer == QLatin1String("no")  ? A::No
                                                      : A::Cancel;
    resume(chosen);
    emit findBusyChanged();
}

void Application::setFindQuestionHandler(std::function<int(int, const QString &)> handler)
{
    m_findQuestionHandler = std::move(handler);
}

QString Application::findReplaceActivated(const QString &find, int shellWindow)
{
    auto *session = windowSession(shellWindow);
    auto &editor = *views(shellWindow).editor;
    auto &finder = *views(shellWindow).find;
    if (!session || !editor.hasLine())
        return find;
    const bool tl = application::translationMode(*session);
    std::vector<int> roles{tl ? 1 : 0};
    if (tl)
        roles.push_back(0);
    for (const int role : roles) {
        const auto [from, to] = editor.fieldSelectionOf(role);
        if (from >= to)
            continue;
        if (std::pair(from, to) == editor.displaySpan(role, finder.lastStart(), finder.lastEnd()))
            return find;
        const QString shown = role == 1 ? editor.translationText() : editor.text();
        const QString selected = shown.mid(from, to - from);
        finder.selectionAdopted();
        const auto lower = [](const QString &t) { return t.toCaseFolded(); };
        return lower(selected) != lower(find) ? selected : find;
    }
    return find;
}

void Application::resetFindReplace()
{
    finder().reset();
}

QVariantList Application::findResults(int shellWindow) const
{
    QVariantList rows;
    for (const auto &r : views(shellWindow).find->results()) {
        const QString text = QString(reinterpret_cast<const QChar *>(r.text.data()), static_cast<qsizetype>(r.text.size()));
        QVariantMap row{{QStringLiteral("header"), r.header},
                        {QStringLiteral("text"), text},
                        {QStringLiteral("checked"), r.checked},
                        {QStringLiteral("visible"), r.visible}};
        if (!r.header) {
            row.insert(QStringLiteral("line"), r.idLine);
            row.insert(QStringLiteral("file"), !r.document.has_value());
            row.insert(QStringLiteral("before"), text.left(r.start));
            row.insert(QStringLiteral("match"), text.mid(r.start, r.length));
            row.insert(QStringLiteral("after"), text.mid(r.start + r.length));
        }
        rows << row;
    }
    return rows;
}

bool Application::findResultsShown(int shellWindow) const
{
    return views(shellWindow).find->resultsShown();
}

bool Application::canReplaceFindResults(int shellWindow) const
{
    return views(shellWindow).find->canReplaceChecked();
}

void Application::checkFindResults(bool check)
{
    finder().checkAll(check);
    emit findResultsChanged();
}

void Application::toggleFindResult(int row)
{
    finder().toggleChecked(static_cast<std::size_t>(std::max(row, 0)));
    emit findResultsChanged();
}

void Application::toggleFindGroup(int row)
{
    finder().toggleGroup(static_cast<std::size_t>(std::max(row, 0)));
    emit findResultsChanged();
}

void Application::showFindResult(int row)
{
    finder().showResult(static_cast<std::size_t>(std::max(row, 0)));
}

void Application::replaceFindResults(const QString &replacement)
{
    finder().replaceChecked(toU8(replacement));
    emit findResultsChanged();
}

void Application::checkResolution()
{
    auto *session = targetSession();
    const auto &video = activeMedia().video->session();
    if (!askForBadResolution() || !session || targetUntitled() ||
        session->document().format() != core::SubtitleFormat::Ass ||
        video.state() != application::VideoSession::State::Ready || !video.lastFrame())
        return;
    const auto subs = application::scriptResolution(session->document());
    const auto geometry = video.sourceGeometry();
    const int width = geometry.width, height = geometry.height;
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
    const bool hasVideo = activeMedia().video->session().state() == application::VideoSession::State::Ready && activeMedia().video->session().lastFrame();
    if (hasVideo)
        video = {activeMedia().video->session().sourceGeometry().width, activeMedia().video->session().sourceGeometry().height};
    return {{QStringLiteral("subsWidth"), subs.width},
            {QStringLiteral("subsHeight"), subs.height},
            {QStringLiteral("videoWidth"), video.width},
            {QStringLiteral("videoHeight"), video.height},
            {QStringLiteral("hasVideo"), hasVideo}};
}

bool Application::resample(int subsWidth, int subsHeight, int width, int height, bool stretch, bool adjustRotations)
{
    if ((subsWidth == width && subsHeight == height) || subsWidth < 1 || subsHeight < 1)
        return false;
    return changeResolution(subsWidth, subsHeight, width, height, true, stretch, adjustRotations);
}

bool Application::matchVideoResolution(int option)
{
    const auto values = resampleValues();
    if (values.isEmpty())
        return false;
    return changeResolution(values.value(QStringLiteral("subsWidth")).toInt(), values.value(QStringLiteral("subsHeight")).toInt(),
                            values.value(QStringLiteral("videoWidth")).toInt(), values.value(QStringLiteral("videoHeight")).toInt(),
                            option != 0, option == 2, true);
}

bool Application::changeResolution(int fromWidth, int fromHeight, int width, int height, bool resample, bool stretch,
                                   bool adjustRotations)
{
    auto *session = targetSession();
    if (!session)
        return false;
    QStringList unscaled, unadjusted;
    application::ResampleOptions options;
    options.stretch = stretch;
    options.adjustRotations = adjustRotations;
    const auto &video = activeMedia().video->session();
    if (video.state() == application::VideoSession::State::Ready && video.lastFrame())
        options.frameHeight = video.lastFrame()->height;
    options.measure = perspectiveMeasure();
    options.unscaled = [&unscaled](int row, const std::u16string &value, const std::u16string &tag) {
        unscaled << unscaledValue(row, value, tag);
    };
    options.unadjusted = [&unadjusted](int row) { unadjusted << unadjustedRotation(row); };
    const bool done =
        application::changeResolution(*session, {fromWidth, fromHeight}, {width, height}, resample, options).has_value();
    noticeUnscaled(*m_log, unscaled);
    noticeUnadjusted(*m_log, unadjusted);
    editor().reloadFromSession();
    refreshViews();
    return done;
}

application::visual::perspective::TextMeasure Application::perspectiveMeasure() const
{
    application::visual::perspective::TextMeasure measure;
    if (const ui::VisualToolsController *tools = activeViews().visualTools.get()) {
        measure.port = tools->textMeasure();
        measure.leased = [tools](const std::string &name) { return tools->fontLeased(name); };
        measure.fallsBackToLeased = [tools](const std::string &font, bool bold, bool italic, std::u16string_view text) {
            return tools->fallsBackToLeased(font, bold, italic, text);
        };
    }
    return measure;
}

namespace {

QVariantMap sizeOf(application::Resolution r)
{
    return {{QStringLiteral("width"), r.width}, {QStringLiteral("height"), r.height}};
}

application::Resolution resolutionOf(const QVariant &size)
{
    const auto map = size.toMap();
    return {map.value(QStringLiteral("width")).toInt(), map.value(QStringLiteral("height")).toInt()};
}

const char *questionSetting(const QString &kind)
{
    if (kind == QLatin1String("missingMatrix"))
        return "video.missingMatrix";
    if (kind == QLatin1String("matrixMismatch"))
        return "video.matrixMismatch";
    if (kind == QLatin1String("missingLayoutRes"))
        return "video.missingLayoutRes";
    if (kind == QLatin1String("layoutResMismatch"))
        return "video.layoutResMismatch";
    if (kind == QLatin1String("untaggedMatrix"))
        return "video.untaggedMatrixWarning";
    if (kind == QLatin1String("hdr"))
        return "video.hdrWarning";
    return nullptr;
}

}

void Application::checkVideoMatrix()
{
    auto *session = targetSession();
    const auto &video = activeMedia().video->session();
    if (!m_askVideoProperties || video.dummy() || !session || session->document().format() != core::SubtitleFormat::Ass)
        return;
    const auto geometry = video.sourceGeometry();
    const auto &colour = video.colour();
    const QString recommended = QString::fromStdString(application::recommendedMatrix(colour, geometry.width, geometry.height));
    const QString path = QString::fromStdString(video.path());
    if (application::untaggedMatrix(colour) && m_settings->boolean("video.untaggedMatrixWarning"))
        emit videoPropertyQuestion({{QStringLiteral("kind"), QStringLiteral("untaggedMatrix")},
                                    {QStringLiteral("video"), path},
                                    {QStringLiteral("recommended"), recommended}});
    if (application::hdrOrWideGamut(colour) && m_settings->boolean("video.hdrWarning"))
        emit videoPropertyQuestion({{QStringLiteral("kind"), QStringLiteral("hdr")}, {QStringLiteral("video"), path}});
    const auto check = application::checkMatrix(session->document(), recommended.toStdString());
    if (check.question == application::MatrixQuestion::None)
        return;
    const QString kind = check.question == application::MatrixQuestion::Missing ? QStringLiteral("missingMatrix")
                                                                                 : QStringLiteral("matrixMismatch");
    const auto action = static_cast<application::VideoPropertyAction>(m_settings->integer(questionSetting(kind)));
    const QVariantMap question{{QStringLiteral("kind"), kind},
                               {QStringLiteral("video"), path},
                               {QStringLiteral("script"), QString::fromStdString(check.script)},
                               {QStringLiteral("recommended"), recommended}};
    if (action == application::VideoPropertyAction::Set)
        answerVideoProperty(question, 1);
    else if (action == application::VideoPropertyAction::Ask)
        emit videoPropertyQuestion(question);
}

void Application::checkLayoutResolution()
{
    auto *session = targetSession();
    const auto &video = activeMedia().video->session();
    if (!m_askVideoProperties || video.dummy() || !session || session->document().format() != core::SubtitleFormat::Ass)
        return;
    const auto geometry = video.sourceGeometry();
    const application::Resolution size{geometry.width, geometry.height};
    const auto check = application::checkLayoutResolution(
        session->document(), size,
        static_cast<application::LayoutResMismatch>(m_settings->integer("video.layoutResMismatch")));
    if (check.question == application::LayoutQuestion::None)
        return;
    const bool missing = check.question == application::LayoutQuestion::Missing;
    const QVariantMap question{{QStringLiteral("kind"), missing ? QStringLiteral("missingLayoutRes") : QStringLiteral("layoutResMismatch")},
                               {QStringLiteral("video"), QString::fromStdString(video.path())},
                               {QStringLiteral("videoSize"), sizeOf(size)},
                               {QStringLiteral("layoutSize"), sizeOf(check.layout)},
                               {QStringLiteral("playSize"), sizeOf(check.play)},
                               {QStringLiteral("aspectChanged"), check.aspectChanged}};
    if (!missing) {
        emit videoPropertyQuestion(question);
        return;
    }
    switch (static_cast<application::VideoPropertyAction>(m_settings->integer("video.missingLayoutRes"))) {
    case application::VideoPropertyAction::Ask: emit videoPropertyQuestion(question); break;
    case application::VideoPropertyAction::Set: answerVideoProperty(question, 1); break;
    case application::VideoPropertyAction::SetToPlayRes: answerVideoProperty(question, 2); break;
    case application::VideoPropertyAction::Ignore: break;
    }
}

bool Application::answerVideoProperty(const QVariantMap &question, int choice)
{
    auto *session = targetSession();
    const auto &video = activeMedia().video->session();
    if (!session || choice < 1 || video.state() != application::VideoSession::State::Ready ||
        QString::fromStdString(video.path()) != question.value(QStringLiteral("video")).toString())
        return false;
    const QString kind = question.value(QStringLiteral("kind")).toString();
    bool done = false;
    if (kind == QLatin1String("missingMatrix") || kind == QLatin1String("matrixMismatch")) {
        done = application::setMatrix(*session, question.value(QStringLiteral("recommended")).toString().toStdString())
                   .has_value();
    } else if (kind == QLatin1String("missingLayoutRes") || kind == QLatin1String("layoutResMismatch")) {
        const auto layout = choice == 2 ? application::writtenScriptResolution(session->document())
                                        : resolutionOf(question.value(QStringLiteral("videoSize")));
        done = application::setLayoutResolution(*session, layout).has_value();
    }
    if (done)
        refreshViews();
    return done;
}

void Application::stopAskingVideoProperty(const QString &kind, int choice)
{
    using application::VideoPropertyAction;
    const char *setting = questionSetting(kind);
    if (!setting)
        return;
    if (kind == QLatin1String("untaggedMatrix") || kind == QLatin1String("hdr"))
        m_settings->set(setting, false);
    else if (kind == QLatin1String("layoutResMismatch"))
        m_settings->set(setting, static_cast<int>(application::LayoutResMismatch::Never));
    else
        m_settings->set(setting, static_cast<int>(choice == 2   ? VideoPropertyAction::SetToPlayRes
                                                  : choice == 1 ? VideoPropertyAction::Set
                                                                : VideoPropertyAction::Ignore));
}

void Application::setAskForBadResolution(bool on)
{
    m_settings->set("video.dontAskForBadResolution", !on);
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

std::optional<core::ConversionOptions> Application::assConversionOptions() const
{
    core::ConversionOptions options;
    const auto &video = activeMedia().video->session();
    const bool hasVideo = video.state() == application::VideoSession::State::Ready;
    const float frameMs = hasVideo ? video.legacyTimebase().frameDuration() : 0.f;
    options.fps = m_settings->boolean("convert.fpsFromVideo") && hasVideo && frameMs > 0.f
                      ? 1000.0 / frameMs
                      : m_settings->value(QStringLiteral("convert.fps")).toString().toDouble();
    if (!std::isfinite(options.fps) || options.fps < 1 || options.fps > 1000)
        return std::nullopt;
    options.videoFps = hasVideo && frameMs > 0.f ? 1000.0 / frameMs : 23.976;
    options.prefix = utf8(m_settings->text("convert.assTagsToInsertInLine"));
    options.newEndTimes = m_settings->boolean("convert.newEndTimes");
    options.timePerCharacter = m_settings->integer("convert.timePerCharacter");
    options.resolutionWidth = utf8(m_settings->text("convert.resolutionWidth"));
    options.resolutionHeight = utf8(m_settings->text("convert.resolutionHeight"));
    const std::u8string styleName = utf8(m_settings->text("convert.style"));
    if (const auto found = m_styleManager->catalogStore().find(utf8(m_settings->text("convert.styleCatalog")), styleName))
        options.style = *found;
    else
        options.style = application::defaultStyle(styleName);
    return options;
}

namespace {

QString invalidConversionFrameRate()
{
    return QCoreApplication::translate("HikariSub.Documents",
                                       "The frame rate in Settings › Conversion isn’t valid. Correct it and save again.");
}

QStringList assConversionChanges(const core::ConversionReport &r, const QString &style)
{
    QStringList out;
    if (r.commentsRemoved)
        out << QCoreApplication::translate("HikariSub.Conversion", "%n comment line(s) removed.", nullptr, r.commentsRemoved);
    if (r.drawingsCleared)
        out << QCoreApplication::translate("HikariSub.Conversion", "%n drawing(s) removed.", nullptr, r.drawingsCleared);
    if (r.emptyRemoved)
        out << QCoreApplication::translate("HikariSub.Conversion", "%n line(s) left without text removed.", nullptr, r.emptyRemoved);
    if (r.duplicatesRemoved)
        out << QCoreApplication::translate("HikariSub.Conversion", "%n duplicate line(s) removed.", nullptr, r.duplicatesRemoved);
    if (r.reordered)
        out << QCoreApplication::translate("HikariSub.Conversion", "Lines are sorted by time.");
    if (r.textChanged)
        out << QCoreApplication::translate("HikariSub.Conversion", "Formatting tags converted or removed in %n line(s).", nullptr,
                                           r.textChanged);
    if (r.fieldsDropped)
        out << QCoreApplication::translate("HikariSub.Conversion", "%n line(s) get the conversion style “%1”.", nullptr,
                                           r.fieldsDropped)
                   .arg(style);
    if (r.markersDropped)
        out << QCoreApplication::translate("HikariSub.Conversion", "Bookmarks, hidden lines and groups removed from %n line(s).",
                                           nullptr, r.markersDropped);
    if (r.timesChanged)
        out << QCoreApplication::translate("HikariSub.Conversion",
                                           "Times changed (rounding, frames or new end times) in %n line(s).", nullptr,
                                           r.timesChanged);
    return out;
}

}

std::optional<core::ConversionResult> Application::convertedToAss(const application::EditSession &session) const
{
    const auto options = assConversionOptions();
    if (!options)
        return std::nullopt;
    const QCollator collator;
    return core::convertDocument(session.document(), core::SubtitleFormat::Ass, *options,
                                 [&](std::u8string_view a, std::u8string_view b) {
                                     return collator.compare(
                                         QString::fromUtf8(reinterpret_cast<const char *>(a.data()), qsizetype(a.size())),
                                         QString::fromUtf8(reinterpret_cast<const char *>(b.data()), qsizetype(b.size())));
                                 });
}

QVariantMap Application::assConversionPreview(qulonglong id, const QUrl &file, const QString &format) const
{
    const auto document = documentOf(id);
    const auto *session = document ? m_files->session(*document) : nullptr;
    if (!session || !savesAsAss(session->document().format(), file.toLocalFile(), format))
        return {};
    const auto result = convertedToAss(*session);
    if (!result)
        return {{QStringLiteral("convert"), true}, {QStringLiteral("problem"), invalidConversionFrameRate()}};
    return {{QStringLiteral("convert"), true},
            {QStringLiteral("changes"), assConversionChanges(result->report, m_settings->text("convert.style"))}};
}

bool Application::convertToAss(application::DocumentId document)
{
    auto *session = m_files->session(document);
    if (!session)
        return false;
    const QString title = QCoreApplication::translate("HikariSub.Documents", "Save as ASS");
    auto result = convertedToAss(*session);
    if (!result) {
        m_log->notice(ui::LogController::Error, title, invalidConversionFrameRate());
        return false;
    }
    std::set<core::LineId> touches;
    for (const auto *line : session->document().lines())
        touches.insert(line->id);
    application::Command command{"Subtitles conversion", session->revision(), touches,
                                 [&](core::Document &d) {
                                     d = std::move(result->document);
                                     return true;
                                 }};
    command.keepGroups = false;
    if (!session->run(command))
        return false;
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
    if (document == m_workspace.editingTarget())
        editor().reloadFromSession();
    refreshViews();
    if (const QStringList changes = assConversionChanges(result->report, m_settings->text("convert.style")); !changes.isEmpty())
        m_log->notice(ui::LogController::Info, title, changes.join(u' '));
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
    if (activeMedia().video->session().lastFrame()) {
        const auto geometry = activeMedia().video->session().sourceGeometry();
        videoWidth = geometry.width;
        videoHeight = geometry.height;
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
    const auto now = application::scriptProperties(session->document());
    if (!edited("matrix"))
        p.matrix = now.matrix;
    if (!edited("wrapStyle"))
        p.wrapStyle = now.wrapStyle;
    if (!edited("reverseCollisions"))
        p.reverseCollisions = now.reverseCollisions;
    if (!edited("scaledBorderAndShadow"))
        p.scaledBorderAndShadow = now.scaledBorderAndShadow;
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
    editor().reloadFromSession();
    refreshViews();
    return done;
}

const application::Settings &Application::documentSettings(application::DocumentId document) const
{
    return windowSettings(m_workspace.window(document).value_or(m_workspace.activeWindow()));
}

QVariantList Application::scriptInfoQuestion(qulonglong id) const
{
    const auto document = documentOf(id);
    const auto *session = document ? m_files->session(*document) : nullptr;
    if (!session || !documentSettings(*document).boolean("scriptProperties.askForChange"))
        return {};
    auto q = [](const std::u8string &s) { return QString::fromUtf8(reinterpret_cast<const char *>(s.data()), qsizetype(s.size())); };
    QVariantList rows;
    for (const auto &change : application::scriptInfoOnSave(session->document(), documentSettings(*document)))
        rows << QVariantMap{{QStringLiteral("key"), q(change.key)},
                            {QStringLiteral("from"), q(change.from)},
                            {QStringLiteral("to"), q(change.to)}};
    return rows;
}

void Application::approveScriptInfo(qulonglong id, const QStringList &keys)
{
    const auto document = documentOf(id);
    if (!document)
        return;
    auto &approved = m_scriptInfoApproved[document->value];
    approved.clear();
    for (const QString &key : keys)
        approved.push_back(toU8(key));
}

void Application::applyScriptInfoOnSave(application::DocumentId document)
{
    auto *session = m_files->session(document);
    auto approval = m_scriptInfoApproved.extract(document.value);
    if (!session)
        return;
    const auto &settings = documentSettings(document);
    auto changes = application::scriptInfoOnSave(session->document(), settings);
    if (settings.boolean("scriptProperties.askForChange")) {
        const std::vector<std::u8string> approved = approval ? std::move(approval.mapped()) : std::vector<std::u8string>{};
        std::erase_if(changes, [&](const auto &c) { return std::ranges::find(approved, c.key) == approved.end(); });
    }
    application::applyScriptInfo(*session, changes);
}

bool Application::makeGroups()
{
    const auto shown = shownLines();
    return runFilter([&](application::EditSession &s) { return application::makeGroups(s, shown); });
}

bool Application::canConvertFolds() const
{
    const auto *session = targetSession();
    return session && application::foldConversion(session->document()).converted > 0;
}

bool Application::convertFoldsToGroups()
{
    std::optional<application::FoldConversion> result;
    const bool done = runFilter([&](application::EditSession &s) -> std::expected<void, application::CommandRefusal> {
        auto converted = application::convertFoldsToGroups(s);
        if (!converted)
            return std::unexpected(converted.error());
        result = *converted;
        return {};
    });
    if (result)
        emit foldsConverted(static_cast<int>(result->converted), static_cast<int>(result->skipped));
    return done;
}

bool Application::toggleGroup(qulonglong description)
{
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
                                                            {"effect", SortKey::Effect}, {"layer", SortKey::Layer},
                                                            {"text", SortKey::Text},
                                                            {"textWithoutTags", SortKey::TextWithoutTags}};
    auto *session = targetSession();
    const auto *found = std::find_if(std::begin(keys), std::end(keys),
                                     [&](const auto &k) { return key == QLatin1String(k.first); });
    if (!session || found == std::end(keys))
        return false;
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
    editor().reloadFromSession();
    refreshViews();
    return done;
}

bool Application::duplicateLines()
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    const bool done =
        application::duplicateLines(*session, {}, m_settings->boolean("grid.duplicationDontChangeSelection")).has_value();
    editor().reloadFromSession();
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
    editor().reloadFromSession();
    refreshViews();
    return done;
}

void Application::toggleHideTags()
{
    m_settings->set("grid.hideTags", !shell().hideTags());
    shell().setHideTags(m_settings->boolean("grid.hideTags"), m_settings->text("grid.tagsSwapCharacter"));
}

bool Application::deleteText()
{
    auto *session = targetSession();
    if (!session)
        return false;
    const bool done = application::deleteText(*session, shownLines()).has_value();
    editor().reloadFromSession();
    refreshViews();
    return done;
}

bool Application::textCommand(const QString &command)
{
    using application::text_commands::Command;
    static const std::map<QString, Command> commands{
        {"balancedBreak", Command::InsertBalancedBreak}, {"breakLeft", Command::MoveBreakLeft},
        {"breakRight", Command::MoveBreakRight},         {"removeBreaks", Command::RemoveBreaks},
        {"wrapAtBreaksOnly", Command::ToggleWrapStyle2}, {"lowercase", Command::Lowercase},
        {"uppercase", Command::Uppercase},               {"titleCase", Command::TitleCase},
        {"sentenceCase", Command::SentenceCase},         {"fullWidth", Command::FullWidth},
        {"halfWidth", Command::HalfWidth},               {"swapAlternatives", Command::SwapAlternatives},
    };
    auto *session = targetSession();
    const auto found = commands.find(command);
    if (!session || found == commands.end())
        return false;
    static const auto services = backends::icuTextServices();
    ui::QtTextMeasurePort measure;
    const bool done = application::text_commands::run(*session, found->second, services, measure, shownLines()).has_value();
    editor().reloadFromSession();
    refreshViews();
    return done;
}

bool Application::selectionWrapsAtBreaksOnly() const
{
    const auto target = m_workspace.editingTarget();
    const auto *session = target ? m_files->session(*target) : nullptr;
    return session && application::text_commands::selectionWrapsAtBreaksOnly(*session, shownLines());
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
    editor().reloadFromSession();
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
    editor().reloadFromSession();
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
    editor().reloadFromSession();
    refreshViews();
    return done;
}

QQuickWindow *Application::openShellWindow(QQmlEngine &engine)
{
    if (m_shellWindows.empty())
        qGuiApp->installEventFilter(this);
    const int id = m_nextShellWindow++;
    auto layout = std::make_unique<ui::WorkspaceLayoutController>(QString(), m_workspaceLayout->savedLayoutsFile());
    layout->setShellWindow(id);
    layout->reservePresetNames(m_workspaceLayout->reservedPresetNames());
    for (auto *other : layouts()) {
        connect(other, &ui::WorkspaceLayoutController::savedLayoutsWritten, layout.get(),
                &ui::WorkspaceLayoutController::reloadSavedLayouts);
        connect(layout.get(), &ui::WorkspaceLayoutController::savedLayoutsWritten, other,
                &ui::WorkspaceLayoutController::reloadSavedLayouts);
    }
    m_shellWindows[id].layout = std::move(layout);
    auto media = std::make_unique<WindowMedia>();
    media->window = id;
    makeVideo(*media);
    makeAudio(*media);
    auto views = std::make_unique<WindowViews>();
    views->media = media.get();
    makeViews(*views, id);
    m_shellWindows[id].media = std::move(media);
    m_shellWindows[id].views = std::move(views);
    wireMedia(*m_shellWindows[id].media);
    QQmlComponent component(&engine, QStringLiteral("Hikari.Ui"), QStringLiteral("Main"));
    auto *window = qobject_cast<QQuickWindow *>(component.createWithInitialProperties(qmlProperties(id)));
    if (!window) {
        m_shellWindows.erase(id);
        return nullptr;
    }
    m_shellWindows[id].window = window;
    activateShellWindow(id);
    return window;
}

void Application::closeShellWindowDocuments(int shellWindow)
{
    const bool wasActive = m_workspace.activeWindow() == shellWindow;
    if (const auto reference = m_workspace.reference(); reference && m_workspace.window(*reference) == shellWindow)
        m_comparison.remove();
    for (const auto id : m_workspace.removeWindow(shellWindow)) {
        m_comparison.forget(id);
        discardRecovery(id);
        m_files->close(id);
        forgetTab(id);
    }
    m_layoutDocuments.erase(shellWindow);
    if (wasActive)
        refreshViews();
    else
        emit tabsChanged();
}

std::optional<int> Application::shellWindowOf(const QWindow *window)
{
    while (window && window->transientParent())
        window = window->transientParent();
    const QVariant id = window ? window->property("shellWindow") : QVariant();
    return id.isValid() ? std::optional(id.toInt()) : std::nullopt;
}

bool Application::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::TouchBegin:
    case QEvent::TabletPress:
    case QEvent::FocusIn:
    case QEvent::ShortcutOverride:
    case QEvent::KeyPress: {
        const QWindow *window = qobject_cast<QWindow *>(watched);
        if (!window)
            if (const auto *item = qobject_cast<QQuickItem *>(watched))
                window = item->window();
        if (const auto shellWindow = shellWindowOf(window))
            activateShellWindow(*shellWindow);
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

ui::WorkspaceLayoutController &Application::layoutOf(int shellWindow)
{
    if (const auto found = m_shellWindows.find(shellWindow); found != m_shellWindows.end() && found->second.layout)
        return *found->second.layout;
    return *m_workspaceLayout;
}

ui::WorkspaceLayoutController &Application::layoutFor(application::DocumentId document)
{
    return layoutOf(m_workspace.window(document).value_or(0));
}

std::vector<ui::WorkspaceLayoutController *> Application::layouts()
{
    std::vector<ui::WorkspaceLayoutController *> out{m_workspaceLayout.get()};
    for (const auto &[id, shell] : m_shellWindows)
        out.push_back(shell.layout.get());
    return out;
}

namespace {

void notePlace(const QWindow &window, application::SessionWindow &place)
{
    if (!window.isVisible())
        return;
    place.maximized = window.windowStates().testFlag(Qt::WindowMaximized);
    if (const QRect g = window.geometry(); window.windowStates() == Qt::WindowNoState && !g.isEmpty())
        place.geometry = application::SessionGeometry{g.x(), g.y(), g.width(), g.height()};
}

}

void Application::shellWindowLoaded(QQuickWindow *window)
{
    const auto id = shellWindowOf(window);
    if (!id)
        return;
    if (*id == 0)
        m_firstWindow = window;
    const auto keepPlace = [this, window, id = *id] { notePlace(*window, m_windowPlaces[id]); };
    for (const auto changed : {&QWindow::xChanged, &QWindow::yChanged, &QWindow::widthChanged, &QWindow::heightChanged})
        connect(window, changed, this, keepPlace);
    connect(window, &QWindow::windowStateChanged, this, keepPlace);
    keepPlace();
}

QQuickWindow *Application::shellQuickWindow(int shellWindow) const
{
    if (shellWindow == 0)
        return m_firstWindow;
    const auto found = m_shellWindows.find(shellWindow);
    return found == m_shellWindows.end() ? nullptr : found->second.window.data();
}

std::vector<int> Application::sessionWindows() const
{
    std::vector<int> out;
    if (!m_firstWindowHidden)
        out.push_back(0);
    for (const auto &[id, shell] : m_shellWindows)
        if (shell.window && !shell.closing)
            out.push_back(id);
    for (const auto tab : m_workspace.allTabs())
        if (const auto window = m_workspace.window(tab); window && std::ranges::find(out, *window) == out.end())
            out.push_back(*window);
    if (out.empty())
        out.push_back(0);
    return out;
}

application::SessionWindow Application::sessionWindow(int shellWindow)
{
    auto &place = m_windowPlaces[shellWindow];
    if (const QQuickWindow *window = shellQuickWindow(shellWindow))
        notePlace(*window, place);
    return place;
}

QString Application::sideBars(int shellWindow) const
{
    if (const auto found = m_windowPlaces.find(shellWindow); found != m_windowPlaces.end() && found->second.sideBars)
        return QString::fromStdString(application::sideBarsJson(*found->second.sideBars));
    return m_settings->text("workspace.sideBars");
}

void Application::setSideBars(int shellWindow, const QString &json)
{
    const auto bars = application::parseSideBars(json.toStdString());
    if (!bars || m_windowPlaces[shellWindow].sideBars == bars)
        return;
    m_windowPlaces[shellWindow].sideBars = bars;
    m_settings->set("workspace.sideBars", QString::fromStdString(application::sideBarsJson(*bars)));
}

void Application::placeShellWindow(QQuickWindow &window, const application::SessionWindow &saved)
{
    if (window.windowStates() != Qt::WindowNoState)
        window.setWindowStates(Qt::WindowNoState);
    if (const auto &g = saved.geometry) {
        const QRect client(g->x, g->y, g->width, g->height);
        if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
            window.resize(client.size());
        } else if (QScreen *primary = QGuiApplication::primaryScreen()) {
            QList<QRect> available;
            for (QScreen *screen : QGuiApplication::screens())
                available << screen->availableGeometry();
            const QMargins margins = window.frameMargins();
            const QRect fitted = ui::fitOnScreens(client.marginsAdded(margins), margins.top() + ui::dockchrome::kHeaderHeight,
                                                  available, primary->availableGeometry());
            window.setGeometry(fitted.marginsRemoved(margins));
        }
    }
    if (saved.maximized)
        window.setWindowStates(Qt::WindowMaximized);
}

bool Application::saveLayoutForQuit()
{
    const int active = m_workspace.activeWindow();
    if (const auto target = m_workspace.editingTarget(); active != 0 && target)
        if (const auto arrangement = layoutOf(active).tabArrangement(target->value))
            return m_workspaceLayout->saveForQuit(*arrangement);
    return m_workspaceLayout->saveForQuit();
}

QVariantList Application::reviewCloseWindow(int shellWindow)
{
    m_closingWindow = shellWindow;
    return reviewClose(QStringLiteral("window"));
}

bool Application::otherShellWindowOpen(int shellWindow) const
{
    if (shellWindow != 0 && m_firstWindow && !m_firstWindowHidden && m_firstWindow->isVisible())
        return true;
    return std::ranges::any_of(m_shellWindows, [&](const auto &entry) {
        const auto &[id, shell] = entry;
        return id != shellWindow && shell.window && !shell.closing && shell.window->isVisible();
    });
}

std::optional<int> Application::otherUsableWindow(int besides) const
{
    if (besides != 0 && m_firstWindow && !m_firstWindowHidden)
        return 0;
    for (const auto &[id, shell] : m_shellWindows)
        if (id != besides && shell.window && !shell.closing)
            return id;
    return std::nullopt;
}

void Application::leaveShellWindow(int shellWindow)
{
    if (m_workspace.activeWindow() != shellWindow)
        return;
    if (const auto next = otherUsableWindow(shellWindow))
        activateShellWindow(*next);
}

void Application::hideFirstWindow()
{
    if (!m_firstWindow || m_firstWindowHidden)
        return;
    stopMedia(*m_mainMedia);
    leaveShellWindow(0);
    m_firstWindowHidden = true;
    m_closeApproved.reset();
    m_windowPlaces.erase(0);
    closeShellWindowDocuments(0);
    m_hiddenFloating.clear();
    for (QWindow *floating : ui::hideFloatingPanelsOf(m_firstWindow))
        m_hiddenFloating << floating;
    m_firstWindow->hide();
    noteLastUsedWindow();
    saveLastSession();
}

void Application::showFirstWindow()
{
    if (!m_firstWindowHidden)
        return;
    m_firstWindowHidden = false;
    if (m_firstWindow)
        m_firstWindow->show();
    for (const auto &floating : std::exchange(m_hiddenFloating, {}))
        if (floating)
            floating->show();
}

void Application::noteLastUsedWindow()
{
    if (std::exchange(m_lastUsedWindow, m_workspace.activeWindow()) != m_workspace.activeWindow())
        emit lastUsedWindowChanged();
}

void Application::noteCrashWindows()
{
    if (!m_crashReports)
        return;
    QList<report::Window> windows;
    for (const int window : sessionWindows())
        windows << report::Window{static_cast<int>(m_workspace.tabs(window).size()), window == m_workspace.activeWindow()};
    m_crashReports->setWindows(windows);
}

void Application::shellWindowClosed(int shellWindow)
{
    forgetShellWindow(shellWindow);
    noteCrashWindows();
    saveLastSession();
}

void Application::forgetShellWindow(int shellWindow)
{
    if (m_closeApproved == shellWindow)
        m_closeApproved.reset();
    const auto found = m_shellWindows.find(shellWindow);
    if (found == m_shellWindows.end())
        return;
    if (found->second.closing)
        return;
    m_windowPlaces.erase(shellWindow);
    const QPointer<QQuickWindow> window = found->second.window;
    if (!window) {
        m_shellWindows.erase(found);
        return;
    }
    keepProjectTabs(shellWindow);
    m_projects->forget(shellWindow);
    found->second.closing = true;
    stopMedia(media(shellWindow));
    leaveShellWindow(shellWindow);
    closeShellWindowDocuments(shellWindow);
    connect(window, &QObject::destroyed, this, [this, shellWindow] {
        QMetaObject::invokeMethod(this, [this, shellWindow] { m_shellWindows.erase(shellWindow); }, Qt::QueuedConnection);
    });
    window->deleteLater();
}

QVariantMap Application::qmlProperties(int shellWindow)
{
    auto *layout = m_workspaceLayout.get();
    if (const auto found = m_shellWindows.find(shellWindow); found != m_shellWindows.end())
        layout = found->second.layout.get();
    return {{QStringLiteral("shellWindow"), shellWindow},
            {QStringLiteral("shell"), QVariant::fromValue(views(shellWindow).shell.get())},
            {QStringLiteral("editor"), QVariant::fromValue(views(shellWindow).editor.get())},
            {QStringLiteral("video"), QVariant::fromValue(media(shellWindow).video.get())},
            {QStringLiteral("audio"), QVariant::fromValue(media(shellWindow).audio.get())},
            {QStringLiteral("automation"), QVariant::fromValue(static_cast<QObject *>(m_automation.get()))},
            {QStringLiteral("automationManager"), QVariant::fromValue(m_automation->managerController())},
            {QStringLiteral("templateEditor"), QVariant::fromValue(views(shellWindow).templateEditor.get())},
            {QStringLiteral("nodeEffects"), QVariant::fromValue(views(shellWindow).nodeEffects.get())},
            {QStringLiteral("automationDialogs"), QVariant::fromValue(m_automation->dialogs())},
            {QStringLiteral("automationPicker"), QVariant::fromValue(m_automation->picker())},
            {QStringLiteral("automationResolution"), QVariant::fromValue(m_automation->resolutionPrompt())},
            {QStringLiteral("log"), QVariant::fromValue(m_log.get())},
            {QStringLiteral("tagButtons"), QVariant::fromValue(m_tagButtons.get())},
            {QStringLiteral("colourPicker"), QVariant::fromValue(m_colourPicker.get())},
            {QStringLiteral("workspaceLayout"), QVariant::fromValue(layout)},
            {QStringLiteral("shiftTimes"), QVariant::fromValue(m_shiftTimes.get())},
            {QStringLiteral("gridFilter"), QVariant::fromValue(m_gridFilter.get())},
            {QStringLiteral("visualTools"), QVariant::fromValue(views(shellWindow).visualTools.get())},
            {QStringLiteral("videoView"), QVariant::fromValue(views(shellWindow).videoView.get())},
            {QStringLiteral("statusBar"), QVariant::fromValue(views(shellWindow).statusBar.get())},
            {QStringLiteral("videoFullscreen"), QVariant::fromValue(views(shellWindow).fullscreen.get())},
            {QStringLiteral("automationHotkeys"), QVariant::fromValue(static_cast<QObject *>(m_automationHotkeys.get()))},
            {QStringLiteral("settingsImport"), QVariant::fromValue(static_cast<QObject *>(m_settingsImport.get()))},
            {QStringLiteral("hotkeys"), QVariant::fromValue(static_cast<QObject *>(m_hotkeys.get()))},
            {QStringLiteral("updates"), QVariant::fromValue(static_cast<QObject *>(m_updates.get()))},
            {QStringLiteral("whatsNew"), QVariant::fromValue(static_cast<QObject *>(m_whatsNew.get()))},
            {QStringLiteral("analytics"), QVariant::fromValue(static_cast<QObject *>(m_analytics.get()))},
            {QStringLiteral("starPrompt"), QVariant::fromValue(static_cast<QObject *>(m_starPrompt.get()))},
            {QStringLiteral("crashReports"), QVariant::fromValue(static_cast<QObject *>(m_crashReports.get()))},
            {QStringLiteral("aiSettings"), QVariant::fromValue(static_cast<QObject *>(m_aiSettings.get()))},
            {QStringLiteral("styleManager"), QVariant::fromValue(static_cast<QObject *>(m_styleManager.get()))},
            {QStringLiteral("kanjiTimer"), QVariant::fromValue(static_cast<QObject *>(views(shellWindow).kanjiTimer.get()))},
            {QStringLiteral("alignToVideo"), QVariant::fromValue(static_cast<QObject *>(m_alignToVideo.get()))},
            {QStringLiteral("videoAnalysis"), QVariant::fromValue(static_cast<QObject *>(m_videoAnalysis.get()))},
            {QStringLiteral("lyricsImport"), QVariant::fromValue(static_cast<QObject *>(m_lyricsImport.get()))},
            {QStringLiteral("qcNotes"), QVariant::fromValue(static_cast<QObject *>(m_qcNotes.get()))},
            {QStringLiteral("fontCollector"), QVariant::fromValue(static_cast<QObject *>(m_fontCollector.get()))},
            {QStringLiteral("fontCatalogs"), QVariant::fromValue(static_cast<QObject *>(m_fontCatalogs.get()))},
            {QStringLiteral("matroska"), QVariant::fromValue(static_cast<QObject *>(m_matroska.get()))},
            {QStringLiteral("review"), QVariant::fromValue(static_cast<QObject *>(m_review.get()))},
            {QStringLiteral("agent"), QVariant::fromValue(static_cast<QObject *>(m_agent.get()))},
            {QStringLiteral("inlineAi"), QVariant::fromValue(static_cast<QObject *>(m_inlineAi.get()))},
            {QStringLiteral("skills"), QVariant::fromValue(static_cast<QObject *>(m_skillsSettings.get()))},
            {QStringLiteral("app"), QVariant::fromValue(static_cast<QObject *>(this))}};
}

void Application::waitForWrites()
{
    m_port->waitIdle();
    QCoreApplication::processEvents();
}

void Application::settingChanged(const QString &id)
{
    if (id.startsWith(QLatin1String("grammar.")) || id == QLatin1String("editor.spellchecker"))
        applyGrammarSettings();
    if (id == QLatin1String("trust.folders") && m_projects)
        for (const int window : sessionWindows())
            if (const QString root = m_projects->root(window);
                !root.isEmpty() && projectTrusted(root) != views(window).settings->trusted())
                applyWindowProject(window);
    if (id == QLatin1String("audio.outputHostApi") && activeMedia().audioPlayer)
        activeMedia().audioPlayer->reopenOutput();
    if (id == QLatin1String("audio.playMono") && activeMedia().audioPlayer)
        activeMedia().audioPlayer->setMono(m_settings->boolean("audio.playMono"));
    if (id == QLatin1String("audio.outputDevice") && m_audioRouting)
        m_audioRouting->refresh();
    if (id == QLatin1String("video.playbackPlayer"))
        choosePlayer();
    if (id == QLatin1String("subtitles.saveWithVideoName"))
        emit saveWithVideoNameChanged();
    else if (id == QLatin1String("video.dontAskForBadResolution"))
        emit askForBadResolutionChanged();
    else if (id == QLatin1String("autosave.maxFiles") && !m_recoveryDir.isEmpty() && m_recovery)
        m_recovery->setCapacity(m_settings->integer("autosave.maxFiles"));
    else if (id == QLatin1String("grid.hideColumns") && !m_resettingSettings)
        for (auto *views : allViews())
            views->shell->setHiddenColumns(m_settings->integer("grid.hideColumns"));
    else if (id == QLatin1String("grid.columnWidths") && !m_resettingSettings)
        for (auto *views : allViews())
            views->shell->setGridColumnWidths(columnWidthsFrom(m_settings->list("grid.columnWidths")));
    else if (id == QLatin1String("grid.autoHideColumns") && !m_resettingSettings)
        for (auto *views : allViews())
            views->shell->setAutoHideColumns(m_settings->boolean("grid.autoHideColumns"));
    else if (id == QLatin1String("grid.foldGenerated") && !m_resettingSettings)
        for (auto *views : allViews())
            views->shell->setFoldGenerated(m_settings->boolean("grid.foldGenerated"));
    else if (id == QLatin1String("video.disableLiveEditing") || id.startsWith(QLatin1String("grid.calcSpaces"))) {
        for (auto *views : allViews())
            views->editor->optionsChanged();
        refreshVideo();
    }
    else if (id == QLatin1String("editor.hideTags") && !m_resettingSettings)
        for (auto *views : allViews())
            views->editor->setShowTags(!m_settings->boolean("editor.hideTags"));
    else if (id == QLatin1String("editor.timesToFramesSwitch") && !m_resettingSettings)
        for (auto *views : allViews())
            views->editor->setShowFrames(m_settings->boolean("editor.timesToFramesSwitch"));
    else if (id == QLatin1String("grid.hideTags") || id == QLatin1String("grid.tagsSwapCharacter"))
        for (auto *views : allViews())
            views->shell->setHideTags(m_settings->boolean("grid.hideTags"), m_settings->text("grid.tagsSwapCharacter"));
    else if (id == QLatin1String("program.language"))
        switchLanguage();
    else if (id == QLatin1String("program.font") || id == QLatin1String("program.fontSize"))
        applyProgramFont();
    else if (id == QLatin1String("automation.traceLevel"))
        m_automation->manager().setTraceLevel(m_settings->integer("automation.traceLevel"));
    else if (id == QLatin1String("video.subtitleProvider") &&
             m_renderer.select(m_settings->text("video.subtitleProvider").toStdString())) {
        m_fontCollector->setDisplayRenderer(QString::fromStdString(m_renderer.provider()));
        activeMedia().videoRevision.reset();
        activeMedia().videoScript.clear();
        refreshVideo();
        applySubtitlesAt();
    } else if (id == QLatin1String("video.previewSubtitlesAt")) {
        applySubtitlesAt();
    } else if (id == QLatin1String("video.renderCost")) {
        m_renderer.countCost(m_settings->boolean("video.renderCost"));
        activeMedia().videoRevision.reset();
        activeMedia().videoScript.clear();
        refreshVideo();
    }
}

void Application::applySubtitlesAt()
{
    const bool screen = m_settings->integer("video.previewSubtitlesAt") == 1 && m_renderer.usesLibass();
    activeMedia().video->setSubtitlesAtScreenSize(screen);
    if (m_styleManager)
        m_styleManager->setPreviewAtScreenSize(screen);
    if (m_fontCatalogs)
        m_fontCatalogs->setPreviewAtScreenSize(screen);
}

namespace {

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

}

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

}

bool Application::includesCsri()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool Application::includesVsfilter()
{
#ifdef _WIN32
    return QFileInfo::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/Csri/VSFiltermod.dll"));
#else
    return false;
#endif
}

QVariantMap Application::openSettingsDialog()
{
    auto &lists = m_optionsLists;
    lists = {};
    lists.languageTags = {"en"};
    lists.languageNames = {"English"};
    for (const QString &tag : m_localisation->catalogLanguages()) {
        lists.languageTags.push_back(tag.toStdString());
        lists.languageNames.push_back(languageName(tag).toStdString());
    }
    lists.findLanguage = [](std::string_view tag) { return languageName(qs(tag)).toStdString(); };
    lists.sameIgnoringCase = [](std::string_view a, std::string_view b) {
            const QString x = qs(a), y = qs(b);
            return x.size() == y.size() && x.compare(y, Qt::CaseInsensitive) == 0;
        };
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
            lists.dictionaryNames.push_back(QCoreApplication::translate("HikariSub.Options",
                                                                        "Put .dic and .aff files in the “Dictionary” folder").toStdString());
    }
    lists.catalogs = stdList(m_styleManager->catalogs());
    lists.currentCatalog = m_styleManager->catalog().toStdString();
    if (const auto load = application::optionsCatalogToLoad(m_settings->settings(), lists)) {
        m_styleManager->loadCatalog(QString::fromStdString(*load));
        lists.currentCatalog = *load;
    }
    lists.styles = stdList(m_styleManager->storeStyles());
    lists.renderers = m_renderer.providers();
    ensureAudioRoute();
    lists.outputDevices = application::outputDeviceChoices(m_audioDevices->outputs(),
                                                           m_settings->settings().text("audio.outputDevice"),
                                                           m_settings->settings().text("audio.outputDeviceName"));
#ifdef _WIN32
    lists.pathSeparator = '\\';
    lists.slashesForBackslashes = false;
#endif
    const auto open = application::openOptionsDialog(m_settings->settings(), lists);
    m_hotkeys->beginOptions();
    QStringList warnings;
    if (open.catalogMissing)
        warnings << QCoreApplication::translate(
            "HikariSub.Options", "The style catalogue chosen for conversion no longer exists. The default is used instead.");
    if (open.styleMissing)
        warnings << QCoreApplication::translate(
            "HikariSub.Options", "The style chosen for conversion no longer exists. The default is used instead.");
    auto values = toVariant(open.state);
    addAppearance(values);
    values.insert(QStringLiteral("notices.hiddenQuestions"), m_settings->list("notices.hiddenQuestions"));
    values.insert(QStringLiteral("automation.dialogAnswers"), m_settings->list("automation.dialogAnswers"));
    values.insert(QStringLiteral("trust.folders"), m_settings->list("trust.folders"));
    values.insert(QLatin1String(AiFeatures::kCustomAgentsSetting), m_settings->list(AiFeatures::kCustomAgentsSetting));
    QStringList rendererLabels;
    for (const auto &name : lists.renderers)
        rendererLabels << subtitleRendererLabel(name);
    QStringList outputDeviceLabels;
    for (const auto &choice : lists.outputDevices)
        outputDeviceLabels << outputDeviceLabel(choice);
    return {{QStringLiteral("values"), values},
            {QStringLiteral("languages"), qList(lists.languageNames)},
            {QStringLiteral("languageTags"), qList(lists.languageTags)},
            {QStringLiteral("dictionaries"), qList(lists.dictionaryNames)},
            {QStringLiteral("catalogs"), qList(lists.catalogs)},
            {QStringLiteral("styles"), qList(lists.styles)},
            {QStringLiteral("renderers"), qList(lists.renderers)},
            {QStringLiteral("rendererLabels"), rendererLabels},
            {QStringLiteral("outputDevices"), outputDeviceLabels},
            {QStringLiteral("warnings"), warnings}};
}

QString Application::outputDeviceLabel(const application::OutputDeviceChoice &choice)
{
    using Kind = application::OutputDeviceChoice::Kind;
    switch (choice.kind) {
    case Kind::SystemDefault:
        return QCoreApplication::translate("HikariSub.Options", "System default");
    case Kind::Missing:
        //: An output device chosen in Settings > Audio that is not plugged in; %1 is its name
        return QCoreApplication::translate("HikariSub.Options", "%1 (not connected)").arg(QString::fromStdString(choice.name));
    case Kind::Device:
        break;
    }
    return QString::fromStdString(choice.name);
}

QString Application::subtitleRendererLabel(const std::string &name)
{
    if (backends::isVsfilterMod(name))
        return QCoreApplication::translate("HikariSub.Options", "%1 (slow; only for VSFilterMod tags)")
            .arg(QString::fromStdString(name));
    return QString::fromStdString(name);
}

namespace {
std::vector<std::string_view> appearanceSettings()
{
    std::vector<std::string_view> ids;
    for (const auto &setting : application::settingDefinitions())
        if (setting.id.starts_with("appearance."))
            ids.push_back(setting.id);
    return ids;
}

bool validAppearance(std::string_view id, const QVariant &value)
{
    if (id == ui::windowframe::kTitleBarSetting)
        return QStringList{QStringLiteral("auto"), QStringLiteral("system"), QStringLiteral("custom")}.contains(value.toString());
    if (id == ui::theme::kThemeSetting)
        return ui::theme::codeFromName(value.toString()).has_value();
    if (id == ui::theme::kFollowSystemSetting)
        return value.typeId() == QMetaType::Bool;
    if (id == ui::theme::kLightAccentSetting || id == ui::theme::kDarkAccentSetting) {
        const bool dark = id == ui::theme::kDarkAccentSetting;
        return ui::theme::accent(dark, value.toString()).key == value.toString();
    }
    return application::parseSettingColour(value.toString().toStdString()).has_value();
}
}

void Application::addAppearance(QVariantMap &values) const
{
    for (const auto id : appearanceSettings())
        values.insert(qs(id), m_settings->value(qs(id)));
}

void Application::applySettings(const QVariantMap &values, bool resetAll)
{
    if (resetAll)
        resetAllSettings();
    for (const auto id : appearanceSettings()) {
        const auto found = values.constFind(qs(id));
        if (found == values.cend() || !validAppearance(id, *found) || *found == m_settings->value(qs(id)))
            continue;
        m_settings->setValue(qs(id), *found);
    }
    if (const auto hidden = values.constFind(QStringLiteral("notices.hiddenQuestions")); hidden != values.cend()) {
        const QStringList list = hidden->toStringList();
        if (list != m_settings->list("notices.hiddenQuestions"))
            m_settings->setValue(QStringLiteral("notices.hiddenQuestions"), list);
    }
    if (const auto answers = values.constFind(QStringLiteral("automation.dialogAnswers"));
        answers != values.cend() && answers->toStringList().isEmpty() && !m_settings->list("automation.dialogAnswers").isEmpty()) {
        m_settings->setValue(QStringLiteral("automation.dialogAnswers"), QStringList());
        m_automation->forgetRepeat();
    }
    if (const auto trusted = values.constFind(QStringLiteral("trust.folders")); trusted != values.cend()) {
        const QStringList kept = m_settings->list("trust.folders");
        QStringList list = trusted->toStringList();
        list.removeIf([&](const QString &folder) { return !kept.contains(folder); });
        if (list != kept)
            m_settings->setValue(QStringLiteral("trust.folders"), list);
    }
    if (const auto custom = values.constFind(QLatin1String(AiFeatures::kCustomAgentsSetting)); custom != values.cend()) {
        const QStringList list = custom->toStringList();
        if (list != m_settings->list(AiFeatures::kCustomAgentsSetting))
            m_settings->set(AiFeatures::kCustomAgentsSetting, list);
    }
    const auto written = application::commitOptionsDialog(m_settings->settings(), m_optionsLists, fromVariant(values));
    const auto wrote = [&](std::string_view id) { return std::ranges::find(written, id) != written.end(); };
    if (wrote("editor.spellchecker"))
        m_spellingOn = m_settings->boolean("editor.spellchecker");
    if (wrote("editor.dictionaryLanguage"))
        restartSpellChecker();
    if (wrote("editor.spellchecker") || wrote("editor.dictionaryLanguage"))
        spellingRefresh();
    else if (wrote("editor.suggestionsOnDoubleClick"))
        emit spellingChanged();
    if (wrote("program.font") || wrote("program.fontSize"))
        destroyDialogs();
    if (std::ranges::any_of(written, [](const std::string &id) { return id.starts_with("updater."); }))
        m_updates->reload();
    m_hotkeys->commitOptions();
}

namespace {

constexpr std::string_view kMoveTags = "translation.autoMoveTagsFromOriginal";

application::OptionsState projectableOnly(application::OptionsState state)
{
    std::erase_if(state, [](const auto &entry) {
        const auto *setting = application::findSetting(entry.first);
        return !setting || !setting->projectable;
    });
    return state;
}

}

QVariantMap Application::openProjectSettings(int shellWindow)
{
    auto &window = *views(shellWindow).settings;
    const auto *project = window.project();
    if (!project)
        return {};
    auto values = toVariant(projectableOnly(application::openOptionsDialog(window.settings(), m_optionsLists).state));
    values.insert(qs(kMoveTags), window.settings().boolean(kMoveTags));
    auto yours = toVariant(projectableOnly(application::openOptionsDialog(m_settings->settings(), m_optionsLists).state));
    yours.insert(qs(kMoveTags), m_settings->boolean(kMoveTags.data()));
    QVariantMap sources;
    for (const auto &setting : application::settingDefinitions())
        if (const auto file = project->source(setting))
            sources.insert(qs(setting.id), *file == ui::ProjectFile::Local ? QStringLiteral("local") : QStringLiteral("project"));
    return {{QStringLiteral("values"), values}, {QStringLiteral("yours"), yours}, {QStringLiteral("sources"), sources}};
}

QStringList Application::projectOverrides(int shellWindow) const
{
    QStringList ids;
    const auto &window = *views(shellWindow).settings;
    for (const auto &setting : application::settingDefinitions())
        if (setting.projectable && window.overridden(setting.id))
            ids << qs(setting.id);
    return ids;
}

bool Application::applyProjectSettings(int shellWindow, const QVariantMap &values, const QStringList &cleared)
{
    auto &window = *views(shellWindow).settings;
    auto *project = window.project();
    if (!project)
        return false;
    window.takeProjectWriteFailure();
    auto &target = window.projectWrites();
    for (const QString &id : cleared)
        if (const auto *setting = application::findSetting(id.toStdString());
            setting && project->source(*setting) == ui::ProjectFile::Shared)
            target.remove(*setting);
    application::Settings writes(target);
    application::commitOptionsDialog(writes, m_optionsLists, projectableOnly(fromVariant(values)));
    if (const auto moveTags = values.constFind(qs(kMoveTags));
        moveTags != values.cend() && moveTags->toBool() != writes.boolean(kMoveTags))
        writes.set(kMoveTags, moveTags->toBool());
    const bool failed = window.takeProjectWriteFailure();
    applyWindowProject(shellWindow);
    emit projectsChanged();
    return !failed;
}

bool Application::gettingStartedDue() const
{
    return m_settings->text("workspace.gettingStarted") == u"due";
}

QVariantMap Application::openGettingStarted()
{
    QVariantMap opened = openSettingsDialog();
    opened.insert(QStringLiteral("legacy"), m_importStore && !SettingsImportStore::discoverRoots(
                                                                 SettingsImportStore::defaultCandidates()).isEmpty());
    return opened;
}

QVariantMap Application::gettingStartedImported(const QVariantMap &values)
{
    QVariantMap out = values;
    if (!m_settingsImport->plan())
        return out;
    const auto state = application::gettingStartedState(m_settings->settings(), m_optionsLists,
                                                        m_settingsImport->chosenValues());
    for (auto [id, value] : toVariant(state).asKeyValueRange())
        out.insert(id, value);
    return out;
}

void Application::previewLanguage(int index)
{
    if (m_localisation && index >= 0 && std::size_t(index) < m_optionsLists.languageTags.size())
        m_localisation->setLanguage(qs(m_optionsLists.languageTags[std::size_t(index)]));
}

void Application::endLanguagePreview()
{
    switchLanguage();
}

bool Application::finishGettingStarted(const QVariantMap &values, bool import)
{
    auto state = fromVariant(values);
    for (const auto id : appearanceSettings())
        if (const auto found = values.constFind(qs(id)); found != values.cend())
            state[std::string(id)] = found->typeId() == QMetaType::Bool
                                         ? application::SettingValue(found->toBool())
                                         : application::SettingValue(found->toString().toStdString());
    const auto pages = application::gettingStartedPageSettings(state);
    if (import && !m_settingsImport->importChosenBut(pages))
        return false;
    // MUST KILL: must run before applySettings below
    endLanguagePreview();
    QVariantMap written;
    for (const auto &id : pages)
        if (const auto found = values.constFind(qs(id)); found != values.cend())
            written.insert(qs(id), *found);
    const QVariantMap live = toVariant(application::gettingStartedState(m_settings->settings(), m_optionsLists, {}));
    QStringList changed;
    if (import)
        changed << QStringLiteral("import");
    for (auto [id, value] : written.asKeyValueRange())
        if (live.value(id) != value)
            changed << qs(application::gettingStartedPageOf(id.toStdString()));
    applySettings(written);
    m_settings->set("workspace.gettingStarted", QStringLiteral("done"));
    m_analytics->gettingStartedFinished(changed);
    return true;
}

void Application::skipGettingStarted()
{
    endLanguagePreview();
    m_settings->set("workspace.gettingStarted", QStringLiteral("done"));
    m_analytics->gettingStartedSkipped();
}

QVariantList Application::sampleSpellingMarks(const QString &text, bool on, int dictionary)
{
    QVariantList out;
    const auto &symbols = m_optionsLists.dictionarySymbols;
    if (!on || !m_spellingLoader || dictionary < 0 || std::size_t(dictionary) >= symbols.size())
        return out;
    const QString language = qs(symbols[std::size_t(dictionary)]);
    if (!m_sampleChecker || language != m_sampleCheckerLanguage) {
        std::vector<std::filesystem::path> folders;
        for (const QString &folder : std::as_const(m_dictionaryDirs))
            folders.emplace_back(folder.toStdU16String());
        m_sampleChecker = std::make_unique<application::SpellChecker>(std::move(folders), m_spellingLoader);
        m_sampleCheckerLanguage = language;
        m_sampleCheckerReady = m_sampleChecker->initialize(language.toStdU16String()) == application::SpellChecker::Status::Ready;
    }
    if (!m_sampleCheckerReady)
        return out;
    const auto marks = application::editorMarks(text.toStdU16String(), core::SubtitleFormat::Ass, *m_sampleChecker,
                                                m_spellingText, true);
    for (std::size_t i = 0; i + 1 < marks.errors.size(); i += 2)
        out << marks.errors[i] << marks.errors[i + 1] + 1;
    return out;
}

QString Application::sampleFrame(int renderer, int width, int height)
{
    if (width <= 0 || height <= 0)
        return {};
    if (!m_sampleRenderer) {
        m_sampleRenderer = std::make_unique<backends::SubtitleRendererSelection>();
        m_sampleRenderer->setCsriFolder(std::filesystem::path(QCoreApplication::applicationDirPath().toStdU16String()) / u"Csri");
    }
    const auto &names = m_optionsLists.renderers;
    m_sampleRenderer->select(renderer >= 0 && std::size_t(renderer) < names.size() ? names[std::size_t(renderer)]
                                                                                   : std::string("libass"));
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    {
        QPainter painter(&image);
        QLinearGradient sky(0, 0, width, height);
        sky.setColorAt(0, QColor(0x41, 0x56, 0x6B));
        sky.setColorAt(0.55, QColor(0x7A, 0x8C, 0x78));
        sky.setColorAt(1, QColor(0xC7, 0xA2, 0x7A));
        painter.fillRect(image.rect(), sky);
    }
    const QString text = QCoreApplication::translate("HikariSub.GettingStarted", "I said I’d be there by nine.");
    const QByteArray script =
        QStringLiteral("[Script Info]\r\nPlayResX: %1\r\nPlayResY: %2\r\nScaledBorderAndShadow: Yes\r\nScriptType: v4.00+\r\n"
                       "WrapStyle: 0\r\n[V4+ Styles]\r\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
                       "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
                       "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\r\n"
                       "Style: Default,Arial,%3,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,-1,0,0,0,100,100,0,0,1,2,1,2,"
                       "10,10,%4,1\r\n\r\n[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, "
                       "Effect, Text\r\nDialogue: 0,0:00:00.00,1:00:00.00,Default,,0000,0000,0000,,%5\r\n")
            .arg(width)
            .arg(height)
            .arg(height / 6)
            .arg(height / 9)
            .arg(text)
            .toUtf8();
    application::RenderSnapshot snapshot;
    snapshot.script.resize(std::size_t(script.size()));
    std::memcpy(snapshot.script.data(), script.constData(), std::size_t(script.size()));
    if (m_sampleRenderer->prepare(std::move(snapshot)))
        if (const auto frame = m_sampleRenderer->render(core::DocumentTime(1'000'000), width, height))
            application::compositeOverlay(*frame, image.bits(), width, height, image.bytesPerLine());
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

void Application::playTestSound(int device)
{
    const auto &devices = m_optionsLists.outputDevices;
    if (device < 0 || std::size_t(device) >= devices.size())
        return;
    constexpr int rate = 48000, frames = rate / 2, fade = rate / 50;
    std::vector<std::int16_t> samples(frames);
    for (int i = 0; i < frames; ++i) {
        const double gain = std::min({1.0, double(i) / fade, double(frames - i) / fade});
        samples[std::size_t(i)] = static_cast<std::int16_t>(10000 * gain * std::sin(2 * 3.14159265358979 * 660 * i / rate));
    }
    auto tone = std::make_shared<application::DisplayAudio>(rate, frames);
    tone->appendFrames(samples.data(), frames, 1);
    tone->finish();
    const auto &choice = devices[std::size_t(device)];
    if (activeMedia().audioPlayer->playing())
        activeMedia().audioPlayer->stop();
    activeMedia().audioPlayer->setOutputDevice(choice.kind == application::OutputDeviceChoice::Kind::SystemDefault ? std::string()
                                                                                                        : choice.name);
    activeMedia().audioPlayer->switchOutput();
    const std::weak_ptr<const application::DisplayAudio> played = tone;
    activeMedia().audioPlayer->playClip(std::move(tone));
    activeMedia().audioPlayer->setOutputDevice(m_noticedRoute.systemDefault ? std::string() : m_noticedRoute.name);
    QTimer::singleShot(frames * 1000 / rate + 200, this, [this, test = ++m_testSounds, played] {
        if (test != m_testSounds)
            return;
        if (const auto clip = played.lock(); clip && activeMedia().audioPlayer->playingClip(clip.get()))
            activeMedia().audioPlayer->stop();
        activeMedia().audioPlayer->switchOutput();
    });
}

void Application::destroyDialogs()
{
    if (m_findOpened && std::ranges::none_of(allViews(), [](const WindowViews *views) { return views->find->busy(); })) {
        emit findReplaceDestroyed();
        m_findOpened = false;
        createFindReplace(m_mainViews, 0);
        for (auto &[id, shell] : m_shellWindows)
            if (shell.views)
                createFindReplace(*shell.views, id);
        emit findResultsChanged();
    }
    if (m_selectLinesOpened) {
        m_settings->set("selectLines.options", m_selectOptions);
        m_selectLinesOpened = false;
        m_selectRecent = m_settings->list("selectLines.recentSelections").mid(0, 20);
        emit selectLinesDestroyed();
    }
}

QVariantMap Application::resetSettings(const QVariantMap &values)
{
    application::MemorySettingsStorage storage;
    const application::Settings defaults(storage);
    auto refreshed = toVariant(application::refreshOptionsDialogAfterReset(defaults, m_optionsLists, fromVariant(values)));
    const auto keep = [&](std::string_view id) {
        if (const auto found = values.constFind(qs(id)); found != values.cend())
            refreshed.insert(qs(id), *found);
    };
    for (const auto id : appearanceSettings())
        keep(id);
    for (const auto &b : application::optionsBindings())
        if (b.page == application::OptionsPage::Appearance) {
            keep(b.setting);
            if (!b.sizeSetting.empty())
                keep(b.sizeSetting);
        }
    refreshed.insert(QStringLiteral("notices.hiddenQuestions"), QStringList());
    refreshed.insert(QStringLiteral("trust.folders"), QStringList());
    refreshed.insert(QLatin1String(AiFeatures::kCustomAgentsSetting), QStringList());
    keep("automation.dialogAnswers");
    return refreshed;
}

QVariantMap Application::resetSettingsPage(const QVariantMap &values, const QString &page)
{
    const auto which = application::optionsPageOfId(page.toStdString());
    if (!which)
        return values;
    application::MemorySettingsStorage storage;
    const application::Settings defaults(storage);
    if (*which == application::OptionsPage::Conversion)
        if (const auto load = application::optionsCatalogToLoad(defaults, m_optionsLists)) {
            m_styleManager->loadCatalog(QString::fromStdString(*load));
            m_optionsLists.currentCatalog = *load;
            m_optionsLists.styles = stdList(m_styleManager->storeStyles());
        }
    QVariantMap out = values;
    const auto refreshed =
        toVariant(application::refreshOptionsDialogAfterReset(defaults, m_optionsLists, fromVariant(values), which));
    for (auto it = refreshed.cbegin(); it != refreshed.cend(); ++it)
        out.insert(it.key(), it.value());
    if (*which == application::OptionsPage::Appearance)
        for (const auto id : appearanceSettings())
            out.insert(qs(id), ui::SettingsStore::toVariant(defaults.value(id)));
    if (*which == application::OptionsPage::General)
        out.insert(QStringLiteral("notices.hiddenQuestions"), QStringList());
    if (*which == application::OptionsPage::Automation)
        out.insert(QStringLiteral("trust.folders"), QStringList());
    return out;
}

void Application::resetAllSettings()
{
    auto &store = m_settings->settings();
    std::vector<std::pair<std::string_view, application::SettingValue>> kept;
    std::vector<std::string_view> keep{"recent.subtitles", "recent.subtitlesOpened", "recent.video", "recent.audio",
                                       application::kAutomationHotkeysSetting, application::kHotkeysSetting,
                                       application::kAudioHotkeysSetting,
                                       "workspace.settingsPage",
                                       "automation.dialogAnswers", "automation.dialogPresets",
                                       "nodeEffects.exportFormat"};
    const auto appearance = appearanceSettings();
    keep.insert(keep.end(), appearance.begin(), appearance.end());
    for (const std::string_view id : keep)
        if (store.isSet(id))
            kept.emplace_back(id, store.value(id));
    m_resettingSettings = true;
    m_settings->resetAll();
    for (const auto &[id, value] : kept)
        store.set(id, value);
    m_resettingSettings = false;
    m_copyColumns = int(m_settings->integer("grid.copyColumns") | (m_settings->integer("grid.copyColumnTags") & core::column::Tags));
    m_pasteColumns = int(m_settings->integer("grid.pasteColumns") | (m_settings->integer("grid.pasteColumnTags") & core::column::Tags));
    m_gridFilter->settingsReset();
    m_updates->reload();
    for (auto *views : allViews()) {
        views->shell->setAutoHideColumns(m_settings->boolean("grid.autoHideColumns"));
        views->shell->setGridColumnWidths(columnWidthsFrom(m_settings->list("grid.columnWidths")));
        views->shell->setFoldGenerated(m_settings->boolean("grid.foldGenerated"));
    }
    if (!m_selectLinesOpened) {
        m_selectOptions = m_settings->integer("selectLines.options");
        m_selectRecent = m_settings->list("selectLines.recentSelections").mid(0, 20);
    }
    m_colourPicker->settingsReset();
    emit spellingChanged();
}

QStringList Application::settingsStyles() const
{
    return qList(m_optionsLists.styles);
}

QVariantMap Application::chooseSettingsCatalog(const QVariantMap &values, int index)
{
    auto state = fromVariant(values);
    if (index >= 0 && index < qsizetype(m_optionsLists.catalogs.size())) {
        m_styleManager->loadCatalog(QString::fromStdString(m_optionsLists.catalogs[std::size_t(index)]));
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

}
