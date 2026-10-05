// P9: the tab menu (legacy Notebook::ContextMenu, Notebook.cpp:882-969, and
// OnTabSel, 971-1040), tab reordering by drag (Notebook::OnMouseEvent,
// 397-400 and 537-556), "show in folder" (SelectInFolder, config.cpp:
// 1048-1062), a Ctrl+click on a recent file (HikariSubFrame::OnRecent,
// 1593-1626), several dropped files opening as tabs (OpenFiles, 1831-1916),
// OPEN_SUBS_IN_NEW_TAB and associated-file discovery on open (OpenFile,
// FindFile and Notebook::LoadVideo, HikariSubFrame.cpp:1323-1442 and
// 1704-1729, Notebook.cpp:1155-1290), all at 20d647c4.

#include "hikari/app/application.h"

#include "hikari/application/associated_files.h"
#include "hikari/application/recent_files.h"
#include "hikari/backends/show_in_folder.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QTimer>

#include <algorithm>

namespace hikari::app {

namespace {

QString qs(const std::string &s)
{
    return QString::fromStdString(s);
}

#ifdef _WIN32
constexpr bool kWindows = true;
#else
constexpr bool kWindows = false;
#endif

} // namespace

void Application::setupTabMenu(const Options &options)
{
    m_revealInFolder = options.revealInFolder ? options.revealInFolder
                                              : [](const QString &path) { backends::selectInFolder(path); };
    // Legacy Options.pathfull/Subs: the settings folder stands for pathfull
    // (as for Indices and AudioCache).
    m_legacyAutosaveDir = !options.legacyAutosaveDir.isEmpty() ? options.legacyAutosaveDir
                          : !m_settingsFile.isEmpty()
                              ? QFileInfo(m_settingsFile).absolutePath() + QStringLiteral("/Subs")
                              : QString();
    connect(m_video.get(), &ui::VideoController::offerAnswered, this, &Application::answerAssociation);
    connect(m_video.get(), &ui::VideoController::offerWithdrawn, this, [this] {
        if (const auto target = m_workspace.editingTarget())
            m_offers.erase(target->value); // a video opened on request instead
    });
}

std::optional<application::DocumentId> Application::documentOf(qulonglong id) const
{
    if (id == 0)
        return m_workspace.editingTarget();
    const application::DocumentId document{id};
    if (!m_workspace.title(document))
        return std::nullopt;
    return document;
}

qulonglong Application::tabDocument(int index) const
{
    const auto tabs = m_workspace.tabs();
    return index < 0 || index >= static_cast<int>(tabs.size()) ? 0 : tabs[static_cast<std::size_t>(index)].value;
}

QString Application::tabVideo(application::DocumentId document) const
{
    const auto it = m_tabMedia.find(document.value);
    return it == m_tabMedia.end() ? QString() : it->second.video;
}

// --- the tab menu -------------------------------------------------------------

QVariantMap Application::tabMenu(int index)
{
    const auto tabs = m_workspace.tabs();
    const auto target = m_workspace.editingTarget();
    // MENU_CHOOSE + g: every tab by its subtitles' name, the active one a
    // radio item (Notebook.cpp:886-889).
    QVariantList rows;
    for (const auto id : tabs) {
        const std::string *title = m_workspace.title(id);
        rows << QVariantMap{{QStringLiteral("title"), title ? qs(*title) : QString()},
                            {QStringLiteral("current"), target == id}};
    }
    bool save = false;
    QVariantList folders;
    if (index >= 0 && index < static_cast<int>(tabs.size())) {
        const auto id = tabs[static_cast<std::size_t>(index)];
        // MENU_SAVE + i: enabled while that tab is modified (891).
        if (auto *session = m_files->session(id))
            save = session->isDirty();
        // MENU_OPEN_*_FOLDER for the tab under the pointer: each of its
        // SubsPath, VideoPath, AudioPath and KeyframesPath that is set (895-910).
        const auto media = m_tabMedia.find(id.value);
        const TabMedia none;
        const TabMedia &m = media == m_tabMedia.end() ? none : media->second;
        const auto destination = m_files->destination(id);
        const QString audio = !m.audio.isEmpty() ? m.audio : m.audioFromVideo ? m.video : QString();
        const std::pair<const char *, QString> entries[] = {
            {"subtitles", destination ? QDir::toNativeSeparators(qs(destination->value)) : QString()},
            {"video", m.video},
            {"audio", audio},
            {"keyframes", m.keyframes}};
        for (const auto &[kind, path] : entries)
            if (!path.isEmpty())
                folders << QVariantMap{{QStringLiteral("kind"), QLatin1String(kind)}, {QStringLiteral("path"), path}};
    }
    return {{QStringLiteral("tabs"), rows}, {QStringLiteral("save"), save}, {QStringLiteral("folders"), folders}};
}

void Application::chooseTab(int index, int firstVisible)
{
    // Notebook::OnTabSel (Notebook.cpp:1019-1039): the chosen tab and the
    // first visible one trade places, and the chosen one becomes active
    // there (iter = firstVisibleTab), even when it was active already.
    // Legacy wrote no session here.
    const auto tabs = m_workspace.tabs();
    const int size = static_cast<int>(tabs.size());
    if (index < 0 || index >= size)
        return;
    const auto chosen = tabs[static_cast<std::size_t>(index)];
    const int first = std::clamp(firstVisible, 0, size - 1);
    m_workspace.swapTabs(static_cast<std::size_t>(index), static_cast<std::size_t>(first));
    m_workspace.setEditingTarget(chosen);
    refreshViews(); // also emits tabsChanged for the new order
}

bool Application::dragTab(int from, int to)
{
    // Notebook::OnMouseEvent (537-556): with the left button down over
    // another tab, the pressed tab and that one swap; the pressed tab, which
    // the press made active (ChangePage), stays active (iter = i).
    const auto tabs = m_workspace.tabs();
    const int size = static_cast<int>(tabs.size());
    if (from < 0 || from >= size || to < 0 || to >= size || from == to)
        return false;
    const auto dragged = tabs[static_cast<std::size_t>(from)];
    m_workspace.swapTabs(static_cast<std::size_t>(from), static_cast<std::size_t>(to));
    m_tabsSwapped = true;
    if (m_workspace.editingTarget() != dragged) {
        m_workspace.setEditingTarget(dragged);
        refreshViews();
    } else {
        emit tabsChanged(); // only the order changed
    }
    return true;
}

void Application::endTabDrag()
{
    // "if (tabsWasSwapped && event.ButtonUp()) SaveLastSession()" (397-400).
    if (std::exchange(m_tabsSwapped, false))
        saveLastSession();
}

QVariantList Application::reviewCloseAll()
{
    return reviewClose(QStringLiteral("all"));
}

void Application::closeAllTabs()
{
    // OnTabSel's MENU_CHOOSE - 1 (Notebook.cpp:978-1018): every tab goes,
    // each removing the comparison, and one new (Untitled) tab is made when
    // none is left. Legacy wrote no session here.
    m_comparison.remove();
    for (const auto id : m_workspace.tabs()) {
        m_comparison.forget(id);
        discardRecovery(id); // reviewed: saved or explicitly discarded
        m_files->close(id);
        m_workspace.remove(id);
        forgetTab(id);
    }
    if (m_workspace.tabs().empty()) {
        const auto id = m_files->createNew();
        m_workspace.add(id, tr("Untitled").toStdString());
        m_workspace.setEditingTarget(id);
    }
    refreshViews();
}

// --- show in folder -----------------------------------------------------------

void Application::showInFolder(const QString &path)
{
    if (!path.isEmpty())
        m_revealInFolder(QDir::toNativeSeparators(path));
}

bool Application::revealRecent(const QString &path)
{
    // OnRecent: "if (Modif == wxMOD_CONTROL) { SelectInFolder(filename); return; }"
    // — Ctrl alone, not with Shift or Alt.
    if (QGuiApplication::keyboardModifiers() != Qt::ControlModifier)
        return false;
    showInFolder(path);
    return true;
}

// --- associated files ---------------------------------------------------------

application::TabMediaPaths Application::tabMediaPaths(application::DocumentId document) const
{
    const auto it = m_tabMedia.find(document.value);
    if (it == m_tabMedia.end())
        return {};
    const TabMedia &m = it->second;
    const QString audio = !m.audio.isEmpty() ? m.audio : m.audioFromVideo ? m.video : QString();
    return {m.video.toStdString(), audio.toStdString(), m.keyframes.toStdString()};
}

void Application::offerAssociations(application::DocumentId document)
{
    // OpenFile on subtitles: FindFile looks for a video of the same name
    // beside them, then LoadVideo(tab, that video or "", loadPrompt) asks
    // about it and the Script Info associations (Notebook.cpp:1155-1222).
    m_offers.erase(document.value);
    auto *session = m_files->session(document);
    const auto destination = m_files->destination(document);
    if (!session || !destination || destination->value.empty())
        return;
    auto associations = application::resolveMediaAssociations(
        session->document(), destination->value,
        [](const std::string &p) { return QFileInfo(QString::fromStdString(p)).isFile(); }, kWindows);
    // Paths are shown and compared in the platform's form (legacy wxString paths).
    for (auto *reference : {&associations.video, &associations.audio, &associations.keyframes})
        if (*reference && (*reference)->resolved)
            (*reference)->resolved = QDir::toNativeSeparators(qs(*(*reference)->resolved)).toStdString();
    QString directoryVideo;
    if (const auto found =
            application::findSameNamedFile(std::filesystem::path(qs(destination->value).toStdU16String()), true))
        directoryVideo = QDir::toNativeSeparators(QString::fromStdU16String(found->u16string()));
    const auto offer = application::associationOffer(associations, directoryVideo.toStdString(), tabMediaPaths(document));
    if (offer.asks())
        m_offers[document.value] = PendingOffer{offer, m_openBatch};
}

void Application::showAssociationOffer()
{
    const auto target = m_workspace.editingTarget();
    const auto it = target ? m_offers.find(target->value) : m_offers.end();
    if (it == m_offers.end()) {
        m_video->withdrawOffer();
        return;
    }
    // The question as Notebook::LoadVideo builds it (1194-1216).
    const auto &offer = it->second.offer;
    QString prompt;
    if (offer.associated()) {
        if (!offer.video.empty())
            prompt += tr("Video: ") + qs(offer.video) + u'\n';
        if (!offer.audio.empty())
            prompt += tr("Audio: ") + qs(offer.audio) + u'\n';
        if (!offer.keyframes.empty())
            prompt += tr("Keyframes: ") + qs(offer.keyframes) + u'\n';
        prompt.prepend(tr("Associated files:\n"));
    }
    if (!offer.directoryVideo.empty()) {
        if (!prompt.isEmpty())
            prompt += u'\n';
        prompt += tr("Video from directory:\n") + QFileInfo(qs(offer.directoryVideo)).fileName();
    }
    // wxOK and wxYES: "Load associated" and "Load from directory" together,
    // "Yes" alone (SetOkLabel(_("Yes")), or the plain Yes button).
    const bool both = offer.associated() && !offer.directoryVideo.empty();
    const QString associated = !offer.associated() ? QString() : both ? tr("Load associated") : tr("Yes");
    const QString directory = offer.directoryVideo.empty() ? QString() : both ? tr("Load from directory") : tr("Yes");
    m_video->setOffer(prompt, associated, directory);
}

void Application::loadAssociations(application::DocumentId document, const application::AssociationLoad &load)
{
    // Notebook.cpp:1262-1281: the video (with its own audio unless an
    // associated audio file opens), then the audio, then the keyframes. A
    // tab that is not shown gets them when it is shown (P6's tab media).
    const bool shown = m_workspace.editingTarget() == document;
    TabMedia &media = m_tabMedia[document.value];
    if (!load.video.empty()) {
        media.video = QDir::toNativeSeparators(qs(load.video));
        media.position = 0;
        if (shown) {
            if (!load.videoAudio)
                m_keepTabAudio = media.video; // LoadVideo(..., loadAudio = false)
            m_video->openVideo(media.video);
        }
    }
    if (!load.audio.empty()) {
        const QString audio = qs(load.audio);
        const QString path = audio.startsWith(QLatin1String("dummy")) ? audio : QDir::toNativeSeparators(audio);
        media.audio = path;
        media.audioFromVideo = false;
        if (shown)
            m_audio->openAudio(path); // OpenAudioInTab(tab, 30040, audiopath)
    }
    if (!load.keyframes.empty()) {
        const QString keyframes = QDir::toNativeSeparators(qs(load.keyframes));
        if (shown) {
            if (const QString problem = openKeyframes(QUrl::fromLocalFile(qs(load.keyframes))); !problem.isEmpty())
                m_log->log(problem);
        } else {
            media.keyframes = keyframes;
        }
    }
}

void Application::answerAssociation(int answer, bool applyToAll)
{
    const auto target = m_workspace.editingTarget();
    const auto it = target ? m_offers.find(target->value) : m_offers.end();
    if (it == m_offers.end())
        return;
    const auto kind = answer == 0   ? application::AssociationAnswer::LoadAssociated
                      : answer == 1 ? application::AssociationAnswer::LoadFromDirectory
                                    : application::AssociationAnswer::No;
    const auto batch = it->second.batch;
    const auto load = application::associationLoad(it->second.offer, kind);
    m_offers.erase(it);
    loadAssociations(*target, load);
    bool loaded = !load.video.empty() || !load.audio.empty() || !load.keyframes.empty();
    // "Apply to All" (ASK_ONCE, promptResult): the rest of the same opening
    // takes the same answer without asking (legacy ResetPrompt starts each
    // opening afresh).
    if (applyToAll)
        for (auto other = m_offers.begin(); other != m_offers.end();) {
            if (other->second.batch != batch) {
                ++other;
                continue;
            }
            const auto otherLoad = application::associationLoad(other->second.offer, kind);
            const application::DocumentId id{other->first};
            other = m_offers.erase(other);
            loadAssociations(id, otherLoad);
            loaded = loaded || !otherLoad.video.empty() || !otherLoad.audio.empty() || !otherLoad.keyframes.empty();
        }
    if (loaded)
        saveLastSession(); // OpenFile ends with SaveLastSession once LoadVideo is done
    emit tabsChanged();
}

// --- opening ------------------------------------------------------------------

bool Application::openInNewTab(application::StagedOpen staged, const QString &path)
{
    // OPEN_SUBS_IN_NEW_TAB (OpenFile, HikariSubFrame.cpp:1360-1365):
    // AddPage(true), so the subtitles open in a new tab after the last one
    // without asking about the current tab's work (nonewtab = false).
    ++m_openBatch;
    const auto id = publish(std::move(staged), path, false);
    if (!id)
        return false;
    m_tabMedia[id->value]; // a new tab: no media of its own yet
    m_workspace.setEditingTarget(*id);
    offerAssociations(*id);
    refreshViews();
    QTimer::singleShot(0, this, [this] { checkResolution(); });
    trimAudioCache();
    saveLastSession();
    return true;
}

QVariantMap Application::openVideoFile(const QString &path)
{
    // OpenFile on a video (1342-1355): FindFile looks for subtitles named as
    // the video; "Load subtitles named ...?" unless the tab has them already.
    QString subtitles;
    if (const auto found = application::findSameNamedFile(std::filesystem::path(path.toStdU16String()), false)) {
        subtitles = QDir::toNativeSeparators(QString::fromStdU16String(found->u16string()));
        const auto target = m_workspace.editingTarget();
        const auto destination = target ? m_files->destination(*target) : std::nullopt;
        if (destination && QDir::toNativeSeparators(qs(destination->value)) == subtitles)
            subtitles.clear(); // tab->SubsPath == secondFileName
    }
    return {{QStringLiteral("subtitles"), subtitles}};
}

void Application::openVideo(const QString &path)
{
    m_video->openVideo(path);
}

QVariantMap Application::reviewOpenWithVideo(const QString &subtitles, const QString &video)
{
    // "Yes": the subtitles load into the tab (SavePrompt(2) first), without
    // the association question (LoadVideo's loadPrompt is false for a
    // video), and the video opens after them; a cancelled review or a failed
    // load opens nothing (OpenFile returns before LoadVideo).
    m_openFromVideo = true;
    QVariantMap result = reviewOpen(subtitles);
    if (result.value(QStringLiteral("ok")).toBool())
        m_videoAfterOpen = video;
    else
        m_openFromVideo = false;
    return result;
}

QVariantMap Application::openDropped(const QList<QUrl> &urls)
{
    QStringList files;
    for (const QUrl &url : urls)
        if (url.isLocalFile())
            files << url.toLocalFile();
    // Legacy sorts by the locale's collation.
    QCollator collator;
    std::sort(files.begin(), files.end(), [&](const QString &a, const QString &b) { return collator.compare(a, b) < 0; });
    auto result = [](const QString &kind, const QString &path = {}, const QVariantList &rows = {}) {
        return QVariantMap{{QStringLiteral("kind"), kind}, {QStringLiteral("path"), path}, {QStringLiteral("rows"), rows}};
    };
    if (files.isEmpty()) {
        trimAudioCache(); // OpenFiles over nothing still ends with DeleteAudioCache
        return result(QString());
    }
    if (files.size() == 1) {
        // OpenFiles with one file: OpenFile(files[0]).
        const QString &file = files.front();
        switch (application::openKindOf(file.toStdString(), true)) {
        case application::OpenKind::Subtitles: return result(QStringLiteral("subtitles"), file);
        case application::OpenKind::Video: return result(QStringLiteral("video"), file);
        case application::OpenKind::Script: m_automation->loadScript(QUrl::fromLocalFile(file)); break;
        case application::OpenKind::Keyframes:
            // KeyframesPath = filename; OpenKeyframes; SetRecent(3).
            if (const QString problem = openKeyframes(QUrl::fromLocalFile(file)); !problem.isEmpty())
                m_log->log(problem);
            break;
        case application::OpenKind::Refused: break;
        }
        trimAudioCache();
        return result(QString());
    }
    // Several (OpenFiles, 1835-1852): subtitles, scripts (loaded at once) and
    // everything else but archives and programs as videos.
    PendingFiles pending;
    for (const QString &file : files) {
        switch (application::openKindOf(file.toStdString(), false)) {
        case application::OpenKind::Subtitles: pending.subtitlePaths << file; break;
        case application::OpenKind::Script: m_automation->loadScript(QUrl::fromLocalFile(file)); break;
        case application::OpenKind::Video:
        case application::OpenKind::Keyframes: pending.videos << QDir::toNativeSeparators(file); break;
        case application::OpenKind::Refused: break;
        }
    }
    // Staged first (L58-staged-replacement); a file that cannot be read ends
    // the opening there, as legacy's "if (!LoadSubtitles) break".
    for (int i = 0; i < pending.subtitlePaths.size(); ++i) {
        const QString &path = pending.subtitlePaths[i];
        auto staged = m_files->stageOpen({QFileInfo(path).absoluteFilePath().toStdString()});
        if (!staged) {
            m_log->log(tr("Could not open %1; nothing was changed.").arg(QFileInfo(path).fileName()));
            pending.count = i;
            break;
        }
        pending.subtitles.push_back(std::move(*staged));
    }
    if (pending.count < 0)
        pending.count = static_cast<int>(std::max(pending.subtitlePaths.size(), pending.videos.size()));
    // The first goes into the editing target when it has neither subtitles
    // of a file nor a video (1872-1875); the others into new tabs after the
    // last one.
    const auto target = m_workspace.editingTarget();
    pending.reuseFirst = target && targetUntitled() && tabVideo(*target).isEmpty();
    auto *targetSession = target ? m_files->session(*target) : nullptr;
    if (pending.count > 0 && pending.reuseFirst && targetSession && targetSession->isDirty()) {
        // The target's unsaved work is reviewed first, as OpenFile's
        // SavePrompt(2) did for subtitles alone.
        const QVariantList rows = reviewClose(QStringLiteral("files"));
        m_pendingFiles = std::move(pending);
        return result(QStringLiteral("files"), QString(), rows);
    }
    applyFiles(std::move(pending), false);
    return result(QStringLiteral("files"));
}

void Application::applyFiles(PendingFiles files, bool skipFirst)
{
    ++m_openBatch; // ResetPrompt: one "Apply to All" for the whole opening
    const auto original = m_workspace.editingTarget();
    std::optional<application::DocumentId> last;
    const int subtitles = static_cast<int>(files.subtitles.size());
    const int videos = static_cast<int>(files.videos.size());
    for (int i = 0; i < files.count; ++i) {
        if (i == 0 && skipFirst)
            continue; // the review was cancelled: OpenFile returned without loading
        std::optional<application::DocumentId> id;
        if (i < subtitles) {
            id = publish(std::move(files.subtitles[static_cast<std::size_t>(i)]),
                         files.subtitlePaths[i], false);
            if (!id)
                break;
        } else {
            // A video beyond the subtitles opens in a new tab of its own
            // (InsertTab, OpenFile(videos[i])).
            id = m_files->createNew();
            m_workspace.add(*id, tr("Untitled").toStdString());
        }
        if (i == 0 && files.reuseFirst && original && *original != *id) {
            // Into the same tab (LoadSubtitles on the current page), whose
            // audio and keyframes stay, as replaceTarget keeps them.
            TabMedia media;
            if (const auto it = m_tabMedia.find(original->value); it != m_tabMedia.end())
                media = it->second;
            media.video.clear();
            media.position = 0;
            media.audio = m_audio->hasAudio() ? QDir::toNativeSeparators(m_audio->path()) : QString();
            discardRecovery(*original);
            m_files->close(*original);
            m_workspace.replace(*original, *id);
            m_comparison.replaced(*original, *id);
            forgetTab(*original);
            m_tabMedia[id->value] = media;
        } else {
            m_tabMedia[id->value]; // a new tab: no media of its own yet
        }
        if (i < videos) {
            // LoadVideo(tab, videos[i], -1, false, true): no question.
            m_tabMedia[id->value].video = files.videos[i];
            m_tabMedia[id->value].position = 0;
        } else if (i < subtitles) {
            offerAssociations(*id); // OpenFile(subs[i]): the folder's video and the associations
        }
        last = id;
    }
    if (last)
        m_workspace.setEditingTarget(*last);
    refreshViews();
    QTimer::singleShot(0, this, [this] { checkResolution(); });
    trimAudioCache(); // DeleteAudioCache
    saveLastSession();
}

} // namespace hikari::app
