// P6: tabs and session restore over the shared Workspace (legacy Notebook,
// HikariSubFrame and hikarisubApp at 20d647c4).
//
// The tabs are the Workspace's Documents but the protected reference; the
// editing target is the active tab. Legacy kept a VideoBox and an AudioBox
// in every tab; here the one Video panel and Audio box show the active tab's
// media, so each tab keeps what it had open (its video and the position
// reached, an audio file of its own, keyframes) and gets it back when it is
// shown again. A tab never shows another tab's audio (C05-audio-association).
//
// Sessions use the legacy files unchanged (application/session_file.h).
// Loading replaces every open tab, as legacy did, but only after the staged
// subtitles are read and the open Documents' unsaved work is reviewed as for
// Quit (approved P6-session-review; legacy destroyed the tabs without asking).
// A session entry whose file is missing becomes an unresolved restore that
// stays visible with Retry, Relink and Remove (the accepted 2026-09-29
// lifecycle choice) and is written back to the session until removed; the
// tab itself is kept as legacy kept it (Untitled when its subtitles are gone).

#include "hikari/app/application.h"

#include "hikari/application/session_file.h"

#include <QDir>
#include <algorithm>
#include <QFile>
#include <QFileInfo>
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

} // namespace

// --- tabs -------------------------------------------------------------------

QVariantList Application::tabs() const
{
    QVariantList rows;
    const auto target = m_workspace.editingTarget();
    for (const auto id : m_workspace.tabs()) {
        const std::string *title = m_workspace.title(id);
        const QString name = title ? qs(*title) : QString();
        QString label = name;
        // HikariSubFrame::Label: "<history step>*<name>" while modified.
        const auto *session = m_files->session(id);
        const bool modified = session && session->isDirty();
        if (modified)
            label = QStringLiteral("%1*%2").arg(session->historyCursor()).arg(name);
        // Notebook draws at most maxCharPerTab characters of a name.
        label.truncate(m_tabTextMax);
        QString video;
        if (const auto it = m_tabMedia.find(id.value); it != m_tabMedia.end() && !it->second.video.isEmpty())
            video = QFileInfo(it->second.video).fileName();
        rows << QVariantMap{{QStringLiteral("id"), QVariant::fromValue<qulonglong>(id.value)},
                            {QStringLiteral("label"), label},
                            {QStringLiteral("title"), name},
                            {QStringLiteral("modified"), modified},
                            {QStringLiteral("current"), target == id},
                            // The tab's tooltip: SubsName, then VideoName.
                            {QStringLiteral("tip"), name + QLatin1Char('\n') + video}};
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

void Application::addPage(bool fromTabBar)
{
    // Notebook::AddPage: a new tab after the last one becomes active. The
    // tab bar's "+" and a double click there (AddPage(true, true)) write
    // the session; GLOBAL_ADD_PAGE (InsertTab) does not.
    newDocument();
    if (fromTabBar)
        saveLastSession();
}

void Application::changeTab(int step)
{
    // HikariSubFrame::OnPageChange: nothing with fewer than two tabs; past
    // either end it wraps.
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
    // Notebook::ChangePage: the playing video pauses (here the Video panel
    // closes it and shows the new tab's), the new tab is shown.
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
    // R1: Notebook::DeletePage removes the comparison, whichever tab goes
    // (Notebook.cpp:241-242), and the tab's grid with its table.
    m_comparison.remove();
    m_comparison.forget(document);
    discardRecovery(document); // reviewed: saved or explicitly discarded
    m_files->close(document);
    m_workspace.remove(document); // the legacy DeletePage successor becomes active
    forgetTab(document);
    // Notebook::DeletePage: with no tab left, a new empty (Untitled) one.
    if (m_workspace.tabs().empty()) {
        const auto id = m_files->createNew();
        m_workspace.add(id, tr("Untitled").toStdString());
        m_workspace.setEditingTarget(id);
    }
    refreshViews();
    saveLastSession(); // Notebook::DeletePage ends with SaveLastSession
}

void Application::forgetTab(application::DocumentId document)
{
    m_tabMedia.erase(document.value);
    m_matroskaPaths.erase(document.value);
    m_originalColumns.erase(document.value); // E5: the tab's Grid goes with it
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
    // Legacy loaded the subtitles into the same tab, whose VideoBox and
    // AudioBox stayed. The Video panel offers the new Document's own
    // association as before (I1); the audio box and keyframes stay the tab's.
    TabMedia media;
    if (const auto it = m_tabMedia.find(old->value); it != m_tabMedia.end())
        media = it->second;
    if (keepMedia) {
        // Y9 (OnMkvSubs): Clearing resets the Grid's scroll; the tab's
        // video, its position, audio and keyframes stay.
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
    // R1: SubsGrid::Clearing deletes the tab's table; CG1/CG2 still name the
    // tab (HikariSubFrame.cpp:1386-1395 then finds no table to remove).
    m_comparison.replaced(*old, replacement);
    forgetTab(*old);
    m_tabMedia[replacement.value] = media;
    refreshViews();
}

void Application::setTargetScroll(int row)
{
    if (m_scrollRestoring)
        return; // the Grid has not shown this tab's scroll yet
    if (const auto target = m_workspace.editingTarget())
        m_tabMedia[target->value].scroll = std::max(0, row);
}

// --- each tab's media ---------------------------------------------------------

void Application::trackTabMedia()
{
    // Notebook's font and TAB_TEXT_MAX_CHARS are read when it is built.
    if (const int chars = m_settings->integer("program.tabTextMaxChars"); chars > 19)
        m_tabTextMax = chars;
    // What the active tab shows becomes its own: an opened video (legacy
    // VideoPath), an audio file other than the video (AudioPath).
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
        // A restored position is shown once the video is ready (legacy LoadVideo then Seek).
        // Queued: the session applies a pending seek (V6: the active Line's
        // start with OPEN_VIDEO_AT_ACTIVE_LINE) right after reporting Ready,
        // and the tab's position comes after it, as legacy Seek followed LoadVideo. Until it has
        // landed the tab's position is the pending one (saveLastSession, leaving).
        if (m_pendingTabSeek && !m_tabSeekQueued && m_pendingTabSeek->first == path && video.frameCount() > 0) {
            m_tabSeekQueued = true;
            QMetaObject::invokeMethod(this, [this, path] {
                if (!m_tabSeekQueued || !m_pendingTabSeek || m_pendingTabSeek->first != path)
                    return; // another tab became active meanwhile
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
        m_tabMedia[target->value].audio = native == QDir::toNativeSeparators(video) ? QString() : native;
    });
    m_audioConnections << connect(m_audio.get(), &ui::AudioController::changed, this, [this] {
        const auto target = m_workspace.editingTarget();
        if (target && !m_audio->hasAudio())
            if (const auto it = m_tabMedia.find(target->value); it != m_tabMedia.end())
                it->second.audio.clear(); // GLOBAL_CLOSE_AUDIO: AudioPath cleared
    });
}

int Application::targetVideoPosition() const
{
    // VideoBox::Tell: the shown frame's time, 0 without a video.
    const auto &video = m_video->session();
    if (video.state() != application::VideoSession::State::Ready)
        return 0;
    const auto frame = video.shownFrame() ? video.shownFrame() : video.requestedFrame();
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
    // A restored position not shown yet stays the tab's position.
    if (!m_pendingTabSeek && !it->second.video.isEmpty() && QString::fromStdString(video.path()) == it->second.video &&
        video.state() == application::VideoSession::State::Ready)
        it->second.position = targetVideoPosition();
}

void Application::enterTabMedia(std::optional<application::DocumentId> previous,
                                std::optional<application::DocumentId> document)
{
    m_pendingTabSeek.reset();
    m_tabSeekQueued = false;
    m_scrollRestoring = true; // until the Grid calls scrollRestored()
    m_keepTabAudio.clear();
    const bool known = document && m_tabMedia.contains(document->value);
    if (!known) {
        // Subtitles opened into the empty window take what it shows (legacy's
        // first tab); a tab closed to nothing leaves nothing playing.
        if (document && !previous)
            m_tabMedia[document->value].audio =
                m_audio->hasAudio() ? QDir::toNativeSeparators(m_audio->path()) : QString();
        else if (m_audio->hasAudio())
            m_audio->closeAudio();
        if (!document || previous)
            m_pendingKeyframes.clear();
        if (document && previous)
            m_tabMedia[document->value]; // a new tab: nothing of its own yet
        emit tabShown(0);
        return;
    }
    const TabMedia media = m_tabMedia[document->value];
    // Y9: subtitles loaded into the tab from its own video leave that video
    // open as it is (OnMkvSubs only reloads the subtitles on it).
    const auto &shownVideo = m_video->session();
    const bool keepVideo = std::exchange(m_keepVideoOnEnter, false) && !media.video.isEmpty() &&
                           QString::fromStdString(shownVideo.path()) == media.video &&
                           shownVideo.state() == application::VideoSession::State::Ready;
    if (!keepVideo)
        m_pendingKeyframes = media.keyframes; // applied when the video is ready
    if (!media.video.isEmpty() && !keepVideo) {
        m_pendingTabSeek = std::pair(media.video, media.position);
        if (!media.audio.isEmpty())
            m_keepTabAudio = media.video;
        m_video->openVideo(media.video);
    }
    const QString shown = m_audio->hasAudio() ? QDir::toNativeSeparators(m_audio->path()) : QString();
    if (!media.audio.isEmpty()) {
        if (shown != media.audio) {
            if (media.audio == QLatin1String(application::AudioBox::kDummyName))
                m_audio->openDummy();
            else
                m_audio->openAudio(media.audio);
        }
    } else if (!shown.isEmpty() && shown != media.video) {
        // Another tab's audio: the tab's video brings its own back when it is ready.
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

// --- sessions ---------------------------------------------------------------

QString Application::lastSessionPath() const
{
    // Legacy Options.configPath/LastSession.txt; none without a settings file.
    return m_settingsFile.isEmpty() ? QString() : QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/LastSession.txt");
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
    // HikariSubFrame: GLOBAL_ASK_FOR_LOAD_LAST_SESSION sets 1 or 0,
    // GLOBAL_LOAD_LAST_SESSION_ON_START 2 or 0; checking one unchecks the other.
    if (value == sessionRestore())
        return;
    m_settings->set("session.restore", value);
    emit sessionRestoreChanged();
}

bool Application::saveLastSession(bool closing, const QString &path)
{
    const QString file = path.isEmpty() ? lastSessionPath() : path;
    if (file.isEmpty())
        return false;
    const auto target = m_workspace.editingTarget();
    std::vector<application::SessionTab> tabs;
    for (const auto id : m_workspace.tabs()) {
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
        // The shown frame's time, once the video is ready and a restored
        // position has landed (until then the tab's own position).
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
        tabs.push_back(std::move(tab));
    }
    const std::string bytes =
        application::writeSession("HikariSub v" HIKARI_VERSION, closing, tabs);
    QDir().mkpath(QFileInfo(file).absolutePath()); // FileWrite makes the folder
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(bytes.data(), static_cast<qint64>(bytes.size()));
    return out.commit();
}

void Application::endSession()
{
    saveLastSession(true); // HikariSubFrame::OnClose: SaveLastSession(true)
}

QString Application::saveSessionTo(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty())
        return QStringLiteral("failed");
    // OnExternalSession: a read-only file asks for another name.
    if (const QFileInfo info(path); info.exists() && !info.isWritable())
        return QStringLiteral("readonly");
    return saveLastSession(false, path) ? QString() : QStringLiteral("failed");
}

namespace {

std::optional<std::string> readSessionText(const QString &path)
{
    if (path.isEmpty())
        return std::nullopt;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray bytes = file.readAll();
    return application::decodeSessionBytes(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

} // namespace

bool Application::lastSessionCrashed() const
{
    // Notebook::CheckLastSession == 2: a file without "[Close session]".
    const auto text = readSessionText(lastSessionPath());
    return text && !application::sessionClosed(*text);
}

QString Application::startupSession() const
{
    // hikarisubApp::OnInit (release build): only without paths to open.
    // Legacy also loaded without asking when Options.HasCrashed(), set when
    // Config.txt ended with "___Program Crashed___"; nothing at 20d647c4
    // writes that marker (SaveOptions' `crashed` is never passed true, the
    // Windows exception filter only writes a minidump), so after a crash
    // legacy reached the CheckLastSession prompt below, as here.
    if (m_startedWithPaths || lastSessionPath().isEmpty())
        return {};
    switch (sessionRestore()) {
    case 2: return QStringLiteral("load");
    case 1: return QStringLiteral("ask"); // a "No" goes on to the crash check
    default: break;
    }
    return lastSessionCrashed() ? QStringLiteral("crash") : QString();
}

QVariantMap Application::reviewSession(const QUrl &file)
{
    auto result = [](bool ok, const QString &problem, const QVariantList &rows = {}) {
        return QVariantMap{{QStringLiteral("ok"), ok}, {QStringLiteral("problem"), problem}, {QStringLiteral("rows"), rows}};
    };
    const QString path = file.isEmpty() ? lastSessionPath() : (file.isLocalFile() ? file.toLocalFile() : file.toString());
    const auto text = readSessionText(path);
    if (!text)
        return result(false, QString()); // no file or an empty one: legacy did nothing
    auto session = application::parseSession(*text);
    if (!session) {
        const QString problem = tr("Session file is corrupt");
        m_log->log(problem);
        return result(false, problem);
    }
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
    const QVariantList rows = reviewClose(QStringLiteral("session"));
    m_pendingSession = std::move(pending);
    return result(true, QString(), rows);
}

void Application::selectRow(application::DocumentId document, int row)
{
    // Notebook::LoadSubtitles: SetActive(active) when it is a Line's row.
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
    // Notebook::LoadLastSession destroys every tab (each was reviewed).
    // R1-session-off: legacy destroyed them without RemoveComparison
    // (Notebook.cpp:1388-1392), leaving hasCompare on and CG1/CG2 dangling;
    // loading a session turns the comparison off.
    m_comparison.remove();
    for (const auto id : m_workspace.documents()) {
        discardRecovery(id);
        m_files->close(id);
        m_workspace.remove(id);
    }
    m_tabMedia.clear();
    m_originalColumns.clear(); // E5: every tab's Grid is destroyed
    m_unresolved.clear();
    std::optional<application::DocumentId> last;
    for (std::size_t i = 0; i < pending.tabs.size(); ++i) {
        const auto &tab = pending.tabs[i];
        const QString subtitles = qs(tab.subtitles);
        std::optional<application::DocumentId> id;
        // L58-staged-replacement: a file changed (or gone) since it was
        // staged, while the review waited, is read again now.
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
            // A tab whose subtitles could not load stays, Untitled (legacy AddPage).
            id = m_files->createNew();
            m_workspace.add(*id, tr("Untitled").toStdString());
            if (!subtitles.isEmpty())
                m_unresolved.push_back({*id, QStringLiteral("subtitles"), subtitles, tab.active, 0});
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
            if (isFile(video)) {
                media.video = QDir::toNativeSeparators(video);
                media.position = tab.position;
            } else {
                m_unresolved.push_back({*id, QStringLiteral("video"), video, 0, tab.position});
            }
        }
        // C05-audio-association: only the tab's own "Audio:" line.
        if (const QString audio = qs(tab.audio); !audio.isEmpty()) {
            if (isFile(audio) || audio == QLatin1String(application::AudioBox::kDummyName))
                media.audio = audio == QLatin1String(application::AudioBox::kDummyName) ? audio : QDir::toNativeSeparators(audio);
            else
                m_unresolved.push_back({*id, QStringLiteral("audio"), audio, 0, 0});
        }
        m_tabMedia[id->value] = media;
        last = id;
    }
    // AddPage(false) made each loaded tab the active one: the last stays active.
    if (last)
        m_workspace.setEditingTarget(*last);
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
    const bool dummyAudio = entry.kind == QLatin1String("audio") && entry.path == QLatin1String(application::AudioBox::kDummyName);
    if (!isFile(entry.path) && !dummyAudio) {
        m_log->log(tr("Could not find %1").arg(QDir::toNativeSeparators(entry.path)));
        return false;
    }
    TabMedia &media = m_tabMedia[entry.document.value];
    if (entry.kind == QLatin1String("subtitles")) {
        // Only into the placeholder as it was restored: work done there since stays.
        auto *placeholder = m_files->session(entry.document);
        const auto destination = m_files->destination(entry.document);
        if (!placeholder || placeholder->isDirty() || (destination && !destination->value.empty())) {
            m_log->log(tr("The tab of %1 has other work now; open the file in a new tab.").arg(QFileInfo(entry.path).fileName()));
            return false;
        }
        auto staged = m_files->stageOpen({QFileInfo(entry.path).absoluteFilePath().toStdString()});
        if (!staged) {
            m_log->log(tr("Could not open %1; nothing was changed.").arg(QFileInfo(entry.path).fileName()));
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
        m_comparison.replaced(entry.document, *id); // R1: as replaceTarget
        m_tabMedia.erase(entry.document.value);
        m_originalColumns.erase(entry.document.value); // E5: as forgetTab
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
            if (isTarget() && dummyAudio)
                m_audio->openDummy();
            else if (isTarget())
                m_audio->openAudio(native);
        } else if (entry.kind == QLatin1String("keyframes")) {
            media.keyframes = native;
            if (isTarget())
                if (const QString problem = openKeyframes(QUrl::fromLocalFile(entry.path)); !problem.isEmpty())
                    m_log->log(problem);
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
    saveLastSession(); // the session forgets it
    emit unresolvedRestoresChanged();
}

} // namespace hikari::app
