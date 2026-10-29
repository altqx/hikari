#include "hikari/app/application.h"

#include "hikari/backends/desktop_handoff.h"
#include "hikari/backends/ffms_matroska.h"
#include "hikari/backends/legacy_text_file.h"
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
#include "hikari/application/grid_groups.h"
#include "hikari/application/grid_split.h"
#include "hikari/application/grid_translation.h"
#include "hikari/application/script_properties.h"
#include "hikari/application/shift_times.h"
#include "hikari/application/select_lines.h"
#include "hikari/application/resample.h"
#include "hikari/application/spell_checker.h"
#include "hikari/application/legacy_dir.h"
#include "hikari/application/video_matrix.h"
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
#include "theme.h"
#include "window_frame.h"

#include <QRegularExpression>
#include <QClipboard>
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
#include <cmath>
#include <cstdlib>
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
        m_app.protectReference();
        std::vector<application::FindTab> out;
        for (const auto id : m_app.m_workspace.tabs())
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
                                shown.value(QStringLiteral("text")).toString());
            reply(0);
            return;
        }
        const int id = ++m_app.m_findQuestionId;
        if (answer)
            m_app.m_findAnswers.emplace(id, std::move(answer));
        emit m_app.findQuestion(id, kind, shown);
        emit m_app.findBusyChanged();
    }
    static QVariantMap questionMap(const application::FindQuestion &q)
    {
        using K = application::FindQuestion::Kind;
        using I = application::FindQuestion::Info;
        const QString findAndReplace = QCoreApplication::translate("HikariSub.Search", "Find and replace");
        const QString document = qs(q.document);
        QString title = findAndReplace, text, detail, severity = QStringLiteral("info");
        switch (q.kind) {
        case K::Wrap:
            text = QCoreApplication::translate("HikariSub.Search", "Reached the end of the subtitles.");
            detail = QCoreApplication::translate("HikariSub.Search", "Search again from the first line?");
            break;
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
            title = QCoreApplication::translate("HikariSub.Search", "Replace in subtitles");
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
                title = QCoreApplication::translate("HikariSub.Search", "Find in files");
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
    void finished() override { m_app.findFinished(); }
    void logLine(const application::FindLog &line) override
    {
        using K = application::FindLog::Kind;
        const bool error = line.kind == K::InvalidRegex || line.kind == K::OutsideReplaceTab;
        m_app.m_log->notice(error ? ui::LogController::Error : ui::LogController::Warning,
                            QCoreApplication::translate("HikariSub.Search", "Find and replace"), logText(line));
    }
    static QString logText(const application::FindLog &line)
    {
        using K = application::FindLog::Kind;
        switch (line.kind) {
        case K::InvalidRegex: return QCoreApplication::translate("HikariSub.Search", "Invalid regular expression '%1': %2")
                                         .arg(qs(line.first), qs(line.second));
        case K::MatchError: return QCoreApplication::translate("HikariSub.Search", "Failed to find match for regular expression: %1")
                                       .arg(qs(line.first));
        case K::LineEdited:
            return QCoreApplication::translate("HikariSub.Search", "Line %1 cannot be replaced,\n"
                                                               "cause it was edited.").arg(qs(line.first));
        case K::CannotBackUp: return QCoreApplication::translate("HikariSub.Search", "Cannot back up %1.").arg(qs(line.first));
        case K::OutsideReplaceTab: break;
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
        if (role == 1)
            m_app.m_editor->selectRaw(application::translationMode(*session) ? 1 : 0, start, end);
        else if (role == 0)
            m_app.m_editor->selectRaw(0, start, end);
        else if (role == 2 || role == 3)
            m_app.m_editor->selectInBox(role, start, end);
    }
    void changed(application::DocumentId) override
    {
        m_app.m_editor->reloadFromSession();
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
                m_app.m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Search", "Find and replace"),
                                    QCoreApplication::translate("HikariSub.Search", "Cannot open file %1.").arg(name));
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
        const QString title = QCoreApplication::translate("HikariSub.Search", "Find and replace");
        m_app.m_log->notice(ui::LogController::Error, title,
                            QCoreApplication::translate("HikariSub.Search", "Cannot write file %1; it was left as it was.").arg(name));
        if (!write(original))
            m_app.m_log->notice(ui::LogController::Error, title,
                                QCoreApplication::translate("HikariSub.Search", "%1 could not be restored; its backup is in %2.")
                                    .arg(name, m_app.m_replaceBackup));
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
        m_editor->writeFinished();
        writeFinished(r);
    });
    m_files = std::make_unique<application::DocumentFiles>(*m_reader, *m_writes);
    m_log = std::make_unique<ui::LogController>();
    m_shell = std::make_unique<ui::ShellController>(m_workspace);
    m_editor = std::make_unique<ui::LineEditorController>(*m_files);
    m_editor->setCommittedListener([this] { refreshViews(); });
    m_mediaSource = std::make_unique<backends::FfmsIndexedSource>(mediaHelperPath(options.mediaHelper));
    m_renderer.setCsriFolder(std::filesystem::path(QCoreApplication::applicationDirPath().toStdU16String()) / u"Csri");
    m_renderer.setLog([this](const std::string &text) { m_log->log(QString::fromStdString(text)); });
    m_videoSource = std::make_unique<application::DummyVideoSource>(*m_mediaSource);
    m_video = std::make_unique<ui::VideoController>(*m_videoSource, m_renderer);
    m_video->setMediaInfo(m_mediaSource.get(), m_mediaSource.get());
    m_generalPlayer = std::make_unique<backends::QtGeneralPlayer>(options.playbackAudio);
    m_video->session().setGeneralPlayer(m_generalPlayer.get());
    m_video->session().setFrameWorker([this](std::function<void()> task) {
        QMetaObject::invokeMethod(this, std::move(task), Qt::QueuedConnection);
    });
    connect(m_generalPlayer.get(), &backends::QtGeneralPlayer::frameDelivered, this, [this](const QVideoFrame &frame) {
        const qint64 end = frame.endTime();
        const auto colour = m_video->session().playerColour();
        const bool planes = m_video->session().presenterConvertsYcbcr();
        m_video->session().submitGeneralFrame(
            frame.startTime(), end > frame.startTime() ? std::optional<std::int64_t>(end) : std::nullopt,
            [frame, colour, planes](application::FramePool &pool) -> std::shared_ptr<application::IndexedFrame> {
                auto out = pool.acquire();
                const bool made = planes ? backends::videoFrameToYcbcr(frame, *out, colour)
                                         : backends::videoFrameToBgra(frame, *out, colour);
                return made ? out : nullptr;
            });
    });
    m_automation = std::make_unique<AutomationShell>(
        AutomationShell::Paths{luaHelperPath(options.luaHelper), automationPath(options.automationDir),
                               options.userAutomationDir},
        *m_files,
        m_workspace, *m_editor, *m_video, *m_shell, *m_mediaSource);
    m_automation->setDocumentChanged([this] { refreshViews(); });
    m_templateEditor = std::make_unique<ui::TemplateEditorController>();
    m_templateEditor->setDocumentKey([this]() -> ui::TemplateEditorController::DocumentKey {
        const auto target = m_workspace.editingTarget();
        return target ? ui::TemplateEditorController::DocumentKey{target->value, m_files->generation(*target)}
                      : ui::TemplateEditorController::DocumentKey{};
    });
    m_templateEditor->setSessionSource([this]() -> application::EditSession * {
        const auto target = m_workspace.editingTarget();
        return target ? m_files->session(*target) : nullptr;
    });
    m_templateEditor->setSessionOf([this](ui::TemplateEditorController::DocumentKey key) -> application::EditSession * {
        const application::DocumentId id{key.first};
        auto *session = m_files->session(id);
        return session && m_files->generation(id) == key.second ? session : nullptr;
    });
    m_templateEditor->setTemplater(&m_automation->templater());
    m_templateEditor->setHostState([this] { return m_automation->hostState(); });
    connect(m_video.get(), &ui::VideoController::changed, this, [this] {
        if (!m_video->playing())
            m_templateEditor->hostChanged();
    });
    m_automation->setBeforeRun([this] { m_templateEditor->commit(); });
    m_files->setBeforeSave([this] { m_templateEditor->commit(); });
    connect(m_templateEditor.get(), &ui::TemplateEditorController::documentEdited, this, [this] { refreshViews(); });
    connect(m_templateEditor.get(), &ui::TemplateEditorController::otherDocumentEdited, this,
            [this](std::pair<std::uint64_t, std::uint64_t> document) {
                scheduleAutosave(application::DocumentId{document.first});
            });
    connect(m_templateEditor.get(), &ui::TemplateEditorController::videoChanged, this, [this] {
        if (m_videoShowsTemplate || m_templateEditor->videoPreview())
            showVideoSubtitles();
    });
    connect(m_video.get(), &ui::VideoController::changed, this, [this] {
        if (!m_templateEditor->videoPreview())
            return;
        const auto &video = m_video->session();
        const auto frame = video.reportedFrame();
        const auto at = frame ? video.frameStart(*frame) : std::nullopt;
        const auto window = m_templateEditor->videoWindow(at ? at->microseconds() / 1000 : 0, video.playing());
        if (m_templateWindow != std::pair(window.fromMs, window.toMs))
            showVideoSubtitles();
    });
    connect(m_automation.get(), &AutomationShell::karaokeApplied, this,
            [this](quint64 document, const QByteArray &report) {
                const application::DocumentId id{document};
                if (m_files->session(id))
                    m_templateEditor->applied(report.toStdString(),
                                              ui::TemplateEditorController::DocumentKey{document, m_files->generation(id)});
            });
    connect(m_automation.get(), &AutomationShell::progressChanged, m_templateEditor.get(),
            [this] { m_templateEditor->refresh(); });
    m_settingsFile = options.settingsFile;
    m_userFolder = options.userFolder;
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
        // MUST KILL: must run before m_importStore->recover()
        convertRules();
        m_importStore->recover();
    }
    m_settingsImport = std::make_unique<SettingsImportController>(m_importStore.get());
    if (firstStart && Localisation::firstStartLanguage(options.systemUiLanguages) == u"pl") {
        m_settings->set("program.language", QStringLiteral("pl"));
        m_settings->set("editor.dictionaryLanguage", QStringLiteral("pl"));
    }
    if (firstStart && options.gettingStarted)
        m_settings->set("workspace.gettingStarted", QStringLiteral("due"));
    connect(m_settings.get(), &ui::SettingsStore::changed, this, &Application::settingChanged);
    m_automation->manager().setTraceLevel(m_settings->integer("automation.traceLevel"));
    m_automation->setSettings(m_settings.get());
    if (options.autoload)
        m_automation->autoload();
    m_renderer.select(m_settings->text("video.subtitleProvider").toStdString());
    m_renderer.countCost(m_settings->boolean("video.renderCost"));
    ui::theme::useSettings(m_settings.get());
    m_tagButtons = std::make_unique<ui::TagButtonsController>(*m_settings);
    m_colourPicker = std::make_unique<ui::ColourPickerController>(*m_settings);
    m_shiftTimes = std::make_unique<ui::ShiftTimesController>(*m_settings);
    m_editor->setOptionsSource([this] {
        ui::LineEditorController::Options o;
        o.liveEditing = !m_settings->boolean("video.disableLiveEditing");
        o.dontAdvanceOnTimes = m_settings->boolean("editor.dontGoToNextLineOnTimesEdit");
        o.allCharsForCps = m_settings->boolean("grid.calcSpacesAndPunctuationForCps");
        o.allCharsForWraps = m_settings->boolean("grid.calcSpacesAndPunctuationForWraps");
        return o;
    });
    m_editor->setShowFramesSetting(m_settings->boolean("editor.timesToFramesSwitch"),
                                   [this](bool on) { m_settings->set("editor.timesToFramesSwitch", on); });
    m_editor->setShowTagsSetting(!m_settings->boolean("editor.hideTags"),
                                 [this](bool show) { m_settings->set("editor.hideTags", !show); });
    connect(m_editor.get(), &ui::LineEditorController::frameDisplayChanged, this, [this] {
        const auto *frames = m_editor->frameTimebase();
        m_shell->setFrameTimebase(frames ? std::optional(*frames) : std::nullopt);
    });
    m_selectOptions = m_settings->integer("selectLines.options");
    m_selectRecent = m_settings->list("selectLines.recentSelections").mid(0, 20);
    m_workspaceLayout = std::make_unique<ui::WorkspaceLayoutController>(
        m_settingsFile.isEmpty() ? QString() : QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/layout.json"),
        options.presetsDir.isEmpty() ? QString() : options.presetsDir + QStringLiteral("/layouts.json"));
    m_gridFilter = std::make_unique<ui::GridFilterController>(*m_settings);
    m_visualTools = std::make_unique<ui::VisualToolsController>(*m_video, *m_settings, [this] { return targetSession(); });
    m_visualTools->setShownLines([this] { return shownLines(); });
    m_visualTools->setEditorHotkey([this](int id) {
        if (!m_hotkeys)
            return application::defaultHotkey({id, application::EditorHotkey});
        return m_hotkeys->accelOf(QString::fromUtf8(std::string(application::hotkeySymbol(id))), application::EditorHotkey)
            .toStdString();
    });
    m_visualTools->setEdited([this] {
        m_editor->reloadFromSession();
        refreshViews();
    });
    m_visualTools->setPreview([this](const core::Document *preview) {
        auto *session = targetSession();
        if (!session)
            return;
        setVideoFonts(m_workspace.editingTarget());
        m_video->session().setSubtitles(rendererScript(preview ? *preview : session->document()));
        m_videoScript.clear();
    });
    m_visualTools->setLog([this](const QString &text) { m_log->notice(ui::LogController::Warning, m_visualTools->toolName(), text); });
    m_visualTools->setIgnoreFiltered([this] { return m_gridFilter->ignoreInActions(); });
    const auto visualRole = [this] {
        int role = 0;
        if (const auto *s = targetSession(); s && application::translationMode(*s) && s->selection().active) {
            const auto draft = s->draftRecord();
            for (const auto *line : s->document().lines())
                if (line->id == *s->selection().active)
                    role = !((draft && draft->id == line->id) ? draft->translation : line->translation).empty() ? 1 : 0;
        }
        return role;
    };
    m_visualTools->setEditorSelection([this, visualRole]() { return m_editor->rawFieldSelection(visualRole()); });
    m_visualTools->setEditorSelectionPlacer([this, visualRole](long from, long to) {
        m_editor->selectRaw(visualRole(), static_cast<int>(from), static_cast<int>(to));
    });
    m_videoView = std::make_unique<ui::VideoViewController>(*m_video, *m_visualTools, *m_settings);
    m_videoView->setPlayer(m_generalPlayer.get());
    m_videoFullscreen = std::make_unique<ui::VideoFullscreenController>(*m_video, *m_videoView, *m_settings);
    choosePlayer();
    connect(m_video.get(), &ui::VideoController::changed, this, [this] {
        if (directShowPlayback())
            m_video->session().preparePlayer();
    });
    m_video->session().setLog([this](const std::string &) {
        m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Options", "Video"),
                      QCoreApplication::translate("HikariSub.Video", "Cannot change YCbCr matrix"));
    });
    setUpTranslationControls();
    setUpFileArrangements();
    m_statusBar = std::make_unique<ui::StatusBarController>(*m_video, *m_visualTools, [this]() -> const core::Document * {
        auto *session = targetSession();
        return session ? &session->document() : nullptr;
    });
    m_findHost = std::make_unique<FindHost>(*this);
    createFindReplace();
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
                                                                      QCoreApplication::translate("HikariSub.Options", "Hotkeys"), message);
                                                    });
    m_analytics = std::make_unique<Analytics>(*m_settings, options.analytics);
    m_crashReports = std::make_unique<CrashReports>(*m_settings, *m_analytics, *m_log, options.crashReports);
    m_crashReports->setKnown(
        [this] {
            QStringList names;
            for (const auto id : m_workspace.documents())
                if (const std::string *title = m_workspace.title(id))
                    names << QString::fromStdString(*title);
            for (const auto &[id, media] : m_tabMedia)
                names << media.video << media.audio << media.keyframes;
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
    connect(m_video.get(), &ui::VideoController::changed, m_analytics.get(), [this] {
        using State = application::VideoSession::State;
        const State state = m_video->session().state();
        const State before = std::exchange(m_videoUsageState, state);
        if (state == before)
            return;
        if (state == State::Opening)
            m_videoOpenClock.start();
        else if (state == State::Ready || state == State::Failed)
            m_analytics->mediaOpened(true, before == State::Opening ? m_videoOpenClock.elapsed() : 0, state == State::Ready,
                                     m_video->dummy() ? QStringLiteral("dummy") : QStringLiteral("file"));
    });
    m_updates = std::make_unique<UpdateChecker>(
        *m_settings, options.updateFeed,
        options.appVersion.isEmpty() ? QStringLiteral(HIKARI_VERSION) : options.appVersion, options.updateEnvironment);
    m_whatsNew = std::make_unique<WhatsNew>(*m_updates, WhatsNew::cacheFileIn(options.updateEnvironment));
    {
        const QString catalogDir = !options.catalogDir.isEmpty() ? options.catalogDir
                                   : !m_settingsFile.isEmpty()   ? QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/Catalog")
                                                                 : QDir::temp().filePath(QStringLiteral("hikari-catalogs-%1").arg(QCoreApplication::applicationPid()));
        StyleManagerController::Hooks hooks;
        hooks.target = [this] { return targetSession(); };
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
            m_editor->reloadFromSession();
            refreshViews();
        };
        hooks.fonts = [this] {
            return subtitleFonts(m_workspace.editingTarget());
        };
        hooks.lastCatalog = [this] { return m_settings->text("styles.lastCatalog"); };
        hooks.catalogChosen = [this](const QString &name) { m_settings->set("styles.lastCatalog", name); };
        m_styleManager = std::make_unique<StyleManagerController>(std::filesystem::path(catalogDir.toStdU16String()), std::move(hooks));
    }
    {
        KanjiTimerController::Hooks hooks;
        hooks.target = [this] { return targetSession(); };
        hooks.targetId = [this] { return m_workspace.editingTarget() ? m_workspace.editingTarget()->value : 0; };
        hooks.refresh = [this] {
            m_editor->reloadFromSession();
            refreshViews();
        };
        hooks.dictionary = [folder = readingDictionaryPath(options.readingDictionaryDir)]() -> application::ReadingAnalyzer {
            std::shared_ptr<const backends::MecabReadings> readings =
                backends::MecabReadings::open(std::filesystem::path(folder.toStdU16String()));
            return readings ? backends::MecabReadings::analyzer(std::move(readings)) : application::ReadingAnalyzer{};
        };
        m_kanjiTimer = std::make_unique<KanjiTimerController>(*m_settings, std::move(hooks));
    }
    {
        FontCollectorController::Hooks hooks;
        hooks.tabs = [this] {
            std::vector<FontCollectorController::Tab> out;
            for (const auto id : m_workspace.tabs()) {
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
            m_matroskaRequested = m_video->requestedVideo();
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
            m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Fonts", "Font catalogs"), text);
        };
        hooks.documentFonts = [this] { return attachedFontLeases(m_workspace.editingTarget()); };
        const auto report = listFileReport(*m_log);
        hooks.fileConverted = report.converted;
        hooks.fileProblem = report.problem;
        hooks.fileReplaced = report.replaced;
        hooks.fontsChanged = [this] {
            application::reloadSubtitleFonts();
            if (!m_fontCatalogs)
                return;
            setVideoFonts(m_videoDocument);
            if (m_videoDocument) {
                m_videoRevision.reset();
                m_videoScript.clear();
                refreshVideo();
            }
        };
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
        std::vector<std::filesystem::path> folders{std::filesystem::path(m_dictionaryDir.toStdU16String())};
        if (!m_bundledDictionaryDir.isEmpty() && m_bundledDictionaryDir != m_dictionaryDir)
            folders.emplace_back(m_bundledDictionaryDir.toStdU16String());
        m_spellChecker = std::make_unique<application::SpellChecker>(std::move(folders), options.spellingBackend);
        m_spellChecker->setLoadedHandler(
            [this] { QMetaObject::invokeMethod(this, [this] { spellingRefresh(); }, Qt::QueuedConnection); });
    }
    setUpGrammar(options);
    m_spellingLoader = options.spellingBackend;
    m_shell->setSpelling([this](std::u16string_view text, core::SubtitleFormat format, bool spell, int replaceTagsLen) {
        application::SpellChecker *checker = m_spellingStarted ? m_spellChecker.get() : nullptr;
        return core::legacy::checkTextAndBrackets(text, format, m_spellingText.segment,
                                                  spell && m_settings->boolean("editor.spellchecker") && checker ? checker->wordCheck()
                                                                                   : core::legacy::WordCheck{},
                                                  replaceTagsLen);
    });
    m_shell->setChangeStates(
        [this](const core::LineRecord &line) {
            const auto *session = targetSession();
            return session ? session->changeState(line) : 0;
        },
        [this](const core::LineRecord &line) {
            const auto reference = m_workspace.reference();
            const auto *session = reference ? m_files->session(*reference) : nullptr;
            return session ? session->changeState(line) : 0;
        });
    m_shell->setHideTags(m_settings->boolean("grid.hideTags"), m_settings->text("grid.tagsSwapCharacter"));
    m_editor->tagList()->setOptionsStore([this] { return m_settings->integer("textEditor.tagListOptions"); },
                                         [this](int options) { m_settings->set("textEditor.tagListOptions", options); });
    m_shell->setHiddenColumns(m_settings->integer("grid.hideColumns"));
    connect(m_shell.get(), &ui::ShellController::hiddenColumnsChanged, this,
            [this] { m_settings->set("grid.hideColumns", m_shell->hiddenColumns()); });
    m_shell->setAutoHideColumns(m_settings->boolean("grid.autoHideColumns"));
    connect(m_shell.get(), &ui::ShellController::autoHideColumnsChanged, this, [this] {
        // MUST KILL: the guard keeps Set default's re-sync from setting the key
        if (m_settings->boolean("grid.autoHideColumns") != m_shell->autoHideColumns())
            m_settings->set("grid.autoHideColumns", m_shell->autoHideColumns());
    });
    m_shell->setFoldGenerated(m_settings->boolean("grid.foldGenerated"));
    connect(m_shell.get(), &ui::ShellController::foldGeneratedChanged, this, [this] {
        selectFoldedRunsWhole();
        if (m_settings->boolean("grid.foldGenerated") != m_shell->foldGenerated())
            m_settings->set("grid.foldGenerated", m_shell->foldGenerated());
    });
    m_recent.set(m_settings->settings().list("recent.subtitles"));
    m_recent.setOpenedRecords(m_settings->settings().list("recent.subtitlesOpened"));
    m_copyColumns = m_settings->integer("grid.copyColumns");
    m_pasteColumns = m_settings->integer("grid.pasteColumns");
    m_editRefresh = new QTimer(this);
    m_editRefresh->setSingleShot(true);
    m_editRefresh->setInterval(0);
    connect(m_editRefresh, &QTimer::timeout, this, &Application::refreshVideo);
    connect(m_editor.get(), &ui::LineEditorController::changed, this, [this] {
        if (const auto *session = targetSession())
            m_shell->setDraft(session->draftRecord());
        m_editRefresh->start();
        scheduleAutosave();
        if (m_templateEditor && m_templateEditor->shown())
            m_templateEditor->refresh();
    });
    connect(m_video.get(), &ui::VideoController::changed, this, [this] {
        const auto &video = m_video->session();
        const bool ready = video.state() == application::VideoSession::State::Ready;
        const QString path = ready ? QString::fromStdString(video.path()) : QString();
        if (path == m_editorTimebaseVideo)
            return;
        m_editorTimebaseVideo = path;
        m_editor->setVideoTimebase(ready ? std::optional(video.legacyTimebase()) : std::nullopt);
    });
    connect(m_video.get(), &ui::VideoController::changed, this, &Application::logVideoFailure);
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
    connect(m_video.get(), &ui::VideoController::changed, this, [this] {
        if (m_pendingKeyframes.isEmpty() || m_video->session().state() != application::VideoSession::State::Ready)
            return;
        const QString path = std::exchange(m_pendingKeyframes, QString());
        const QString problem = openKeyframes(QUrl::fromLocalFile(path));
        if (!problem.isEmpty())
            m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.MainWindow", "Open keyframes"),
                          problem);
    });
    connect(m_editor.get(), &ui::LineEditorController::leftLineCommitted, this,
            [this](qulonglong id) { m_leftEditedLine = core::LineId{id}; });
    connect(m_editor.get(), &ui::LineEditorController::lineChanged, this, [this](qulonglong id) {
        const auto target = m_workspace.editingTarget();
        auto *session = target ? m_files->session(*target) : nullptr;
        if (!session)
            return;
        session->setSelection(gridSelection().plain(session->selection(), core::LineId{id}));
        m_shell->setSelection(session->selection());
        m_editor->selectionChanged();
        followEditingLine();
        if (m_templateEditor)
            m_templateEditor->refresh();
    });
    m_editor->setVideoTimeSource([this]() -> std::optional<std::int64_t> {
        const auto &session = m_video->session();
        const auto frame = session.reportedFrame();
        const auto start = frame ? session.frameStart(*frame) : std::nullopt;
        if (!start)
            return std::nullopt;
        return start->microseconds() / 1000;
    });
    m_audioSource = std::make_unique<backends::FfmsIndexedSource>(mediaHelperPath(options.mediaHelper));
    m_audio = std::make_unique<ui::AudioController>(*m_audioSource);
    m_automation->setAudioBox([this]() -> const application::AudioBox * { return m_audio ? &m_audio->box() : nullptr; });
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
        m_audio->setSettings(m_audioSettings);
    }
    m_audio->setSettingsStore(m_settings.get());
    {
        const bool device = options.playbackAudio;
        auto make = options.makeAudioOutput;
        const bool windowsApis = options.outputHostApiSetting;
        m_audioPlayer = std::make_unique<backends::AudioBoxPlayer>(
            [this, device, make, windowsApis]() -> application::AudioOutputPort & {
                backends::PortAudioOutput::Options output;
                output.hostApi = backends::PortAudioOutput::hostApiForSetting(m_settings->integer("audio.outputHostApi"),
                                                                              windowsApis);
                if (!m_audioOutput || output.hostApi != m_audioOutputHostApi) {
                    m_audioOutput.reset();
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
        m_audio->setPlaybackSettings([this] { return m_settings->integer("audio.markPlayTime"); });
        m_audio->setVideoPlayback([this] { return m_video->playing(); }, [this] { m_video->pause(); });
        m_audioConnections << connect(m_audioPlayer.get(), &backends::AudioBoxPlayer::failed, this,
                                      [this](const QString &message) {
                                          m_log->notice(ui::LogController::Error,
                                                        QCoreApplication::translate("HikariSub.Options", "Audio"), message);
                                      });
    }
    {
        if (options.makeAudioDevices)
            m_audioDevices = options.makeAudioDevices();
        else if (options.playbackAudio)
            m_audioDevices = std::make_unique<backends::QtAudioDevices>();
        else
            m_audioDevices = std::make_unique<application::NoAudioDevices>();
        m_audioRouting = std::make_unique<application::AudioOutputRouting>(
            *m_audioDevices, [this] { return m_settings->settings().text("audio.outputDevice"); },
            [this](const application::OutputRoute &route) { routeAudio(route, true); });
        routeAudio(m_audioRouting->route(), false);
    }
    m_recentAudio.set(m_settings->settings().list("recent.audio"));
    m_audioConnections << connect(m_audio.get(), &ui::AudioController::changed, this, [this] {
        using State = application::AudioBox::State;
        const application::AudioBox &box = m_audio->box();
        const State state = box.state();
        const State before = std::exchange(m_audioUsageState, state);
        if (state == before)
            return;
        if (state == State::Opening || state == State::Loading) {
            if (state == State::Opening || !m_audioOpenClock.isValid())
                m_audioOpenClock.start();
            return;
        }
        const qint64 elapsed = m_audioOpenClock.isValid() ? m_audioOpenClock.elapsed() : 0;
        const bool opening = m_audioOpenClock.isValid() || state == State::Ready;
        m_audioOpenClock.invalidate();
        const bool failed = box.error() && *box.error() != application::SourceError::Cancelled;
        if (!opening || (state == State::Closed && !failed))
            return;
        m_analytics->mediaOpened(false, elapsed, state == State::Ready,
                                 box.fromVideo()                                    ? QStringLiteral("from_video")
                                 : application::AudioBox::isDummyName(box.path()) ? QStringLiteral("dummy")
                                                                                   : QStringLiteral("file"));
    });
    m_audioConnections << connect(m_audio.get(), &ui::AudioController::opened, this,
                                  [this](const QString &path, bool) { rememberRecentAudio(path); });
    m_audioConnections << connect(m_audio.get(), &ui::AudioController::logged, this, [this](const QString &message, bool debug, bool error) {
        if (debug)
            qDebug().noquote() << message;
        else
            m_log->notice(error ? ui::LogController::Error : ui::LogController::Warning,
                          QCoreApplication::translate("HikariSub.MainWindow", "Open audio"), message);
    });
    m_audioConnections << connect(m_editor.get(), &ui::LineEditorController::changed, this, &Application::refreshAudio);
    m_audioConnections << connect(m_video.get(), &ui::VideoController::changed, this, &Application::followVideoInAudio);
    m_audioConnections << connect(m_audio.get(), &ui::AudioController::changed, this, &Application::releaseReadIndexHandoff);
    m_audioConnections << connect(m_audio.get(), &ui::AudioController::findingOnsetsChanged, this,
                                  &Application::noticeOnsets);
    m_audioConnections << connect(m_audio.get(), &ui::AudioController::changed, this, [this] {
        if (m_templateEditor && !m_video->playing())
            m_templateEditor->hostChanged();
    });
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
        m_audio->setTimingHooks(std::move(hooks));
    }
    m_audio->setLineText([this](int key) -> QString {
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
    m_audio->setFollowAudio([this](int ms) { showVideoAtAudio(ms); });
    m_video->setOpenFilter([this](const QString &path, std::function<void(application::IndexRequest)> asVideo) {
        QPointer<Application> self(this);
        m_mediaSource->probe(path.toStdString(), [self, path, asVideo = std::move(asVideo)](
                                                     std::expected<application::MediaProbe, application::AudioFailure> probe) {
            if (!self)
                return;
            if (!probe)
                return asVideo({});
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
    const auto documentForMedia = [this] {
        if (home())
            newDocument();
    };
    m_video->setBeforeOpen(documentForMedia);
    m_audio->setBeforeOpen(documentForMedia);
    trackTabMedia();
    trackVideoSources();
    trackVideoFollow();
    startLocalisation();
    setupTabMenu(options);
    reportUserFolder(options.userFolderMove, options.userFolderSeed);
    reportSettingsFiles();
    convertListFiles();
    refreshViews();
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
                                                  "This portable copy keeps its settings in %1. An earlier version's "
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

void Application::routeAudio(const application::OutputRoute &route, bool move)
{
    m_generalPlayer->setOutputDevice(route.id);
    m_audioPlayer->setOutputDevice(route.systemDefault ? std::string() : route.name);
    if (move)
        m_audioPlayer->switchOutput();
    noticeAudioRoute(std::exchange(m_noticedRoute, route), route);
}

void Application::refreshAudio()
{
    if (!m_audio)
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
                m_audio->setActiveText(core::toUtf16(line->translation.empty() ? line->text : line->translation));
            }
            lines.push_back({static_cast<int>(line->start.value.microseconds() / 1000),
                             static_cast<int>(line->end.value.microseconds() / 1000), shown(line->id)});
        }
        if (activeId)
            key = std::tuple(target->value, activeId->value, session->revision());
    }
    bool select = key && key != m_audioLine;
    if (select && m_audioCommitting && m_audioLine && std::get<0>(*key) == std::get<0>(*m_audioLine) &&
        std::get<1>(*key) == std::get<1>(*m_audioLine))
        select = false;
    m_audioLine = key;
    m_audio->setLines(std::move(lines), active, select);
}

void Application::followVideoInAudio()
{
    const auto &video = m_video->session();
    const auto state = video.state();
    if (state == application::VideoSession::State::Opening) {
        m_audioFollowedVideo.clear();
        m_pendingIndexHandoff.clear();
    } else if (state == application::VideoSession::State::Closed) {
        m_audioFollowedVideo.clear();
    }
    if (state == application::VideoSession::State::Ready && m_audioFollowedVideo != QString::fromStdString(video.path())) {
        m_audioFollowedVideo = QString::fromStdString(video.path());
        if (keepTabAudio(m_audioFollowedVideo)) {
        } else if (video.dummy()) {
            if (const QString own = dummyVideoAudio(m_audioFollowedVideo);
                !own.isEmpty() && !(m_audio->hasAudio() && m_audio->path() == own))
                m_audio->openAudio(own);
        } else if (video.hasAudio()) {
            m_audio->openFromVideo(m_audioFollowedVideo, video.audioTrack(), video.newIndex(),
                                   QString::fromStdString(video.indexHandoff()));
            m_pendingIndexHandoff = video.indexHandoff();
            releaseReadIndexHandoff();
        } else if (m_audio->hasAudio())
            m_audio->closeAudio();
        m_audio->trimCache();
    }
    if (state != application::VideoSession::State::Ready) {
        if (m_audioTimebase) {
            m_audioTimebase.reset();
            m_audioKeyframes.clear();
            m_audio->setKeyframes({});
            m_audio->setKeyframeSnapTimes({});
        }
        m_audio->setVideoTime(std::nullopt);
        syncAudioWithVideo();
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
        std::vector<int> snap;
        snap.reserve(ms.size());
        for (const int keyMs : ms)
            snap.push_back(m_audioTimebase->startTimeFor(m_audioTimebase->frameAt(keyMs)));
        m_audio->setKeyframeSnapTimes(std::move(snap));
        m_audio->setKeyframes(std::move(ms));
    }
    std::optional<int> paused;
    if (!video.playing() && !video.stopped())
        if (const auto frame = video.reportedFrame())
            paused = m_audioTimebase->msAt(*frame);
    m_audio->setVideoTime(paused);
    syncAudioWithVideo();
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
    if (!session || !m_editor->editable())
        return false;
    const bool before = std::exchange(m_audioCommitting, true);
    m_editor->timesFromAudio(request.startMs, request.endMs, request.duration);
    const auto outcome = application::commitAudioTimes(*session, request, shownLines());
    if (request.save)
        m_editor->audioSent();
    m_editor->reloadFromSession();
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
    int time = 0;
    const auto &video = m_video->session();
    if (video.state() == application::VideoSession::State::Ready)
        if (const auto frame = video.reportedFrame())
            time = video.legacyTimebase().msAt(*frame);
    m_audio->showTime(time, mark);
}

void Application::openAudioFromVideo()
{
    const auto &video = m_video->session();
    if (video.state() == application::VideoSession::State::Ready) {
        if (const QString own = dummyVideoAudio(QString::fromStdString(video.path())); !own.isEmpty())
            return m_audio->openAudio(own);
        m_audio->openAudio(QString::fromStdString(video.path()));
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
    QString from = QString::fromStdString(m_video->session().path());
    if (from.isEmpty() && !m_recentVideo.entries().empty())
        from = QString::fromStdString(m_recentVideo.entries().front());
    if (from.isEmpty() || application::isDummyVideo(from.toStdString()))
        return {};
    return QUrl::fromLocalFile(QFileInfo(from).absolutePath());
}

Application::~Application()
{
    ui::DialogPlacement::setOpenedHook(nullptr);
    m_renderer.observeCost(nullptr);
    endSession();
    // MUST KILL: teardown order: the audio side before the video and helpers it reads through
    for (const auto &connection : std::as_const(m_audioConnections))
        disconnect(connection);
    m_video->setOpenFilter(nullptr);
    m_video->setBeforeOpen(nullptr);
    m_audio.reset();
    m_audioRouting.reset();
    m_audioPlayer.reset();
    m_audioOutput.reset();
    m_audioSource.reset();
    m_port->waitIdle();
    saveMisspellRules();
    if (m_localisation)
        QGuiApplication::setFont(m_startFont);
}

void Application::saveMisspellRules()
{
    if (m_misspellRules && !m_misspellRules->empty() && m_rulesFile)
        m_rulesFile->write(ui::rulesToToml(*m_misspellRules));
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
        if (staged.error().lyrics)
            m_log->log(openProblem(path, staged.error()));
        return std::nullopt;
    }
    return publish(std::move(*staged), path, asReference);
}

QString Application::openProblem(const QString &path, const application::StageFailure &failure)
{
    const QString name = QFileInfo(path).fileName();
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
        what = QCoreApplication::translate("HikariSub.Documents", "\"%1\" is not a time").arg(excerpt);
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
        what = QCoreApplication::translate("HikariSub.Documents", "\"%1\" has no time").arg(excerpt);
        break;
    case Kind::RepeatedWordLine:
        what = QCoreApplication::translate("HikariSub.Documents", "a line with word times can have only one line time");
        break;
    case Kind::NotWellFormed:
        what = QCoreApplication::translate("HikariSub.Documents", "the XML is broken at \"%1\"").arg(excerpt);
        break;
    case Kind::NotTtml:
        what = QCoreApplication::translate("HikariSub.Documents", "%1 is not a TTML document").arg(excerpt);
        break;
    case Kind::TooDeep:
        what = QCoreApplication::translate("HikariSub.Documents", "the elements are nested more than %1 deep at \"%2\"")
                   .arg(QString::number(core::kMaxTtmlDepth), excerpt);
        break;
    case Kind::TooLarge:
        what = QCoreApplication::translate("HikariSub.Documents", "it is too large for a lyrics file");
        break;
    case Kind::NoLyrics: what = QCoreApplication::translate("HikariSub.Documents", "it has no timed lyrics"); break;
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
    if (!asReference)
        rememberRecent(QFileInfo(path).absoluteFilePath().toStdString());
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

void Application::releaseReadIndexHandoff()
{
    if (m_pendingIndexHandoff.empty() || (m_audio->hasAudio() && !m_audio->loaded()))
        return;
    m_mediaSource->releaseIndexHandoff(std::exchange(m_pendingIndexHandoff, std::string()));
}

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
    protectReference();
    showTabArrangement();
    if (m_workspace.editingTarget())
        spellChecker();
    reportGroupBreak();
    scheduleAutosave();
    if (m_styleManager)
        m_styleManager->targetRefreshed();
    const auto target = m_workspace.editingTarget();
    const auto reference = m_workspace.reference();
    auto *targetSession = target ? m_files->session(*target) : nullptr;
    auto *referenceSession = reference ? m_files->session(*reference) : nullptr;
    refreshComparison();
    m_shell->setShowOriginal(target && showOriginal(*target), reference && showOriginal(*reference));
    m_shell->refresh(targetSession ? &targetSession->document() : nullptr,
                     referenceSession ? &referenceSession->document() : nullptr,
                     target ? m_comparison.table(*target) : nullptr,
                     reference ? m_comparison.table(*reference) : nullptr);
    if (targetSession) {
        m_shell->setDraft(targetSession->draftRecord());
        m_shell->setSelection(targetSession->selection());
        m_editor->selectionChanged();
    }
    if (referenceSession)
        m_shell->setReferenceSelection(referenceSession->selection());
    followEditingLine();
    if (m_templateEditor) {
        m_templateEditor->setEditable(target && m_workspace.checkContentCommand(*target).has_value());
        m_templateEditor->refresh();
    }
    if (target != m_editorDocument) {
        m_editorDocument = target;
        m_editor->setDocument(target, target && m_workspace.checkContentCommand(*target).has_value());
        if (m_find)
            m_find->reset();
    }
    // MUST KILL: must run after m_editor->setDocument above
    if (m_styleManager)
        m_styleManager->documentEdited();
    if (m_kanjiTimer)
        m_kanjiTimer->documentEdited();
    refreshVideo();
    refreshAudio();
    if (m_statusBar)
        m_statusBar->refresh();
    emit tabsChanged();
}

bool Application::showVideoSubtitles()
{
    const auto target = m_videoDocument;
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    if (setVideoFonts(target))
        m_videoScript.clear();
    bool edited = false;
    std::optional<core::Document> draft;
    if (!m_settings->boolean("video.disableLiveEditing") && m_editorDocument == target && m_video->hasVideo())
        draft = m_editor->draftDocument();
    std::optional<core::Document> templated;
    if (m_templateEditor && m_templateEditor->videoPreview()) {
        const auto &video = m_video->session();
        const auto frame = video.reportedFrame();
        const auto at = frame ? video.frameStart(*frame) : std::nullopt;
        const std::int64_t ms = at ? at->microseconds() / 1000 : 0;
        templated = m_templateEditor->videoDocument(draft ? *draft : session->document(), ms, video.playing());
        const auto window = m_templateEditor->videoWindow(ms, video.playing());
        m_templateWindow = std::pair(window.fromMs, window.toMs);
    }
    if (draft || templated || m_videoShowsDraft || m_videoShowsTemplate || session->revision() != m_videoRevision) {
        if (session->revision() != m_videoRevision)
            edited = m_videoRevision.has_value();
        m_videoRevision = session->revision();
        m_videoShowsDraft = draft.has_value();
        m_videoShowsTemplate = templated.has_value();
        auto script = m_visualTools->subtitles(templated ? *templated : draft ? *draft : session->document(),
                                               [this](const core::Document &d) { return rendererScript(d); });
        if (script != m_videoScript) {
            m_videoScript = script;
            m_video->session().setSubtitles(std::move(script));
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

std::vector<application::FontLease> Application::subtitleFonts(std::optional<application::DocumentId> document)
{
    auto fonts = attachedFontLeases(document);
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
    if (std::ranges::equal(fonts, m_videoFonts, same))
        return false;
    m_videoFonts = fonts;
    if (m_visualTools) {
        decltype(m_leasedFontNames) read;
        std::vector<std::string> names;
        for (const auto &font : m_videoFonts) {
            if (!font.bytes)
                continue;
            const auto known = std::ranges::find(m_leasedFontNames, font.bytes, &decltype(read)::value_type::first);
            auto own = known != m_leasedFontNames.end() ? known->second : backends::fontNames(*font.bytes);
            names.insert(names.end(), own.begin(), own.end());
            read.emplace_back(font.bytes, std::move(own));
        }
        m_leasedFontNames = std::move(read);
        m_visualTools->setLeasedFonts(names);
    }
    m_video->session().setFonts(std::move(fonts));
    return true;
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
    if (target != m_videoDocument) {
        const auto previous = m_videoDocument;
        leaveTabMedia(previous);
        m_videoDocument = target;
        m_videoRevision.reset();
        m_videoScript.clear();
        m_videoLine.reset();
        if (!m_keepVideoOnEnter) {
            m_video->resetForTarget();
            showAssociationOffer();
        }
        enterTabMedia(previous, target);
    }
    m_visualTools->refresh();
    if (!session)
        return;
    const bool edited = showVideoSubtitles();
    m_video->session().setMatrix(application::sessionVideoMatrix(*session));
#ifdef _WIN32
    if (m_directShow) // W1: legacy RendererDirectShow::SetColorSpace
        m_directShow->setColorSpace(m_video->session().matrix());
#endif
    const auto active = session->selection().active;
    std::optional<std::pair<core::DocumentTime, core::DocumentTime>> lineTimes;
    if (active)
        for (const auto *line : session->document().lines())
            if (line->id == *active)
                lineTimes = std::pair(line->start.value, line->end.value);
    m_video->setActiveLineTimes(lineTimes);
    bool rowChanged = false;
    if (active && active != m_videoLine) {
        rowChanged = m_videoLine.has_value();
        m_videoLine = active;
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
    if (m_templateEditor)
        m_templateEditor->commit();
    endFindOpen(false);
    const bool quitReplaced = m_closeThen == QLatin1String("quit");
    m_closeThen = then;
    if (quitReplaced)
        emit quitCancelled();
    if (then == QLatin1String("quit"))
        ++m_quitReviews;
    m_closing.clear();
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
    if (then != QLatin1String("tab"))
        m_closingTab.reset();
    m_closeTarget = m_workspace.editingTarget();
    if (then != QLatin1String("session"))
        m_pendingSession.reset();
    if (then == QLatin1String("quit") || then == QLatin1String("session"))
        scope = m_workspace.documents();
    else if (then == QLatin1String("tab"))
        scope = m_closingTab ? std::vector{*m_closingTab} : std::vector<application::DocumentId>{};
    else if (then == QLatin1String("all"))
        scope = m_workspace.tabs();
    else if (const auto target = m_workspace.editingTarget())
        scope = {*target};
    QVariantList rows;
    for (const auto id : scope) {
        auto *session = m_files->session(id);
        if (!session || (!session->isDirty() && !(m_kanjiTimer && m_kanjiTimer->pendingFor(id.value) > 0)))
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
        if (c.save && m_kanjiTimer && !m_kanjiTimer->applyTo(id.value, *session)) {
            m_closing.clear();
            emit closeFinished(false, QCoreApplication::translate("HikariSub.Documents",
                                                                  "A document could not be saved; nothing was closed."));
            return;
        }
        if (c.save) {
            QString path = row.value(QStringLiteral("path")).toString();
            if (const QString ext = extensionFor(session->document().format()); !path.isEmpty() && !path.endsWith(ext))
                path += u'.' + ext;
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
        && then != QLatin1String("all");
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
                    m_videoFullscreen->enterWhenShown(videoAfter);
                m_video->openVideo(videoAfter);
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
        if (const auto tab = std::exchange(m_closingTab, std::nullopt))
            closeDocument(*tab);
    } else if (then == QLatin1String("all")) {
        closeAllTabs();
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
    if (quit)
        emit quitCancelled();
    m_pendingOpen.reset();
    m_pendingOpenPath.clear();
    m_pendingSession.reset();
    m_closingTab.reset();
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
    const QString video = *document == m_workspace.editingTarget() ? readyVideo(m_video->session()) : tabVideo(*document);
    if (path.isEmpty() || m_formatChanged.contains(document->value) || (saveWithVideoName() && !video.isEmpty() &&
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
    const QString video = *document == m_workspace.editingTarget() ? readyVideo(m_video->session()) : tabVideo(*document);
    const auto importPath = m_importPaths.find(document->value);
    const QString path = !video.isEmpty() && saveWithVideoName() ? video
                         : destination && !destination->value.empty() ? QString::fromStdString(destination->value)
                         : importPath != m_importPaths.end()            ? importPath->second
                                                                       : QString();
    const QString ext = extensionFor(session->document().format());
    const QString filter = QCoreApplication::translate("HikariSub.Documents", "Subtitle file ") +
                           (ext == QStringLiteral("txt") ? QStringLiteral("(*.txt *.sub)") : QStringLiteral("(*.%1)").arg(ext));
    const QFileInfo info(path);
    return {{QStringLiteral("folder"), path.isEmpty() ? QUrl() : QUrl::fromLocalFile(info.absolutePath())},
            {QStringLiteral("file"), path.isEmpty() ? QUrl() : QUrl::fromLocalFile(info.absolutePath() + u'/' + info.completeBaseName())},
            {QStringLiteral("filter"), filter},
            {QStringLiteral("extension"), ext}};
}

QString Application::saveChosen(const QUrl &file)
{
    return saveChosenFor(0, file);
}

QString Application::saveChosenFor(qulonglong id, const QUrl &file)
{
    const auto document = documentOf(id);
    auto *session = document ? m_files->session(*document) : nullptr;
    QString path = file.toLocalFile();
    if (!session || path.isEmpty())
        return QStringLiteral("failed");
    if (readOnly(path))
        return QStringLiteral("readonly");
    const QString ext = extensionFor(session->document().format());
    if (!path.endsWith(ext))
        path += u'.' + ext;
    if (readOnly(path))
        return QStringLiteral("readonly");
    auto plan = m_files->prepareSave(*document, application::DestinationKey{QFileInfo(path).absoluteFilePath().toStdString()});
    if (!plan || !m_files->startSave(std::move(*plan)))
        return QStringLiteral("failed");
    m_formatChanged.erase(document->value);
    if (*document == m_workspace.editingTarget())
        m_editor->reloadFromSession();
    refreshViews();
    return {};
}

bool Application::saveDocument(qulonglong id)
{
    const auto document = documentOf(id);
    if (!document)
        return false;
    if (*document == m_workspace.editingTarget())
        return m_editor->save();
    auto plan = m_files->prepareSave(*document);
    const std::string *title = m_workspace.title(*document);
    const QString name = title ? QString::fromStdString(*title) : QString();
    if (!plan || !m_files->startSave(std::move(*plan))) {
        m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Documents", "Save"),
                      QCoreApplication::translate("HikariSub.Documents", "%1 could not be saved.").arg(name));
        return false;
    }
    refreshViews();
    return true;
}

QVariantList Application::saveAll()
{
    m_templateEditor->commit();
    QVariantList later;
    for (const auto id : m_workspace.tabs()) {
        auto *session = m_files->session(id);
        if (!session || !session->isDirty())
            continue;
        const QString route = saveRouteFor(id.value);
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
    m_editor->reloadFromSession();
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
    m_editor->reloadFromSession();
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
    m_videoRevision.reset();
    m_videoScript.clear();
    m_videoLine.reset();
    m_comparedRevisions.erase(target->value);
    refreshComparison();
    m_editor->reloadFromSession();
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
        return false;
    session->setSelection(std::move(next));
    m_shell->setSelection(session->selection());
    m_editor->selectionChanged();
    if (m_templateEditor)
        m_templateEditor->refresh();
    refreshVideo();
    followEditingLine();
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
        applySelection(m_shell->withFoldedRuns(gridSelection().plain(session->selection(), core::LineId{id})));
}

void Application::extendSelection(int rows)
{
    const auto target = m_workspace.editingTarget();
    if (auto *session = target ? m_files->session(*target) : nullptr)
        applySelection(m_shell->withFoldedRuns(gridSelection().shiftKey(session->selection(), rows)));
}

void Application::toggleFold(qulonglong first)
{
    const core::LineId id{first};
    if (!m_shell->foldGenerated() || m_shell->runFirst(id) != id)
        return;
    const bool fold = !m_shell->runFolded(id);
    if (auto *session = targetSession(); fold && session) {
        const application::Selection &current = session->selection();
        application::Selection next = m_shell->foldedWhole(current, id);
        if ((next.active != current.active || next.selected != current.selected) && !applySelection(std::move(next)))
            return;
    }
    m_shell->setRunFolded(id, fold);
}

void Application::selectFoldedRunsWhole()
{
    auto *session = targetSession();
    if (!session || !m_shell->foldGenerated())
        return;
    const application::Selection &current = session->selection();
    application::Selection next = m_shell->foldedWhole(current);
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
            applySelection(m_shell->withFoldedRuns(rules.plain(session->selection(), line)));
    } else if (shift)
        applySelection(m_shell->withFoldedRuns(rules.shiftClick(session->selection(), line, ctrl)));
    else if (ctrl)
        applySelection(m_shell->withFoldedRuns(rules.ctrlClick(session->selection(), line)));
    else
        applySelection(m_shell->withFoldedRuns(rules.plain(session->selection(), line)));
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
        applySelection(m_shell->withFoldedRuns(gridSelection().shiftClick(session->selection(), core::LineId{id}, false)));
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
        const auto &video = m_video->session();
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
    const auto shown = m_shell->actionLines();
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

}

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
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "The starting time"), Start},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "End time"), End},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "Actor"), Actor},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "Styles"), Style},
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
    } else if (format == core::SubtitleFormat::TMPlayer) {
        rows = {{QT_TRANSLATE_NOOP("HikariSub.Grid", "The starting time"), Start},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "Text"), Text}};
    } else {
        // MUST KILL: the End row is labelled "The starting time" on purpose
        rows = {{QT_TRANSLATE_NOOP("HikariSub.Grid", "The starting time"), Start},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "The starting time"), End},
                {QT_TRANSLATE_NOOP("HikariSub.Grid", "Text"), Text}};
    }
    const int chosen = paste ? m_pasteColumns : m_copyColumns;
    QVariantList out;
    for (const auto &[label, bit] : rows)
        out << QVariantMap{{QStringLiteral("label"), QCoreApplication::translate("HikariSub.Grid", label)},
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
    const auto frame = video.reportedFrame();
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
    m_shell->setStatusText(QCoreApplication::translate("HikariSub.Session", "Autosave"));
    // Qt::CoarseTimer may fire up to 5 % early.
    QTimer::singleShot(5000, Qt::PreciseTimer, this, [this] { m_shell->setStatusText(QString()); });
    application::RecoveryContent content;
    const auto activeLine = application::activeLineToSave(*session);
    const auto layout = m_files->layoutToSave(document, session->document());
    core::Document saved = application::withActiveLine(session->document(), activeLine);
    if (layout)
        saved.setHikariProperty(application::kLayoutKey, *layout);
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
            || m_video->requestedVideo() != m_matroskaRequested;
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
        media.video = QDir::toNativeSeparators(readyVideo(m_video->session()));
        media.position = targetVideoPosition();
        media.audio = m_audio->hasAudio() ? QDir::toNativeSeparators(m_audio->path()) : QString();
        m_keepVideoOnEnter = !media.video.isEmpty();
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
        m_log->notice(ui::LogController::Info, QCoreApplication::translate("HikariSub.Session", "Open auto save"),
                      QCoreApplication::translate("HikariSub.Session", "Auto save folder is empty"));
#else
        m_log->notice(ui::LogController::Info, QCoreApplication::translate("HikariSub.Session", "Open auto save"), QDir(folder).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)
                       ? QCoreApplication::translate("HikariSub.Session", "Cannot open auto save folder")
                       : QCoreApplication::translate("HikariSub.Session", "Auto save folder is empty"));
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
        m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Session", "Open auto save"),
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
    const auto &video = m_video->session();
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
    if (kind == QLatin1String("frames"))
        return runFilter([&](application::EditSession &s) { return application::splitIntoFrames(s, timebase, shown); });
    const auto text = kind == QLatin1String("chars")   ? application::SplitText::Characters
                      : kind == QLatin1String("words") ? application::SplitText::Words
                      : kind == QLatin1String("wraps") ? application::SplitText::Wraps
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
    if (!showOriginal(*m_workspace.editingTarget()))
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
        m_tabMedia[target->value].keyframes = QDir::toNativeSeparators(path);
    auto &video = m_video->session();
    if (video.state() != application::VideoSession::State::Ready) {
        if (QString problem; keyframesWithoutVideo(path, problem))
            return problem;
        m_pendingKeyframes = path;
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
    return m_video->session().state() == application::VideoSession::State::Ready &&
           m_video->session().legacyTimebase().exact();
}

QString Application::shiftTimes()
{
    auto *session = targetSession();
    if (!session)
        return QCoreApplication::translate("HikariSub.TimeShift", "No document open");
    const auto &video = m_video->session();
    const bool hasVideo = video.state() == application::VideoSession::State::Ready;
    const auto timebase = video.legacyTimebase();
    application::ShiftContext context;
    if (hasVideo) {
        context.timebase = &timebase;
        context.keyframes = video.keyframes();
        if (const auto frame = video.reportedFrame()) {
            context.videoFrame = *frame;
            context.videoFrameStartMs = timebase.startTimeFor(*frame);
            context.videoFrameEndMs = timebase.endTimeFor(*frame);
        }
    }
    if (m_audio && m_audio->hasMark())
        context.audioMarkMs = m_audio->markMs();
    auto settings = m_shiftTimes->settings();
    settings.timePerCharacter = m_settings->integer("convert.timePerCharacter");
    const auto result = application::shiftTimes(*session, settings, context, actionLines(*session));
    m_editor->reloadFromSession();
    refreshViews();
    if (!result) {
        if (const auto *problem = std::get_if<application::ShiftProblem>(&result.error())) {
            switch (*problem) {
            case application::ShiftProblem::NoStylesChosen:
                return QCoreApplication::translate("HikariSub.TimeShift", "No styles selected for time shifting");
            case application::ShiftProblem::NoLinesSelected:
                return QCoreApplication::translate("HikariSub.TimeShift", "No lines selected for shifting");
            case application::ShiftProblem::NoExactTimebase:
                return QCoreApplication::translate("HikariSub.TimeShift", "Video was not loaded using FFMS2");
            }
        }
        return QCoreApplication::translate("HikariSub.TimeShift", "The times could not be shifted.");
    }
    if (result->endCorrectionSkipped)
        m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.TimeShift", "Shift times"),
                      QCoreApplication::translate("HikariSub.TimeShift", "Video was not loaded using FFMS2"));
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
        for (const auto id : m_workspace.tabs())
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
                          QCoreApplication::translate("HikariSub.SelectLines", "Invalid regular expression '%1': %2")
                              .arg(fromUtf16(result->invalidExpression->first), fromUtf16(result->invalidExpression->second)));
        for (const auto &error : result->matchErrors)
            m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.SelectLines", "Select lines"),
                          regexMatchError(error));
        count += result->count;
        if (result->clipboard)
            setClipboardText(*result->clipboard);
    }
    m_editor->reloadFromSession();
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

QString unscaledValue(int row, const std::u16string &value, const std::u16string &tag)
{
    return QCoreApplication::translate("HikariSub.Conversion", "In line %1, value '%2' cannot be scaled\nin tag '%3'")
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

QString replacerTitle()
{
    return QCoreApplication::translate("HikariSub.MisspellReplacer", "Fix minor errors");
}

void logRegexErrors(ui::LogController &log, const std::vector<application::ReplacerRule> &rules, bool checkedOnly)
{
    for (const auto &error : application::replacerRegexErrors(rules, checkedOnly))
        log.notice(ui::LogController::Error, replacerTitle(),
                   QCoreApplication::translate("HikariSub.SelectLines", "Invalid regular expression '%1': %2")
                       .arg(fromUtf16(error.expression), fromUtf16(error.message)));
}

QString regexMatchError(const std::u16string &error)
{
    return QCoreApplication::translate("HikariSub.SelectLines", "Failed to find match for regular expression: %1")
        .arg(fromUtf16(error));
}

void logMatchErrors(ui::LogController &log, const application::ReplacerMatchErrors &errors)
{
    for (const auto &error : errors)
        log.notice(ui::LogController::Warning, replacerTitle(), regexMatchError(error));
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

std::vector<application::ReplacerRule> &Application::misspellRuleList()
{
    if (!m_misspellRules) {
        if (const auto items = m_rulesFile ? m_rulesFile->read() : std::nullopt)
            m_misspellRules = std::get<std::vector<application::ReplacerRule>>(ui::rulesFromToml(*items));
        else
            m_misspellRules = application::readReplacerRules({}).rules;
    }
    return *m_misspellRules;
}

void Application::convertRules()
{
    const QString folder = QFileInfo(m_settingsFile).absolutePath();
    m_rulesFile = ui::ListFile::make(
        folder + QStringLiteral("/rules.toml"), folder + QStringLiteral("/Rules.txt"), ui::kRulesFormat,
        [this](const std::filesystem::path &legacy) -> std::optional<toml::array> {
            const auto read = readLegacyRulesFile(legacy);
            if (!read)
                return std::nullopt;
            for (const auto &line : read->invalid)
                m_log->notice(ui::LogController::Warning, replacerTitle(),
                              QCoreApplication::translate("HikariSub.MisspellReplacer", "Rule \"%1\" is invalid.")
                                  .arg(fromUtf8(line)));
            return ui::rulesToToml(read->rules);
        },
        listFileReport(*m_log));
    m_rulesFile->convert();
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
    m_visualTools->setShapesFile(std::move(shapes));
    m_visualTools->setAllTagsFile(std::move(tags));
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
        m_log->notice(ui::LogController::Warning, replacerTitle(),
                      QCoreApplication::translate("HikariSub.MisspellReplacer", "Cannot change rules\nwhen find results window is open"));
        return false;
    }
    misspellRuleList().push_back(replacerRule(rule));
    return true;
}

bool Application::editMisspellRule(int index, const QVariantMap &rule)
{
    if (m_misspellResultsShown) {
        m_log->notice(ui::LogController::Warning, replacerTitle(),
                      QCoreApplication::translate("HikariSub.MisspellReplacer", "Cannot change rules\nwhen find results window is open"));
        return false;
    }
    auto &rules = misspellRuleList();
    if (index < 0 || static_cast<std::size_t>(index) >= rules.size()) {
        m_log->notice(ui::LogController::Error, replacerTitle(),
                      QStringLiteral("edit rule - bad selection of rule %1 - %2").arg(index).arg(rules.size()));
        return false;
    }
    const bool checked = rules[static_cast<std::size_t>(index)].checked;
    rules[static_cast<std::size_t>(index)] = replacerRule(rule);
    rules[static_cast<std::size_t>(index)].checked = checked;
    return true;
}

bool Application::removeMisspellRule(int index)
{
    if (m_misspellResultsShown) {
        m_log->notice(ui::LogController::Warning, replacerTitle(),
                      QCoreApplication::translate("HikariSub.MisspellReplacer", "Cannot change rules\nwhen find results window is open"));
        return false;
    }
    auto &rules = misspellRuleList();
    if (index < 0 || static_cast<std::size_t>(index) >= rules.size()) {
        m_log->notice(ui::LogController::Error, replacerTitle(),
                      QStringLiteral("edit rule - bad selection of rule %1 - %2").arg(index).arg(rules.size()));
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
        logRegexErrors(*m_log, misspellRuleList(), true);
        application::ReplacerMatchErrors matchErrors;
        const auto finds = application::findErrors(*session, misspellRuleList(), replacerScope(scope), &matchErrors);
        logMatchErrors(*m_log, matchErrors);
        if (finds.empty())
            continue;
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
            logRegexErrors(*m_log, misspellRuleList(), true);
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
                m_log->notice(ui::LogController::Warning, replacerTitle(),
                              QCoreApplication::translate("HikariSub.MisspellReplacer", "Line %1 cannot be replaced,\ncause it was edited.")
                                  .arg(problem.lineNumber));
            else if (problem.kind == K::MatchError)
                m_log->notice(ui::LogController::Warning, replacerTitle(), regexMatchError(problem.found));
            else
                m_log->notice(ui::LogController::Warning, replacerTitle(),
                              QCoreApplication::translate("HikariSub.MisspellReplacer",
                                                          "Cannot replace \"%1\" to \"%2\", with rule \"%3\" in line %4.")
                                  .arg(fromUtf16(problem.found), fromUtf8(problem.replace), fromUtf8(problem.find))
                                  .arg(problem.lineNumber));
        }
    }
    m_editor->reloadFromSession();
    refreshViews();
}

void Application::showMisspellFind(int index)
{
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
                m_settings->set("editor.spellchecker", false);
                const bool noDictionary = status == application::SpellChecker::Status::NoDictionary;
                const QString message =
                    status == application::SpellChecker::Status::NoDictionary
                        ? QCoreApplication::translate("HikariSub.Spelling",
                                                      "No dictionary files were found in the \"%1\" folder.\n"
                                                      "Spell checking will be disabled")
                              .arg(QDir::toNativeSeparators(QFileInfo(m_dictionaryDir).absoluteFilePath()))
                        : QCoreApplication::translate("HikariSub.Spelling", "Failed to initialize spell checker.");
                QMetaObject::invokeMethod(this, [this, message, noDictionary] {
                    m_log->notice(noDictionary ? ui::LogController::Warning : ui::LogController::Error,
                                  QCoreApplication::translate("HikariSub.Spelling", "Spellchecker"),
                                  QString(message).replace(QLatin1Char('\n'), QLatin1Char(' ')));
                    emit spellingChanged();
                }, Qt::QueuedConnection);
            }
        }
    }
    return m_spellChecker.get();
}

void Application::restartSpellChecker()
{
    if (m_spellChecker)
        m_spellChecker->clear();
    m_spellingStarted = false;
    emit spellCheckerRestarted();
}

void Application::spellingRefresh()
{
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
    if (!m_spellingOn || role != spellingRole() || !core::syntax::spellChecked(m_editor->syntaxState()))
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
    if (!m_spellingOn || role != spellingRole() || !core::syntax::spellChecked(m_editor->syntaxState()))
        return {};
    const auto raw = editorRaw(role);
    auto *checker = spellChecker();
    if (!raw || !checker)
        return {};
    const auto format = targetSession()->document().format();
    const auto marks = application::editorMarks(*raw, format, *checker, m_spellingText, true);
    int at = position;
    if (!m_editor->showTags())
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
    m_editor->reloadFromSession();
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
        return spellCheckState(changed, QCoreApplication::translate("HikariSub.Spelling", "The edited line cannot be committed."));
    const auto *current = m_spellWalk && m_spellWalk->current() ? &*m_spellWalk->current() : nullptr;
    QStringList suggestions;
    if (current)
        for (const auto &s : current->suggestions)
            suggestions << qstr(s);
    int row = 0;
    QString before, after;
    if (const auto *session = current ? targetSession() : nullptr) {
        const auto lines = session->document().lines();
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (lines[i]->id == current->line)
                row = static_cast<int>(i) + 1;
        const auto raw = session->selection().active == current->line ? editorRaw(spellingRole()) : std::nullopt;
        if (raw) {
            QString text = qstr(*raw);
            int from = current->start, to = current->end + 1;
            if (!m_editor->showTags()) {
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
            {QStringLiteral("available"), m_spellChecker && m_spellChecker->ready()},
            {QStringLiteral("line"), row},
            {QStringLiteral("before"), before},
            {QStringLiteral("after"), after},
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
    m_editor->commit();
    m_spellWalk = std::make_unique<application::SpellCheckWalk>(*checker, m_spellingText);
    m_spellWalkDocument = m_workspace.editingTarget();
    m_spellWalk->next(*session, walkOptions(options));
    showSpellCheckWord();
    return spellCheckState(false);
}

std::optional<QVariantMap> Application::restartStaleSpellCheck(application::EditSession &session,
                                                               const QVariantMap &options)
{
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
    auto *session = targetSession();
    if (!m_spellWalk || !session)
        return unchangedState();
    if (auto restarted = restartStaleSpellCheck(*session, options))
        return *restarted;
    const auto result = m_spellWalk->replace(*session, replacement.toStdU16String(), walkOptions(options));
    if (!result)
        return spellCheckState(false, refusalText(result.error()));
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
    m_find = std::make_unique<application::FindReplace>(*m_findHost, unicodeFold);
}

QVariantMap Application::openFindReplace(int tab)
{
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
    auto s = application::findReplaceFromOptions(m_settings->integer("find.options"));
    s.tab = static_cast<application::FindReplaceSettings::Tab>(std::clamp(tab, 0, 2));
    findFirstEntries(s, m_find->recent());
    s.styles = toU8(m_settings->text("find.styles"));
    return findMap(s, m_find->recent());
}

void Application::saveFindReplaceSettings(const QVariantMap &settings)
{
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
    const bool tl = application::translationMode(*session);
    std::vector<int> roles{tl ? 1 : 0};
    if (tl)
        roles.push_back(0);
    for (const int role : roles) {
        const auto [from, to] = m_editor->fieldSelectionOf(role);
        if (from >= to)
            continue;
        if (std::pair(from, to) == m_editor->displaySpan(role, m_find->lastStart(), m_find->lastEnd()))
            return find;
        const QString shown = role == 1 ? m_editor->translationText() : m_editor->text();
        const QString selected = shown.mid(from, to - from);
        m_find->selectionAdopted();
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
            row.insert(QStringLiteral("line"), QCoreApplication::translate("HikariSub.Search", "Line %1: ").arg(r.idLine));
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
    const bool hasVideo = m_video->session().state() == application::VideoSession::State::Ready && m_video->session().lastFrame();
    if (hasVideo)
        video = {m_video->session().lastFrame()->width, m_video->session().lastFrame()->height};
    return {{QStringLiteral("subsWidth"), subs.width},
            {QStringLiteral("subsHeight"), subs.height},
            {QStringLiteral("videoWidth"), video.width},
            {QStringLiteral("videoHeight"), video.height},
            {QStringLiteral("hasVideo"), hasVideo}};
}

bool Application::resample(int subsWidth, int subsHeight, int width, int height, bool stretch)
{
    auto *session = targetSession();
    if (!session || (subsWidth == width && subsHeight == height) || subsWidth < 1 || subsHeight < 1)
        return false;
    QStringList unscaled;
    const auto warn = [&unscaled](int row, const std::u16string &value, const std::u16string &tag) {
        unscaled << unscaledValue(row, value, tag);
    };
    const bool done = application::changeResolution(*session, {subsWidth, subsHeight}, {width, height}, true, stretch, warn)
                          .has_value();
    noticeUnscaled(*m_log, unscaled);
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
    QStringList unscaled;
    const auto warn = [&unscaled](int row, const std::u16string &value, const std::u16string &tag) {
        unscaled << unscaledValue(row, value, tag);
    };
    const bool done = application::changeResolution(*session, subs, video, option != 0, option == 2, warn).has_value();
    noticeUnscaled(*m_log, unscaled);
    m_editor->reloadFromSession();
    refreshViews();
    return done;
}

void Application::setAskForBadResolution(bool on)
{
    m_settings->set("video.dontAskForBadResolution", !on);
}

namespace {

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

}

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
    m_conversionPlan.reset();
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
    if (!session || session->document().scriptInfo(u8"TLMode") == std::optional<std::u8string>(u8"Yes"))
        return {};
    const auto format = session->document().format();
    QStringList out;
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

QStringList Application::conversionStyles(const QString &catalog) const
{
    QStringList out;
    if (const auto styles = m_styleManager->catalogStore().list(utf8(catalog)))
        for (const auto &style : *styles)
            out << QString::fromUtf8(reinterpret_cast<const char *>(style.name.data()), qsizetype(style.name.size()));
    return out;
}

QVariantMap Application::previewConversion(const QString &targetName)
{
    m_conversionPlan.reset();
    auto *session = targetSession();
    const auto target = formatNamed(targetName);
    if (!session || !target || !conversionTargets().contains(targetName))
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("problem"),
                 QCoreApplication::translate("HikariSub.Conversion", "This document can't be converted to that format.")}};
    const QVariantMap o = conversionOptions();
    core::ConversionOptions options;
    const auto &video = m_video->session();
    const bool hasVideo = video.state() == application::VideoSession::State::Ready;
    const float frameMs = hasVideo ? video.legacyTimebase().frameDuration() : 0.f;
    options.fps = o.value(QStringLiteral("fpsFromVideo")).toBool() && hasVideo && frameMs > 0.f
                      ? 1000.0 / frameMs
                      : o.value(QStringLiteral("fps")).toString().toDouble();
    if (!std::isfinite(options.fps) || options.fps < 1 || options.fps > 1000)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("fpsInvalid"), true},
                {QStringLiteral("problem"),
                 QCoreApplication::translate("HikariSub.Conversion", "Invalid FPS. Correct it in options and try again.")}};
    options.videoFps = hasVideo && frameMs > 0.f ? 1000.0 / frameMs : 23.976;
    options.prefix = utf8(o.value(QStringLiteral("prefix")).toString());
    options.newEndTimes = o.value(QStringLiteral("newEndTimes")).toBool();
    options.timePerCharacter = o.value(QStringLiteral("timePerCharacter")).toInt();
    options.resolutionWidth = utf8(o.value(QStringLiteral("resolutionWidth")).toString());
    options.resolutionHeight = utf8(o.value(QStringLiteral("resolutionHeight")).toString());
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
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("problem"),
                 QCoreApplication::translate("HikariSub.Conversion", "This document can't be converted to that format.")}};
    const auto &r = result->report;
    QStringList losses;
    QStringList kinds;
    const auto add = [&](const QString &text, bool removes) {
        losses << text;
        kinds << (removes ? QStringLiteral("warning") : QStringLiteral("info"));
    };
    if (r.headerDropped)
        add(QCoreApplication::translate("HikariSub.Conversion", "The script properties and styles are removed."), true);
    if (r.commentsRemoved)
        add(QCoreApplication::translate("HikariSub.Conversion", "%n comment line(s) removed.", nullptr, r.commentsRemoved), true);
    if (r.drawingsCleared)
        add(QCoreApplication::translate("HikariSub.Conversion", "%n drawing(s) removed.", nullptr, r.drawingsCleared), true);
    if (r.emptyRemoved)
        add(QCoreApplication::translate("HikariSub.Conversion", "%n line(s) left without text removed.", nullptr, r.emptyRemoved), true);
    if (r.duplicatesRemoved)
        add(QCoreApplication::translate("HikariSub.Conversion", "%n duplicate line(s) removed.", nullptr, r.duplicatesRemoved), true);
    if (r.reordered)
        add(QCoreApplication::translate("HikariSub.Conversion", "Lines are sorted by time."), false);
    if (r.textChanged)
        add(QCoreApplication::translate("HikariSub.Conversion", "Formatting tags converted or removed in %n line(s).", nullptr,
                                        r.textChanged),
            *target != core::SubtitleFormat::Ass);
    if (r.fieldsDropped)
        add(*target == core::SubtitleFormat::Ass
                ? QCoreApplication::translate("HikariSub.Conversion", "%n line(s) get the conversion style \"%1\".", nullptr,
                                              r.fieldsDropped)
                      .arg(o.value(QStringLiteral("style")).toString())
                : QCoreApplication::translate("HikariSub.Conversion",
                                              "Layer, style, actor, margins or effect removed from %n line(s).", nullptr,
                                              r.fieldsDropped),
            *target != core::SubtitleFormat::Ass);
    if (r.markersDropped)
        add(QCoreApplication::translate("HikariSub.Conversion", "Bookmarks, hidden lines and groups removed from %n line(s).",
                                        nullptr, r.markersDropped),
            true);
    if (r.timesChanged)
        add(QCoreApplication::translate("HikariSub.Conversion",
                                        "Times changed (rounding, frames or new end times) in %n line(s).", nullptr,
                                        r.timesChanged),
            false);
    m_conversionPlan = std::make_unique<ConversionPlan>(
        ConversionPlan{m_workspace.editingTarget()->value, session->revision(), o, frameMs, std::move(result->document)});
    return {{QStringLiteral("ok"), true}, {QStringLiteral("losses"), losses}, {QStringLiteral("kinds"), kinds}};
}

bool Application::acceptConversion()
{
    auto plan = std::move(m_conversionPlan);
    auto *session = targetSession();
    const auto target = m_workspace.editingTarget();
    const auto &video = m_video->session();
    const float frameMs =
        video.state() == application::VideoSession::State::Ready ? video.legacyTimebase().frameDuration() : 0.f;
    if (!plan || !session || !target || target->value != plan->document || session->revision() != plan->revision ||
        conversionOptions() != plan->options || frameMs != plan->frameMs)
        return false;
    std::set<core::LineId> touches;
    for (const auto *line : session->document().lines())
        touches.insert(line->id);
    application::Command command{"Subtitles conversion", session->revision(), touches,
                                 [&](core::Document &d) {
                                     d = plan->converted;
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

void Application::toggleHideTags()
{
    m_settings->set("grid.hideTags", !m_shell->hideTags());
    m_shell->setHideTags(m_settings->boolean("grid.hideTags"), m_settings->text("grid.tagsSwapCharacter"));
}

bool Application::deleteText()
{
    auto *session = targetSession();
    if (!session)
        return false;
    const bool done = application::deleteText(*session, shownLines()).has_value();
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
            {QStringLiteral("templateEditor"), QVariant::fromValue(m_templateEditor.get())},
            {QStringLiteral("automationDialogs"), QVariant::fromValue(m_automation->dialogs())},
            {QStringLiteral("automationPicker"), QVariant::fromValue(m_automation->picker())},
            {QStringLiteral("automationResolution"), QVariant::fromValue(m_automation->resolutionPrompt())},
            {QStringLiteral("log"), QVariant::fromValue(m_log.get())},
            {QStringLiteral("tagButtons"), QVariant::fromValue(m_tagButtons.get())},
            {QStringLiteral("colourPicker"), QVariant::fromValue(m_colourPicker.get())},
            {QStringLiteral("workspaceLayout"), QVariant::fromValue(m_workspaceLayout.get())},
            {QStringLiteral("shiftTimes"), QVariant::fromValue(m_shiftTimes.get())},
            {QStringLiteral("gridFilter"), QVariant::fromValue(m_gridFilter.get())},
            {QStringLiteral("visualTools"), QVariant::fromValue(m_visualTools.get())},
            {QStringLiteral("videoView"), QVariant::fromValue(m_videoView.get())},
            {QStringLiteral("statusBar"), QVariant::fromValue(m_statusBar.get())},
            {QStringLiteral("videoFullscreen"), QVariant::fromValue(m_videoFullscreen.get())},
            {QStringLiteral("automationHotkeys"), QVariant::fromValue(static_cast<QObject *>(m_automationHotkeys.get()))},
            {QStringLiteral("settingsImport"), QVariant::fromValue(static_cast<QObject *>(m_settingsImport.get()))},
            {QStringLiteral("hotkeys"), QVariant::fromValue(static_cast<QObject *>(m_hotkeys.get()))},
            {QStringLiteral("updates"), QVariant::fromValue(static_cast<QObject *>(m_updates.get()))},
            {QStringLiteral("whatsNew"), QVariant::fromValue(static_cast<QObject *>(m_whatsNew.get()))},
            {QStringLiteral("analytics"), QVariant::fromValue(static_cast<QObject *>(m_analytics.get()))},
            {QStringLiteral("crashReports"), QVariant::fromValue(static_cast<QObject *>(m_crashReports.get()))},
            {QStringLiteral("styleManager"), QVariant::fromValue(static_cast<QObject *>(m_styleManager.get()))},
            {QStringLiteral("kanjiTimer"), QVariant::fromValue(static_cast<QObject *>(m_kanjiTimer.get()))},
            {QStringLiteral("fontCollector"), QVariant::fromValue(static_cast<QObject *>(m_fontCollector.get()))},
            {QStringLiteral("fontCatalogs"), QVariant::fromValue(static_cast<QObject *>(m_fontCatalogs.get()))},
            {QStringLiteral("matroska"), QVariant::fromValue(static_cast<QObject *>(m_matroska.get()))},
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
    if (id == QLatin1String("audio.outputHostApi") && m_audioPlayer)
        m_audioPlayer->reopenOutput();
    if (id == QLatin1String("audio.outputDevice") && m_audioRouting)
        m_audioRouting->refresh();
    if (id == QLatin1String("video.playbackPlayer"))
        choosePlayer();
    if (id == QLatin1String("subtitles.saveWithVideoName"))
        emit saveWithVideoNameChanged();
    else if (id == QLatin1String("video.dontAskForBadResolution"))
        emit askForBadResolutionChanged();
    else if (id.startsWith(QLatin1String("convert.")))
        m_conversionPlan.reset();
    else if (id == QLatin1String("autosave.maxFiles") && !m_recoveryDir.isEmpty() && m_recovery)
        m_recovery->setCapacity(m_settings->integer("autosave.maxFiles"));
    else if (id == QLatin1String("grid.hideColumns") && !m_resettingSettings)
        m_shell->setHiddenColumns(m_settings->integer("grid.hideColumns"));
    else if (id == QLatin1String("grid.autoHideColumns") && !m_resettingSettings)
        m_shell->setAutoHideColumns(m_settings->boolean("grid.autoHideColumns"));
    else if (id == QLatin1String("grid.foldGenerated") && !m_resettingSettings)
        m_shell->setFoldGenerated(m_settings->boolean("grid.foldGenerated"));
    else if (id == QLatin1String("video.disableLiveEditing") || id.startsWith(QLatin1String("grid.calcSpaces"))) {
        m_editor->optionsChanged();
        refreshVideo();
    }
    else if (id == QLatin1String("editor.hideTags") && !m_resettingSettings)
        m_editor->setShowTags(!m_settings->boolean("editor.hideTags"));
    else if (id == QLatin1String("editor.timesToFramesSwitch") && !m_resettingSettings)
        m_editor->setShowFrames(m_settings->boolean("editor.timesToFramesSwitch"));
    else if (id == QLatin1String("grid.hideTags") || id == QLatin1String("grid.tagsSwapCharacter"))
        m_shell->setHideTags(m_settings->boolean("grid.hideTags"), m_settings->text("grid.tagsSwapCharacter"));
    else if (id == QLatin1String("program.language"))
        switchLanguage();
    else if (id == QLatin1String("program.font") || id == QLatin1String("program.fontSize"))
        applyProgramFont();
    else if (id == QLatin1String("automation.traceLevel"))
        m_automation->manager().setTraceLevel(m_settings->integer("automation.traceLevel"));
    else if (id == QLatin1String("video.subtitleProvider") &&
             m_renderer.select(m_settings->text("video.subtitleProvider").toStdString())) {
        m_fontCollector->setDisplayRenderer(QString::fromStdString(m_renderer.provider()));
        m_videoRevision.reset();
        m_videoScript.clear();
        refreshVideo();
        applySubtitlesAt();
    } else if (id == QLatin1String("video.previewSubtitlesAt")) {
        applySubtitlesAt();
    } else if (id == QLatin1String("video.renderCost")) {
        m_renderer.countCost(m_settings->boolean("video.renderCost"));
        m_videoRevision.reset();
        m_videoScript.clear();
        refreshVideo();
    }
}

void Application::applySubtitlesAt()
{
    const bool screen = m_settings->integer("video.previewSubtitlesAt") == 1 && m_renderer.usesLibass();
    m_video->setSubtitlesAtScreenSize(screen);
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
                                                                        "Put files .dic and .aff to \"Dictionary\" folder").toStdString());
    }
    lists.catalogs = stdList(m_styleManager->catalogs());
    lists.currentCatalog = m_styleManager->catalog().toStdString();
    if (const auto load = application::optionsCatalogToLoad(m_settings->settings(), lists)) {
        m_styleManager->loadCatalog(QString::fromStdString(*load));
        lists.currentCatalog = *load;
    }
    lists.styles = stdList(m_styleManager->storeStyles());
    lists.renderers = m_renderer.providers();
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
    const auto missing = [](const char *what) {
        return QCoreApplication::translate("HikariSub.Options",
                                           "The selected %1 for conversion does not exist\n"
                                           "and will be changed to the default")
            .arg(QCoreApplication::translate("HikariSub.Options", what));
    };
    if (open.catalogMissing)
        warnings << missing(QT_TRANSLATE_NOOP("HikariSub.Options", "catalog for style"));
    if (open.styleMissing)
        warnings << missing(QT_TRANSLATE_NOOP("HikariSub.Options", "style"));
    auto values = toVariant(open.state);
    addAppearance(values);
    values.insert(QStringLiteral("notices.hiddenQuestions"), m_settings->list("notices.hiddenQuestions"));
    values.insert(QStringLiteral("automation.dialogAnswers"), m_settings->list("automation.dialogAnswers"));
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
    if (m_audioPlayer->playing())
        m_audioPlayer->stop();
    m_audioPlayer->setOutputDevice(choice.kind == application::OutputDeviceChoice::Kind::SystemDefault ? std::string()
                                                                                                        : choice.name);
    m_audioPlayer->switchOutput();
    const std::weak_ptr<const application::DisplayAudio> played = tone;
    m_audioPlayer->playClip(std::move(tone));
    m_audioPlayer->setOutputDevice(m_noticedRoute.systemDefault ? std::string() : m_noticedRoute.name);
    QTimer::singleShot(frames * 1000 / rate + 200, this, [this, test = ++m_testSounds, played] {
        if (test != m_testSounds)
            return;
        if (const auto clip = played.lock(); clip && m_audioPlayer->playingClip(clip.get()))
            m_audioPlayer->stop();
        m_audioPlayer->switchOutput();
    });
}

void Application::destroyDialogs()
{
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
                                       "automation.dialogAnswers", "automation.dialogPresets"};
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
    m_copyColumns = m_settings->integer("grid.copyColumns");
    m_pasteColumns = m_settings->integer("grid.pasteColumns");
    m_gridFilter->settingsReset();
    m_updates->reload();
    m_shell->setAutoHideColumns(m_settings->boolean("grid.autoHideColumns"));
    m_shell->setFoldGenerated(m_settings->boolean("grid.foldGenerated"));
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
