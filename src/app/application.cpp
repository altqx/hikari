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
#include "spelling_text.h"
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
#include "theme.h"
#include "window_frame.h"

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
    // The open tabs, as legacy's Notebook pages: a tab shown as the reference
    // (Ctrl+Q) is searched, as legacy searched the previewed tab, but its
    // session refuses the replacements; a reference that is not a tab is not
    // an open subtitle.
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
    // HikariMessageBox / HikariMessageDialog without a nested event loop: the
    // question is shown (findQuestion: a message box; a message is a
    // notice) and the engine resumes when it is answered
    // (answerFindQuestion); a message needs no answer. Tests answer at once.
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
            reply(answer ? 3 : 0); // nobody to ask: Cancel
            return;
        }
        // NT1 (#302): a message is a notice titled with the command that
        // gave it; an invalid path is a warning (PU2-find-questions).
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
    // PU2 (#304): the message box's title (the command), main line, detail
    // and severity, by the message-box rules (visual-language.md "Popups";
    // the PU2-find-* departures name the legacy texts). A message (no
    // answer) is a notice: its title and main line.
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
            // “A”, “B” or “C”.
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
    // The legacy log lines, translated (the engine's English is the source).
    // NT1: each is a notice of Find and replace, an error when the
    // expression does not compile.
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
        else if (role == 2 || role == 3)
            m_app.m_editor->selectInBox(role, start, end); // E4: ActorEdit/EffectEdit->SetTextSelection
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
            if (const auto id = m_app.open(name)) {
                // With no Document open (SH2: from the Home page) it became
                // the editing target, which showLine then finds unchanged.
                if (!target)
                    m_app.refreshViews();
                return done(id);
            }
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
    // O5: legacy LoadOptions() returns 2 when there is no settings file yet;
    // decided before anything here may write one.
    const bool firstStart = !options.settingsFile.isEmpty() && !QFileInfo::exists(options.settingsFile);
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
    // W2: CSRI renderers come from the program's Csri folder (legacy
    // csrilib_os_init); their messages go to the log silently (HikariLogSilent).
    m_renderer.setCsriFolder(std::filesystem::path(QCoreApplication::applicationDirPath().toStdU16String()) / u"Csri");
    m_renderer.setLog([this](const std::string &text) { m_log->log(QString::fromStdString(text)); });
    m_videoSource = std::make_unique<application::DummyVideoSource>(*m_mediaSource);
    m_video = std::make_unique<ui::VideoController>(*m_videoSource, m_renderer);
    m_video->setMediaInfo(m_mediaSource.get(), m_mediaSource.get()); // V3: track names, chapters
    // V1: playback through the general player; its frames are shown with the
    // overlay, and a pause hands back to the exact indexed frame.
    m_generalPlayer = std::make_unique<backends::QtGeneralPlayer>(options.playbackAudio);
    m_video->session().setGeneralPlayer(m_generalPlayer.get());
    // Its frames are made BGRA (or kept as Y'CbCr planes when the presenter
    // converts them on the GPU) and their overlay rendered on the session's
    // worker; the results come back to this thread, the newest only.
    m_video->session().setFrameWorker([this](std::function<void()> task) {
        QMetaObject::invokeMethod(this, std::move(task), Qt::QueuedConnection);
    });
    connect(m_generalPlayer.get(), &backends::QtGeneralPlayer::frameDelivered, this, [this](const QVideoFrame &frame) {
        const qint64 end = frame.endTime();
        // The paused frame's matrix and range (V4's), so playing does not change the colours.
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
    // KE2: the Template editor edits the editing target; its preview runs on
    // the shell's templater and shows on the video.
    m_templateEditor = std::make_unique<ui::TemplateEditorController>();
    // A reload is new content: the user's trust in the old one does not
    // carry over (KE2-preview-trust).
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
    // The video, keyframes, audio selection and frame shown a preview read:
    // a change of them, once paused, previews again.
    m_templateEditor->setHostState([this] { return m_automation->hostState(); });
    connect(m_video.get(), &ui::VideoController::changed, this, [this] {
        if (!m_video->playing())
            m_templateEditor->hostChanged();
    });
    // A shortcut's or the menu's macro starts from the template just typed.
    m_automation->setBeforeRun([this] { m_templateEditor->commit(); });
    // A save writes the template just typed (Save all sees the file it made
    // dirty too).
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
    // A new frame redraws the preview only when its span changes (stepping
    // frame by frame; while the Line loops the span stays).
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
    // The report is the run's Document's: another tab chosen meanwhile
    // keeps its own, and the run's shows it once chosen again.
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
    rememberStartSession(); // PU1: before anything writes the session
    // R6-dictionary-location: user dictionaries in the user folder's
    // dictionaries (UD1; else the settings folder's Dictionary), bundled ones
    // in the program folder's (legacy's only one).
    m_dictionaryDir = options.dictionaryDir;
    if (m_dictionaryDir.isEmpty() && !m_settingsFile.isEmpty())
        m_dictionaryDir = QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/Dictionary");
    if (!m_dictionaryDir.isEmpty())
        m_dictionaryDirs << m_dictionaryDir;
    if (const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/Dictionary");
        !m_dictionaryDirs.contains(bundled))
        m_dictionaryDirs << bundled;
    // O1: the settings registry over the INI file (in memory without one).
    m_settings = std::make_unique<ui::SettingsStore>(m_settingsFile);
    // O3: an import activated in an earlier session takes effect now, before
    // anything reads the profile.
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
        // W2: a renderer name that is not installed here is not proposed.
        m_importStore->rendererInstalled = [this](std::string_view name) {
            return std::ranges::contains(m_renderer.providers(), name);
        };
        m_importStore->recover();
    }
    m_settingsImport = std::make_unique<SettingsImportController>(m_importStore.get());
    // O5: the first start on a Polish system takes Polish for the interface
    // and the spell checker, before anything reads either
    // (hikarisubApp.cpp:319-325).
    if (firstStart && Localisation::firstStartLanguage(options.systemUiLanguages) == u"pl") {
        m_settings->set("program.language", QStringLiteral("pl"));
        m_settings->set("editor.dictionaryLanguage", QStringLiteral("pl"));
    }
    // FR1: Getting started opens at the first start, and at the next one
    // when the program was left before it was answered.
    if (firstStart && options.gettingStarted)
        m_settings->set("workspace.gettingStarted", QStringLiteral("due"));
    connect(m_settings.get(), &ui::SettingsStore::changed, this, &Application::settingChanged);
    // #383: scripts load with the profile's trace level, so they load after it.
    m_automation->manager().setTraceLevel(m_settings->integer("automation.traceLevel"));
    if (options.autoload)
        m_automation->autoload();
    m_renderer.select(m_settings->text("video.subtitleProvider").toStdString()); // W2
    m_renderer.countCost(m_settings->boolean("video.renderCost")); // KE1-render-cost-overlay
    // K2: the theme layer follows this profile's appearance (the controls'
    // palette, the icons and the owner-drawn items).
    ui::theme::useSettings(m_settings.get());
    m_tagButtons = std::make_unique<ui::TagButtonsController>(*m_settings);
    m_colourPicker = std::make_unique<ui::ColourPickerController>(*m_settings);
    m_shiftTimes = std::make_unique<ui::ShiftTimesController>(*m_settings);
    // E4: the options the Line editor reads, and its Times/Frames switch
    // (EDITBOX_TIMES_TO_FRAMES_SWITCH, saved when it is switched; LE2: from
    // the Video panel's menu or Settings > Editor).
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
    // LE3-tags-shown: the editor's hidden-tag view as stored (shown by
    // default), saved when it is switched (EDITBOX_HIDE_TAGS, Settings > Editor).
    m_editor->setShowTagsSetting(!m_settings->boolean("editor.hideTags"),
                                 [this](bool show) { m_settings->set("editor.hideTags", !show); });
    connect(m_editor.get(), &ui::LineEditorController::frameDisplayChanged, this, [this] {
        const auto *frames = m_editor->frameTimebase();
        m_shell->setFrameTimebase(frames ? std::optional(*frames) : std::nullopt);
    });
    m_selectOptions = m_settings->integer("selectLines.options");
    // Legacy keeps 20 when the dialog opens.
    m_selectRecent = m_settings->list("selectLines.recentSelections").mid(0, 20);
    // D1: the panel layout beside the settings (none without a settings
    // file); UD1: the saved layouts in the presets folder.
    m_workspaceLayout = std::make_unique<ui::WorkspaceLayoutController>(
        m_settingsFile.isEmpty() ? QString() : QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/layout.json"),
        options.presetsDir.isEmpty() ? QString() : options.presetsDir + QStringLiteral("/layouts.json"));
    m_gridFilter = std::make_unique<ui::GridFilterController>(*m_settings);
    // T1: the visual tools edit the editing target, never the reference.
    m_visualTools = std::make_unique<ui::VisualToolsController>(*m_video, *m_settings, [this] { return targetSession(); });
    // T5: the drawing's shape presets beside the settings (legacy
    // Config/ShapesSettings.txt); none without a settings file.
    if (!m_settingsFile.isEmpty())
        m_visualTools->setShapesFile(QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/ShapesSettings.txt"));
    // T6: the all-tags tool's definitions beside them (legacy
    // Config/AllTagsSettings.txt); the shifter and the all-tags tool edit
    // only the selected Lines the Grid shows (SubsFile::GetSelections), and
    // the all-tags tool takes the Line editor's "Insert difference" keys.
    if (!m_settingsFile.isEmpty())
        m_visualTools->setAllTagsFile(QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/AllTagsSettings.txt"));
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
    // T2: the video shows a gesture's staged texts (legacy's dummy rendering),
    // the tools log as legacy's HikariLog did, and they read the Grid's
    // "Ignore filtering in some actions" (SubsGrid::ignoreFiltered).
    m_visualTools->setPreview([this](const core::Document *preview) {
        auto *session = targetSession();
        if (!session)
            return;
        setVideoFonts(m_workspace.editingTarget()); // 38c
        m_video->session().setSubtitles(rendererScript(preview ? *preview : session->document())); // E5
        m_videoScript.clear(); // E4: the next refresh shows the draft again
    });
    // NT1: a gesture the tool refused, titled with the tool's name.
    m_visualTools->setLog([this](const QString &text) { m_log->notice(ui::LogController::Warning, m_visualTools->toolName(), text); });
    m_visualTools->setIgnoreFiltered([this] { return m_gridFilter->ignoreInActions(); });
    // T3: the Line editor's caret in the text the tools edit (the
    // translation in TLMode unless it is empty), for FindTag's mode 0.
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
    // V4: zoom, aspect, volume, the context menu and the snapshots; the
    // general player takes the video volume.
    m_videoView = std::make_unique<ui::VideoViewController>(*m_video, *m_visualTools, *m_settings);
    m_videoView->setPlayer(m_generalPlayer.get());
    m_videoFullscreen = std::make_unique<ui::VideoFullscreenController>(*m_video, *m_videoView, *m_settings); // V5, FS1
    // W1: the player video.playbackPlayer chooses (the general player but on
    // Windows with DirectShow chosen).
    choosePlayer();
    connect(m_video.get(), &ui::VideoController::changed, this, [this] {
        if (directShowPlayback()) // legacy built the DirectShow graph as the video loaded
            m_video->session().preparePlayer();
    });
    // HikariLog(_("Cannot change YCbCr matrix")) (ProviderFFMS2.cpp:402, 408, 979).
    m_video->session().setLog([this](const std::string &) {
        m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Options", "Video"),
                      QCoreApplication::translate("HikariSub.Video", "Cannot change YCbCr matrix"));
    });
    setUpTranslationControls(); // E5
    setUpFileArrangements();    // WS2
    // P10: the status bar's fields from the video, its view and the editing target.
    m_statusBar = std::make_unique<ui::StatusBarController>(*m_video, *m_visualTools, [this]() -> const core::Document * {
        auto *session = targetSession();
        return session ? &session->document() : nullptr;
    });
    // F1: find and replace. Its options (FIND_REPLACE_OPTIONS,
    // FIND_REPLACE_STYLES) are read from the registry when the tool shows a
    // tab; its recent lists when the tool is first opened (openFindReplace).
    m_findHost = std::make_unique<FindHost>(*this);
    createFindReplace();
    if (!m_settingsFile.isEmpty())
        m_replaceBackup = QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/ReplaceBackup");
    // S4: validation errors go to the Log (NT1: a menu run's as an error
    // toast); the script editor setting.
    m_automation->setLog([this](const QString &line, AutomationShell::LogLine kind) {
        const QString title = QCoreApplication::translate("HikariSub.Automation", "Automation");
        switch (kind) {
        case AutomationShell::LogLine::Notice: m_log->notice(ui::LogController::Error, title, line); break;
        case AutomationShell::LogLine::Quiet: m_log->log(line); break;
        case AutomationShell::LogLine::QuietError: m_log->log(line, ui::LogController::Error, title); break;
        }
    });
    m_automation->setSettings(m_settings.get());
    m_automationHotkeys = std::make_unique<AutomationHotkeysController>(*m_automation, *m_settings);
    // O2: legacy LoadHkeys at startup, then SetAccels.
    m_hotkeys = std::make_unique<HotkeysController>(*m_automationHotkeys, *m_settings,
                                                    [this](const QString &message, bool warning) {
                                                        m_log->notice(warning ? ui::LogController::Warning : ui::LogController::Info,
                                                                      QCoreApplication::translate("HikariSub.Options", "Hotkeys"), message);
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
            return id == m_workspace.reference() ? nullptr : m_files->session(id); // R2-reference-tray
        };
        hooks.documents = [this] {
            // Every Document but the reference, which is never written (R2-reference-tray).
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
        hooks.fonts = [this] { // Y6: the external fonts render in the preview; 38c: the script's attached fonts too
            return subtitleFonts(m_workspace.editingTarget());
        };
        hooks.lastCatalog = [this] { return m_settings->text("styles.lastCatalog"); };
        hooks.catalogChosen = [this](const QString &name) { m_settings->set("styles.lastCatalog", name); };
        m_styleManager = std::make_unique<StyleManagerController>(std::filesystem::path(catalogDir.toStdU16String()), std::move(hooks));
    }
    {
        // KA3: the Kanji timer window over the editing target.
        KanjiTimerController::Hooks hooks;
        hooks.target = [this] { return targetSession(); };
        hooks.targetId = [this] { return m_workspace.editingTarget() ? m_workspace.editingTarget()->value : 0; };
        hooks.refresh = [this] {
            m_editor->reloadFromSession();
            refreshViews();
        };
        // KA4: MeCab over UniDic, opened when Furigana is first on
        hooks.dictionary = [folder = readingDictionaryPath(options.readingDictionaryDir)]() -> application::ReadingAnalyzer {
            std::shared_ptr<const backends::MecabReadings> readings =
                backends::MecabReadings::open(std::filesystem::path(folder.toStdU16String()));
            return readings ? backends::MecabReadings::analyzer(std::move(readings)) : application::ReadingAnalyzer{};
        };
        m_kanjiTimer = std::make_unique<KanjiTimerController>(*m_settings, std::move(hooks));
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
                if (const auto media = m_tabMedia.find(id.value); media != m_tabMedia.end())
                    tab.video = media->second.video; // Y9: the tab's VideoPath
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
        m_fontCollector->setDisplayRenderer(QString::fromStdString(m_renderer.provider())); // W2
        // Y9: "Demux fonts from loaded MKV file" reads attachments through a
        // media helper of its own.
        m_fontCollector->setMatroskaPort(
            std::make_unique<backends::FfmsMatroska>(mediaHelperPath(options.mediaHelper)));
    }
    {
        // Y9: GRID_SUBS_FROM_MKV on the editing target's tab video.
        MatroskaController::Hooks hooks;
        hooks.videoPath = [this] { return targetVideoPath(); };
        hooks.modified = [this] {
            auto *session = targetSession();
            return session && session->isDirty();
        };
        // File > Open on an MKV file (HikariSubFrame.cpp:1021-1025): a tab
        // with subtitles of a file, or loaded from an MKV (OnMkvSubs' SubsPath)
        // or from lyrics (KA6), gets a new tab.
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
        // Y6: Font catalogs (legacy Config/FontCatalogs.txt) and the font
        // lists of the font dialog, the Style editor and the catalog window.
        QString dir = options.fontCatalogDir;
        if (dir.isEmpty() && !m_settingsFile.isEmpty())
            dir = QFileInfo(m_settingsFile).absolutePath();
        if (dir.isEmpty()) {
            m_fontCatalogTemp = std::make_unique<QTemporaryDir>();
            dir = m_fontCatalogTemp->path();
        }
        FontCatalogsController::Hooks hooks;
        // CollectFontsFromSubtitles: every tab from the first, or the active one.
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
        // FontEnumerator's RefreshVideo(true): libass's fonts are loaded
        // again (ReloadLibraries, so a font installed meanwhile is found), and
        // the video's subtitles render again with the external fonts.
        hooks.fontsChanged = [this] {
            application::reloadSubtitleFonts();
            if (!m_fontCatalogs)
                return;
            setVideoFonts(m_videoDocument); // 38c: with the attached fonts first
            if (m_videoDocument) {
                m_videoRevision.reset();
                m_videoScript.clear(); // the same script goes with the new fonts
                refreshVideo();
            }
        };
        m_fontCatalogs = std::make_unique<FontCatalogsController>(*m_settings, std::filesystem::path(dir.toStdU16String()),
                                                                  std::move(hooks));
        setVideoFonts(std::nullopt);
    }
    applySubtitlesAt(); // SV1
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
    setUpGrammar(options); // GR1 (#364)
    m_spellingLoader = options.spellingBackend;
    m_shell->setSpelling([this](std::u16string_view text, core::SubtitleFormat format, bool spell, int replaceTagsLen) {
        application::SpellChecker *checker = m_spellingStarted ? m_spellChecker.get() : nullptr;
        return core::legacy::checkTextAndBrackets(text, format, m_spellingText.segment,
                                                  spell && m_settings->boolean("editor.spellchecker") && checker ? checker->wordCheck()
                                                                                   : core::legacy::WordCheck{},
                                                  replaceTagsLen);
    });
    // E6: the changed-Line mark of each Grid's Document, and GRID_HIDE_TAGS
    // with GRID_TAGS_SWAP_CHARACTER (SubsGrid.cpp:60, SubsGridWindow.cpp:249).
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
    // E6: TEXT_EDITOR_TAG_LIST_OPTIONS, read when a tag list opens and
    // written by its menu.
    m_editor->tagList()->setOptionsStore([this] { return m_settings->integer("textEditor.tagListOptions"); },
                                         [this](int options) { m_settings->set("textEditor.tagListOptions", options); });
    // GRID_HIDE_COLUMNS (G7).
    m_shell->setHiddenColumns(m_settings->integer("grid.hideColumns"));
    connect(m_shell.get(), &ui::ShellController::hiddenColumnsChanged, this,
            [this] { m_settings->set("grid.hideColumns", m_shell->hiddenColumns()); });
    // GR3: the Grid menu's and Settings' "Hide empty columns", one setting.
    m_shell->setAutoHideColumns(m_settings->boolean("grid.autoHideColumns"));
    connect(m_shell.get(), &ui::ShellController::autoHideColumnsChanged, this, [this] {
        // Only a change: Set default's re-sync leaves the key unset.
        if (m_settings->boolean("grid.autoHideColumns") != m_shell->autoHideColumns())
            m_settings->set("grid.autoHideColumns", m_shell->autoHideColumns());
    });
    // KP1: the Grid's Fold generated lines.
    m_shell->setFoldGenerated(m_settings->boolean("grid.foldGenerated"));
    connect(m_shell.get(), &ui::ShellController::foldGeneratedChanged, this, [this] {
        selectFoldedRunsWhole();
        if (m_settings->boolean("grid.foldGenerated") != m_shell->foldGenerated())
            m_settings->set("grid.foldGenerated", m_shell->foldGenerated());
    });
    m_recent.set(m_settings->settings().list("recent.subtitles"));
    m_recent.setOpenedRecords(m_settings->settings().list("recent.subtitlesOpened")); // SH2
    m_copyColumns = m_settings->integer("grid.copyColumns");
    m_pasteColumns = m_settings->integer("grid.pasteColumns");
    // An edit refreshes the video once per event-loop turn, however many
    // changes it made (legacy EditBox::OnEdit's OpenSubsLater, a CallAfter
    // flushed by VideoBox::FlushLater, VideoBox.cpp:1861-1890): keystrokes
    // queued behind a slow parse are shown together, by one parse.
    m_editRefresh = new QTimer(this);
    m_editRefresh->setSingleShot(true);
    m_editRefresh->setInterval(0);
    connect(m_editRefresh, &QTimer::timeout, this, &Application::refreshVideo);
    connect(m_editor.get(), &ui::LineEditorController::changed, this, [this] {
        // GR2: the Grid row shows the draft as it is typed, uncommitted.
        if (const auto *session = targetSession())
            m_shell->setDraft(session->draftRecord());
        m_editRefresh->start();
        scheduleAutosave();
        if (m_templateEditor && m_templateEditor->shown())
            m_templateEditor->refresh(); // KE2: Apply runs on the draft, so the preview does
    });
    // E4: the editor's time fields show frames of the open video (TimeCtrl::SetVideoBox).
    connect(m_video.get(), &ui::VideoController::changed, this, [this] {
        const auto &video = m_video->session();
        const bool ready = video.state() == application::VideoSession::State::Ready;
        const QString path = ready ? QString::fromStdString(video.path()) : QString();
        if (path == m_editorTimebaseVideo)
            return;
        m_editorTimebaseVideo = path;
        m_editor->setVideoTimebase(ready ? std::optional(video.legacyTimebase()) : std::nullopt);
    });
    // A video that cannot be opened is reported once, a notice (V3: with
    // legacy ProviderFFMS2's messages).
    connect(m_video.get(), &ui::VideoController::changed, this, &Application::logVideoFailure);
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
            m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.MainWindow", "Open keyframes"),
                          problem);
    });
    // The editor moved the active Line itself (Enter, Ctrl+D, Undo): a plain selection there.
    connect(m_editor.get(), &ui::LineEditorController::leftLineCommitted, this,
            [this](qulonglong id) { m_leftEditedLine = core::LineId{id}; });
    connect(m_editor.get(), &ui::LineEditorController::lineChanged, this, [this](qulonglong id) {
        const auto target = m_workspace.editingTarget();
        auto *session = target ? m_files->session(*target) : nullptr;
        if (!session)
            return;
        session->setSelection(gridSelection().plain(session->selection(), core::LineId{id}));
        m_shell->setSelection(session->selection());
        m_editor->selectionChanged(); // LE4: no longer mixed
        followEditingLine(); // R2: the editor's own move (E4 sends the draft before it)
        if (m_templateEditor)
            m_templateEditor->refresh(); // KE2: the preview follows the active karaoke Line
    });
    // The editor's Start/End difference measures from the frame the Video
    // panel reports: the player's while playing (legacy EditBox::
    // OnPasteDifferents read VideoBox::Tell).
    m_editor->setVideoTimeSource([this]() -> std::optional<std::int64_t> {
        const auto &session = m_video->session();
        const auto frame = session.reportedFrame();
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
                                      [this](const QString &message) {
                                          m_log->notice(ui::LogController::Error,
                                                        QCoreApplication::translate("HikariSub.Options", "Audio"), message);
                                      });
    }
    // H2 (decision 60): both audio paths follow the system's output devices:
    // the chosen device (audio.outputDevice) while it is connected, else the
    // default. A change of the route moves them at once, mid-playback too.
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
    m_recentAudio.set(m_settings->settings().list("recent.audio")); // AUDIO_RECENT_FILES
    m_audioConnections << connect(m_audio.get(), &ui::AudioController::opened, this,
                                  [this](const QString &path, bool) { rememberRecentAudio(path); });
    m_audioConnections << connect(m_audio.get(), &ui::AudioController::logged, this, [this](const QString &message, bool debug, bool error) {
        if (debug)
            qDebug().noquote() << message; // legacy HikariLogDebug: debug builds only
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
            m_templateEditor->hostChanged(); // KE2: an audio box opened or closed
    });
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
    // KA2-tap-to-time: the tap bar's next Line (keys as refreshAudio gives them)
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
    m_audio->setFollowAudio([this](int ms) { showVideoAtAudio(ms); }); // AV2
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
    // SH2: media opened while the Home page shows (no Document open) opens
    // in a new Untitled Document, as legacy's always-present tab held it.
    const auto documentForMedia = [this] {
        if (home())
            newDocument();
    };
    m_video->setBeforeOpen(documentForMedia);
    m_audio->setBeforeOpen(documentForMedia);
    trackTabMedia(); // P6
    trackVideoSources(); // V3
    trackVideoFollow(); // V6
    startLocalisation(); // O5
    setupTabMenu(options); // P9
    reportUserFolder(options.userFolderMove, options.userFolderSeed); // UD1
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

// A1: legacy AudioDisplay::SetDialogue runs when the active Line changes or
// the editor commits (EditBox::SetLine, Send); every redraw shades the other
// Lines. The controller redraws only when what it shows changed.
// H2: general playback takes the route's device by its id (Qt's own), the
// editor output by its name in PortAudio's list, or PortAudio's default while
// the route follows the system default.
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
    } else if (state == application::VideoSession::State::Closed) {
        // AV3: a dummy video opens at once (never seen Opening), so a new
        // open of it is known by the close before it
        m_audioFollowedVideo.clear();
    }
    if (state == application::VideoSession::State::Ready && m_audioFollowedVideo != QString::fromStdString(video.path())) {
        // an indexed video brings its track into the box, opened from the
        // index file the video wrote; a video without audio closes the box
        m_audioFollowedVideo = QString::fromStdString(video.path());
        if (keepTabAudio(m_audioFollowedVideo)) {
            // P6: a restored tab's own audio file stays (legacy dontLoadAudio)
        } else if (video.dummy()) {
            // AV3 (#240, AV3-dummy-video-audio): a dummy video brings its own
            // silent audio of its length (legacy loaded the dummy without
            // changing the audio, Notebook::LoadVideo's dontLoadAudio).
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
        syncAudioWithVideo(); // AV2
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
    if (!video.playing() && !video.stopped())
        if (const auto frame = video.reportedFrame())
            paused = m_audioTimebase->msAt(*frame);
    m_audio->setVideoTime(paused);
    syncAudioWithVideo(); // AV2
}

void Application::rememberRecentAudio(const QString &path)
{
    if (application::AudioBox::isDummyName(path.toStdString()))
        return; // AV3: a dummy video's generated silence is no file to reopen
    m_recentAudio.add(path.toStdString());
    m_settings->settings().set("recent.audio", m_recentAudio.entries()); // SetRecent's SetTable
}

// A3: legacy AudioDisplay::CommitChanges' edit-box part.
bool Application::commitAudioTimes(const application::AudioCommitRequest &request)
{
    auto *session = targetSession();
    if (!session || !m_editor->editable())
        return false;
    const bool before = std::exchange(m_audioCommitting, true);
    // E4: StartEdit/EndEdit/DurEdit->SetTime(time, true): the warning colour
    // on each field whose time changes; Send (with `save`) clears it again.
    m_editor->timesFromAudio(request.startMs, request.endMs, request.duration);
    const auto outcome = application::commitAudioTimes(*session, request, shownLines());
    if (request.save)
        m_editor->audioSent();
    m_editor->reloadFromSession();
    if (outcome && outcome->stepped)
        refreshViews();
    m_audioCommitting = before;
    // SubsGrid::NextLine: the next Line, selected alone; its SetLine plays
    // the play-after choice (V6: autoPlay)
    if (outcome && outcome->next) {
        m_lineChangeOrigin = LineChangeOrigin{false, true};
        applySelection(gridSelection().plain(session->selection(), *outcome->next));
        m_lineChangeOrigin.reset();
    }
    return outcome.has_value();
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
    // VideoBox::Tell: the reported frame's time (the player's while
    // playing), 0 without video
    int time = 0;
    const auto &video = m_video->session();
    if (video.state() == application::VideoSession::State::Ready)
        if (const auto frame = video.reportedFrame())
            time = video.legacyTimebase().msAt(*frame);
    m_audio->showTime(time, mark);
}

// Legacy OpenAudioInTab with no path: the video's file (a provider of its own).
void Application::openAudioFromVideo()
{
    const auto &video = m_video->session();
    if (video.state() == application::VideoSession::State::Ready) {
        // AV3: a dummy video's audio is its silence, the video's own as when
        // it opened (no AudioPath of the tab's)
        if (const QString own = dummyVideoAudio(QString::fromStdString(video.path())); !own.isEmpty())
            return m_audio->openAudio(own);
        m_audio->openAudio(QString::fromStdString(video.path()));
        // P9: legacy AudioPath = VideoPath (OpenAudioInTab with no path).
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
    // legacy OnOpenAudio (HikariSubFrame.cpp:2177-2178): the video's folder,
    // else the latest recent video's (V3)
    QString from = QString::fromStdString(m_video->session().path());
    if (from.isEmpty() && !m_recentVideo.entries().empty())
        from = QString::fromStdString(m_recentVideo.entries().front());
    if (from.isEmpty() || application::isDummyVideo(from.toStdString()))
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
    m_video->setBeforeOpen(nullptr);
    m_audio.reset();
    m_audioRouting.reset(); // H2: no route change after this
    m_audioPlayer.reset(); // A4: the player stops before its output goes
    m_audioOutput.reset();
    m_audioSource.reset();
    m_port->waitIdle(); // no write may outlive the services it reports to
    saveMisspellRules();
    if (m_localisation)
        QGuiApplication::setFont(m_startFont); // O5: the program font was the application's
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
    if (!staged) {
        if (staged.error().lyrics)
            m_log->log(openProblem(path, staged.error())); // KA6: say what is wrong with the lyrics
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
        // A long lyrics line, cut short, never between a surrogate pair's halves.
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
    // KA6: imported lyrics are named, and saved, as ASS beside their file.
    const QFileInfo file(path);
    const QString saveAs = QDir::toNativeSeparators(file.absolutePath() + u'/' + file.completeBaseName() + QStringLiteral(".ass"));
    if (imported) {
        m_importPaths[id->value] = saveAs;
        m_lyricsImports.insert(id->value);
    }
    m_workspace.add(*id, (imported ? QFileInfo(saveAs).fileName() : file.fileName()).toStdString(), asReference);
    // WS2: the tab takes the arrangement its file keeps (a Session's, set
    // after this, takes precedence). A file opened as the reference keeps it
    // for when it becomes a tab (closeReference, promoteReference).
    takeFileArrangement(*id);
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
    ++m_openBatch; // P9: the association question (legacy OpenFile's LoadVideo prompt)
    if (emptyTab)
        replaceTarget(*id);
    offerAssociations(*id);
    showAssociationOffer();
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
    showTabArrangement(); // WS1: the tab's own panel arrangement
    // F3: legacy creates the spell checker when the Grid first paints a Line.
    if (m_workspace.editingTarget())
        spellChecker();
    reportGroupBreak();
    scheduleAutosave();
    if (m_styleManager) // SY1: an open Style manager follows
        m_styleManager->targetRefreshed();
    const auto target = m_workspace.editingTarget();
    const auto reference = m_workspace.reference();
    auto *targetSession = target ? m_files->session(*target) : nullptr;
    auto *referenceSession = reference ? m_files->session(*reference) : nullptr;
    refreshComparison(); // R1: an edited compared Document is compared again
    m_shell->setShowOriginal(target && showOriginal(*target), reference && showOriginal(*reference)); // E5
    m_shell->refresh(targetSession ? &targetSession->document() : nullptr,
                     referenceSession ? &referenceSession->document() : nullptr,
                     target ? m_comparison.table(*target) : nullptr,
                     reference ? m_comparison.table(*reference) : nullptr);
    if (targetSession) {
        m_shell->setDraft(targetSession->draftRecord()); // GR2
        m_shell->setSelection(targetSession->selection());
        m_editor->selectionChanged(); // LE4: the mixed position
    }
    if (referenceSession) // R2: the reference's own selection
        m_shell->setReferenceSelection(referenceSession->selection());
    followEditingLine(); // R2: a linked reference follows the active Line
    // The editor only ever edits the editing target, never the reference.
    if (m_templateEditor) {
        m_templateEditor->setEditable(target && m_workspace.checkContentCommand(*target).has_value());
        m_templateEditor->refresh();
    }
    if (target != m_editorDocument) {
        m_editorDocument = target;
        m_editor->setDocument(target, target && m_workspace.checkContentCommand(*target).has_value());
        // F1: legacy OnPageChanged calls FR->Reset(): the next search starts over.
        if (m_find)
            m_find->reset();
    }
    // TP1: the Styles panel, once the Line editor is on the target (the
    // panel picks the active Line's Style when its own is gone).
    if (m_styleManager)
        m_styleManager->documentEdited();
    if (m_kanjiTimer) // KA3
        m_kanjiTimer->documentEdited();
    refreshVideo();
    refreshAudio();
    if (m_statusBar)
        m_statusBar->refresh(); // P10
    emit tabsChanged(); // P6: titles, modified marks and the active tab
}

bool Application::showVideoSubtitles()
{
    const auto target = m_videoDocument;
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    // 38c: the script's attached fonts; a script that is the same as shown
    // is prepared again when they change
    if (setVideoFonts(target))
        m_videoScript.clear();
    bool edited = false;
    // E4: live video editing (EditBox::OnEdit's OpenSubsLater, connected
    // unless DISABLE_LIVE_VIDEO_EDITING, EditBox.cpp:349-352): the video
    // shows the pending draft; without it, the committed Document only.
    std::optional<core::Document> draft;
    if (!m_settings->boolean("video.disableLiveEditing") && m_editorDocument == target && m_video->hasVideo())
        draft = m_editor->draftDocument();
    // KE2: the Template editor's preview of the current karaoke Line, in
    // place of its generated Lines, with only the Lines over the frame.
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
            edited = m_videoRevision.has_value(); // V6: legacy SetModified / Undo
        m_videoRevision = session->revision();
        m_videoShowsDraft = draft.has_value();
        m_videoShowsTemplate = templated.has_value();
        // T4: with the visual tool's preview (its Lines, the vector clip's
        // mask); E5: as the renderer takes it (GetVisible).
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
            m_attachedFonts.erase(document->value); // closed
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
        // VT3: the Perspective tool does not measure a font only the renderer
        // sees. Each lease's names are read from its bytes once.
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
        return; // commitAndAdvance refreshes once the move is made
    if (m_editRefresh)
        m_editRefresh->stop(); // this refresh shows the edits it was to show
    const auto left = std::exchange(m_leftEditedLine, std::nullopt);
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (target != m_videoDocument) {
        const auto previous = m_videoDocument;
        leaveTabMedia(previous); // P6: the tab shown so far keeps its video position
        m_videoDocument = target;
        m_videoRevision.reset();
        m_videoScript.clear();
        m_videoLine.reset();
        // P9: the question asked when the target's subtitles opened (legacy
        // OpenFile's LoadVideo prompt), until it is answered; switching tabs
        // asks nothing new. Y9: subtitles loaded from the tab's video keep it
        // open.
        if (!m_keepVideoOnEnter) {
            m_video->resetForTarget();
            showAssociationOffer();
        }
        enterTabMedia(previous, target); // P6: the tab's own video, audio and keyframes
    }
    m_visualTools->refresh(); // T1: the script resolution, the format and the active Line
    if (!session)
        return;
    const bool edited = showVideoSubtitles();
    // V4: the Script properties YCbCr matrix on the video's colours.
    m_video->session().setMatrix(application::sessionVideoMatrix(*session));
#ifdef _WIN32
    if (m_directShow) // W1: legacy RendererDirectShow::SetColorSpace
        m_directShow->setColorSpace(m_video->session().matrix());
#endif
    const auto active = session->selection().active;
    // V2: the times field and the go-to commands follow the active Line.
    std::optional<std::pair<core::DocumentTime, core::DocumentTime>> lineTimes;
    if (active)
        for (const auto *line : session->document().lines())
            if (line->id == *active)
                lineTimes = std::pair(line->start.value, line->end.value);
    m_video->setActiveLineTimes(lineTimes);
    // V6: the video follows the active Line as legacy's MOVE_VIDEO_TO_ACTIVE_LINE
    // and VIDEO_PLAY_AFTER_SELECTION choices say (EditBox::SetLine), and an
    // edit as ShowEditOnVideo does; the Document's first sight does neither.
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

} // namespace

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
        m_templateEditor->commit(); // KE2: the template just typed is reviewed with the rest
    endFindOpen(false); // another review replaces the file result's
    const bool quitReplaced = m_closeThen == QLatin1String("quit");
    m_closeThen = then;
    if (quitReplaced)
        emit quitCancelled(); // WS5
    if (then == QLatin1String("quit"))
        ++m_quitReviews; // WS5
    m_closing.clear();
    if (then != QLatin1String("open")) {
        m_pendingOpen.reset();
        m_pendingOpenPath.clear();
        m_videoAfterOpen.clear(); // P9
        m_videoAfterOpenFullscreen = false;
        m_openFromVideo = false;
    }
    if (then != QLatin1String("files"))
        m_pendingFiles.reset(); // P9
    std::vector<application::DocumentId> scope;
    if (then != QLatin1String("tab"))
        m_closingTab.reset(); // P6
    m_closeTarget = m_workspace.editingTarget();
    if (then != QLatin1String("session"))
        m_pendingSession.reset(); // P6
    if (then == QLatin1String("quit") || then == QLatin1String("session")) // P6: a session replaces every tab
        scope = m_workspace.documents();
    else if (then == QLatin1String("tab")) // P6: a middle-clicked tab
        scope = m_closingTab ? std::vector{*m_closingTab} : std::vector<application::DocumentId>{};
    else if (then == QLatin1String("all")) // P9: Close all tabs
        scope = m_workspace.tabs();
    else if (const auto target = m_workspace.editingTarget())
        scope = {*target};
    QVariantList rows;
    for (const auto id : scope) {
        auto *session = m_files->session(id);
        // KA3-close-review: the Kanji timer's pairs not applied are unsaved work too
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
QString extensionFor(core::SubtitleFormat format); // with Save as's helpers below
} // namespace

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
        // KA3-close-review: Save writes the Kanji timer's pairs not applied first
        if (c.save && m_kanjiTimer && !m_kanjiTimer->applyTo(id.value, *session)) {
            m_closing.clear();
            emit closeFinished(false, QCoreApplication::translate("HikariSub.Documents",
                                                                  "A document could not be saved; nothing was closed."));
            return;
        }
        if (c.save) {
            QString path = row.value(QStringLiteral("path")).toString();
            // A file picked for an Untitled row gets its format's extension,
            // as Save as gives it (saveChosenFor).
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
    // E6: the saved Lines' marks turn saved (SubsGridBase.cpp:391).
    if ((result.outcome == application::WriteOutcome::Written ||
         result.outcome == application::WriteOutcome::DurabilityUncertain) &&
        (m_workspace.editingTarget() == result.document || m_workspace.reference() == result.document))
        refreshViews();
    // PU1-disk-change-bar: a published write, confirmed or not, moved the
    // Document to its file.
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
    // Save As of an Untitled or renamed Document: the title follows the file.
    if (result.outcome == application::WriteOutcome::Written) {
        rememberRecent(result.destination.value, false); // legacy SetRecent after a save
        if (auto *session = m_files->session(result.document); session && !session->isDirty())
            discardRecovery(result.document); // the work is in the file
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
    const auto reviewed = std::exchange(m_closeTarget, std::nullopt);
    m_closing.clear();
    m_closeThen.clear();
    if (!kept.isEmpty()) {
        // PU1-unsaved-window: the review shows again with the problem; a
        // retry finishes the same command on the same tab (Cancel ends it).
        m_closeThen = then;
        m_closeTarget = reviewed;
        if (then == QLatin1String("quit"))
            emit quitCancelled(); // WS5
        refreshViews();
        emit closeFinished(false, QCoreApplication::translate("HikariSub.Documents",
                                                              "Not closed, because saving failed or there is newer work: %1")
                                      .arg(kept.join(QStringLiteral(", "))));
        return;
    }
    // PU1-unsaved-window: one Document's question closes at its answer, so
    // another tab may be the target once the save is written; a command on
    // the target acts on the reviewed one's tab (gone: nothing is replaced).
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
            discardRecovery(id); // every Document reviewed
        m_quitApproved = true;
        emit quitApprovedChanged();
    } else if (then == QLatin1String("new")) {
        // P6: the Untitled Document takes the tab's place (legacy same tab).
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
            replaceTarget(*id); // P6: loaded into the same tab (legacy OpenFile)
            // P9: the folder's video and the Script Info associations are
            // offered (LoadVideo's loadPrompt), but not for subtitles found
            // beside a video, which opens after them.
            ++m_openBatch;
            if (!fromVideo) {
                offerAssociations(*id);
                showAssociationOffer();
            } else if (!videoAfter.isEmpty()) {
                // V5: OpenFile(path, fulls) reaches LoadVideo's SetFullscreen
                // only now; a cancelled review or a failed load never does.
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
        saveLastSession(); // legacy OpenFile ends with SaveLastSession
    } else if (then == QLatin1String("session")) {
        applySession(); // P6
    } else if (then == QLatin1String("tab")) {
        if (const auto tab = std::exchange(m_closingTab, std::nullopt))
            closeDocument(*tab); // P6
    } else if (then == QLatin1String("all")) {
        closeAllTabs(); // P9
    } else if (then == QLatin1String("files")) {
        if (auto files = std::exchange(m_pendingFiles, std::nullopt))
            applyFiles(std::move(*files), false); // P9
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
    const bool quit = m_closeThen == QLatin1String("quit");
    m_closeThen.clear();
    if (quit)
        emit quitCancelled(); // WS5
    m_pendingOpen.reset();
    m_pendingOpenPath.clear();
    m_pendingSession.reset(); // P6
    m_closingTab.reset();
    m_videoAfterOpen.clear(); // P9: OpenFile returned before LoadVideo
    m_videoAfterOpenFullscreen = false;
    m_openFromVideo = false;
    endFindOpen(false);
    // P9: a cancelled review of the first dropped subtitles skips them only
    // (OpenFile returned true; OpenFiles went on with the next).
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
    return saveRouteFor(0);
}

// P9: the Video a tab's Save looks at (legacy atab->VideoName): the shown
// video for the editing target, a tab's own otherwise.
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
    // Y9: subtitles loaded from an MKV are named after it (OnMkvSubs' SubsPath);
    // KA6: imported lyrics after their file.
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
    // Legacy EndsWith(ext), without the dot.
    const QString ext = extensionFor(session->document().format());
    if (!path.endsWith(ext))
        path += u'.' + ext;
    if (readOnly(path))
        return QStringLiteral("readonly");
    auto plan = m_files->prepareSave(*document, application::DestinationKey{QFileInfo(path).absoluteFilePath().toStdString()});
    if (!plan || !m_files->startSave(std::move(*plan)))
        return QStringLiteral("failed");
    m_formatChanged.erase(document->value); // legacy originalFormat = subsFormat
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
        return m_editor->save(); // the Line editor reports a refusal
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
    // Legacy SaveAll: Save(false, i, false) for every modified tab in order.
    m_templateEditor->commit(); // KE2: a template just typed makes its tab modified
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
    originalColumns(*m_workspace.editingTarget()).turnedOff(session->document()); // E5: showOriginal = false
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
    backends::desktop::openUrl(QUrl(QStringLiteral("https://github.com/altqx/hikari/issues")));
}

void Application::openWebsite()
{
    backends::desktop::openUrl(QUrl(QString::fromUtf8(application::kSiteUrl)));
}

void Application::openUserGuide(const QString &kind, const QString &id)
{
    using application::GuideSurface;
    // An unknown kind names no page: the guide's home.
    const std::string surface = kind == QLatin1String("panel") || kind == QLatin1String("window") ? id.toStdString() : "";
    const auto url = application::guideUrl(kind == QLatin1String("panel") ? GuideSurface::Panel : GuideSurface::Window,
                                           surface);
    backends::desktop::openUrl(QUrl(QString::fromStdString(url)));
}

QString Application::userFolder() const
{
    return QDir::toNativeSeparators(m_userFolder);
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
    // P9: OPEN_SUBS_IN_NEW_TAB (legacy OpenFile): with the option on and
    // subtitles of a file in the tab, they open in a new tab, unasked
    // (not for subtitles found beside a video, which load into the tab).
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

// Legacy IsMissingLocalFile with this platform's checks.
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

} // namespace

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
    // SH2: legacy AppendRecent dropped missing local files from the list
    // here; they stay for the Home page, which marks them and removes them.
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
    // Home shows native separators; the list keeps the path as it was added.
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

bool Application::acceptFileOnDisk(qulonglong id)
{
    const auto document = documentOf(id);
    if (!document || !m_files->acceptExternalChange(*document))
        return false;
    recordFileTime(*document); // the change accepted is the file as it is now
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
    // R1: a compared Document reloaded is compared again (the new session
    // starts at the same revision as the one it replaced).
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
    // Compared again (the bar stays): the earlier copy of the file goes, not
    // to a tab of its own, and the file is read as it is now.
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
        return false; // e.g. a draft that cannot be committed
    session->setSelection(std::move(next));
    m_shell->setSelection(session->selection());
    m_editor->selectionChanged(); // LE4: the mixed position
    if (m_templateEditor)
        m_templateEditor->refresh(); // KE2: the preview follows the active karaoke Line
    refreshVideo();
    followEditingLine(); // R2
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

// KP1 (#286): a run of generated Lines folded or unfolded from the Grid. A
// folded run is selected whole or not at all, as its first Line is: a run
// folded over the active Line leaves its first Line active and the run
// selected.
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

// KP1: the folded runs selected whole or not at all once folding turns on.
// Afterwards a selection that reaches a run in part unfolds it instead
// (ShellController::setSelection).
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
    // V6: legacy SubsGrid::OnMouseEvent (SubsGridWindow.cpp:1625-1649): a
    // press makes the Line active through SetLine(row, ..., nochangeline,
    // autoPlay = !ctrl) when GRID_CHANGE_ACTIVE_ON_SELECTION is on or Ctrl is
    // up; a double click does only with Ctrl (and then selects the Line alone).
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
    // A press on the active Line: SetLine's "goto done" still plays.
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
        // Legacy needs video for these: the reported frame's start and the
        // next one's (GetFrameTime: FrameShownAt(Tell()), the player's frame
        // while playing).
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

int Application::selectedLineCount(bool shownOnly) const
{
    auto *session = targetSession();
    if (!session)
        return 0;
    const auto &selected = session->selection().selected;
    if (!shownOnly)
        return int(selected.size());
    // Paste columns skips the selected lines the filter hides (Codex review).
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
    // The labels' source texts, translated below.
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
        // Legacy labels the end time "The starting time" too.
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
    const auto frame = video.reportedFrame(); // legacy Tell: the player's while playing
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
    // As Select lines on the Style field: the Grid's hidden Lines only with
    // "Ignore filtering in some actions".
    const application::LineVisible shown = m_gridFilter->ignoreInActions() ? application::LineVisible{} : shownLines();
    std::vector<core::LineId> found;
    for (const auto *line : session->document().lines())
        if (line->style == name && (!shown || shown(line->id)))
            found.push_back(line->id);
    const auto current = session->selection().active;
    if (found.empty()) { // as Select mode: the old selection goes, the active Line stays
        applySelection(application::Selection{current, {}, current, {}});
        return false;
    }
    std::set<core::LineId> selected(found.begin(), found.end());
    const core::LineId active = current && selected.contains(*current) ? *current : found.front();
    // A new range: Shift extends from the active Line, as after Select lines.
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
    // P10: legacy SubsGrid::OnBackupTimer shows "Autosave" in the status
    // bar's first field, and its grid's nullifyTimer empties the field 5 s
    // after, whatever it shows then (SubsGridBase.cpp:1636-1651,
    // SubsGrid.cpp:54-57); each grid had its own timer. (A precise timer: a
    // coarse one may fire 5 % early.)
    m_shell->setStatusText(QCoreApplication::translate("HikariSub.Session", "Autosave"));
    QTimer::singleShot(5000, Qt::PreciseTimer, this, [this] { m_shell->setStatusText(QString()); });
    application::RecoveryContent content;
    // S4: legacy's autosave is SaveFile too, so it writes the active Line
    // into the file and the open Document (SubsGridBase.cpp:1470 at 20d647c4).
    // WS2: the recovery file has the tab's panel arrangement, as a save
    // writes it, so a recovered copy has it.
    const auto activeLine = application::activeLineToSave(*session);
    const auto layout = m_files->layoutToSave(document, session->document());
    core::Document saved = application::withActiveLine(session->document(), activeLine);
    if (layout)
        saved.setHikariProperty(application::kLayoutKey, *layout);
    content.bytes = core::encodeSubtitle(saved, m_files->saveOptions()); // E5: as SaveFile
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
    // The arrangement stays the recovery file's: the open Document keeps
    // its file's section, so a Save with the setting off writes no other.
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

// The editing target's video, as its tab has it (the file an MKV read
// takes, not the one the player may still be closing).
QString Application::targetVideoPath() const
{
    const auto target = m_workspace.editingTarget();
    const auto it = target ? m_tabMedia.find(target->value) : m_tabMedia.end();
    return it == m_tabMedia.end() ? QString() : it->second.video;
}

void Application::applyMatroska(application::MatroskaLoaded loaded, bool newTab)
{
    // PU1-mkv-notices: the window stayed usable while the track was read. A
    // tab that is no longer the editing target, or whose Document changed
    // since, keeps what was typed: the subtitles open in a new tab. A draft
    // being typed doesn't move the revision, so a pending one counts too; a
    // tab that opened another video meanwhile keeps that video's subtitles.
    // Started on Home, a Document opened meanwhile keeps its tab too.
    if (!newTab && m_matroskaTarget) {
        auto *session = m_files->session(*m_matroskaTarget);
        newTab = m_workspace.editingTarget() != m_matroskaTarget || !session || session->revision() != m_matroskaRevision
            || session->draftLine() != m_matroskaDraftLine || session->draftText() != m_matroskaDraft
            || m_reloads != m_matroskaReloads // reloaded: a new session at the same revision
            || targetVideoPath() != m_matroskaVideo
            // another video asked for in the tab, still being opened (the
            // tab's own, opened again on coming back to it, is not)
            || m_video->requestedVideo() != m_matroskaRequested;
    } else if (!newTab && m_matroskaFromHome) {
        newTab = m_workspace.editingTarget().has_value();
    }
    m_matroskaTarget.reset();
    m_matroskaFromHome = false;
    if (newTab) {
        // File > Open: InsertTab then OnMkvSubs in it (HikariSubFrame.cpp:
        // 1021-1028): a new tab after the last one, with no media, takes the
        // loaded Document; the tab before keeps its subtitles. The tab is made
        // only now, so a load that ends without subtitles leaves none behind.
        const QString subsPath = QDir::toNativeSeparators(QString::fromStdString(loaded.subtitlePath));
        const auto id = m_files->createUnsaved(std::move(loaded.document));
        if (auto *session = m_files->session(id)) {
            selectLegacyActiveLine(*session);
            selectRow(id, 0); // InsertSelection(currentLine) of a new tab
        }
        m_workspace.add(id, QFileInfo(subsPath).fileName().toStdString());
        m_importPaths[id.value] = subsPath;
        m_tabMedia[id.value]; // a new tab: no media of its own yet
        m_workspace.setEditingTarget(id);
        refreshViews();
        QTimer::singleShot(0, this, [this] { checkResolution(); });
        return;
    }
    // SubsGrid::OnMkvSubs after GetSubtitles (SubsGrid.cpp:1149-1190) and
    // GetSubtitles' Clearing/EndLoad (Demux.cpp:126-164): the tab's
    // Document is replaced by the loaded one, which has unsaved work
    // (EndLoad's ForgetSaved) and asks for a name at the next save
    // (originalFormat = -1); SubsPath and SubsName are the video's name
    // with ".ass" or ".srt". The tab keeps its video, audio and keyframes,
    // and InsertSelection(currentLine) selects the row that was active.
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
        // File > Open into the empty window (legacy's first tab, with the
        // video opened there): OnMkvSubs only loads the subtitles onto that
        // video, which stays open, with its position and audio.
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
    // Hikari->SetSubsResolution (SubsGrid.cpp:1189).
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
    m_workspace.add(id, QCoreApplication::translate("HikariSub.Session", "%1 (recovered)")
                            .arg(QString::fromStdString(content->title)).toStdString());
    takeFileArrangement(id); // WS2: the arrangement the autosave kept
    m_workspace.setEditingTarget(id);
    refreshViews();
    return true;
}

// P9: the legacy app's autosaves in its Subs folder (AutoSaveOpen), read only.
QVariantList Application::legacyAutosaves()
{
    QVariantList rows;
    // Legacy wrote them to Options.dataPath/Subs (AutoSaveOpen.cpp:127):
    // its program folder, or HikariSub/Legacy once installed (ADR 0019),
    // the roots the settings importer finds; never 0.1's own folder (#387).
    QString folder = m_legacyAutosaveDir;
    if (folder.isEmpty() && !m_settingsFile.isEmpty())
        for (const auto &root : SettingsImportStore::discoverRoots(SettingsImportStore::defaultCandidates()))
            if (QFileInfo(root.path + QStringLiteral("/Subs")).isDir()) {
                folder = root.path + QStringLiteral("/Subs");
                break;
            }
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return rows; // no legacy folder: nothing to list (and nothing logged)
    const auto files = application::listLegacyAutosaves(std::filesystem::path(folder.toStdU16String()),
                                                        QCoreApplication::translate("HikariSub.Documents", "Untitled").toStdU16String());
    if (files.empty()) {
        // GenerateList: "Cannot open auto save folder" when FindFirstFileW
        // finds nothing (the Linux build's, for an empty folder too; on
        // Windows "." and ".." are found), else "Auto save folder is empty".
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
            // PU1-autosaves-window: the time as recoveryBundles() gives it,
            // for the Autosaves window's date filter and order.
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
    // AutoSaveOpen::FindFiles: the query's words (split at spaces, empty ones
    // dropped: wxTOKEN_STRTOK) found in the lowered name, all of them or any
    // ("All words"); an empty query shows every file. Lowering follows
    // U1-unicode-case (every letter, whatever the interface language).
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
    // Legacy OnOkClick opened the autosave itself (Hikari->OpenFile), so a
    // save wrote into the Subs folder; under L58-recovery-copy it opens as a
    // new unsaved Untitled copy and the file is only read.
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
    takeFileArrangement(id); // WS2
    m_tabMedia[id.value]; // a new tab
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
        const auto frame = video.reportedFrame(); // legacy Tell: the player's while playing
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
    // E5-tlmode-needs-script-info: not PlainText (translationHint says why).
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
    // SubsGrid::OnPasteTextTl reads through OpenWrite::FileOpen (test = true):
    // R4-uchardet's charset and R5-per-platform line ends (CRLF is LF on
    // Windows, the Linux build keeps "\r"). A file it cannot read, decode, or
    // that is empty changes nothing.
    const auto read = backends::readLegacyTextFile(path);
    if (!read) {
        m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Translation", "Paste translation"),
                      QCoreApplication::translate("HikariSub.Translation", "Could not open %1; nothing was changed.")
                          .arg(QFileInfo(path).fileName()));
        return false;
    }
    const QString text = QString::fromStdU16String(*read);
    // Legacy compares the extension after the last '.' exactly.
    const QString extension = path.section(QLatin1Char('.'), -1);
    const auto shown = shownLines();
    const bool done = application::pasteTranslation(*session, toU8(text), toU8(extension), shown).has_value();
    if (done)
        originalColumns(*m_workspace.editingTarget()).pasted(session->document()); // E5: showOriginal = true
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
    // E5: MoveTextTL returns without showOriginal (SubsGrid.cpp:1037).
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
        m_tabMedia[target->value].keyframes = QDir::toNativeSeparators(path); // P6: legacy KeyframesPath
    auto &video = m_video->session();
    if (video.state() != application::VideoSession::State::Ready) {
        // V3: with the audio box, at 23.976 fps (VideoBox::OpenKeyframes)
        if (QString problem; keyframesWithoutVideo(path, problem))
            return problem;
        m_pendingKeyframes = path; // applied when a video opens
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
        // VideoBox::GetFrameTime: the reported frame's (the player's while
        // playing) midpoint start and end times.
        context.keyframes = video.keyframes();
        if (const auto frame = video.reportedFrame()) {
            context.videoFrame = *frame;
            context.videoFrameStartMs = timebase.startTimeFor(*frame);
            context.videoFrameEndMs = timebase.endTimeFor(*frame);
        }
    }
    // A3: the audio box's mark (legacy ChangeTimes' moveTimeOptions & 8 with ABox->hasMark)
    if (m_audio && m_audio->hasMark())
        context.audioMarkMs = m_audio->markMs();
    // "New times" lengths: the Conversion time per letter (legacy's
    // CONVERT_TIME_PER_CHARACTER, SubsGridBase.cpp:439)
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
        // A tab shown as the reference is selected in, as legacy's previewed
        // tab was, but refuses Cut, Delete, Move and Set as comment.
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
        // wxRegEx's wxLogError calls, per tab as legacy compiled per tab.
        if (result->invalidExpression)
            m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.SelectLines", "Select lines"),
                          QCoreApplication::translate("HikariSub.SelectLines", "Invalid regular expression '%1': %2")
                              .arg(fromUtf16(result->invalidExpression->first), fromUtf16(result->invalidExpression->second)));
        for (const auto &error : result->matchErrors)
            m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.SelectLines", "Select lines"),
                          regexMatchError(error));
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

// A value a resample could not scale (legacy logged each, two lines).
QString unscaledValue(int row, const std::u16string &value, const std::u16string &tag)
{
    return QCoreApplication::translate("HikariSub.Conversion", "In line %1, value '%2' cannot be scaled\nin tag '%3'")
        .arg(row)
        .arg(QString::fromStdU16String(value), QString::fromStdU16String(tag))
        .replace(QLatin1Char('\n'), QLatin1Char(' '));
}

// NT1: one notice for a resample's values that could not be scaled, each a
// line of its Details.
void noticeUnscaled(ui::LogController &log, const QStringList &values)
{
    if (values.isEmpty())
        return;
    log.notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Conversion", "Resample subtitles"),
               QCoreApplication::translate("HikariSub.Conversion", "%n value(s) could not be scaled.", nullptr,
                                           int(values.size())),
               values.join(QLatin1Char('\n')));
}

// NT1: notices of Fix minor errors (the replacer's menu command).
QString replacerTitle()
{
    return QCoreApplication::translate("HikariSub.MisspellReplacer", "Fix minor errors");
}

// wxRegEx::Compile's wxLogError for each rule that does not compile, at the
// points MisspellReplacer compiles its rules.
void logRegexErrors(ui::LogController &log, const std::vector<application::ReplacerRule> &rules, bool checkedOnly)
{
    for (const auto &error : application::replacerRegexErrors(rules, checkedOnly))
        log.notice(ui::LogController::Error, replacerTitle(),
                   QCoreApplication::translate("HikariSub.SelectLines", "Invalid regular expression '%1': %2")
                       .arg(fromUtf16(error.expression), fromUtf16(error.message)));
}

// wx_regexec's wxLogError for each Matches or Replace that failed with a
// PCRE2 error (the match or heap limit), counted as no match.
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
            m_log->notice(ui::LogController::Warning, replacerTitle(),
                          QCoreApplication::translate("HikariSub.MisspellReplacer", "Rule \"%1\" is invalid.").arg(fromUtf8(line)));
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
        m_log->notice(ui::LogController::Warning, replacerTitle(),
                      QCoreApplication::translate("HikariSub.MisspellReplacer", "Cannot change rules\nwhen find results window is open"));
        return false;
    }
    misspellRuleList().push_back(replacerRule(rule)); // added unchecked
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
    // The checkbox stays as it was.
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
                // Legacy turns SPELLCHECKER_ON off and says why (NT1: a toast,
                // a warning without dictionaries). The Line editor's switch
                // stays: with no dictionary it marks brackets only.
                m_settings->set("editor.spellchecker", false);
                const bool noDictionary = status == application::SpellChecker::Status::NoDictionary;
                const QString message =
                    status == application::SpellChecker::Status::NoDictionary
                        // Legacy names "<settings folder>\Dictionary"; the path
                        // takes the platform's separators.
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
    // SpellChecker::Destroy: the next use creates it again.
    if (m_spellChecker)
        m_spellChecker->clear();
    m_spellingStarted = false;
    emit spellCheckerRestarted();
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
    // E4: none for a comment (TextEditor::SetState's SpellCheckerOnOff).
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
        return spellCheckState(changed, QCoreApplication::translate("HikariSub.Spelling", "The edited line cannot be committed."));
    const auto *current = m_spellWalk && m_spellWalk->current() ? &*m_spellWalk->current() : nullptr;
    QStringList suggestions;
    if (current)
        for (const auto &s : current->suggestions)
            suggestions << qstr(s);
    // PU6: the panel names the word's Line (the Grid's number) and shows the
    // text around it as the Line editor does (tags hidden or not, \N as a
    // space), at most kContext characters a side, cut at a word (#409).
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
    // CONVERT_FPS_FROM_VIDEO takes the open video's rate.
    options.fps = o.value(QStringLiteral("fpsFromVideo")).toBool() && hasVideo && frameMs > 0.f
                      ? 1000.0 / frameMs
                      : o.value(QStringLiteral("fps")).toString().toDouble();
    // PU6: not a number ("nan", "inf") or past any real frame rate is
    // invalid too (legacy refused only a rate under 1).
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
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("problem"),
                 QCoreApplication::translate("HikariSub.Conversion", "This document can't be converted to that format.")}};
    const auto &r = result->report;
    QStringList losses;
    // PU6: what each one is, "warning" for what the conversion removes.
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
    // Into ASS the markup is converted; into another format some of it is
    // stripped for good, a warning.
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
    // A changed Document or options invalidates the plan (C02), and so does
    // another video (or none): its frame rate is in the plan.
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
    // PU6: the rendering fields the user left alone are the Document's now,
    // not the dialog's snapshot (the main window can change or undo them
    // while the dialog is open beside a floating panel).
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

// E6: GLOBAL_HIDE_TAGS, SubsGrid::HideOverrideTags (SubsGridWindow.cpp:1959-1965):
// the Grid's switch flips and GRID_HIDE_TAGS keeps it.
void Application::toggleHideTags()
{
    m_settings->set("grid.hideTags", !m_shell->hideTags());
    m_shell->setHideTags(m_settings->boolean("grid.hideTags"), m_settings->text("grid.tagsSwapCharacter"));
}

// E6: GLOBAL_REMOVE_TEXT, SubsGrid::DeleteText (SubsGridBase.cpp:928-936).
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

// O1: live application of changed settings, where legacy reads the option
// when it acts (the others are read at their next use).
void Application::settingChanged(const QString &id)
{
    if (id.startsWith(QLatin1String("grammar.")) || id == QLatin1String("editor.spellchecker"))
        applyGrammarSettings(); // GR1: harper-ls's dialect, rules and SpellCheck
    if (id == QLatin1String("audio.outputHostApi") && m_audioPlayer)
        m_audioPlayer->reopenOutput();
    if (id == QLatin1String("audio.outputDevice") && m_audioRouting)
        m_audioRouting->refresh(); // H2: the outputs move to the chosen device
    if (id == QLatin1String("video.playbackPlayer"))
        choosePlayer(); // W1
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
    else if (id == QLatin1String("grid.autoHideColumns") && !m_resettingSettings)
        m_shell->setAutoHideColumns(m_settings->boolean("grid.autoHideColumns"));
    else if (id == QLatin1String("grid.foldGenerated") && !m_resettingSettings)
        m_shell->setFoldGenerated(m_settings->boolean("grid.foldGenerated"));
    else if (id == QLatin1String("video.disableLiveEditing") || id.startsWith(QLatin1String("grid.calcSpaces"))) {
        m_editor->optionsChanged(); // E4: the counters and Duration follow at once
        refreshVideo();             // live editing on or off: the draft or the committed Lines
    }
    // LE2, LE3: Settings > Editor switches the Line editor's view; Set default
    // leaves it as shown, as legacy's EditBox kept its radios.
    else if (id == QLatin1String("editor.hideTags") && !m_resettingSettings)
        m_editor->setShowTags(!m_settings->boolean("editor.hideTags"));
    else if (id == QLatin1String("editor.timesToFramesSwitch") && !m_resettingSettings)
        m_editor->setShowFrames(m_settings->boolean("editor.timesToFramesSwitch"));
    else if (id == QLatin1String("grid.hideTags") || id == QLatin1String("grid.tagsSwapCharacter"))
        m_shell->setHideTags(m_settings->boolean("grid.hideTags"), m_settings->text("grid.tagsSwapCharacter"));
    else if (id == QLatin1String("program.language")) // O5: live, wherever it is written
        switchLanguage();
    else if (id == QLatin1String("program.font") || id == QLatin1String("program.fontSize"))
        applyProgramFont(); // O5 (legacy SetOptions: Hikari->SetFont(*Options.GetFont()))
    else if (id == QLatin1String("automation.traceLevel"))
        m_automation->manager().setTraceLevel(m_settings->integer("automation.traceLevel"));
    else if (id == QLatin1String("video.subtitleProvider") &&
             m_renderer.select(m_settings->text("video.subtitleProvider").toStdString())) {
        // W2: legacy DestroyProviders and Notebook::RefreshVideo
        // (OptionsDialog.cpp:1148-1152): the loaded video's subtitles are
        // prepared again through the chosen renderer and the frame redrawn.
        m_fontCollector->setDisplayRenderer(QString::fromStdString(m_renderer.provider()));
        m_videoRevision.reset();
        m_videoScript.clear(); // E4: the same script goes to the new renderer too
        refreshVideo();
        applySubtitlesAt(); // SV1: Screen size is libass's only
    } else if (id == QLatin1String("video.previewSubtitlesAt")) {
        applySubtitlesAt();
    } else if (id == QLatin1String("video.renderCost")) {
        // KE1-render-cost-overlay: libass counts only while the overlay is
        // shown; the frame is drawn again so it shows the counts at once.
        m_renderer.countCost(m_settings->boolean("video.renderCost"));
        m_videoRevision.reset();
        m_videoScript.clear();
        refreshVideo();
    }
}

// SV1-screen-size-subtitles: "Preview subtitles at" Screen size draws libass
// at the picture's size on screen; a CSRI filter stays at the video's.
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
    // O5: English, then the catalogs (OptionsDialog.cpp:322-333: the
    // available translations sorted, each by FindLanguage).
    lists.languageTags = {"en"};
    lists.languageNames = {"English"};
    for (const QString &tag : m_localisation->catalogLanguages()) {
        lists.languageTags.push_back(tag.toStdString());
        lists.languageNames.push_back(languageName(tag).toStdString());
    }
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
            lists.dictionaryNames.push_back(QCoreApplication::translate("HikariSub.Options",
                                                                        "Put files .dic and .aff to \"Dictionary\" folder").toStdString());
    }
    // The conversion catalog: its option's catalog is loaded (LoadStyles), then
    // its Styles are listed.
    lists.catalogs = stdList(m_styleManager->catalogs());
    lists.currentCatalog = m_styleManager->catalog().toStdString();
    if (const auto load = application::optionsCatalogToLoad(m_settings->settings(), lists)) {
        m_styleManager->loadCatalog(QString::fromStdString(*load)); // not the manager's choice (SY1)
        lists.currentCatalog = *load;
    }
    lists.styles = stdList(m_styleManager->storeStyles());
    lists.renderers = m_renderer.providers(); // W2: GetProviders
    // H2: System default, the connected devices, a chosen one not connected.
    lists.outputDevices = application::outputDeviceChoices(m_audioDevices->outputs(),
                                                           m_settings->settings().text("audio.outputDevice"),
                                                           m_settings->settings().text("audio.outputDeviceName"));
#ifdef _WIN32
    lists.pathSeparator = '\\';
    lists.slashesForBackslashes = false;
#endif
    const auto open = application::openOptionsDialog(m_settings->settings(), lists);
    // O2: the Hotkeys page's list (AddHotkeysOnList).
    m_hotkeys->beginOptions();
    QStringList warnings;
    // "The selected %s for conversion does not exist\nand will be changed to the default".
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
    // PK1 (#301): the questions hidden with "Don't ask again", which the
    // Editor page's Ask again empties.
    values.insert(QStringLiteral("notices.hiddenQuestions"), m_settings->list("notices.hiddenQuestions"));
    // AD2 (#311): script dialogs' last answers, which the Automation page's
    // Clear last answers empties.
    values.insert(QStringLiteral("automation.dialogAnswers"), m_settings->list("automation.dialogAnswers"));
    // W2: the renderers as the choice shows them; VSFilterMod says what it
    // is for (user 2026-10-13: slow, only for its own tags).
    QStringList rendererLabels;
    for (const auto &name : lists.renderers)
        rendererLabels << subtitleRendererLabel(name);
    QStringList outputDeviceLabels;
    for (const auto &choice : lists.outputDevices)
        outputDeviceLabels << outputDeviceLabel(choice);
    return {{QStringLiteral("values"), values},
            {QStringLiteral("languages"), qList(lists.languageNames)},
            {QStringLiteral("languageTags"), qList(lists.languageTags)}, // DX1: the docs export's entries
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

// K2: the Appearance page's settings (the theme, following the system, each
// mode's accent preset and the high-contrast pickers), which replace legacy's
// Themes page (theme files stay excluded). They are not legacy options:
// "Set default" leaves them, as ResetDefault left the theme.
namespace {
std::vector<std::string_view> appearanceSettings()
{
    std::vector<std::string_view> ids;
    for (const auto &setting : application::settingDefinitions())
        if (setting.id.starts_with("appearance."))
            ids.push_back(setting.id);
    return ids;
}

// A staged value the setting accepts: a theme's code, an accent preset's
// key, a colour ("#RRGGBB" or "#RRGGBBAA"), the title bar's choice (SH1).
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
} // namespace

void Application::addAppearance(QVariantMap &values) const
{
    for (const auto id : appearanceSettings())
        values.insert(qs(id), m_settings->value(qs(id)));
}

void Application::applySettings(const QVariantMap &values, bool resetAll)
{
    // PF1-staged-reset: "Reset all settings…" takes effect here, before the
    // staged values (every page's defaults, and the changes made since).
    if (resetAll)
        resetAllSettings();
    // K2: the appearance, saved when it differs; the theme follows at once.
    for (const auto id : appearanceSettings()) {
        const auto found = values.constFind(qs(id));
        if (found == values.cend() || !validAppearance(id, *found) || *found == m_settings->value(qs(id)))
            continue;
        m_settings->setValue(qs(id), *found);
    }
    // PK1 (#301): Ask again, saved by OK or Apply like the options.
    if (const auto hidden = values.constFind(QStringLiteral("notices.hiddenQuestions")); hidden != values.cend()) {
        const QStringList list = hidden->toStringList();
        if (list != m_settings->list("notices.hiddenQuestions"))
            m_settings->setValue(QStringLiteral("notices.hiddenQuestions"), list);
    }
    // AD2: Clear last answers, saved by OK or Apply; Repeat's record goes
    // with them.
    if (const auto answers = values.constFind(QStringLiteral("automation.dialogAnswers"));
        answers != values.cend() && answers->toStringList().isEmpty() && !m_settings->list("automation.dialogAnswers").isEmpty()) {
        m_settings->setValue(QStringLiteral("automation.dialogAnswers"), QStringList());
        m_automation->forgetRepeat();
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
    // PU7 (#309): the Updates page's options, which the update checker holds
    // (and writes back with its next check's time).
    if (std::ranges::any_of(written, [](const std::string &id) { return id.starts_with("updater."); }))
        m_updates->reload();
    // O2: the Hotkeys page's list, a bound control of SetOptions.
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
    // The import first: when it cannot be staged (the legacy files changed,
    // staging failed) nothing is written and the window stays open with the
    // importer's status.
    // The pages' state: the Settings window's controls, and the look's
    // settings, which are not among them.
    auto state = fromVariant(values);
    for (const auto id : appearanceSettings())
        if (const auto found = values.constFind(qs(id)); found != values.cend())
            state[std::string(id)] = found->typeId() == QMetaType::Bool
                                         ? application::SettingValue(found->toBool())
                                         : application::SettingValue(found->toString().toStdString());
    const auto pages = application::gettingStartedPageSettings(state);
    if (import && !m_settingsImport->importChosenBut(pages))
        return false;
    // The stored language first: Finish's own switch (settingChanged)
    // happens only when the language changes.
    endLanguagePreview();
    // Only what the pages hold: the rest of the Settings window's state
    // (values it normalises when it opens) would otherwise be written now
    // and, as a later edit, win over the import.
    QVariantMap written;
    for (const auto &id : pages)
        if (const auto found = values.constFind(qs(id)); found != values.cend())
            written.insert(qs(id), *found);
    applySettings(written);
    m_settings->set("workspace.gettingStarted", QStringLiteral("done"));
    return true;
}

void Application::skipGettingStarted()
{
    endLanguagePreview();
    m_settings->set("workspace.gettingStarted", QStringLiteral("done"));
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
                                                m_spellingText);
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
    // A dusk sky, and a Line in the default Style's look at the bottom.
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
    // Half a second of 660 Hz, faded in and out.
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
    // The next open is the route's again: once the tone is over it stops (a
    // clip this long plays on into silence) and its stream closes, or what
    // plays by then moves to the route; unless another Test sound came since.
    m_audioPlayer->setOutputDevice(m_noticedRoute.systemDefault ? std::string() : m_noticedRoute.name);
    QTimer::singleShot(frames * 1000 / rate + 200, this, [this, test = ++m_testSounds, played] {
        if (test != m_testSounds)
            return;
        if (const auto clip = played.lock(); clip && m_audioPlayer->playingClip(clip.get()))
            m_audioPlayer->stop();
        m_audioPlayer->switchOutput();
    });
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
    // PF1-staged-reset: "Reset all settings…" shows every page's defaults
    // (as ResetDefault refreshes the controls, its defects included) and
    // changes nothing until OK or Apply (applySettings with resetAll); the
    // appearance and the hotkeys stay as they are.
    application::MemorySettingsStorage storage;
    const application::Settings defaults(storage);
    auto refreshed = toVariant(application::refreshOptionsDialogAfterReset(defaults, m_optionsLists, fromVariant(values)));
    const auto keep = [&](std::string_view id) {
        if (const auto found = values.constFind(qs(id)); found != values.cend())
            refreshed.insert(qs(id), *found);
    };
    for (const auto id : appearanceSettings())
        keep(id);
    // The Appearance page's bound options too: the program font and its size.
    for (const auto &b : application::optionsBindings())
        if (b.page == application::OptionsPage::Appearance) {
            keep(b.setting);
            if (!b.sizeSetting.empty())
                keep(b.sizeSetting);
        }
    refreshed.insert(QStringLiteral("notices.hiddenQuestions"), QStringList());
    // AD2: script dialogs' answers are no option either; a Clear last
    // answers staged before stays staged.
    keep("automation.dialogAnswers");
    return refreshed;
}

QVariantMap Application::resetSettingsPage(const QVariantMap &values, const QString &page)
{
    // PF1-staged-reset: "Reset page", the page's controls at their defaults,
    // staged like any change. The Appearance page's settings too (K2's,
    // previewed by the dialog), and the General page's questions hidden with
    // "Don't ask again". The Hotkeys page's is the list's (optionsResetAll).
    const auto which = application::optionsPageOfId(page.toStdString());
    if (!which)
        return values;
    application::MemorySettingsStorage storage;
    const application::Settings defaults(storage);
    // Conversion: the default catalog loaded and its Styles listed, as
    // choosing it does, so the default Style is found among them.
    if (*which == application::OptionsPage::Conversion)
        if (const auto load = application::optionsCatalogToLoad(defaults, m_optionsLists)) {
            m_styleManager->loadCatalog(QString::fromStdString(*load)); // not the manager's choice (SY1)
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
    // config::ResetDefault resets every option in memory. What long-lived
    // legacy objects hold is not reset, and they write it back later:
    // HikariSubFrame's recent subtitles, video and audio lists (SetRecent at
    // the next addition, OnClose at exit), kept here at once.
    // PF1-staged-reset: the hotkeys are left as they are (legacy's
    // Hkeys.ResetDefaults() reset them in memory); the Hotkeys page's "Reset
    // page" restores them.
    auto &store = m_settings->settings();
    std::vector<std::pair<std::string_view, application::SettingValue>> kept;
    // The appearance is not an option: ResetDefault leaves it, as it left the theme.
    std::vector<std::string_view> keep{"recent.subtitles", "recent.subtitlesOpened", "recent.video", "recent.audio",
                                       application::kAutomationHotkeysSetting, application::kHotkeysSetting,
                                       application::kAudioHotkeysSetting,
                                       // PF1: the Settings window's page shown last, no option either
                                       "workspace.settingsPage",
                                       // AD2: script dialogs' answers and presets, no options
                                       // either (Clear last answers… and ⋯ › Delete preset
                                       // forget them)
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
    // Options legacy reads when it acts take the defaults at once: copy and
    // paste columns, the grid filter, the update checker.
    m_copyColumns = m_settings->integer("grid.copyColumns");
    m_pasteColumns = m_settings->integer("grid.pasteColumns");
    m_gridFilter->settingsReset(); // per-grid "ignore filtering" is kept (SubsGrid::ignoreFiltered)
    m_updates->reload();
    // GR3: empty-column hiding has no legacy owner; both Grids take the default.
    m_shell->setAutoHideColumns(m_settings->boolean("grid.autoHideColumns"));
    m_shell->setFoldGenerated(m_settings->boolean("grid.foldGenerated")); // KP1
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
}

QStringList Application::settingsStyles() const
{
    return qList(m_optionsLists.styles);
}

QVariantMap Application::chooseSettingsCatalog(const QVariantMap &values, int index)
{
    auto state = fromVariant(values);
    if (index >= 0 && index < qsizetype(m_optionsLists.catalogs.size())) {
        // Options.SaveOptions(false) then LoadStyles: the current catalog is saved first.
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

} // namespace hikari::app
