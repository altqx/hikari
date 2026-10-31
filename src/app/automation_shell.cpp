#include "hikari/app/automation_shell.h"

#include "hikari/application/script_resolution.h"
#include "hikari/backends/desktop_handoff.h"
#include "hikari/backends/karaoke_preview_runner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <numeric>
#include <ranges>
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

QString sharedIncludes(const AutomationShell::Paths &paths)
{
    QStringList dirs;
    for (const auto &dir : folders(paths).include)
        dirs << QString::fromStdString(dir);
    return dirs.join(QLatin1Char('\n'));
}

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
        const int g = std::gcd(frame->width, frame->height);
        return VideoSize{frame->width, frame->height, frame->width / g, frame->height / g};
    }
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
    // Aegisub's get_audio_selection gives the current Line's times, not the audio box's selection.
    std::optional<std::pair<std::int64_t, std::int64_t>> audioSelection() const override
    {
        const application::AudioBox *box = audioBox ? audioBox() : nullptr;
        if (!box || !box->isOpen())
            return std::nullopt;
        const auto target = m_workspace.editingTarget();
        const auto *session = target ? m_files.session(*target) : nullptr;
        if (!session || !session->selection().active)
            return std::nullopt;
        // MUST KILL: reads the draft's times because previews run before Apply commits it
        if (const auto draft = session->draftRecord(); draft && draft->id == *session->selection().active)
            return std::pair<std::int64_t, std::int64_t>{draft->start.value.microseconds() / 1000,
                                                         draft->end.value.microseconds() / 1000};
        for (const auto *line : session->document().lines())
            if (line->id == *session->selection().active)
                return std::pair<std::int64_t, std::int64_t>{line->start.value.microseconds() / 1000,
                                                             line->end.value.microseconds() / 1000};
        return std::nullopt;
    }
    std::optional<Project> project() const override
    {
        Project p;
        p.videoFrameShown = m_video.session().reportedFrame().value_or(0);
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
    std::optional<const application::DisplayAudio *> peakAudio() const override
    {
        const application::AudioBox *box = audioBox ? audioBox() : nullptr;
        if (!box || !box->isOpen())
            return std::nullopt;
        return box->audio();
    }
    // MUST KILL: the video path and frame count stand in for the frame times
    std::string state() const
    {
        const auto &s = m_video.session();
        std::string out = s.path() + '\n' + std::to_string(static_cast<int>(s.state())) + ' ' +
                          std::to_string(s.frameCount());
        if (const auto size = videoSize())
            out += " size " + std::to_string(size->width) + 'x' + std::to_string(size->height);
        if (const auto frames = keyframes())
            out += " keyframes " + std::to_string(frames->size()) + ' ' +
                   std::to_string(std::hash<std::string_view>{}(
                       std::string_view(reinterpret_cast<const char *>(frames->data()),
                                        frames->size() * sizeof(std::int64_t))));
        if (const auto selection = audioSelection())
            out += " audio " + std::to_string(selection->first) + '-' + std::to_string(selection->second);
        out += " frame " + std::to_string(project()->videoFrameShown) + '\n';
        const auto target = m_workspace.editingTarget();
        if (const auto destination = target ? m_files.destination(*target) : std::nullopt)
            out += destination->value;
        return out;
    }

    std::function<const application::AudioBox *()> audioBox;

private:
    ui::VideoController &m_video;
    application::DocumentFiles &m_files;
    application::Workspace &m_workspace;
};

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

}

AutomationShell::AutomationShell(Paths paths, application::DocumentFiles &files, application::Workspace &workspace,
                                 ui::LineEditorController &editor, ui::VideoController &video,
                                 ui::ShellController &shell, application::IndexedSourcePort &, QObject *parent)
    : QObject(parent), m_paths(std::move(paths)), m_files(files), m_workspace(workspace), m_editor(editor),
      m_video(video), m_shell(shell),
      m_manager(m_paths.helper, sharedIncludes(m_paths))
{
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
    m_dialogs.setMemory(&m_dialogMemory);
    m_manager.setDialogHandler([this](const application::DialogRequest &request, backends::LuaScriptHost::DialogReply reply) {
        presentDialog(request, std::move(reply));
    });
    connect(&m_manager, &backends::AutomationManager::runFinished, this, &AutomationShell::finished);
    connect(&m_manager, &backends::AutomationManager::changed, this, &AutomationShell::progressChanged);
    connect(&m_manager, &backends::AutomationManager::changed, this, &AutomationShell::continueLastScript);
    connect(&m_manager, &backends::AutomationManager::logged, this,
            [this](const QString &, const QString &text, int level) {
                if (m_log.size() < ui::AutomationRuns::kMaxChars)
                    m_log += text.left(ui::AutomationRuns::kMaxChars - m_log.size());
                if (m_run && !text.isEmpty()) {
                    m_runs.append(m_runId, text, level);
                    m_logged = true;
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
        m_task = t.left(ui::AutomationRuns::kMaxText);
        if (m_run)
            m_runs.setTask(m_runId, m_task);
        emit progressChanged();
    });
    m_cardTimer.setSingleShot(true);
    connect(&m_manager, &backends::AutomationManager::macroStarted, this, [this](const QString &path) {
        if (!m_run || m_run->active != m_run->path || path.toStdString() != m_run->path)
            return;
        m_macroStarted = true;
        m_cardTimer.stop();
        updateCardTimer();
    });
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
        m_title = t.left(ui::AutomationRuns::kMaxText);
        emit progressChanged();
    });
    connect(&m_manager, &backends::AutomationManager::dialogWithdrawn, this, [this] { m_dialogs.withdraw(); });
    connect(&m_manager, &backends::AutomationManager::servicesWithdrawn, this, [this] { m_router.withdraw(); });
    m_managerController = std::make_unique<ui::AutomationManagerController>(*this);
    m_managerController->setDocumentScript([this](const std::string &path) {
        return std::ranges::find(m_documentScripts.scripts(), path) != m_documentScripts.scripts().end();
    });
    m_managerController->setAutoload(
        [this] {
            QStringList off;
            for (const QString &script : autoloadScripts())
                if (!autoloadEnabled(script) && !m_manager.host(script.toStdString()))
                    off << script;
            return off;
        },
        [this](const QString &script, bool enabled) { setAutoloadEnabled(script, enabled); });
}

std::string AutomationShell::hostState() const
{
    return static_cast<const MediaAdapter *>(m_media.get())->state();
}

void AutomationShell::setAudioBox(std::function<const application::AudioBox *()> box)
{
    static_cast<MediaAdapter *>(m_media.get())->audioBox = std::move(box);
}

class AutomationShell::Templater final : public application::KaraokeTemplaterPort {
public:
    explicit Templater(AutomationShell &shell)
        : m_shell(shell), m_runner(shell.m_paths.helper, sharedIncludes(shell.m_paths))
    {
        m_runner.setServiceHandler([this](const application::HostServiceRequest &r,
                                          backends::LuaScriptHost::ServiceReply reply) {
            m_shell.m_router.handle(r, std::move(reply));
        });
    }

    std::string path() const
    {
        for (const auto &s : m_shell.m_manager.scripts())
            if ((s.state == application::ScriptStatus::State::Ready ||
                 s.state == application::ScriptStatus::State::Running) &&
                QFileInfo(QString::fromStdString(s.path)).fileName().compare(QStringLiteral("kara-templater.lua"),
                                                                             Qt::CaseInsensitive) == 0 &&
                s.info.macros.size() >= 2)
                return s.path;
        return {};
    }
    void update() { m_runner.setScript(QString::fromStdString(path())); }

    bool available() const override { return m_runner.ready() && !path().empty(); }
    void preview(const application::MacroSnapshot &snapshot, PreviewDone done) override
    {
        m_runner.run(snapshot, std::move(done));
    }
    void cancelPreview() override { m_runner.cancel(); }
    bool apply(bool selectedOnly) override
    {
        const std::string script = path();
        return !script.empty() && m_shell.run(script, selectedOnly ? 1 : 0, RunOrigin::Menu);
    }
    backends::KaraokePreviewRunner &runner() { return m_runner; }

private:
    AutomationShell &m_shell;
    backends::KaraokePreviewRunner m_runner;
};

application::KaraokeTemplaterPort &AutomationShell::templater()
{
    if (!m_templater) {
        m_templater = std::make_unique<Templater>(*this);
        connect(&m_manager, &backends::AutomationManager::changed, this, [this] { m_templater->update(); });
        connect(&m_templater->runner(), &backends::KaraokePreviewRunner::readyChanged, this,
                &AutomationShell::progressChanged);
        m_templater->update();
    }
    return *m_templater;
}

AutomationShell::~AutomationShell()
{
    m_manager.shutdown();
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

bool AutomationShell::autoloadEnabled(const QString &script) const
{
    return !m_settings || !m_settings->list("automation.disabledAutoload")
                               .contains(QFileInfo(script).fileName(),
                                         windowsPaths() ? Qt::CaseInsensitive : Qt::CaseSensitive);
}

void AutomationShell::setAutoloadEnabled(const QString &script, bool enabled)
{
    if (!m_settings || autoloadEnabled(script) == enabled)
        return;
    const std::string path = script.toStdString();
    using State = backends::LuaScriptHost::State;
    const auto *host = m_manager.host(path);
    if (!enabled && host && (host->state() == State::Loading || host->state() == State::Running
                             || (m_run && m_run->active == path)))
        return;
    const QString name = QFileInfo(script).fileName();
    QStringList disabled = m_settings->list("automation.disabledAutoload");
    if (enabled)
        disabled.removeIf([&](const QString &off) {
            return off.compare(name, windowsPaths() ? Qt::CaseInsensitive : Qt::CaseSensitive) == 0;
        });
    else
        disabled << name;
    m_settings->set("automation.disabledAutoload", disabled);
    if (enabled && !m_manager.host(path))
        m_manager.load(path);
    else if (!enabled && m_manager.host(path))
        unload(path);
}

void AutomationShell::autoload()
{
    for (const QString &script : autoloadScripts())
        if (autoloadEnabled(script))
            m_manager.load(script.toStdString());
    m_managerController->refresh();
}

void AutomationShell::reloadAutoload()
{
    for (const QString &script : autoloadScripts()) {
        if (!autoloadEnabled(script))
            continue;
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
    return QFileInfo(QString::fromStdString(path)).fileName();
}

}

void AutomationShell::loadDocumentScript(const std::string &path)
{
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
    if (const auto target = m_workspace.editingTarget())
        m_trustDecisions[trustKey(*target, path)] = true;
    if (m_documentScripts.empty())
        addFromDocument([this, path] { addPickedScript(path); });
    else
        addPickedScript(path);
}

void AutomationShell::addPickedScript(const std::string &path)
{
    if (!m_documentScripts.add(path)) {
        loadDocumentScript(path);
        return;
    }
    loadDocumentScript(path);
    const auto target = m_workspace.editingTarget();
    auto *s = target ? session(*target) : nullptr;
    if (!s)
        return;
    const auto key = wide(application::kAutomationScriptsKey);
    const std::string before = narrow(s->document().scriptInfo(key).value_or(std::u8string()));
    const std::string value = application::scriptInfoWithScript(before, path);
    if (before.empty() || m_readScriptInfo.contains(before))
        m_readScriptInfo.insert(value);
    s->run(application::Command{"Changing the subtitle header", s->revision(), {},
                                [&](core::Document &d) { return d.setScriptInfo(key, wide(value)); }});
    if (m_documentChanged)
        m_documentChanged();
}

std::string AutomationShell::trustKey(application::DocumentId document, const std::string &script) const
{
    const auto destination = m_files.destination(document);
    const std::string file = destination && !destination->value.empty() ? destination->value
                                                                        : '#' + std::to_string(document.value);
    return file + '\n' + script;
}

QStringList AutomationShell::trustedFolders() const
{
    return m_settings ? m_settings->list("automation.trustedFolders") : QStringList();
}

namespace {

QString absoluteScript(const std::string &script)
{
    return QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(QString::fromStdString(script)).absoluteFilePath()));
}

}

bool AutomationShell::trusted(const std::string &script) const
{
    const std::string path = absoluteScript(script).toStdString();
    const auto f = folders(m_paths);
    std::vector<std::string> trusted = f.autoload;
    trusted.push_back(f.include.back());
    for (const QString &folder : trustedFolders())
        trusted.push_back(folder.toStdString());
    return std::ranges::any_of(trusted, [&](const std::string &folder) {
        return application::scriptInFolder(path, folder, windowsPaths());
    });
}

bool AutomationShell::permitted(application::DocumentId document, const std::string &script) const
{
    const auto decided = m_trustDecisions.find(trustKey(document, script));
    return (decided != m_trustDecisions.end() && decided->second) || trusted(script);
}

void AutomationShell::addFromDocument(std::function<void()> then)
{
    addFromDocument(std::move(then), false);
}

void AutomationShell::askToLoadDocumentScripts()
{
    addFromDocument({}, true);
}

void AutomationShell::addFromDocument(std::function<void()> then, bool askAgain)
{
    if (m_trust) {
        if (then)
            m_trust->then.push_back(std::move(then));
        return;
    }
    const auto target = m_workspace.editingTarget();
    const auto *s = target ? session(*target) : nullptr;
    const auto value = s ? s->document().scriptInfo(wide(application::kAutomationScriptsKey)) : std::nullopt;
    std::vector<std::string> ask;
    if (value && (askAgain || m_documentScripts.reads(narrow(*value)))) {
        const auto &listed = m_documentScripts.scripts();
        for (std::string &path : application::scriptInfoScriptPaths(narrow(*value))) {
            if (path.empty() || std::ranges::find(listed, path) != listed.end() ||
                std::ranges::find(ask, path) != ask.end() || trusted(path))
                continue;
            const auto decided = m_trustDecisions.find(trustKey(*target, path));
            if (decided != m_trustDecisions.end() && (decided->second || !askAgain))
                continue;
            if (QFileInfo(QString::fromStdString(path)).isFile())
                ask.push_back(std::move(path));
        }
    }
    if (ask.empty()) {
        if (target)
            readDocumentScripts(*target);
        if (then)
            then();
        return;
    }
    QStringList scripts, folders;
    const auto fileCase = windowsPaths() ? Qt::CaseInsensitive : Qt::CaseSensitive;
    for (const auto &path : ask) {
        const QString absolute = absoluteScript(path);
        if (!scripts.contains(absolute, fileCase))
            scripts << absolute;
        const QString folder = QDir::toNativeSeparators(QFileInfo(absolute).absolutePath());
        if (!folders.contains(folder, fileCase))
            folders << folder;
    }
    m_trust = TrustQuestion{++m_trustSerial, *target, std::move(ask), {}};
    if (then)
        m_trust->then.push_back(std::move(then));
    emit trustRequested(m_trust->id, scripts, folders);
}

void AutomationShell::answerTrust(int id, const QString &answer)
{
    if (!m_trust || m_trust->id != id)
        return;
    TrustQuestion question = std::move(*m_trust);
    m_trust.reset();
    const bool load = answer == QLatin1String("load") || answer == QLatin1String("always");
    for (const auto &script : question.scripts)
        m_trustDecisions[trustKey(question.document, script)] = load;
    if (answer == QLatin1String("always") && m_settings) {
        QStringList folders = trustedFolders();
        for (const auto &script : question.scripts) {
            const QString folder = QDir::toNativeSeparators(QFileInfo(absoluteScript(script)).absolutePath());
            if (!folders.contains(folder, windowsPaths() ? Qt::CaseInsensitive : Qt::CaseSensitive))
                folders << folder;
        }
        m_settings->set("automation.trustedFolders", folders);
        m_settings->sync();
    }
    if (session(question.document)) {
        readDocumentScripts(question.document);
        for (const auto &script : question.scripts)
            if (load && m_documentScripts.add(script))
                loadDocumentScript(script);
        for (auto &then : question.then)
            then();
    }
    emit trustAnswered();
}

void AutomationShell::readDocumentScripts(application::DocumentId document)
{
    const auto *s = session(document);
    const auto value = s ? s->document().scriptInfo(wide(application::kAutomationScriptsKey)) : std::nullopt;
    std::vector<std::string> added;
    if (value)
        m_readScriptInfo.insert(narrow(*value));
    m_documentScripts.readScriptInfo(
        value ? narrow(*value) : std::string(),
        [&](const std::string &path) {
            return QFileInfo(QString::fromStdString(path)).isFile() && permitted(document, path);
        },
        &added);
    for (const auto &path : added)
        loadDocumentScript(path);
}

QStringList AutomationShell::documentScriptsToLoad()
{
    const auto target = m_workspace.editingTarget();
    const auto *s = target ? session(*target) : nullptr;
    const auto value = s ? s->document().scriptInfo(wide(application::kAutomationScriptsKey)) : std::nullopt;
    QStringList out;
    if (!value)
        return out;
    const bool read = m_readScriptInfo.contains(narrow(*value));
    const auto declined = [&](const std::string &path) {
        const auto decided = m_trustDecisions.find(trustKey(*target, path));
        return decided != m_trustDecisions.end() && !decided->second;
    };
    // No file check: checking a UNC path contacts its server.
    const auto &listed = m_documentScripts.scripts();
    const std::unordered_set<std::string_view> loaded(listed.begin(), listed.end());
    std::unordered_set<std::string> offered;
    for (std::string &path : application::scriptInfoScriptPaths(narrow(*value)))
        if (!path.empty() && !loaded.contains(path) && (!read || declined(path)) && offered.insert(path).second)
            out << QString::fromStdString(path);
    return out;
}

void AutomationShell::menuOpened()
{
    if (!m_workspace.editingTarget())
        return;
    addFromDocument();
    for (const auto &script : m_manager.scripts())
        if (m_manager.modifiedSinceLoad(script.path))
            m_manager.reload(script.path);
}

void AutomationShell::runLastLoadedScript()
{
    if (m_documentScripts.empty())
        addFromDocument([this] { finishRunLastLoadedScript(); });
    else
        finishRunLastLoadedScript();
}

void AutomationShell::finishRunLastLoadedScript()
{
    if (m_documentScripts.empty()) {
        emit notice(QStringLiteral("info"), QCoreApplication::translate("HikariSub.MainWindow", "Run the last loaded script"),
                    QCoreApplication::translate("HikariSub.Automation", "This subtitle file does not have any scripts added"), {});
        return;
    }
    const std::string path = m_documentScripts.scripts().back();
    if (m_manager.modifiedSinceLoad(path))
        m_manager.reload(path);
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
        return;
    if (host && (m_run || m_manager.busy()))
        return;
    const std::string path = *m_pendingLast;
    m_pendingLast.reset();
    if (!host)
        return;
    if (host->state() == State::LoadFailed) {
        // MUST KILL: an empty %2 only keeps the existing translations of this two-line text
        emit notice(QStringLiteral("error"), QCoreApplication::translate("HikariSub.MainWindow", "Run the last loaded script"),
                    QCoreApplication::translate("HikariSub.Automation", "Error loading Lua script: %1\n%2")
                        .arg(prettyName(path), QString()).trimmed(),
                    host->lastError());
        editScript(QString::fromStdString(path));
        return;
    }
    if (host->info().macros.empty()) {
        emit notice(QStringLiteral("info"), QCoreApplication::translate("HikariSub.MainWindow", "Run the last loaded script"),
                    QCoreApplication::translate("HikariSub.Automation", "%1 has no macros to run").arg(prettyName(path)),
                    {});
        return;
    }
    if (!run(path, 0, RunOrigin::LastScript) && host->state() == State::Unavailable)
        emit notice(QStringLiteral("error"), QCoreApplication::translate("HikariSub.MainWindow", "Run the last loaded script"),
                    QCoreApplication::translate("HikariSub.Automation", "The script's helper ended; reload the script to use it again."),
                    {});
}

void AutomationShell::editScript(const QString &script)
{
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
        return;
    if (m_settings) {
        m_settings->set("automation.scriptEditor", editor);
        m_settings->sync();
    }
    startEditor(editor, script);
}

void AutomationShell::startEditor(const QString &editor, const QString &script)
{
    if (!backends::desktop::startDetached(editor, {script}))
        emit notice(QStringLiteral("error"), QCoreApplication::translate("HikariSub.Automation", "Edit script"),
                    QCoreApplication::translate("HikariSub.Automation", "Cannot start editor."), {});
}

void AutomationShell::unload(const std::string &path)
{
    m_manager.unload(path);
    m_documentScripts.remove(path);
}

bool AutomationShell::rerunLast()
{
    return m_last && run(m_last->first, m_last->second);
}

void AutomationShell::forgetRepeat()
{
    if (!m_repeat)
        return;
    m_repeat.reset();
    emit progressChanged();
}

bool AutomationShell::repeatLast()
{
    if (!m_repeat)
        return false;
    RepeatRecord record = *m_repeat;
    std::optional<int> ordinal;
    for (const auto &script : m_manager.scripts()) {
        if (script.path != record.path)
            continue;
        const auto &macros = script.info.macros;
        if (record.ordinal >= 0 && static_cast<std::size_t>(record.ordinal) < macros.size() &&
            macros[static_cast<std::size_t>(record.ordinal)].name == record.macro)
            ordinal = record.ordinal;
        for (std::size_t i = 0; !ordinal && i < macros.size(); ++i)
            if (macros[i].name == record.macro)
                ordinal = static_cast<int>(i);
    }
    if (!ordinal) {
        m_repeat.reset();
        m_lastMessage = QCoreApplication::translate("HikariSub.Automation",
                                                    "The last run's macro is no longer loaded, so it can't be repeated.");
        emit progressChanged();
        return false;
    }
    if (!run(record.path, *ordinal))
        return false;
    m_run->repeat = std::move(record.dialogs);
    m_run->replaying = true;
    m_log = QCoreApplication::translate("HikariSub.Automation", "Repeated with the last answers") + QLatin1Char('\n');
    emit progressChanged();
    return true;
}

QString AutomationShell::repeatText() const
{
    if (!m_repeat)
        return QCoreApplication::translate("HikariSub.Automation", "Repeat last run");
    const QString macro = QString::fromStdString(m_repeat->macro);
    const auto answers = std::ranges::find_if(m_repeat->dialogs | std::views::reverse,
                                              [](const AnsweredDialog &d) { return !application::isQuestion(d.request); });
    if (answers == (m_repeat->dialogs | std::views::reverse).end())
        return QCoreApplication::translate("HikariSub.Automation", "Repeat last run: %1").arg(macro);
    const auto &last = *answers;
    const auto pressed = static_cast<std::size_t>(std::max(0, last.answer.pressed));
    const QString button = last.request.buttons.empty()
                               ? QCoreApplication::translate("HikariSub.Automation", "OK")
                               : QString::fromStdString(pressed < last.request.buttons.size() ? last.request.buttons[pressed] : std::string());
    return QCoreApplication::translate("HikariSub.Automation", "Repeat last run: %1, as %2 with the same answers")
        .arg(macro, QString(button).replace(QRegularExpression(QStringLiteral("&(.)")), QStringLiteral("\\1")));
}

void AutomationShell::presentDialog(const application::DialogRequest &request, backends::LuaScriptHost::DialogReply reply)
{
    if (!m_run) {
        m_dialogs.present(m_title, request, std::move(reply));
        return;
    }
    const QString source = QString::fromStdString(m_run->path + '\n' + m_run->macro);
    const QString key = ui::ScriptDialogMemory::keyOf(QString::fromStdString(m_run->active), request);
    auto answered = [this, request, key, reply = std::move(reply)](application::DialogResult result) {
        const bool cancelled = application::cancelAnswer(request, result.pressed);
        if (m_run) {
            m_run->dialogs.push_back({request, result});
            m_run->cancelledAtDialog = m_run->cancelledAtDialog || cancelled;
        }
        auto last = application::lastDialogAnswer(request, result);
        if (!cancelled && !last.answered.empty())
            m_dialogMemory.remember(key, std::move(last));
        reply(std::move(result));
    };
    ui::AutomationDialogController::Shown shown;
    shown.memoryKey = key;
    if (m_run->repeat) {
        const std::size_t at = m_run->dialogs.size();
        const auto &recorded = *m_run->repeat;
        const bool inRecord = at < recorded.size();
        const bool same = m_run->replaying && inRecord && application::sameDialog(recorded[at].request, request);
        if (same && !application::isQuestion(request))
            return answered(application::replayedAnswer(request, recorded[at].answer));
        if (same) {
            shown.notice = QCoreApplication::translate("HikariSub.Automation",
                                                       "Repeat last run asks questions again rather than answering them for you.");
            m_dialogs.present(m_title, request, std::move(answered), source, std::move(shown));
            return;
        }
        shown.notice =
            !m_run->replaying ? QCoreApplication::translate(
                                    "HikariSub.Automation",
                                    "An earlier dialog of this run has changed, so this one is shown with your last answers filled in.")
            : inRecord ? QCoreApplication::translate("HikariSub.Automation",
                                                     "This dialog has changed since the last run, so it is shown with your last answers filled in.")
                       : QCoreApplication::translate("HikariSub.Automation",
                                                     "The last run did not show this dialog, so it is shown with your last answers filled in.");
        m_run->replaying = false;
        application::NamedDialogValues last;
        if (const auto own = m_dialogMemory.lastAnswer(key))
            last = own->answered;
        else if (inRecord)
            last = application::namedDialogValues(recorded[at].request, recorded[at].answer);
        shown.values = application::filledByName(request, application::initialDialogResult(request), last);
    } else if (m_dialogMemory.fillLastAnswers()) {
        if (const auto last = m_dialogMemory.lastAnswer(key))
            shown.values = application::safePrefill(request, *last);
    }
    m_dialogs.present(m_title, request, std::move(answered), source, std::move(shown));
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
        showCard();
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
    if (!validationError.isEmpty())
        m_runs.note(m_runId, validationError, ui::AutomationRuns::kHostError);
    if (!said.isEmpty())
        m_runs.note(m_runId, said, state == QLatin1String("failed") ? ui::AutomationRuns::kHostError
                                                                   : ui::AutomationRuns::kHostInfo);
    m_runs.finish(m_runId, state, said);
    m_cardText = said.left(ui::AutomationRuns::kMaxText);
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
        return false;
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
    if (m_beforeRun)
        m_beforeRun();
    auto snapshot = application::snapshotForMacro(*s);
    m_editor.reloadFromSession();
    if (!snapshot) {
        m_lastMessage = QCoreApplication::translate("HikariSub.Automation",
                                                    "The line being edited can't be committed, so the macro can't start.");
        emit progressChanged();
        return false;
    }
    s->setReadOnly(true);
    Run started{*target, std::move(*snapshot), 0, path, macro, ordinal, origin};
    started.generation = m_files.generation(*target);
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
    updateCardTimer();
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
        const auto *host = m_manager.host(v.path);
        if (!host || host->state() != State::Ready)
            continue;
        run.active = v.path;
        return m_manager.validate(v.path, v.ordinal, run.snapshot);
    }
    run.active = run.path;
    return m_manager.run(run.path, run.ordinal, run.snapshot, true);
}

void AutomationShell::resolveScriptResolution(const application::ScriptResolutionRequest &request,
                                              std::function<void(std::optional<application::Resolution>)> reply)
{
    auto *s = m_run ? session(m_run->target) : nullptr;
    if (!s || s->document().format() != core::SubtitleFormat::Ass ||
        !application::lacksScriptResolution(request.written))
        return reply(std::nullopt);
    std::optional<application::Resolution> video;
    if (const auto size = m_media->videoSize(); size && size->width > 0 && size->height > 0)
        video = application::Resolution{size->width, size->height};
    if (video && m_settings && m_settings->boolean("automation.scriptResolutionFromVideo")) {
        m_run->resolution = *video;
        return reply(*video);
    }
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
        if (m_run->resolution)
            application::setStagedScriptResolution(m_run->snapshot.info, *m_run->resolution);
        ++m_run->step;
        if (startStep(*m_run)) {
            emit progressChanged();
            return;
        }
        outcome = Outcome::HelperLost;
    }
    Run run = std::move(*m_run);
    m_run.reset();
    if (!run.cancelledAtDialog && (outcome == Outcome::Ok || outcome == Outcome::Failed))
        m_repeat = RepeatRecord{run.path, run.macro, run.ordinal, std::move(run.dialogs)};
    auto *s = session(run.target);
    const bool reloaded = s && m_files.generation(run.target) != run.generation;
    if (s)
        s->setReadOnly(false);
    if (reloaded)
        s = nullptr;
    m_router.withdraw();
    m_dialogs.withdraw();
    bool resolutionSet = false;
    if (s && run.resolution) {
        const bool current = s->revision() == run.snapshot.revision;
        resolutionSet = application::setScriptResolution(*s, *run.resolution).has_value();
        // MUST KILL: bumps the snapshot revision so the macro's result still applies
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
            else if (const auto report = ui::readKaraokeReport(host->lastResult()->report)) {
                karaokeReport(*report);
                emit karaokeApplied(run.target.value, QByteArray::fromStdString(host->lastResult()->report));
            }
        } else if (reloaded) {
            text = QCoreApplication::translate("HikariSub.Automation",
                                               "The subtitles were reloaded while the macro ran; nothing was changed.");
        } else if (!s) {
            text = QCoreApplication::translate("HikariSub.Automation",
                                               "Its document was closed while the macro ran; nothing was changed.");
        } else {
            text = QCoreApplication::translate("HikariSub.Automation",
                                               "The macro's changes could not be applied; nothing was changed.");
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

}
