#include "hikari/app/automation_shell.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <numeric>

namespace hikari::app {

namespace {

using application::HostServiceReply;

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
    std::optional<std::vector<std::int64_t>> keyframes() const override
    {
        return std::nullopt; // the indexed timeline does not carry keyframes yet
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
    std::optional<std::pair<std::int64_t, std::int64_t>> audioSelection() const override
    {
        return std::nullopt; // legacy nil without an open audio box
    }
    std::optional<Project> project() const override
    {
        Project p;
        p.videoFrameShown = m_video.session().shownFrame().value_or(0);
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

QString refusalText(const application::MacroApplyFailure &failure)
{
    if (failure.error == application::MacroApplyError::UnsupportedChange)
        return AutomationShell::tr("The macro changed Styles or Script Info, which can't be applied yet; nothing was changed.");
    switch (failure.refusal.value_or(application::CommandRefusal::Invalid)) {
    case application::CommandRefusal::StaleRevision:
        return AutomationShell::tr("The Document changed while the macro ran; nothing was changed.");
    case application::CommandRefusal::Protected:
        return AutomationShell::tr("The protected reference can't be changed.");
    default: return AutomationShell::tr("The macro's changes could not be applied; nothing was changed.");
    }
}

} // namespace

AutomationShell::AutomationShell(Paths paths, application::DocumentFiles &files, application::Workspace &workspace,
                                 ui::LineEditorController &editor, ui::VideoController &video,
                                 ui::ShellController &shell, application::IndexedSourcePort &, QObject *parent)
    : QObject(parent), m_paths(std::move(paths)), m_files(files), m_workspace(workspace), m_editor(editor),
      m_video(video), m_shell(shell),
      m_manager(m_paths.helper, QDir(m_paths.automationDir).filePath(QStringLiteral("automation/Include")))
{
    m_media = std::make_unique<MediaAdapter>(video, files, workspace);
    m_editorPort = std::make_unique<EditorAdapter>(editor, shell, files, workspace);
    m_router.setMedia(m_media.get());
    m_router.setEditor(m_editorPort.get());
    m_router.setClipboard(&m_clipboard);
    m_router.setTextMeasure(&m_measure);
    m_router.setFilePicker(&m_picker);
    m_router.setPathContext([this] {
        application::AutomationPathContext c;
        c.automationDir = QDir::toNativeSeparators(m_paths.automationDir).toStdString();
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
        m_dialogs.present(m_title, request, std::move(reply));
    });
    connect(&m_manager, &backends::AutomationManager::runFinished, this, &AutomationShell::finished);
    connect(&m_manager, &backends::AutomationManager::changed, this, &AutomationShell::progressChanged);
    connect(&m_manager, &backends::AutomationManager::logged, this, [this](const QString &, const QString &text) {
        m_log += text;
        emit progressChanged();
    });
    connect(&m_manager, &backends::AutomationManager::progressChanged, this, [this](const QString &, double p) {
        m_progress = p;
        emit progressChanged();
    });
    connect(&m_manager, &backends::AutomationManager::taskChanged, this, [this](const QString &, const QString &t) {
        m_task = t;
        emit progressChanged();
    });
    connect(&m_manager, &backends::AutomationManager::titleChanged, this, [this](const QString &, const QString &t) {
        m_title = t;
        emit progressChanged();
    });
    connect(&m_manager, &backends::AutomationManager::dialogWithdrawn, this, [this] { m_dialogs.withdraw(); });
    connect(&m_manager, &backends::AutomationManager::servicesWithdrawn, this, [this] { m_router.withdraw(); });
    m_managerController = std::make_unique<ui::AutomationManagerController>(*this);
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

void AutomationShell::autoload()
{
    const QDir dir(autoloadDir());
    for (const QString &file : dir.entryList({QStringLiteral("*.lua"), QStringLiteral("*.moon")}, QDir::Files, QDir::Name))
        m_manager.load(QDir::toNativeSeparators(dir.filePath(file)).toStdString());
}

void AutomationShell::reloadAutoload()
{
    // Legacy "Refresh autoload scripts": loaded ones run their top level again; new ones load.
    const QDir dir(autoloadDir());
    for (const QString &file : dir.entryList({QStringLiteral("*.lua"), QStringLiteral("*.moon")}, QDir::Files, QDir::Name)) {
        const std::string path = QDir::toNativeSeparators(dir.filePath(file)).toStdString();
        if (m_manager.host(path))
            m_manager.reload(path);
        else
            m_manager.load(path);
    }
}

void AutomationShell::loadScript(const QUrl &file)
{
    if (const QString path = file.toLocalFile(); !path.isEmpty())
        m_manager.load(QDir::toNativeSeparators(path).toStdString());
}

bool AutomationShell::rerunLast()
{
    return m_last && run(m_last->first, m_last->second);
}

bool AutomationShell::forceStopOffered() const
{
    const auto *host = m_run ? m_manager.host(m_run->path) : nullptr;
    return host && host->forceStopAvailable();
}

bool AutomationShell::forceStopRun()
{
    return m_run && m_manager.forceStop(m_run->path);
}

bool AutomationShell::run(const std::string &path, int ordinal)
{
    if (m_run || m_manager.busy())
        return false; // one active macro application-wide
    const auto target = m_workspace.editingTarget();
    auto *s = target ? session(*target) : nullptr;
    if (!s) {
        m_lastMessage = tr("Open a Document to run a macro on.");
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
        m_lastMessage = tr("The Line being edited can't be committed, so the macro can't start.");
        emit progressChanged();
        return false;
    }
    s->setReadOnly(true);
    if (!m_manager.run(path, ordinal, *snapshot)) {
        s->setReadOnly(false);
        return false;
    }
    m_run = Run{*target, std::move(*snapshot), path, macro};
    m_last = {path, ordinal};
    m_title = QString::fromStdString(macro);
    m_task.clear();
    m_log.clear();
    m_lastMessage.clear();
    m_progress = 0;
    if (m_documentChanged)
        m_documentChanged();
    emit progressChanged();
    return true;
}

void AutomationShell::finished(const QString &, backends::LuaScriptHost::RunOutcome outcome, const QString &message)
{
    if (!m_run)
        return;
    Run run = std::move(*m_run);
    m_run.reset();
    auto *s = session(run.target);
    if (s)
        s->setReadOnly(false);
    m_router.withdraw();
    m_dialogs.withdraw();
    bool ok = false;
    QString text;
    using Outcome = backends::LuaScriptHost::RunOutcome;
    switch (outcome) {
    case Outcome::Ok: {
        const auto *host = m_manager.host(run.path);
        if (s && host && host->lastResult()) {
            const auto applied = application::applyMacroResult(*s, run.snapshot, *host->lastResult(), run.macro);
            ok = applied.has_value();
            if (!ok)
                text = refusalText(applied.error());
        }
        break;
    }
    case Outcome::Failed: text = message; break;
    case Outcome::Cancelled: text = tr("The macro was cancelled; nothing was changed."); break;
    case Outcome::HelperLost: text = tr("The script's helper ended; reload the script to use it again."); break;
    case Outcome::ForceStopped: text = tr("The macro was force stopped; reload the script to use it again."); break;
    }
    if (!text.isEmpty())
        m_log += (m_log.isEmpty() || m_log.endsWith(QLatin1Char('\n')) ? QString() : QStringLiteral("\n")) + text;
    m_lastMessage = text;
    m_editor.reloadFromSession();
    if (m_documentChanged)
        m_documentChanged();
    emit progressChanged();
    emit runCompleted(ok, text);
}

} // namespace hikari::app
