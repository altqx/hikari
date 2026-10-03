#include "hikari/app/application.h"

#include "hikari/application/grid_commands.h"
#include "hikari/application/media_association.h"
#include "hikari/core/ass_save.h"

#include <QCoreApplication>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <cstdlib>
#include <QFileInfo>

namespace hikari::app {

namespace {

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
    m_shell = std::make_unique<ui::ShellController>(m_workspace);
    m_editor = std::make_unique<ui::LineEditorController>(*m_files);
    m_editor->setCommittedListener([this] { refreshViews(); });
    m_mediaSource = std::make_unique<backends::FfmsIndexedSource>(mediaHelperPath(options.mediaHelper));
    m_video = std::make_unique<ui::VideoController>(*m_mediaSource, m_renderer);
    m_automation = std::make_unique<AutomationShell>(
        AutomationShell::Paths{luaHelperPath(options.luaHelper), automationPath(options.automationDir)}, *m_files,
        m_workspace, *m_editor, *m_video, *m_shell, *m_mediaSource);
    m_automation->setDocumentChanged([this] { refreshViews(); });
    if (options.autoload)
        m_automation->autoload();
    connect(m_editor.get(), &ui::LineEditorController::changed, this, [this] { refreshVideo(); });
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
    refreshViews();
}

Application::~Application()
{
    m_port->waitIdle(); // no write may outlive the services it reports to
}

std::optional<application::DocumentId> Application::open(const QString &path, bool asReference)
{
    auto staged = m_files->stageOpen({QFileInfo(path).absoluteFilePath().toStdString()});
    if (!staged)
        return std::nullopt;
    auto id = m_files->activate(std::move(*staged));
    if (!id)
        return std::nullopt;
    // Legacy SubsGrid::LoadSubtitles: the ASS "Active Line" (or the first Line)
    // is active, selected and the anchor, and the editor shows it.
    if (auto *session = m_files->session(*id); session && !session->selection().active) {
        const auto lines = session->document().lines();
        if (!lines.empty()) {
            std::size_t active = 0;
            if (session->document().format() == core::SubtitleFormat::Ass) {
                const auto value = session->document().scriptInfo(u8"Active Line").value_or(std::u8string());
                const std::string text(value.begin(), value.end());
                const long n = std::strtol(text.c_str(), nullptr, 10); // wxAtoi
                if (n > 0 && static_cast<std::size_t>(n) < lines.size())
                    active = static_cast<std::size_t>(n);
            }
            const auto line = lines[active]->id;
            session->setSelection(application::Selection{line, {line}, line, {}});
        }
    }
    m_workspace.add(*id, QFileInfo(path).fileName().toStdString(), asReference);
    return *id;
}

bool Application::openFile(const QString &path)
{
    if (!open(path))
        return false;
    refreshViews();
    return true;
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
    m_files->close(*target);
    m_workspace.remove(*target);
    refreshViews();
    return true;
}

void Application::refreshViews()
{
    const auto target = m_workspace.editingTarget();
    const auto reference = m_workspace.reference();
    auto *targetSession = target ? m_files->session(*target) : nullptr;
    auto *referenceSession = reference ? m_files->session(*reference) : nullptr;
    m_shell->refresh(targetSession ? &targetSession->document() : nullptr,
                     referenceSession ? &referenceSession->document() : nullptr);
    if (targetSession)
        m_shell->setSelection(targetSession->selection());
    // The editor only ever edits the editing target, never the reference.
    if (target != m_editorDocument) {
        m_editorDocument = target;
        m_editor->setDocument(target, target && m_workspace.checkContentCommand(*target).has_value());
    }
    refreshVideo();
}

void Application::refreshVideo()
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (target != m_videoDocument) {
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
    }
    if (!session)
        return;
    if (session->revision() != m_videoRevision) {
        m_videoRevision = session->revision();
        m_video->session().setSubtitles(core::encodeAss(session->document()));
    }
    const auto active = session->selection().active;
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

QVariantList Application::reviewClose(const QString &then)
{
    m_closeThen = then;
    m_closing.clear();
    std::vector<application::DocumentId> scope;
    if (then == QLatin1String("quit"))
        scope = m_workspace.documents();
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
    if (result.outcome == application::WriteOutcome::Written)
        if (const std::string *title = m_workspace.title(result.document);
            title && QString::fromStdString(*title) != fileTitle(result.destination.value)) {
            const auto destination = m_files->destination(result.document);
            if (destination && destination->value == result.destination.value) {
                m_workspace.setTitle(result.document, fileTitle(result.destination.value).toStdString());
                refreshViews();
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
        m_quitApproved = true;
        emit quitApprovedChanged();
    } else if (then == QLatin1String("new")) {
        closeEditingTarget();
        newDocument();
    } else {
        closeEditingTarget();
    }
    emit closeFinished(true, QString());
}

void Application::cancelClose()
{
    // Saves already completed stay completed (accepted quit review).
    m_closing.clear();
    m_closeThen.clear();
}

bool Application::targetUntitled() const
{
    const auto target = m_workspace.editingTarget();
    const auto destination = target ? m_files->destination(*target) : std::nullopt;
    return target && (!destination || destination->value.empty());
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
    rules.setChangeActiveOnSelection(m_changeActiveOnSelection);
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

bool Application::duplicateLines()
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session)
        return false;
    const bool done = application::duplicateLines(*session).has_value();
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
            {QStringLiteral("automation"), QVariant::fromValue(static_cast<QObject *>(m_automation.get()))},
            {QStringLiteral("automationManager"), QVariant::fromValue(m_automation->managerController())},
            {QStringLiteral("automationDialogs"), QVariant::fromValue(m_automation->dialogs())},
            {QStringLiteral("automationPicker"), QVariant::fromValue(m_automation->picker())},
            {QStringLiteral("app"), QVariant::fromValue(static_cast<QObject *>(this))}};
}

void Application::waitForWrites()
{
    m_port->waitIdle();
    QCoreApplication::processEvents();
}

} // namespace hikari::app
