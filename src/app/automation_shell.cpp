#include "hikari/app/automation_shell.h"

#include "hikari/application/script_resolution.h"
#include "hikari/backends/desktop_handoff.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <numeric>
#include <string_view>
#include <unordered_set>

namespace hikari::app {

namespace {

using application::HostServiceReply;

bool windowsPaths()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

application::AutomationFolders folders(const AutomationShell::Paths &paths)
{
    const auto native = [](const QString &dir) { return QDir::toNativeSeparators(dir).toStdString(); };
    return application::automationFolders(native(paths.automationDir),
                                          paths.userAutomationDir.isEmpty() ? std::string() : native(paths.userAutomationDir),
                                          windowsPaths());
}

// The include folders after the script's own, one per line (the helper's
// Load message, lua_helper.cpp).
QString sharedIncludes(const AutomationShell::Paths &paths)
{
    QStringList dirs;
    for (const auto &dir : folders(paths).include)
        dirs << QString::fromStdString(dir);
    return dirs.join(QLatin1Char('\n'));
}

// Media and Document answers for the macro's host services (legacy
// Automation.cpp frame_from_ms, video_size, get_frame, project_properties...).
class MediaAdapter : public application::MacroMediaPort {
public:
    MediaAdapter(ui::VideoController &video, application::DocumentFiles &files, application::Workspace &workspace)
        : m_video(video), m_files(files), m_workspace(workspace)
    {
    }
    std::optional<std::int64_t> frameFromMs(std::int64_t ms) const override
    {
        const auto &s = m_video.session();
        if (s.state() != application::VideoSession::State::Ready)
            return std::nullopt;
        // The frame displayed at that time: the last one starting at or before it.
        int frame = 0;
        for (int i = 0; i < s.frameCount(); ++i) {
            const auto start = s.frameStart(i);
            if (!start || start->microseconds() > ms * 1000)
                break;
            frame = i;
        }
        return frame;
    }
    std::optional<std::int64_t> msFromFrame(std::int64_t frame) const override
    {
        const auto &s = m_video.session();
        if (s.state() != application::VideoSession::State::Ready || s.frameCount() == 0)
            return std::nullopt;
        const auto start = s.frameStart(static_cast<int>(std::clamp<std::int64_t>(frame, 0, s.frameCount() - 1)));
        return start ? std::optional<std::int64_t>(start->microseconds() / 1000) : std::nullopt;
    }
    std::optional<VideoSize> videoSize() const override
    {
        const auto frame = m_video.session().lastFrame();
        if (m_video.session().state() != application::VideoSession::State::Ready || !frame)
            return std::nullopt;
        // Square pixels: the display aspect is the frame's own.
        const int g = std::gcd(frame->width, frame->height);
        return VideoSize{frame->width, frame->height, frame->width / g, frame->height / g};
    }
    // Legacy get_keyframes (Automation.cpp:183-197): the video timebase's
    // keyframes (the video's own, or a keyframe file's in their place) while a
    // video is open, an empty table when it has none; nil without a video.
    std::optional<std::vector<std::int64_t>> keyframes() const override
    {
        const auto &s = m_video.session();
        if (s.state() != application::VideoSession::State::Ready)
            return std::nullopt;
        return std::vector<std::int64_t>(s.keyframes().begin(), s.keyframes().end());
    }
    void frame(std::int64_t index, bool withSubtitles, std::function<void(std::optional<Frame>)> reply) override
    {
        m_video.session().requestFrame(static_cast<int>(index), withSubtitles,
                                       [reply](std::shared_ptr<const application::IndexedFrame> f) {
                                           if (!f)
                                               return reply(std::nullopt);
                                           Frame out{f->width, f->height, {}};
                                           out.bgra.resize(static_cast<std::size_t>(f->width) * f->height * 4);
                                           for (int y = 0; y < f->height; ++y)
                                               std::copy_n(f->bgra.data() + static_cast<std::size_t>(y) * f->stride,
                                                           static_cast<std::size_t>(f->width) * 4,
                                                           out.bgra.data() + static_cast<std::size_t>(y) * f->width * 4);
                                           reply(std::move(out));
                                       });
    }
    // Legacy lua_get_audio_selection (Automation.cpp:394-410): with an audio
    // box, the start and end of the grid's current Line (not the box's own
    // selection); nil without an audio box.
    std::optional<std::pair<std::int64_t, std::int64_t>> audioSelection() const override
    {
        const application::AudioBox *box = audioBox ? audioBox() : nullptr;
        if (!box || !box->isOpen())
            return std::nullopt;
        const auto target = m_workspace.editingTarget();
        const auto *session = target ? m_files.session(*target) : nullptr;
        if (!session || !session->selection().active)
            return std::nullopt; // legacy always has a current Line
        for (const auto *line : session->document().lines())
            if (line->id == *session->selection().active)
                return std::pair<std::int64_t, std::int64_t>{line->start.value.microseconds() / 1000,
                                                             line->end.value.microseconds() / 1000};
        return std::nullopt;
    }
    std::optional<Project> project() const override
    {
        Project p;
        p.videoFrameShown = m_video.session().reportedFrame().value_or(0); // FrameShownAt(Tell())
        p.videoFile = m_video.session().path();
        return p;
    }
    std::optional<std::string> fileName() const override
    {
        const auto target = m_workspace.editingTarget();
        const auto destination = target ? m_files.destination(*target) : std::nullopt;
        if (!destination || destination->value.empty())
            return std::nullopt;
        return QFileInfo(QString::fromStdString(destination->value)).fileName().toStdString();
    }
    // Legacy reads the video's FFMS2 provider first, else the audio box's;
    // the box holds the video's audio when it opened it, and is the only
    // audio here (a video whose audio legacy had not loaded gave it
    // uninitialised samples: R3, no audio).
    std::optional<const application::DisplayAudio *> peakAudio() const override
    {
        const application::AudioBox *box = audioBox ? audioBox() : nullptr;
        if (!box || !box->isOpen())
            return std::nullopt;
        return box->audio();
    }

    std::function<const application::AudioBox *()> audioBox;

private:
    ui::VideoController &m_video;
    application::DocumentFiles &m_files;
    application::Workspace &m_workspace;
};

// The Line editor's field and the status bar (aegisub.gui, set_status_text).
class EditorAdapter : public application::EditorPort {
public:
    EditorAdapter(ui::LineEditorController &editor, ui::ShellController &shell, application::DocumentFiles &files,
                  application::Workspace &workspace)
        : m_editor(editor), m_shell(shell), m_files(files), m_workspace(workspace)
    {
    }
    std::optional<std::pair<std::int64_t, std::int64_t>> selection() const override
    {
        if (!m_editor.hasLine())
            return std::nullopt;
        const auto [start, end] = m_editor.fieldSelection();
        return std::pair<std::int64_t, std::int64_t>{start, end};
    }
    void setSelection(std::int64_t start, std::int64_t end) override
    {
        m_editor.selectInField(static_cast<int>(start), static_cast<int>(end));
    }
    bool modified() const override
    {
        const auto target = m_workspace.editingTarget();
        auto *session = target ? m_files.session(*target) : nullptr;
        return session && session->draftLine().has_value();
    }
    void setStatusText(const std::string &text) override { m_shell.setStatusText(QString::fromStdString(text)); }

private:
    ui::LineEditorController &m_editor;
    ui::ShellController &m_shell;
    application::DocumentFiles &m_files;
    application::Workspace &m_workspace;
};

// Y4-playres-ask: the router's question, answered by the shell.
class ResolutionAdapter : public application::ScriptResolutionPort {
public:
    using Reply = std::function<void(std::optional<application::Resolution>)>;
    using Resolve = std::function<void(const application::ScriptResolutionRequest &, Reply)>;
    ResolutionAdapter(Resolve resolve, std::function<void()> withdraw)
        : m_resolve(std::move(resolve)), m_withdraw(std::move(withdraw))
    {
    }
    void resolve(const application::ScriptResolutionRequest &request, Reply reply) override
    {
        m_resolve(request, std::move(reply));
    }
    void withdraw() override { m_withdraw(); }

private:
    Resolve m_resolve;
    std::function<void()> m_withdraw;
};

QString refusalText(const application::MacroApplyFailure &failure)
{
    switch (failure.refusal.value_or(application::CommandRefusal::Invalid)) {
    case application::CommandRefusal::StaleRevision:
        return QCoreApplication::translate("HikariSub.Automation", "The document changed while the macro ran; nothing was changed.");
    case application::CommandRefusal::Protected:
        return QCoreApplication::translate("HikariSub.Automation", "The protected reference can't be changed.");
    default: return QCoreApplication::translate("HikariSub.Automation", "The macro's changes could not be applied; nothing was changed.");
    }
}

} // namespace

AutomationShell::AutomationShell(Paths paths, application::DocumentFiles &files, application::Workspace &workspace,
                                 ui::LineEditorController &editor, ui::VideoController &video,
                                 ui::ShellController &shell, application::IndexedSourcePort &, QObject *parent)
    : QObject(parent), m_paths(std::move(paths)), m_files(files), m_workspace(workspace), m_editor(editor),
      m_video(video), m_shell(shell),
      m_manager(m_paths.helper, sharedIncludes(m_paths))
{
    // An installed copy makes its Automation folder as a portable one ships it.
    for (const auto &dir : folders(m_paths).made)
        QDir().mkpath(QDir::fromNativeSeparators(QString::fromStdString(dir)));
    m_media = std::make_unique<MediaAdapter>(video, files, workspace);
    m_editorPort = std::make_unique<EditorAdapter>(editor, shell, files, workspace);
    m_router.setMedia(m_media.get());
    m_router.setEditor(m_editorPort.get());
    m_router.setClipboard(&m_clipboard);
    m_router.setTextMeasure(&m_measure);
    m_router.setFilePicker(&m_picker);
    m_resolutionPort = std::make_unique<ResolutionAdapter>(
        [this](const application::ScriptResolutionRequest &request, ResolutionAdapter::Reply reply) {
            resolveScriptResolution(request, std::move(reply));
        },
        [this] { m_resolutionPrompt.withdraw(); });
    m_router.setScriptResolution(m_resolutionPort.get());
    m_router.setPathContext([this] {
        application::AutomationPathContext c;
        c.automationDir = QDir::toNativeSeparators(m_paths.automationDir).toStdString();
        if (!m_paths.userAutomationDir.isEmpty())
            c.userAutomationDir = QDir::toNativeSeparators(m_paths.userAutomationDir).toStdString();
        c.dictionaryDir = QDir::toNativeSeparators(QDir(m_paths.automationDir).filePath(QStringLiteral("../Dictionary")))
                              .toStdString();
        const auto target = m_workspace.editingTarget();
        if (const auto destination = target ? m_files.destination(*target) : std::nullopt)
            c.subtitlePath = QDir::toNativeSeparators(QString::fromStdString(destination->value)).toStdString();
        c.videoPath = m_video.session().path();
#ifdef _WIN32
        c.windows = true;
#endif
        return c;
    });
    m_manager.setServiceHandler([this](const application::HostServiceRequest &r, backends::LuaScriptHost::ServiceReply reply) {
        m_router.handle(r, std::move(reply));
    });
    m_manager.setDialogHandler([this](const application::DialogRequest &request, backends::LuaScriptHost::DialogReply reply) {
        const QString source = m_run ? QString::fromStdString(m_run->path + '\n' + m_run->macro) : QString();
        m_dialogs.present(m_title, request, std::move(reply), source);
    });
    connect(&m_manager, &backends::AutomationManager::runFinished, this, &AutomationShell::finished);
    connect(&m_manager, &backends::AutomationManager::changed, this, &AutomationShell::progressChanged);
    connect(&m_manager, &backends::AutomationManager::changed, this, &AutomationShell::continueLastScript);
    connect(&m_manager, &backends::AutomationManager::logged, this,
            [this](const QString &, const QString &text, int level) {
                // Bounded as a run's log is (a script can write without end).
                if (m_log.size() < ui::AutomationRuns::kMaxChars)
                    m_log += text.left(ui::AutomationRuns::kMaxChars - m_log.size());
                // An empty write adds nothing to read: the run has not logged.
                if (m_run && !text.isEmpty()) {
                    m_runs.append(m_runId, text, level);
                    m_logged = true;
                    // AD3: output opens the card at once.
                    showCard();
                }
                emit progressChanged();
            });
    connect(&m_manager, &backends::AutomationManager::progressChanged, this, [this](const QString &, double p) {
        m_progress = p;
        m_progressKnown = true;
        if (m_run)
            m_runs.setProgress(m_runId, p);
        emit progressChanged();
    });
    connect(&m_manager, &backends::AutomationManager::taskChanged, this, [this](const QString &, const QString &t) {
        m_task = t;
        if (m_run)
            m_runs.setTask(m_runId, t);
        emit progressChanged();
    });
    m_cardTimer.setSingleShot(true);
    // AD3: an ended run's card goes with its run (Clear runs).
    connect(&m_runs, &ui::AutomationRuns::removed, this, [this](int id) {
        if (id == m_runId && !m_run && !m_cardState.isEmpty()) {
            m_cardState.clear();
            emit progressChanged();
        }
    });
    connect(&m_cardTimer, &QTimer::timeout, this, [this] {
        if (m_run && !waitingForUser())
            showCard();
    });
    connect(&m_dialogs, &ui::AutomationDialogController::changed, this, &AutomationShell::updateCardTimer);
    connect(&m_picker, &ui::AutomationFilePickerController::changed, this, &AutomationShell::updateCardTimer);
    connect(&m_resolutionPrompt, &ui::ScriptResolutionPromptController::changed, this,
            &AutomationShell::updateCardTimer);
    connect(&m_manager, &backends::AutomationManager::titleChanged, this, [this](const QString &, const QString &t) {
        m_title = t;
        emit progressChanged();
    });
    connect(&m_manager, &backends::AutomationManager::dialogWithdrawn, this, [this] { m_dialogs.withdraw(); });
    connect(&m_manager, &backends::AutomationManager::servicesWithdrawn, this, [this] { m_router.withdraw(); });
    m_managerController = std::make_unique<ui::AutomationManagerController>(*this);
    // AM1: the panel lists the Document scripts apart from autoload's (the
    // list changes only with a load, reload or unload, which refresh it).
    m_managerController->setDocumentScript([this](const std::string &path) {
        return std::ranges::find(m_documentScripts.scripts(), path) != m_documentScripts.scripts().end();
    });
}

void AutomationShell::setAudioBox(std::function<const application::AudioBox *()> box)
{
    static_cast<MediaAdapter *>(m_media.get())->audioBox = std::move(box);
}

AutomationShell::~AutomationShell()
{
    m_manager.shutdown(); // helpers end with the application (5 s quit deadline)
}

QString AutomationShell::includeDir() const
{
    return QDir(m_paths.automationDir).filePath(QStringLiteral("automation/Include"));
}

QString AutomationShell::autoloadDir() const
{
    return QDir(m_paths.automationDir).filePath(QStringLiteral("automation/Autoload"));
}

QStringList AutomationShell::autoloadScripts() const
{
    std::vector<std::pair<std::string, std::vector<std::string>>> listed;
    for (const auto &folder : folders(m_paths).autoload) {
        std::vector<std::string> names;
        const QDir dir(QString::fromStdString(folder));
        for (const QString &file : dir.entryList({QStringLiteral("*.lua"), QStringLiteral("*.moon")}, QDir::Files, QDir::Name))
            names.push_back(file.toStdString());
        listed.emplace_back(folder, std::move(names));
    }
    QStringList scripts;
    for (const auto &path : application::chooseAutoloadScripts(listed, windowsPaths()))
        scripts << QString::fromStdString(path);
    return scripts;
}

void AutomationShell::autoload()
{
    for (const QString &script : autoloadScripts())
        m_manager.load(script.toStdString());
}

void AutomationShell::reloadAutoload()
{
    // Legacy "Refresh autoload scripts": loaded ones run their top level again; new ones load.
    for (const QString &script : autoloadScripts()) {
        const std::string path = script.toStdString();
        if (m_manager.host(path))
            m_manager.reload(path);
        else
            m_manager.load(path);
    }
}

namespace {

std::string narrow(const std::u8string &s)
{
    return std::string(s.begin(), s.end());
}

std::u8string wide(const std::string &s)
{
    return std::u8string(s.begin(), s.end());
}

QString prettyName(const std::string &path)
{
    // LuaScript::GetPrettyFilename: HikariPathName(filename).
    return QFileInfo(QString::fromStdString(path)).fileName();
}

} // namespace

void AutomationShell::loadDocumentScript(const std::string &path)
{
    // Legacy Add makes a new LuaScript (its top level runs) for a script
    // not in the Document list, even when autoload has it too; here one
    // helper per file, so a loaded one loads again.
    if (m_manager.host(path))
        m_manager.reload(path);
    else
        m_manager.load(path);
}

void AutomationShell::loadScript(const QUrl &file)
{
    const QString local = file.toLocalFile();
    if (local.isEmpty())
        return;
    const std::string path = QDir::toNativeSeparators(local).toStdString();
    // HikariSubFrame.cpp:899-900 (also 1330-1331, 1844-1845): the
    // Document's scripts are read first while none are listed.
    if (m_documentScripts.empty())
        addFromDocument();
    // Automation::Add (Automation.cpp:1153-1171).
    if (!m_documentScripts.add(path))
        return;
    loadDocumentScript(path);
    const auto target = m_workspace.editingTarget();
    auto *s = target ? session(*target) : nullptr;
    if (!s)
        return;
    // Automation.cpp:1162-1168: "|<path>" is appended to the Script Info
    // value as a "Changing the subtitle header" step (SetModified(ASS_PROPERTIES)).
    const auto key = wide(application::kAutomationScriptsKey);
    const std::string before = narrow(s->document().scriptInfo(key).value_or(std::u8string()));
    const std::string value = application::scriptInfoWithScript(before, path);
    // A value read before stays read with the path the user chose added.
    if (before.empty() || m_readScriptInfo.contains(before))
        m_readScriptInfo.insert(value);
    s->run(application::Command{"Changing the subtitle header", s->revision(), {},
                                [&](core::Document &d) { return d.setScriptInfo(key, wide(value)); }});
    if (m_documentChanged)
        m_documentChanged();
}

void AutomationShell::addFromDocument()
{
    const auto target = m_workspace.editingTarget();
    const auto *s = target ? session(*target) : nullptr;
    const auto value = s ? s->document().scriptInfo(wide(application::kAutomationScriptsKey)) : std::nullopt;
    std::vector<std::string> added;
    if (value)
        m_readScriptInfo.insert(narrow(*value));
    m_documentScripts.readScriptInfo(
        value ? narrow(*value) : std::string(),
        [](const std::string &path) { return QFileInfo(QString::fromStdString(path)).isFile(); }, &added);
    for (const auto &path : added)
        loadDocumentScript(path);
}

QStringList AutomationShell::documentScriptsToLoad()
{
    const auto target = m_workspace.editingTarget();
    const auto *s = target ? session(*target) : nullptr;
    const auto value = s ? s->document().scriptInfo(wide(application::kAutomationScriptsKey)) : std::nullopt;
    QStringList out;
    if (!value || m_readScriptInfo.contains(narrow(*value)))
        return out;
    // No file check before consent: one would open a UNC path's server.
    // Sets keep a long value (thousands of paths) linear on the GUI thread.
    const auto &listed = m_documentScripts.scripts();
    const std::unordered_set<std::string_view> loaded(listed.begin(), listed.end());
    std::unordered_set<std::string> offered;
    for (std::string &path : application::scriptInfoScriptPaths(narrow(*value)))
        if (!path.empty() && !loaded.contains(path) && offered.insert(path).second)
            out << QString::fromStdString(path);
    return out;
}

void AutomationShell::menuOpened()
{
    // HikariSubFrame::OnMenuOpened (HikariSubFrame.cpp:2307-2316): BuildMenu
    // runs while the editor is enabled, i.e. with a Document open.
    if (!m_workspace.editingTarget())
        return;
    addFromDocument(); // Automation.cpp:1359
    // Automation.cpp:1365-1367 and 1427: a script whose file changed reloads.
    for (const auto &script : m_manager.scripts())
        if (m_manager.modifiedSinceLoad(script.path))
            m_manager.reload(script.path);
}

void AutomationShell::runLastLoadedScript()
{
    // HikariSubFrame.cpp:916-936.
    if (m_documentScripts.empty())
        addFromDocument();
    if (m_documentScripts.empty()) {
        emit notice(QStringLiteral("info"), QCoreApplication::translate("HikariSub.MainWindow", "Run the last loaded script"),
                    QCoreApplication::translate("HikariSub.Automation", "This subtitle file does not have any scripts added"), {});
        return;
    }
    const std::string path = m_documentScripts.scripts().back();
    if (m_manager.modifiedSinceLoad(path))
        m_manager.reload(path); // CheckLastModified(true), then Reload
    m_pendingLast = path;
    continueLastScript();
}

void AutomationShell::continueLastScript()
{
    if (!m_pendingLast)
        return;
    const auto *host = m_manager.host(*m_pendingLast);
    using State = backends::LuaScriptHost::State;
    if (host && (host->state() == State::Loading || host->state() == State::Idle))
        return; // its top level still runs (legacy loads synchronously)
    if (host && (m_run || m_manager.busy()))
        return; // it waits for the active macro (legacy's modal progress dialog runs one at a time)
    const std::string path = *m_pendingLast;
    m_pendingLast.reset();
    if (!host)
        return;
    if (host->state() == State::LoadFailed || host->info().macros.empty()) {
        // GetMacro(0) is null: the load error, or the description of a
        // script that registered nothing, then the script editor.
        const QString description = host->state() == State::LoadFailed
                                        ? host->lastError()
                                        : QString::fromStdString(host->info().description);
        // NT1: the description (the load error) is the box's Details; the
        // legacy text keeps its translations, its second line left empty.
        emit notice(QStringLiteral("error"), QCoreApplication::translate("HikariSub.MainWindow", "Run the last loaded script"),
                    QCoreApplication::translate("HikariSub.Automation", "Error loading Lua script: %1\n%2")
                        .arg(prettyName(path), QString()).trimmed(),
                    description);
        editScript(QString::fromStdString(path));
        return;
    }
    if (!run(path, 0, RunOrigin::LastScript) && host->state() == State::Unavailable)
        emit notice(QStringLiteral("error"), QCoreApplication::translate("HikariSub.MainWindow", "Run the last loaded script"),
                    QCoreApplication::translate("HikariSub.Automation", "The script's helper ended; reload the script to use it again."),
                    {});
}

void AutomationShell::editScript(const QString &script)
{
    // Automation::OnEdit (Automation.cpp:1309-1327).
    const QString editor = m_settings ? m_settings->text("automation.scriptEditor") : QString();
    if (editor.isEmpty() || QGuiApplication::queryKeyboardModifiers().testFlag(Qt::ShiftModifier)) {
        emit chooseScriptEditor(script);
        return;
    }
    startEditor(editor, script);
}

void AutomationShell::editWith(const QUrl &editorFile, const QString &script)
{
    const QString editor = QDir::toNativeSeparators(editorFile.isLocalFile() ? editorFile.toLocalFile()
                                                                             : editorFile.toString());
    if (!QFileInfo(editor).isFile())
        return; // wxFileExists
    if (m_settings) {
        m_settings->set("automation.scriptEditor", editor);
        m_settings->sync(); // Options.SaveOptions()
    }
    startEditor(editor, script);
}

void AutomationShell::startEditor(const QString &editor, const QString &script)
{
    // wxExecute({editor, script}) without waiting.
    if (!backends::desktop::startDetached(editor, {script}))
        emit notice(QStringLiteral("error"), QCoreApplication::translate("HikariSub.Automation", "Edit script"),
                    QCoreApplication::translate("HikariSub.Automation", "Cannot start editor."), {});
}

void AutomationShell::unload(const std::string &path)
{
    m_manager.unload(path);
    m_documentScripts.remove(path); // Automation::Remove
}

bool AutomationShell::rerunLast()
{
    return m_last && run(m_last->first, m_last->second);
}

bool AutomationShell::forceStopOffered() const
{
    const auto *host = m_run ? m_manager.host(m_run->active) : nullptr;
    return host && host->forceStopAvailable();
}

void AutomationShell::cancelRun()
{
    if (m_run && !m_cancelling) {
        m_cancelling = true;
        emit progressChanged();
    }
    m_manager.cancel();
}

void AutomationShell::finishCard(const QString &ended, const QString &text, const QString &validationError)
{
    m_cardTimer.stop();
    m_macroStarted = false;
    m_cancelling = false;
    QString state;
    QString said = text;
    if (ended == QLatin1String("applied"))
        state = m_keepLog ? QStringLiteral("problems") : QStringLiteral("applied");
    else if (ended == QLatin1String("cancelled") || ended == QLatin1String("declined"))
        state = QStringLiteral("cancelled");
    else
        state = QStringLiteral("failed");
    if (ended == QLatin1String("invalid"))
        said = QCoreApplication::translate("HikariSub.Automation", "Its validation did not let it run; nothing was changed.");
    // The error and how it ended are the run log's last lines.
    if (!validationError.isEmpty())
        m_runs.note(m_runId, validationError, ui::AutomationRuns::kHostError);
    if (!said.isEmpty())
        m_runs.note(m_runId, said, state == QLatin1String("failed") ? ui::AutomationRuns::kHostError
                                                                   : ui::AutomationRuns::kHostInfo);
    m_runs.finish(m_runId, state, said);
    // The card says the first lines; the run's log keeps the rest.
    m_cardText = said.left(4000);
    // A run that applied and logged nothing, or whose script-resolution
    // question was declined, leaves no card.
    if (ended == QLatin1String("declined") || (state == QLatin1String("applied") && !m_logged))
        m_cardState.clear();
    else
        m_cardState = state;
}

void AutomationShell::dismissCard()
{
    if (m_run || m_cardState.isEmpty())
        return;
    m_cardState.clear();
    emit progressChanged();
}

bool AutomationShell::waitingForUser() const
{
    return m_dialogs.isOpen() || m_picker.isOpen() || m_resolutionPrompt.isOpen();
}

void AutomationShell::updateCardTimer()
{
    if (!m_run || !m_macroStarted || m_cardState != QLatin1String("notice"))
        return;
    // Paused while the user is asked: what was left of the second resumes.
    if (waitingForUser()) {
        if (m_cardTimer.isActive()) {
            m_cardLeft = std::max(0, m_cardTimer.remainingTime());
            m_cardTimer.stop();
        }
    } else if (!m_cardTimer.isActive()) {
        m_cardTimer.start(m_cardLeft);
    }
}

void AutomationShell::showCard()
{
    m_cardTimer.stop();
    if (m_run && m_cardState != QLatin1String("running")) {
        m_cardState = QStringLiteral("running");
        emit progressChanged();
    }
}

bool AutomationShell::forceStopRun()
{
    return m_run && m_manager.forceStop(m_run->active);
}

bool AutomationShell::run(const std::string &path, int ordinal, RunOrigin origin)
{
    if (m_run || m_manager.busy())
        return false; // one active macro application-wide
    const auto target = m_workspace.editingTarget();
    auto *s = target ? session(*target) : nullptr;
    if (!s) {
        m_lastMessage = QCoreApplication::translate("HikariSub.Automation", "Open a document to run a macro on.");
        emit progressChanged();
        return false;
    }
    std::string macro;
    for (const auto &script : m_manager.scripts())
        if (script.path == path && ordinal >= 0 && static_cast<std::size_t>(ordinal) < script.info.macros.size())
            macro = script.info.macros[static_cast<std::size_t>(ordinal)].name;
    // The pending draft is committed first (T43-reconcile); the target is
    // read-only until the macro ends.
    auto snapshot = application::snapshotForMacro(*s);
    m_editor.reloadFromSession();
    if (!snapshot) {
        m_lastMessage = QCoreApplication::translate("HikariSub.Automation",
                                                    "The line being edited can't be committed, so the macro can't start.");
        emit progressChanged();
        return false;
    }
    s->setReadOnly(true);
    // S4: every run validates first, as legacy does (LuaCommand::RunScript,
    // HikariSubFrame::OnRunScript and GLOBAL_AUTOMATION_LOAD_LAST_SCRIPT);
    // S4-run-validation: the macros of the scripts it depends on validate
    // before it, on the same snapshot, and no other macro does.
    Run started{*target, std::move(*snapshot), path, macro, ordinal, origin};
    started.validations = application::dependencyValidations(m_manager.scripts(), path);
    m_macroStarted = false;
    m_cardTimer.stop();
    m_cardLeft = m_cardDelay;
    if (!startStep(started)) {
        s->setReadOnly(false);
        return false;
    }
    m_run = std::move(started);
    m_last = {path, ordinal};
    m_title = QString::fromStdString(macro);
    m_task.clear();
    m_log.clear();
    m_lastMessage.clear();
    m_lastOutcome.clear();
    m_progress = 0;
    m_keepLog = false;
    m_problems = 0;
    m_logged = false;
    m_cancelling = false;
    m_progressKnown = false;
    m_cardState = QStringLiteral("notice");
    m_cardText.clear();
    m_runId = m_runs.begin(m_title, prettyName(path), m_shell.editingTitle());
    updateCardTimer(); // the macro may have started already (no validations)
    if (m_documentChanged)
        m_documentChanged();
    emit progressChanged();
    return true;
}

bool AutomationShell::startStep(Run &run)
{
    using State = backends::LuaScriptHost::State;
    for (; run.step < run.validations.size(); ++run.step) {
        const auto &v = run.validations[run.step];
        // A dependency unloaded, or reloading, since the run began is no
        // longer a loaded and ready script: skipped, as one that was not
        // ready when the run began (S4-run-validation).
        const auto *host = m_manager.host(v.path);
        if (!host || host->state() != State::Ready)
            continue;
        run.active = v.path;
        return m_manager.validate(v.path, v.ordinal, run.snapshot);
    }
    run.active = run.path;
    if (!m_manager.run(run.path, run.ordinal, run.snapshot, true))
        return false;
    m_macroStarted = true;
    m_cardTimer.stop();
    updateCardTimer();
    return true;
}

void AutomationShell::resolveScriptResolution(const application::ScriptResolutionRequest &request,
                                              std::function<void(std::optional<application::Resolution>)> reply)
{
    auto *s = m_run ? session(m_run->target) : nullptr;
    // Nothing to write into (a Document that is not ASS has no Script Info
    // for it), or nothing missing: legacy's values, nothing written.
    if (!s || s->document().format() != core::SubtitleFormat::Ass ||
        !application::lacksScriptResolution(request.written))
        return reply(std::nullopt);
    std::optional<application::Resolution> video;
    if (const auto size = m_media->videoSize(); size && size->width > 0 && size->height > 0)
        video = application::Resolution{size->width, size->height};
    // The user chose to take it from the video without being asked: both
    // keys take the video's size, a value the file has included.
    if (video && m_settings && m_settings->boolean("automation.scriptResolutionFromVideo")) {
        m_run->resolution = *video;
        return reply(*video);
    }
    // No window to ask in (tests, a session without a view): the macro
    // goes on with legacy's values and nothing is written.
    if (!m_resolutionPrompt.asking())
        return reply(std::nullopt);
    ui::ScriptResolutionPromptController::Question question;
    question.script = prettyName(request.script.empty() ? m_run->path : request.script);
    question.suggested = application::suggestedScriptResolution(request.written, video);
    question.video = video;
    m_resolutionPrompt.ask(std::move(question), [this, reply](std::optional<ui::ScriptResolutionPromptController::Answer> answer) {
        if (!m_run)
            return;
        if (!answer) {
            // Cancel: the macro does not go on (the run ends cancelled, quietly).
            m_run->declined = true;
            m_manager.cancel();
            return;
        }
        if (answer->alwaysFromVideo && m_settings) {
            m_settings->set("automation.scriptResolutionFromVideo", true);
            m_settings->sync();
        }
        m_run->resolution = answer->resolution;
        reply(answer->resolution);
    });
}

// KE1-template-timing: the karaoke templater's report goes to the status bar
// (for 10 s) and, per template Line, to the Log without a toast; a run
// that named template problems keeps its progress window open.
void AutomationShell::karaokeReport(const ui::KaraokeReport &report)
{
    const QString summary = ui::karaokeReportSummary(report);
    m_shell.setStatusText(summary);
    QTimer::singleShot(10'000, Qt::PreciseTimer, this, [this, summary, serial = ++m_statusSerial] {
        if (serial == m_statusSerial && m_shell.statusText() == summary)
            m_shell.setStatusText(QString());
    });
    const QString details = ui::karaokeReportDetails(report);
    if (m_logLine)
        m_logLine(details, LogLine::Quiet);
    // AD3: the run's log has it too, so a run naming problems shows them
    // (the run has ended: its id is still the card's).
    m_runs.note(m_runId, details, ui::AutomationRuns::kHostInfo);
    m_keepLog = report.problems > 0;
    m_problems = report.problems;
}

void AutomationShell::finished(const QString &, backends::LuaScriptHost::RunOutcome outcome, const QString &message)
{
    if (!m_run)
        return;
    using Outcome = backends::LuaScriptHost::RunOutcome;
    if (m_run->step < m_run->validations.size() && outcome == Outcome::Ok) {
        // A dependency answered true: the next validation, or the macro.
        // A script resolution set while it validated is staged for the
        // next helper, so the question is not asked again (Y4-playres-ask).
        if (m_run->resolution)
            application::setStagedScriptResolution(m_run->snapshot.info, *m_run->resolution);
        ++m_run->step;
        if (startStep(*m_run)) {
            emit progressChanged();
            return;
        }
        outcome = Outcome::HelperLost; // the macro's own helper is gone meanwhile
    }
    Run run = std::move(*m_run);
    m_run.reset();
    auto *s = session(run.target);
    if (s)
        s->setReadOnly(false);
    m_router.withdraw();
    m_dialogs.withdraw();
    // Y4-playres-ask: the resolution set while the macro ran is its own undo
    // step, before the macro's (whatever the macro's outcome: the user set
    // it). The macro staged the same keys, so its result is read against the
    // snapshot as it is now; a Document changed under the run keeps the
    // snapshot's old revision, and the macro's result is refused as before.
    bool resolutionSet = false;
    if (s && run.resolution) {
        const bool current = s->revision() == run.snapshot.revision;
        resolutionSet = application::setScriptResolution(*s, *run.resolution).has_value();
        if (resolutionSet && current) {
            application::setStagedScriptResolution(run.snapshot.info, *run.resolution);
            run.snapshot.revision = s->revision();
        }
    }
    bool ok = false;
    QString text;
    QString ended;
    switch (outcome) {
    case Outcome::Ok: {
        const auto *host = m_manager.host(run.path);
        if (s && host && host->lastResult()) {
            const auto applied = application::applyMacroResult(*s, run.snapshot, *host->lastResult(), run.macro);
            ok = applied.has_value();
            if (!ok)
                text = refusalText(applied.error());
            else if (const auto report = ui::readKaraokeReport(host->lastResult()->report))
                karaokeReport(*report);
        }
        ended = ok ? QStringLiteral("applied") : QStringLiteral("refused");
        break;
    }
    case Outcome::Failed:
        text = message;
        ended = QStringLiteral("failed");
        break;
    case Outcome::Cancelled:
        ended = run.declined && !resolutionSet ? QStringLiteral("declined") : QStringLiteral("cancelled");
        // A user who declined to set the script resolution chose not to run it.
        // A resolution the user set before cancelling is kept as its own step.
        if (resolutionSet)
            text = QCoreApplication::translate("HikariSub.Automation",
                                               "The macro was cancelled; only the script resolution was set.");
        else if (!run.declined)
            text = QCoreApplication::translate("HikariSub.Automation", "The macro was cancelled; nothing was changed.");
        break;
    case Outcome::HelperLost:
        text = QCoreApplication::translate("HikariSub.Automation", "The script's helper ended; reload the script to use it again.");
        ended = QStringLiteral("lost");
        break;
    case Outcome::ForceStopped:
        text = QCoreApplication::translate("HikariSub.Automation",
                                           "The macro was force stopped; reload the script to use it again.");
        ended = QStringLiteral("stopped");
        break;
    case Outcome::NotValid:
        ended = QStringLiteral("invalid");
        // LuaCommand::Validate logs a runtime error (Automation.cpp:953); the
        // menu says nothing more (RunScript), a hotkey and "Run the last
        // loaded script" say validation failed (HikariSubFrame.cpp:929, 2641).
        // A dependency's failed validation is said the same way, naming
        // the dependency (S4-run-validation).
        // NT1: the runtime error is the box's Details, and an error of the
        // Log; the menu's run only logged it. AD3: the progress card says
        // it failed, so the Log takes it without a toast.
        if (!message.isEmpty() && m_logLine)
            m_logLine(message, LogLine::QuietError);
        if (run.origin != RunOrigin::Menu)
            emit notice(QStringLiteral("error"),
                        run.origin == RunOrigin::LastScript
                            ? QCoreApplication::translate("HikariSub.MainWindow", "Run the last loaded script")
                            : QCoreApplication::translate("HikariSub.Automation", "Automation"),
                        QCoreApplication::translate("HikariSub.Automation", "Validation Lua script '%1' failed")
                            .arg(prettyName(run.active)),
                        message);
        break;
    }
    if (!text.isEmpty() && m_log.size() < ui::AutomationRuns::kMaxChars)
        m_log += ((m_log.isEmpty() || m_log.endsWith(QLatin1Char('\n')) ? QString() : QStringLiteral("\n")) + text)
                     .left(ui::AutomationRuns::kMaxChars - m_log.size());
    m_lastMessage = text;
    m_lastOutcome = ended;
    finishCard(ended, text, outcome == Outcome::NotValid ? message : QString());
    m_editor.reloadFromSession();
    if (m_documentChanged)
        m_documentChanged();
    emit progressChanged();
    emit runCompleted(ok, text);
}

} // namespace hikari::app
