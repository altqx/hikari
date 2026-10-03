#include "hikari/app/application.h"

#include "hikari/application/grid_clipboard.h"
#include "hikari/application/grid_commands.h"
#include "hikari/application/grid_filtering.h"
#include "hikari/application/grid_groups.h"
#include "hikari/application/grid_split.h"
#include "hikari/core/line_groups.h"
#include "hikari/core/style.h"
#include "hikari/core/subtitle_load.h"
#include "hikari/application/media_association.h"
#include "hikari/core/ass_save.h"
#include "automation_services_qt.h"

#include <QClipboard>
#include <QTextBoundaryFinder>
#include <QImage>
#include <QVideoFrame>
#include <cstring>
#include <QCollator>
#include <QGuiApplication>
#include <QLocale>
#include <QDir>
#include <QUuid>
#include <QCoreApplication>
#include <QSettings>
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
#endif

namespace hikari::app {

namespace {

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
    m_tagButtons = std::make_unique<ui::TagButtonsController>(m_settingsFile);
    m_colourPicker = std::make_unique<ui::ColourPickerController>(m_settingsFile);
    m_gridFilter = std::make_unique<ui::GridFilterController>(m_settingsFile);
    m_automationHotkeys = std::make_unique<AutomationHotkeysController>(*m_automation, m_settingsFile);
    // P3: this session's lock marks it as running; bundles of sessions whose
    // lock is gone or stale were left by a crash.
    m_recoveryDir = options.recoveryDir;
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    int capacity = application::RecoveryStore::kDefaultCapacity; // legacy AUTOSAVE_MAX_FILES
    if (!m_settingsFile.isEmpty())
        capacity = QSettings(m_settingsFile, QSettings::IniFormat).value(QStringLiteral("Recovery/Capacity"), capacity).toInt();
    if (!m_recoveryDir.isEmpty()) {
        QDir().mkpath(m_recoveryDir + QStringLiteral("/sessions"));
        m_sessionLock = std::make_unique<QLockFile>(m_recoveryDir + QStringLiteral("/sessions/") + m_sessionId + QStringLiteral(".lock"));
        m_sessionLock->tryLock(0);
    }
    m_recovery = std::make_unique<application::RecoveryStore>(
        m_recoveryDir.isEmpty() ? std::filesystem::path() : std::filesystem::path(m_recoveryDir.toStdU16String()),
        m_sessionId.toStdString(), m_recoveryDir.isEmpty() ? 0 : capacity);
    m_recovery->prune(std::chrono::system_clock::now());
    // GRID_HIDE_COLUMNS (G7).
    if (!m_settingsFile.isEmpty())
        m_shell->setHiddenColumns(QSettings(m_settingsFile, QSettings::IniFormat).value(QStringLiteral("Grid/HiddenColumns"), 0).toInt());
    connect(m_shell.get(), &ui::ShellController::hiddenColumnsChanged, this, [this] {
        if (!m_settingsFile.isEmpty())
            QSettings(m_settingsFile, QSettings::IniFormat).setValue(QStringLiteral("Grid/HiddenColumns"), m_shell->hiddenColumns());
    });
    if (!m_settingsFile.isEmpty()) {
        std::vector<std::string> stored;
        for (const QString &path : QSettings(m_settingsFile, QSettings::IniFormat).value(QStringLiteral("Recent/Subtitles")).toStringList())
            stored.push_back(path.toStdString());
        m_recent.set(std::move(stored));
        const QSettings settings(m_settingsFile, QSettings::IniFormat);
        m_copyColumns = settings.value(QStringLiteral("Grid/CopyColumns"), 0).toInt();
        m_pasteColumns = settings.value(QStringLiteral("Grid/PasteColumns"), 0).toInt();
    }
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
    discardRecovery(*target); // reviewed: saved or explicitly discarded
    m_files->close(*target);
    m_workspace.remove(*target);
    refreshViews();
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
    reportGroupBreak();
    scheduleAutosave();
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

QVariantList Application::reviewClose(const QString &then)
{
    m_closeThen = then;
    m_closing.clear();
    if (then != QLatin1String("open")) {
        m_pendingOpen.reset();
        m_pendingOpenPath.clear();
    }
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
        closeEditingTarget();
        newDocument();
    } else if (then == QLatin1String("open")) {
        closeEditingTarget();
        if (m_pendingOpen) {
            if (const auto id = publish(std::move(*m_pendingOpen), m_pendingOpenPath, false))
                m_workspace.setEditingTarget(*id);
            m_pendingOpen.reset();
            m_pendingOpenPath.clear();
        }
        refreshViews();
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
    m_pendingOpen.reset();
    m_pendingOpenPath.clear();
}

bool Application::targetUntitled() const
{
    const auto target = m_workspace.editingTarget();
    const auto destination = target ? m_files->destination(*target) : std::nullopt;
    return target && (!destination || destination->value.empty());
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
    return subtitles;
}

void Application::rememberRecent(const std::string &path)
{
    m_recent.add(path);
    if (!m_settingsFile.isEmpty()) {
        QStringList list;
        for (const auto &entry : m_recent.entries())
            list << QString::fromStdString(entry);
        QSettings(m_settingsFile, QSettings::IniFormat).setValue(QStringLiteral("Recent/Subtitles"), list);
    }
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
    if (pruned && !m_settingsFile.isEmpty()) {
        QStringList list;
        for (const auto &entry : m_recent.entries())
            list << QString::fromStdString(entry);
        QSettings(m_settingsFile, QSettings::IniFormat).setValue(QStringLiteral("Recent/Subtitles"), list);
    }
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
    if (!m_settingsFile.isEmpty())
        QSettings(m_settingsFile, QSettings::IniFormat)
            .setValue(paste ? QStringLiteral("Grid/PasteColumns") : QStringLiteral("Grid/CopyColumns"), columns);
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
    return runFilter([&](application::EditSession &s) { return application::toggleHiddenBlock(s, documentRow); });
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

bool Application::makeGroups()
{
    const auto shown = shownLines();
    return runFilter([&](application::EditSession &s) { return application::makeGroups(s, shown); });
}

bool Application::toggleGroup(qulonglong description)
{
    return runFilter([&](application::EditSession &s) { return application::toggleGroup(s, core::LineId{description}); });
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
            {QStringLiteral("log"), QVariant::fromValue(m_log.get())},
            {QStringLiteral("tagButtons"), QVariant::fromValue(m_tagButtons.get())},
            {QStringLiteral("colourPicker"), QVariant::fromValue(m_colourPicker.get())},
            {QStringLiteral("gridFilter"), QVariant::fromValue(m_gridFilter.get())},
            {QStringLiteral("automationHotkeys"), QVariant::fromValue(static_cast<QObject *>(m_automationHotkeys.get()))},
            {QStringLiteral("app"), QVariant::fromValue(static_cast<QObject *>(this))}};
}

void Application::waitForWrites()
{
    m_port->waitIdle();
    QCoreApplication::processEvents();
}

} // namespace hikari::app
