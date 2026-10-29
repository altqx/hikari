#include "hikari/app/application.h"

#include "hikari/app/session_json.h"
#include "hikari/application/grid_clipboard.h"
#include "hikari/application/owned_file.h"
#include "hikari/application/session_file.h"

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>

namespace hikari::app {

namespace {

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

std::string utf8(const QString &s)
{
    return s.toStdString();
}

bool isFile(const QString &path)
{
    return !path.isEmpty() && QFileInfo(path).isFile();
}

// UNC and \\?\ or \\.\ device paths can reach other computers; \\?\C:\ is a local drive.
QString networkTarget(const QString &p)
{
    auto separator = [&](qsizetype i) { return i < p.size() && (p[i] == u'\\' || p[i] == u'/'); };
    if (!separator(0) || !separator(1))
        return {};
    auto host = [&](const QString &rest) {
        const QString name = rest.section(QRegularExpression(QStringLiteral("[\\\\/]")), 0, 0);
        return name.isEmpty() ? p : name;
    };
    const bool device = p.size() > 3 && (p[2] == u'?' || p[2] == u'.') && separator(3);
    if (!device)
        return host(p.mid(2));
    const QString rest = p.mid(4);
    const auto drive = rest.size() >= 2 ? rest[0].toLatin1() : 0;
    if (((drive >= 'A' && drive <= 'Z') || (drive >= 'a' && drive <= 'z')) && rest[1] == u':')
        return {};
    if (rest.startsWith(QLatin1String("UNC"), Qt::CaseInsensitive) && rest.size() > 3 &&
        (rest[3] == u'\\' || rest[3] == u'/'))
        return host(rest.mid(4));
    return p;
}

QStringList networkHosts(const application::Session &session)
{
    QStringList hosts;
    for (const auto &tab : session.tabs)
        for (const std::string *path : {&tab.subtitles, &tab.video, &tab.audio, &tab.keyframes})
            if (const QString target = networkTarget(QString::fromStdString(*path));
                !target.isEmpty() && !hosts.contains(target, Qt::CaseInsensitive))
                hosts << target;
    return hosts;
}

std::filesystem::path fsPath(const QString &path)
{
    return std::filesystem::path(path.toStdU16String());
}

QString fileName(const std::filesystem::path &path)
{
    return QString::fromStdU16String(path.filename().u16string());
}

QString sessionProblem(const QString &file, const application::ParseFailure &failure)
{
    QString why = QString::fromStdString(failure.message);
    if (!why.isEmpty() && !why.endsWith(QLatin1Char('.')))
        why += QLatin1Char('.'); // QJsonParseError's text has no full stop
    return failure.line > 0
               ? QCoreApplication::translate("HikariSub.Session", "%1, line %2: %3").arg(file).arg(failure.line).arg(why)
               : QCoreApplication::translate("HikariSub.Session", "%1: %2").arg(file, why);
}

std::optional<QByteArray> readSessionBytes(const QString &path)
{
    const auto bytes = application::readOwnedFile(fsPath(path));
    if (!bytes)
        return std::nullopt;
    return QByteArray(bytes->data(), static_cast<qsizetype>(bytes->size()));
}

}

QVariantList Application::tabs() const
{
    QVariantList rows;
    const auto target = m_workspace.editingTarget();
    for (const auto id : m_workspace.tabs()) {
        const std::string *title = m_workspace.title(id);
        const QString name = title ? qs(*title) : QString();
        QString label = name;
        const auto *session = m_files->session(id);
        const bool modified = session && session->isDirty();
        if (modified)
            label = QStringLiteral("%1*%2").arg(session->historyCursor()).arg(name);
        label.truncate(m_tabTextMax);
        QString video;
        if (const auto it = m_tabMedia.find(id.value); it != m_tabMedia.end() && !it->second.video.isEmpty())
            video = QFileInfo(it->second.video).fileName();
        rows << QVariantMap{{QStringLiteral("id"), QVariant::fromValue<qulonglong>(id.value)},
                            {QStringLiteral("label"), label},
                            {QStringLiteral("title"), name},
                            {QStringLiteral("modified"), modified},
                            {QStringLiteral("current"), target == id},
                            {QStringLiteral("reference"), m_workspace.reference() == id},
                            {QStringLiteral("tip"), name + QLatin1Char('\n') + video},
                            {QStringLiteral("video"), video}};
    }
    return rows;
}

int Application::currentTab() const
{
    const auto tabs = m_workspace.tabs();
    const auto target = m_workspace.editingTarget();
    const auto it = target ? std::ranges::find(tabs, *target) : tabs.end();
    return it == tabs.end() ? -1 : static_cast<int>(it - tabs.begin());
}

qulonglong Application::editingDocument() const
{
    const auto target = m_workspace.editingTarget();
    return target ? target->value : 0;
}

void Application::addPage(bool fromTabBar)
{
    newDocument();
    if (fromTabBar)
        saveLastSession();
}

void Application::changeTab(int step)
{
    const auto tabs = m_workspace.tabs();
    if (tabs.size() < 2)
        return;
    const int size = static_cast<int>(tabs.size());
    int next = currentTab() + (step < 0 ? -1 : 1);
    if (next < 0)
        next = size - 1;
    else if (next >= size)
        next = 0;
    selectTab(next);
}

void Application::selectTab(int index)
{
    const auto tabs = m_workspace.tabs();
    if (index < 0 || index >= static_cast<int>(tabs.size()) || index == currentTab())
        return;
    if (!m_workspace.setEditingTarget(tabs[static_cast<std::size_t>(index)]))
        return;
    refreshViews();
}

QVariantList Application::reviewCloseTab(int index)
{
    const auto tabs = m_workspace.tabs();
    if (index < 0 || index >= static_cast<int>(tabs.size()))
        return {};
    m_closingTab = tabs[static_cast<std::size_t>(index)];
    return reviewClose(QStringLiteral("tab"));
}

void Application::closeDocument(application::DocumentId document)
{
    m_comparison.remove();
    m_comparison.forget(document);
    discardRecovery(document);
    m_files->close(document);
    m_workspace.remove(document);
    forgetTab(document);
    refreshViews();
    saveLastSession();
}

void Application::forgetTab(application::DocumentId document)
{
    m_tabMedia.erase(document.value);
    m_workspaceLayout->forgetTab(document.value);
    m_importPaths.erase(document.value);
    m_lyricsImports.erase(document.value);
    m_originalColumns.erase(document.value);
    m_translationDeclined.erase(document.value);
    m_offers.erase(document.value);
    if (m_kanjiTimer)
        m_kanjiTimer->forget(document.value);
    const auto before = m_unresolved.size();
    std::erase_if(m_unresolved, [&](const UnresolvedRestore &u) { return u.document == document; });
    if (m_unresolved.size() != before)
        emit unresolvedRestoresChanged();
}

void Application::replaceTarget(application::DocumentId replacement, bool keepMedia)
{
    const auto old = m_workspace.editingTarget();
    if (!old || *old == replacement) {
        m_workspace.setEditingTarget(replacement);
        refreshViews();
        return;
    }
    TabMedia media;
    if (const auto it = m_tabMedia.find(old->value); it != m_tabMedia.end())
        media = it->second;
    if (keepMedia) {
        media.scroll = 0;
        if (QString::fromStdString(m_video->session().path()) == media.video &&
            m_video->session().state() == application::VideoSession::State::Ready)
            media.position = targetVideoPosition();
    } else {
        media.video.clear();
        media.position = 0;
    }
    media.audio = m_audio->hasAudio() ? QDir::toNativeSeparators(m_audio->path()) : QString();
    m_keepVideoOnEnter = keepMedia;
    discardRecovery(*old);
    m_files->close(*old);
    m_workspace.replace(*old, replacement);
    openIntoTabArrangement(*old, replacement);
    m_comparison.replaced(*old, replacement);
    forgetTab(*old);
    m_tabMedia[replacement.value] = media;
    refreshViews();
}

void Application::setTargetScroll(int row)
{
    if (m_scrollRestoring)
        return;
    if (const auto target = m_workspace.editingTarget())
        m_tabMedia[target->value].scroll = std::max(0, row);
}

ui::WorkspaceLayoutController::TabStart Application::tabStart(application::DocumentId document) const
{
    const auto *session = m_files->session(document);
    return session && application::translationMode(*session) ? ui::WorkspaceLayoutController::TabStart::Translation
                                                             : ui::WorkspaceLayoutController::TabStart::Copy;
}

void Application::replaceTabArrangement(application::DocumentId old, application::DocumentId replacement,
                                        ui::WorkspaceLayoutController::TabStart start)
{
    m_workspaceLayout->replaceTab(old.value, replacement.value, start);
    if (m_layoutDocument == old)
        m_layoutDocument = replacement;
}

void Application::registerTabArrangements()
{
    for (const auto id : m_workspace.tabs())
        m_workspaceLayout->addTab(id.value, tabStart(id));
}

namespace {
constexpr const char *kLayoutInFile = "subtitles.layoutInFile";
}

void Application::setUpFileArrangements()
{
    m_files->setLayoutSource([this](application::DocumentId document) -> std::optional<std::u8string> {
        if (!m_settings->boolean(kLayoutInFile))
            return std::nullopt;
        const QByteArray text = m_workspaceLayout->fileArrangement(document.value);
        if (text.isEmpty())
            return std::nullopt;
        return std::u8string(reinterpret_cast<const char8_t *>(text.constData()), static_cast<std::size_t>(text.size()));
    });
}

std::optional<QByteArray> Application::fileArrangement(application::DocumentId document, bool report)
{
    if (!m_settings->boolean(kLayoutInFile))
        return std::nullopt;
    const auto *session = m_files->session(document);
    const auto layout = session ? session->document().hikariProperty(application::kLayoutKey) : std::nullopt;
    if (!layout)
        return std::nullopt;
    const QByteArrayView text(reinterpret_cast<const char *>(layout->data()), static_cast<qsizetype>(layout->size()));
    QString problem;
    if (ui::WorkspaceArrangement::parseFromFile(text, {}, &problem))
        return text.toByteArray();
    if (report) {
        const std::string *title = m_workspace.title(document);
        m_log->log(QCoreApplication::translate("HikariSub.Workspace", "The panel layout saved in %1 was not used because %2.")
                       .arg(title ? qs(*title) : QString(), problem));
    }
    return std::nullopt;
}

void Application::takeFileArrangement(application::DocumentId document)
{
    if (const auto file = fileArrangement(document, true))
        m_workspaceLayout->setFileArrangement(document.value, *file);
}

void Application::openIntoTabArrangement(application::DocumentId old, application::DocumentId replacement)
{
    const auto file = fileArrangement(replacement);
    replaceTabArrangement(old, replacement, file ? ui::WorkspaceLayoutController::TabStart::Copy : tabStart(replacement));
    if (file)
        m_workspaceLayout->setFileArrangement(replacement.value, *file);
}

void Application::showTabArrangement()
{
    registerTabArrangements();
    const auto target = m_workspace.editingTarget();
    if (!target || target == m_layoutDocument)
        return;
    m_layoutDocument = target;
    m_workspaceLayout->showTab(target->value, tabStart(*target));
}

void Application::trackTabMedia()
{
    if (const int chars = m_settings->integer("program.tabTextMaxChars"); chars > 19)
        m_tabTextMax = chars;
    connect(m_video.get(), &ui::VideoController::changed, this, [this] {
        const auto &video = m_video->session();
        const auto target = m_workspace.editingTarget();
        if (!target || video.state() != application::VideoSession::State::Ready)
            return;
        TabMedia &media = m_tabMedia[target->value];
        const QString path = QString::fromStdString(video.path());
        if (media.video != path) {
            media.video = path;
            emit tabsChanged();
        }
        if (m_pendingTabSeek && !m_tabSeekQueued && m_pendingTabSeek->first == path && video.frameCount() > 0) {
            m_tabSeekQueued = true;
            // MUST KILL: queued to land after the session's own pending seek on Ready
            QMetaObject::invokeMethod(this, [this, path] {
                if (!m_tabSeekQueued || !m_pendingTabSeek || m_pendingTabSeek->first != path)
                    return;
                const int ms = std::exchange(m_pendingTabSeek, std::nullopt)->second;
                m_tabSeekQueued = false;
                auto &session = m_video->session();
                if (QString::fromStdString(session.path()) == path &&
                    session.state() == application::VideoSession::State::Ready)
                    session.seekTo(core::DocumentTime(std::int64_t{ms} * 1000));
            }, Qt::QueuedConnection);
        }
    });
    m_audioConnections << connect(m_audio.get(), &ui::AudioController::opened, this, [this](const QString &path, bool) {
        const auto target = m_workspace.editingTarget();
        if (!target)
            return;
        const QString native = QDir::toNativeSeparators(path);
        const QString video = QString::fromStdString(m_video->session().path());
        TabMedia &media = m_tabMedia[target->value];
        const bool fromVideo = isVideoAudio(native, video);
        media.audio = fromVideo ? QString() : native;
        if (!fromVideo)
            media.audioFromVideo = false;
    });
    m_audioConnections << connect(m_audio.get(), &ui::AudioController::changed, this, [this] {
        const auto target = m_workspace.editingTarget();
        if (target && !m_audio->hasAudio())
            if (const auto it = m_tabMedia.find(target->value); it != m_tabMedia.end()) {
                it->second.audio.clear();
                it->second.audioFromVideo = false;
            }
    });
}

int Application::targetVideoPosition() const
{
    const auto &video = m_video->session();
    if (video.state() != application::VideoSession::State::Ready)
        return 0;
    const auto frame = video.reportedFrame();
    return frame ? video.legacyTimebase().msAt(*frame) : 0;
}

void Application::leaveTabMedia(std::optional<application::DocumentId> document)
{
    if (!document)
        return;
    const auto it = m_tabMedia.find(document->value);
    if (it == m_tabMedia.end())
        return;
    const auto &video = m_video->session();
    if (!m_pendingTabSeek && !it->second.video.isEmpty() && QString::fromStdString(video.path()) == it->second.video &&
        video.state() == application::VideoSession::State::Ready)
        it->second.position = targetVideoPosition();
}

void Application::enterTabMedia(std::optional<application::DocumentId> previous,
                                std::optional<application::DocumentId> document)
{
    m_pendingTabSeek.reset();
    m_tabSeekQueued = false;
    m_scrollRestoring = true;
    m_keepTabAudio.clear();
    const bool known = document && m_tabMedia.contains(document->value);
    if (!known) {
        if (document && !previous)
            m_tabMedia[document->value].audio =
                m_audio->hasAudio() ? QDir::toNativeSeparators(m_audio->path()) : QString();
        else if (m_audio->hasAudio())
            m_audio->closeAudio();
        if (!document || previous)
            m_pendingKeyframes.clear();
        if (document && previous)
            m_tabMedia[document->value];
        emit tabShown(0);
        return;
    }
    const TabMedia media = m_tabMedia[document->value];
    const auto &shownVideo = m_video->session();
    const bool keepVideo = std::exchange(m_keepVideoOnEnter, false) && !media.video.isEmpty() &&
                           QString::fromStdString(shownVideo.path()) == media.video &&
                           shownVideo.state() == application::VideoSession::State::Ready;
    if (!keepVideo)
        m_pendingKeyframes = media.keyframes;
    if (!media.video.isEmpty() && !keepVideo) {
        m_pendingTabSeek = std::pair(media.video, media.position);
        if (!media.audio.isEmpty())
            m_keepTabAudio = media.video;
        m_video->openVideo(media.video);
    }
    const QString shown = m_audio->hasAudio() ? QDir::toNativeSeparators(m_audio->path()) : QString();
    if (!media.audio.isEmpty()) {
        if (shown != media.audio)
            m_audio->openAudio(media.audio);
    } else if (!shown.isEmpty() && !isVideoAudio(shown, media.video)) {
        m_audio->closeAudio();
    }
    emit tabShown(media.scroll);
}

bool Application::keepTabAudio(const QString &videoPath)
{
    if (m_keepTabAudio.isEmpty() || QDir::toNativeSeparators(videoPath) != QDir::toNativeSeparators(m_keepTabAudio))
        return false;
    m_keepTabAudio.clear();
    return true;
}

QString Application::lastSessionPath() const
{
    return m_settingsFile.isEmpty() ? QString()
                                    : QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/last-session.json");
}

void Application::convertLastSession()
{
    const QString target = lastSessionPath();
    if (target.isEmpty())
        return;
    const QString folder = QFileInfo(m_settingsFile).absolutePath();
    const QString legacy = folder + QStringLiteral("/LastSession.txt");
    if (!QFileInfo::exists(legacy))
        return;
    QLockFile lock(folder + QStringLiteral("/.last-session.lock"));
    if (!lock.tryLock(2000) && lock.error() == QLockFile::LockFailedError)
        return;
    application::convertOnce(fsPath(target), fsPath(legacy), [&] {
        const auto bytes = readSessionBytes(legacy);
        if (!bytes)
            return false;
        application::ParseFailure failure;
        const auto session =
            readSession(std::string_view(bytes->constData(), static_cast<std::size_t>(bytes->size())), &failure);
        if (!session) {
            if (!failure.message.empty())
                m_log->notice(ui::LogController::Warning,
                              QCoreApplication::translate("HikariSub.MainWindow", "Load last session"),
                              QCoreApplication::translate("HikariSub.Session", "%1 It was kept as %2.")
                                  .arg(sessionProblem(QStringLiteral("LastSession.txt"), failure),
                                       QStringLiteral("LastSession.txt.old")));
            return true;
        }
        return application::writeFileAtomically(fsPath(target), writeSessionJson(*session, session->closed).toStdString(),
                                                fsPath(legacy));
    });
}

std::optional<application::Session> Application::loadLastSession()
{
    const QString path = lastSessionPath();
    if (path.isEmpty())
        return std::nullopt;
    std::optional<application::Session> session;
    const auto load = application::loadOwnedFile(
        fsPath(path), [&](std::string_view bytes) -> std::optional<application::ParseFailure> {
            application::ParseFailure failure;
            session = readSession(bytes, &failure);
            if (!session && !failure.message.empty())
                return failure;
            return std::nullopt;
        });
    m_lastSessionWritable = !load.problem || !load.problem->keptAs.empty();
    if (load.problem) {
        const QString problem = sessionProblem(fileName(load.problem->file), load.problem->failure);
        m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.MainWindow", "Load last session"),
                      load.problem->keptAs.empty()
                          ? QCoreApplication::translate("HikariSub.Session",
                                                        "%1 No tabs were restored, and HikariSub will not write over it.")
                                .arg(problem)
                          : QCoreApplication::translate("HikariSub.Session", "%1 It was kept as %2; no tabs were restored.")
                                .arg(problem, fileName(load.problem->keptAs)));
        session.reset();
    }
    return session;
}

QUrl Application::sessionFolder() const
{
    return m_settingsFile.isEmpty() ? QUrl() : QUrl::fromLocalFile(QFileInfo(m_settingsFile).absolutePath());
}

int Application::sessionRestore() const
{
    return m_settings->integer("session.restore");
}

void Application::setSessionRestore(int value)
{
    if (value == sessionRestore())
        return;
    m_settings->set("session.restore", value);
    emit sessionRestoreChanged();
}

bool Application::saveLastSession(bool closing, const QString &path)
{
    const QString file = path.isEmpty() ? lastSessionPath() : path;
    if (file.isEmpty() || (path.isEmpty() && !m_lastSessionWritable))
        return false;
    const auto target = m_workspace.editingTarget();
    registerTabArrangements();
    application::Session session;
    session.header = "HikariSub " HIKARI_VERSION;
    auto &tabs = session.tabs;
    for (const auto id : m_workspace.tabs()) {
        if (id == target)
            session.activeTab = tabs.size();
        application::SessionTab tab;
        TabMedia media;
        if (const auto it = m_tabMedia.find(id.value); it != m_tabMedia.end())
            media = it->second;
        auto unresolved = [&](const char *kind) -> const UnresolvedRestore * {
            for (const auto &u : m_unresolved)
                if (u.document == id && u.kind == QLatin1String(kind))
                    return &u;
            return nullptr;
        };
        tab.video = utf8(media.video);
        tab.position = media.position;
        if (id == target && !media.video.isEmpty() && !m_pendingTabSeek &&
            m_video->session().state() == application::VideoSession::State::Ready &&
            QString::fromStdString(m_video->session().path()) == media.video)
            tab.position = targetVideoPosition();
        if (const auto *u = unresolved("video"); u && media.video.isEmpty()) {
            tab.video = utf8(u->path);
            tab.position = u->position;
        }
        if (const auto destination = m_files->destination(id); destination && !destination->value.empty())
            tab.subtitles = utf8(QDir::toNativeSeparators(qs(destination->value)));
        else if (const auto *u = unresolved("subtitles"))
            tab.subtitles = utf8(u->path);
        if (auto *session = m_files->session(id); session && session->selection().active) {
            const auto lines = session->document().lines();
            for (std::size_t row = 0; row < lines.size(); ++row)
                if (lines[row]->id == *session->selection().active)
                    tab.active = static_cast<int>(row);
        }
        tab.scroll = media.scroll;
        tab.audio = utf8(media.audio);
        if (const auto *u = unresolved("audio"); u && media.audio.isEmpty())
            tab.audio = utf8(u->path);
        tab.keyframes = utf8(media.keyframes);
        if (const auto *u = unresolved("keyframes"); u && media.keyframes.isEmpty())
            tab.keyframes = utf8(u->path);
        if (const auto arrangement = m_workspaceLayout->tabArrangement(id.value))
            tab.layout = arrangement->serialise().toStdString();
        tabs.push_back(std::move(tab));
    }
    const QByteArray bytes = writeSessionJson(session, path.isEmpty() ? std::optional<bool>(closing) : std::nullopt);
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(bytes);
    return out.commit();
}

void Application::endSession()
{
    saveLastSession(true);
}

QString Application::saveSessionTo(const QUrl &file, bool replace)
{
    QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty())
        return QStringLiteral("failed");
    if (path.endsWith(QLatin1String(".kls"), Qt::CaseInsensitive)) {
        path = path.chopped(4) + QStringLiteral(".hksession");
        if (!replace && QFileInfo::exists(path))
            return QStringLiteral("exists");
    }
    if (const QFileInfo info(path); info.exists() && !info.isWritable())
        return QStringLiteral("readonly");
    return saveLastSession(false, path) ? QString() : QStringLiteral("failed");
}

namespace {

std::optional<application::Session> readSessionAt(const QString &path, application::ParseFailure *failure = nullptr)
{
    if (path.isEmpty())
        return std::nullopt;
    const auto bytes = readSessionBytes(path);
    if (!bytes) {
        if (failure && QFileInfo::exists(path))
            *failure = {0, 0, QCoreApplication::translate("HikariSub.Session", "It could not be read.").toStdString()};
        return std::nullopt;
    }
    return readSession(std::string_view(bytes->constData(), static_cast<std::size_t>(bytes->size())), failure);
}

}

bool Application::lastSessionCrashed() const
{
    const auto session = readSessionAt(lastSessionPath());
    return session && !session->closed;
}

QVariantMap Application::lastSessionSummary() const
{
    return {{QStringLiteral("names"), sessionNames(readSessionAt(lastSessionPath()))}};
}

QStringList Application::sessionNames(const std::optional<application::Session> &session)
{
    QStringList names;
    if (session)
        for (const auto &tab : session->tabs) {
            const bool dummy = tab.subtitles.empty() && tab.video.starts_with("?dummy");
            const QString file = !tab.subtitles.empty() ? qs(tab.subtitles) : dummy ? QString() : qs(tab.video);
            if (!file.isEmpty())
                names << QFileInfo(file).fileName();
        }
    return names;
}

void Application::rememberStartSession()
{
    convertLastSession();
    m_startSession = loadLastSession();
}

QString Application::startupSession() const
{
    if (m_startedWithPaths || lastSessionPath().isEmpty())
        return {};
    QString kind;
    switch (sessionRestore()) {
    case 2: kind = QStringLiteral("load"); break;
    case 1: kind = QStringLiteral("ask"); break;
    default: kind = lastSessionCrashed() ? QStringLiteral("crash") : QString(); break;
    }
    m_startupSessionPending = !kind.isEmpty();
    return kind;
}

QString Application::startupOffer() const
{
    if ((m_startSession && !m_startSession->closed) || !recoveryBundles().isEmpty())
        return QStringLiteral("crash");
    if (m_startSession && sessionRestore() == 1)
        return QStringLiteral("ask");
    return {};
}

QStringList Application::startupOfferNames() const
{
    return sessionNames(m_startSession);
}

QVariantMap Application::reviewStartupSession()
{
    return reviewSessionRead(m_startSession);
}

bool Application::forwardedOpenWaits() const
{
    return m_startupSessionPending || !m_closeThen.isEmpty() || m_quitApproved;
}

QVariantMap Application::reviewSession(const QUrl &file, const QStringList &allowedHosts)
{
    if (file.isEmpty())
        return reviewSessionRead(loadLastSession());
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    application::ParseFailure failure;
    auto session = readSessionAt(path, &failure);
    if (!session && !failure.message.empty()) {
        const QString problem = sessionProblem(QFileInfo(path).fileName(), failure);
        m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.MainWindow", "Load session from file"),
                      problem);
        return reviewSessionRead(std::nullopt, problem);
    }
    // Looking up a file on another computer connects to it (Windows signs in to the SMB share).
    const QStringList hosts = session ? networkHosts(*session) : QStringList();
    if (!std::ranges::all_of(hosts, [&](const QString &host) { return allowedHosts.contains(host, Qt::CaseInsensitive); })) {
        QVariantMap result = reviewSessionRead(std::nullopt);
        result.insert(QStringLiteral("network"), hosts);
        return result;
    }
    return reviewSessionRead(std::move(session));
}

QVariantMap Application::reviewSessionRead(std::optional<application::Session> session, const QString &problem)
{
    auto result = [](bool ok, const QString &problem, const QVariantList &rows = {}) {
        return QVariantMap{{QStringLiteral("ok"), ok}, {QStringLiteral("problem"), problem}, {QStringLiteral("rows"), rows}};
    };
    if (!session)
        return result(false, problem);
    PendingSession pending;
    for (const auto &tab : session->tabs) {
        std::optional<application::StagedOpen> staged;
        const QString subtitles = qs(tab.subtitles);
        const QFileInfo info(subtitles);
        if (isFile(subtitles))
            if (auto s = m_files->stageOpen({info.absoluteFilePath().toStdString()}))
                staged = std::move(*s);
        pending.stamps.emplace_back(staged ? info.lastModified() : QDateTime(), staged ? info.size() : -1);
        pending.staged.push_back(std::move(staged));
    }
    pending.tabs = std::move(session->tabs);
    pending.activeTab = session->activeTab;
    const QVariantList rows = reviewClose(QStringLiteral("session"));
    m_pendingSession = std::move(pending);
    return result(true, QString(), rows);
}

void Application::selectRow(application::DocumentId document, int row)
{
    auto *session = m_files->session(document);
    if (!session || row < 0)
        return;
    const auto lines = session->document().lines();
    if (static_cast<std::size_t>(row) >= lines.size())
        return;
    const auto line = lines[static_cast<std::size_t>(row)]->id;
    session->setSelection(application::Selection{line, {line}, line, {}});
}

void Application::applySession()
{
    if (!m_pendingSession)
        return;
    PendingSession pending = std::move(*m_pendingSession);
    m_pendingSession.reset();
    m_comparison.remove();
    for (const auto id : m_workspace.documents()) {
        discardRecovery(id);
        m_files->close(id);
        m_workspace.remove(id);
    }
    m_tabMedia.clear();
    m_workspaceLayout->forgetTabs();
    m_layoutDocument.reset();
    m_originalColumns.clear();
    m_unresolved.clear();
    std::optional<application::DocumentId> last, active;
    for (std::size_t i = 0; i < pending.tabs.size(); ++i) {
        const auto &tab = pending.tabs[i];
        const QString subtitles = qs(tab.subtitles);
        std::optional<application::DocumentId> id;
        if (pending.staged[i]) {
            const QFileInfo info(subtitles);
            if (!info.isFile()) {
                pending.staged[i].reset();
            } else if (info.lastModified() != pending.stamps[i].first || info.size() != pending.stamps[i].second) {
                pending.staged[i].reset();
                if (auto again = m_files->stageOpen({info.absoluteFilePath().toStdString()}))
                    pending.staged[i] = std::move(*again);
            }
        }
        if (pending.staged[i])
            id = publish(std::move(*pending.staged[i]), subtitles, false);
        if (id) {
            selectRow(*id, tab.active);
        } else {
            id = m_files->createNew();
            m_workspace.add(*id, QCoreApplication::translate("HikariSub.Documents", "Untitled").toStdString());
            if (!subtitles.isEmpty())
                m_unresolved.push_back({*id, QStringLiteral("subtitles"), subtitles, tab.active, 0, !tab.layout.empty()});
        }
        TabMedia media;
        media.scroll = tab.scroll;
        if (const QString keyframes = qs(tab.keyframes); !keyframes.isEmpty()) {
            if (isFile(keyframes))
                media.keyframes = keyframes;
            else
                m_unresolved.push_back({*id, QStringLiteral("keyframes"), keyframes, 0, 0});
        }
        if (const QString video = qs(tab.video); !video.isEmpty()) {
            const bool dummy = application::isDummyVideo(video.toStdString()) &&
                               application::parseDummyVideo(video.toStdString());
            if (isFile(video) || dummy) {
                media.video = dummy ? video : QDir::toNativeSeparators(video);
                media.position = tab.position;
            } else {
                m_unresolved.push_back({*id, QStringLiteral("video"), video, 0, tab.position});
            }
        }
        if (const QString audio = qs(tab.audio); !audio.isEmpty()) {
            const bool blank = application::AudioBox::isDummyName(audio.toStdString());
            if (isFile(audio) || blank)
                media.audio = blank ? audio : QDir::toNativeSeparators(audio);
            else
                m_unresolved.push_back({*id, QStringLiteral("audio"), audio, 0, 0});
        }
        m_tabMedia[id->value] = media;
        if (!tab.layout.empty()) {
            const std::string *title = m_workspace.title(*id);
            m_workspaceLayout->setTabArrangement(id->value, QByteArray::fromStdString(tab.layout),
                                                 title ? qs(*title) : QString());
        }
        last = id;
        if (pending.activeTab == i)
            active = id;
    }
    if (active || last)
        m_workspace.setEditingTarget(active ? *active : *last);
    else
        newDocument();
    refreshViews();
    trimAudioCache();
    saveLastSession();
    emit unresolvedRestoresChanged();
    emit sessionRestored(static_cast<int>(m_unresolved.size()));
}

QVariantList Application::unresolvedRestores() const
{
    QVariantList rows;
    const auto tabs = m_workspace.tabs();
    for (std::size_t row = 0; row < m_unresolved.size(); ++row) {
        const auto &u = m_unresolved[row];
        const auto it = std::ranges::find(tabs, u.document);
        const std::string *title = m_workspace.title(u.document);
        rows << QVariantMap{{QStringLiteral("row"), static_cast<int>(row)},
                            {QStringLiteral("tab"), it == tabs.end() ? -1 : static_cast<int>(it - tabs.begin())},
                            {QStringLiteral("title"), title ? qs(*title) : QString()},
                            {QStringLiteral("kind"), u.kind},
                            {QStringLiteral("path"), u.path}};
    }
    return rows;
}

bool Application::retryRestore(int row)
{
    if (row < 0 || static_cast<std::size_t>(row) >= m_unresolved.size())
        return false;
    const UnresolvedRestore entry = m_unresolved[static_cast<std::size_t>(row)];
    const auto isTarget = [&] { return m_workspace.editingTarget() == entry.document; };
    const bool dummyAudio = entry.kind == QLatin1String("audio") && application::AudioBox::isDummyName(entry.path.toStdString());
    if (!isFile(entry.path) && !dummyAudio) {
        m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Session", "Restore session"),
                      QCoreApplication::translate("HikariSub.Session", "Could not find %1").arg(QDir::toNativeSeparators(entry.path)));
        return false;
    }
    TabMedia &media = m_tabMedia[entry.document.value];
    if (entry.kind == QLatin1String("subtitles")) {
        auto *placeholder = m_files->session(entry.document);
        const auto destination = m_files->destination(entry.document);
        if (!placeholder || placeholder->isDirty() || (destination && !destination->value.empty())) {
            m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Session", "Restore session"),
                          QCoreApplication::translate("HikariSub.Session", "The tab of %1 has other work now; open the file in a new tab.")
                           .arg(QFileInfo(entry.path).fileName()));
            return false;
        }
        auto staged = m_files->stageOpen({QFileInfo(entry.path).absoluteFilePath().toStdString()});
        if (!staged) {
            m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Session", "Restore session"),
                          QCoreApplication::translate("HikariSub.Session", "Could not open %1; nothing was changed.")
                           .arg(QFileInfo(entry.path).fileName()));
            return false;
        }
        m_unresolved.erase(m_unresolved.begin() + row);
        const auto id = publish(std::move(*staged), entry.path, false);
        if (!id)
            return false;
        selectRow(*id, entry.active);
        const TabMedia kept = media;
        const bool wasTarget = isTarget();
        discardRecovery(entry.document);
        m_files->close(entry.document);
        m_workspace.replace(entry.document, *id);
        if (entry.layout)
            replaceTabArrangement(entry.document, *id, ui::WorkspaceLayoutController::TabStart::Copy);
        else
            openIntoTabArrangement(entry.document, *id);
        m_comparison.replaced(entry.document, *id);
        m_tabMedia.erase(entry.document.value);
        m_originalColumns.erase(entry.document.value);
        m_tabMedia[id->value] = kept;
        for (auto &u : m_unresolved)
            if (u.document == entry.document)
                u.document = *id;
        if (wasTarget)
            m_workspace.setEditingTarget(*id);
    } else {
        m_unresolved.erase(m_unresolved.begin() + row);
        const QString native = dummyAudio ? entry.path : QDir::toNativeSeparators(entry.path);
        if (entry.kind == QLatin1String("video")) {
            media.video = native;
            media.position = entry.position;
            if (isTarget()) {
                m_pendingTabSeek = std::pair(native, entry.position);
                if (!media.audio.isEmpty())
                    m_keepTabAudio = native;
                m_video->openVideo(native);
            }
        } else if (entry.kind == QLatin1String("audio")) {
            media.audio = native;
            if (isTarget())
                m_audio->openAudio(native);
        } else if (entry.kind == QLatin1String("keyframes")) {
            media.keyframes = native;
            if (isTarget())
                if (const QString problem = openKeyframes(QUrl::fromLocalFile(entry.path)); !problem.isEmpty())
                    m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.MainWindow", "Open keyframes"), problem);
        }
    }
    refreshViews();
    saveLastSession();
    emit unresolvedRestoresChanged();
    return true;
}

bool Application::relinkRestore(int row, const QUrl &file)
{
    if (row < 0 || static_cast<std::size_t>(row) >= m_unresolved.size() || file.isEmpty())
        return false;
    m_unresolved[static_cast<std::size_t>(row)].path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (retryRestore(row))
        return true;
    emit unresolvedRestoresChanged();
    return false;
}

void Application::removeRestore(int row)
{
    if (row < 0 || static_cast<std::size_t>(row) >= m_unresolved.size())
        return;
    m_unresolved.erase(m_unresolved.begin() + row);
    saveLastSession();
    emit unresolvedRestoresChanged();
}

}
