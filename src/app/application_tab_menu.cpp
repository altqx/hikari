#include "hikari/app/application.h"

#include "hikari/application/associated_files.h"
#include "hikari/application/recent_files.h"
#include "hikari/backends/desktop_handoff.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScopeGuard>
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

}

void Application::setupTabMenu(const Options &options)
{
    m_revealInFolder = options.revealInFolder ? options.revealInFolder
                                              : [](const QString &path) { backends::desktop::selectInFolder(path); };
    m_legacyAutosaveDir = options.legacyAutosaveDir;
    connect(m_video.get(), &ui::VideoController::offerAnswered, this, &Application::answerAssociation);
    connect(m_video.get(), &ui::VideoController::offerWithdrawn, this, [this] {
        if (const auto target = m_workspace.editingTarget())
            m_offers.erase(target->value);
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

QVariantMap Application::tabMenu(int index)
{
    const auto tabs = m_workspace.tabs();
    const auto target = m_workspace.editingTarget();
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
        if (auto *session = m_files->session(id))
            save = session->isDirty();
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
    const auto tabs = m_workspace.tabs();
    const int size = static_cast<int>(tabs.size());
    if (index < 0 || index >= size)
        return;
    const auto chosen = tabs[static_cast<std::size_t>(index)];
    const int first = std::clamp(firstVisible, 0, size - 1);
    m_workspace.swapTabs(static_cast<std::size_t>(index), static_cast<std::size_t>(first));
    m_workspace.setEditingTarget(chosen);
    refreshViews();
}

bool Application::dragTab(int from, int to)
{
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
        emit tabsChanged();
    }
    return true;
}

void Application::endTabDrag()
{
    if (std::exchange(m_tabsSwapped, false))
        saveLastSession();
}

QVariantList Application::reviewCloseAll()
{
    return reviewClose(QStringLiteral("all"));
}

void Application::closeAllTabs()
{
    m_comparison.remove();
    for (const auto id : m_workspace.tabs()) {
        m_comparison.forget(id);
        discardRecovery(id);
        m_files->close(id);
        m_workspace.remove(id);
        forgetTab(id);
    }
    refreshViews();
}

void Application::showInFolder(const QString &path)
{
    if (!path.isEmpty())
        m_revealInFolder(QDir::toNativeSeparators(path));
}

void Application::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

bool Application::revealRecent(const QString &path)
{
    if (QGuiApplication::keyboardModifiers() != Qt::ControlModifier)
        return false;
    showInFolder(path);
    return true;
}

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
    m_offers.erase(document.value);
    auto *session = m_files->session(document);
    const auto destination = m_files->destination(document);
    std::string path = destination ? destination->value : std::string();
    if (const auto import = m_importPaths.find(document.value);
        path.empty() && import != m_importPaths.end() && m_lyricsImports.contains(document.value))
        path = import->second.toStdString();
    if (!session || path.empty())
        return;
    auto associations = application::resolveMediaAssociations(
        session->document(), path,
        [](const std::string &p) { return QFileInfo(QString::fromStdString(p)).isFile(); }, kWindows);
    for (auto *reference : {&associations.video, &associations.audio, &associations.keyframes})
        if (*reference && (*reference)->resolved)
            (*reference)->resolved = QDir::toNativeSeparators(qs(*(*reference)->resolved)).toStdString();
    QString directoryVideo;
    if (const auto found =
            application::findSameNamedFile(std::filesystem::path(qs(path).toStdU16String()), true))
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
    const auto &offer = it->second.offer;
    QString prompt;
    if (offer.associated()) {
        if (!offer.video.empty())
            prompt += QCoreApplication::translate("HikariSub.Documents", "Video: ") + qs(offer.video) + u'\n';
        if (!offer.audio.empty())
            prompt += QCoreApplication::translate("HikariSub.Documents", "Audio: ") + qs(offer.audio) + u'\n';
        if (!offer.keyframes.empty())
            prompt += QCoreApplication::translate("HikariSub.Documents", "Keyframes: ") + qs(offer.keyframes) + u'\n';
        prompt.prepend(QCoreApplication::translate("HikariSub.Documents", "Associated files:\n"));
    }
    if (!offer.directoryVideo.empty()) {
        if (!prompt.isEmpty())
            prompt += u'\n';
        prompt += QCoreApplication::translate("HikariSub.Documents",
                                              "Video from directory:\n") + QFileInfo(qs(offer.directoryVideo)).fileName();
    }
    const bool both = offer.associated() && !offer.directoryVideo.empty();
    const QString yes = QCoreApplication::translate("HikariSub.Documents", "Yes");
    const QString associated = !offer.associated() ? QString()
                               : both              ? QCoreApplication::translate("HikariSub.Documents", "Load associated")
                                                   : yes;
    const QString directory = offer.directoryVideo.empty() ? QString()
                              : both                       ? QCoreApplication::translate("HikariSub.Documents", "Load from directory")
                                                           : yes;
    m_video->setOffer(prompt, associated, directory);
}

void Application::loadAssociations(application::DocumentId document, const application::AssociationLoad &load)
{
    const bool shown = m_workspace.editingTarget() == document;
    TabMedia &media = m_tabMedia[document.value];
    if (!load.video.empty()) {
        media.video = QDir::toNativeSeparators(qs(load.video));
        media.position = 0;
        if (shown) {
            if (!load.videoAudio)
                m_keepTabAudio = media.video;
            m_video->openVideo(media.video);
        }
    }
    if (!load.audio.empty()) {
        const QString audio = qs(load.audio);
        const QString path = audio.startsWith(QLatin1String("dummy")) ? audio : QDir::toNativeSeparators(audio);
        media.audio = path;
        media.audioFromVideo = false;
        if (shown)
            m_audio->openAudio(path);
    }
    if (!load.keyframes.empty()) {
        const QString keyframes = QDir::toNativeSeparators(qs(load.keyframes));
        if (shown) {
            if (const QString problem = openKeyframes(QUrl::fromLocalFile(qs(load.keyframes))); !problem.isEmpty())
                m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.MainWindow", "Open keyframes"), problem);
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
        saveLastSession();
    emit tabsChanged();
}

bool Application::openInNewTab(application::StagedOpen staged, const QString &path)
{
    ++m_openBatch;
    const auto id = publish(std::move(staged), path, false);
    if (!id)
        return false;
    m_tabMedia[id->value];
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
    QString subtitles;
    if (const auto found = application::findSameNamedFile(std::filesystem::path(path.toStdU16String()), false)) {
        subtitles = QDir::toNativeSeparators(QString::fromStdU16String(found->u16string()));
        const auto target = m_workspace.editingTarget();
        const auto destination = target ? m_files->destination(*target) : std::nullopt;
        if (destination && QDir::toNativeSeparators(qs(destination->value)) == subtitles)
            subtitles.clear();
    }
    return {{QStringLiteral("subtitles"), subtitles}};
}

void Application::openVideo(const QString &path)
{
    m_video->openVideo(path);
}

QVariantMap Application::reviewOpenWithVideo(const QString &subtitles, const QString &video, bool fullscreen)
{
    m_openFromVideo = true;
    QVariantMap result = reviewOpen(subtitles);
    if (result.value(QStringLiteral("ok")).toBool()) {
        m_videoAfterOpen = video;
        m_videoAfterOpenFullscreen = fullscreen;
    } else
        m_openFromVideo = false;
    return result;
}

namespace {

QStringList sortedFiles(QStringList files)
{
    QCollator collator;
    std::sort(files.begin(), files.end(), [&](const QString &a, const QString &b) { return collator.compare(a, b) < 0; });
    return files;
}

QVariantMap openResult(const QString &kind, const QString &path = {}, const QVariantList &rows = {})
{
    return QVariantMap{{QStringLiteral("kind"), kind}, {QStringLiteral("path"), path}, {QStringLiteral("rows"), rows}};
}

}

QVariantMap Application::openDropped(const QList<QUrl> &urls)
{
    QStringList dropped;
    for (const QUrl &url : urls)
        if (url.isLocalFile())
            dropped << url.toLocalFile();
    const QStringList files = sortedFiles(dropped);
    if (files.isEmpty()) {
        trimAudioCache();
        return openResult(QString());
    }
    if (files.size() == 1) {
        const QString &file = files.front();
        switch (application::openKindOf(file.toStdString(), true)) {
        case application::OpenKind::Subtitles: return openResult(QStringLiteral("subtitles"), file);
        case application::OpenKind::Video: return openResult(QStringLiteral("video"), file);
        case application::OpenKind::Session: return openResult(QStringLiteral("session"), file);
        case application::OpenKind::Script: m_automation->loadScript(QUrl::fromLocalFile(file)); break;
        case application::OpenKind::Keyframes:
            if (const QString problem = openKeyframes(QUrl::fromLocalFile(file)); !problem.isEmpty())
                m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.MainWindow", "Open keyframes"), problem);
            break;
        case application::OpenKind::Refused: break;
        }
        return openResult(QString());
    }
    PendingFiles pending = stageFiles(files);
    const auto target = m_workspace.editingTarget();
    pending.reuseFirst = target && targetUntitled() && tabVideo(*target).isEmpty();
    auto *targetSession = target ? m_files->session(*target) : nullptr;
    if (pending.count > 0 && pending.reuseFirst && targetSession && targetSession->isDirty()) {
        const QVariantList rows = reviewClose(QStringLiteral("files"));
        m_pendingFiles = std::move(pending);
        return openResult(QStringLiteral("files"), QString(), rows);
    }
    applyFiles(std::move(pending), false);
    return openResult(QStringLiteral("files"));
}

Application::PendingFiles Application::stageFiles(const QStringList &files)
{
    PendingFiles pending;
    for (const QString &file : files) {
        switch (application::openKindOf(file.toStdString(), false)) {
        case application::OpenKind::Subtitles: pending.subtitlePaths << file; break;
        case application::OpenKind::Script: m_automation->loadScript(QUrl::fromLocalFile(file)); break;
        case application::OpenKind::Video:
        case application::OpenKind::Keyframes: pending.videos << QDir::toNativeSeparators(file); break;
        case application::OpenKind::Session:
        case application::OpenKind::Refused: break;
        }
    }
    for (int i = 0; i < pending.subtitlePaths.size(); ++i) {
        const QString &path = pending.subtitlePaths[i];
        auto staged = m_files->stageOpen({QFileInfo(path).absoluteFilePath().toStdString()});
        if (!staged) {
            m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Video", "Open subtitles"),
                          openProblem(path, staged.error()));
            pending.count = i;
            break;
        }
        pending.subtitles.push_back(std::move(*staged));
    }
    if (pending.count < 0)
        pending.count = static_cast<int>(std::max(pending.subtitlePaths.size(), pending.videos.size()));
    return pending;
}

bool Application::targetUntouched() const
{
    const auto target = m_workspace.editingTarget();
    auto *session = target ? m_files->session(*target) : nullptr;
    if (!session || !targetUntitled() || session->isDirty() || session->historySize() != 1 || m_audio->hasAudio())
        return false;
    if (const auto it = m_tabMedia.find(target->value); it != m_tabMedia.end() &&
        (!it->second.video.isEmpty() || !it->second.audio.isEmpty() || !it->second.keyframes.isEmpty()))
        return false;
    return std::none_of(m_unresolved.begin(), m_unresolved.end(),
                        [&](const UnresolvedRestore &u) { return u.document == *target; });
}

QVariantMap Application::openForwarded(const QStringList &paths)
{
    const QStringList files = sortedFiles(paths);
    if (files.isEmpty())
        return openResult(QString());
    const auto before = m_workspace.documents();
    bool accepted = false;
    auto ended = qScopeGuard([&] {
        if (accepted || m_workspace.documents() != before)
            emit filesForwarded();
    });
    const bool untouched = targetUntouched();
    if (files.size() == 1) {
        const QString &file = files.front();
        switch (application::openKindOf(file.toStdString(), true)) {
        case application::OpenKind::Subtitles:
            if (untouched) {
                openFile(file);
            } else if (auto staged = m_files->stageOpen({QFileInfo(file).absoluteFilePath().toStdString()})) {
                openInNewTab(std::move(*staged), file);
            } else {
                m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.Video", "Open subtitles"),
                              openProblem(file, staged.error()));
            }
            return openResult(QString());
        case application::OpenKind::Session:
            return openResult(QStringLiteral("session"), file);
        case application::OpenKind::Video:
            accepted = true;
            if (!untouched)
                addPage();
            return openResult(QStringLiteral("video"), file);
        case application::OpenKind::Keyframes:
            if (!untouched)
                addPage();
            if (const QString problem = openKeyframes(QUrl::fromLocalFile(file)); problem.isEmpty())
                accepted = true;
            else
                m_log->notice(ui::LogController::Error, QCoreApplication::translate("HikariSub.MainWindow", "Open keyframes"), problem);
            return openResult(QString());
        default:
            return openDropped({QUrl::fromLocalFile(file)});
        }
    }
    PendingFiles pending = stageFiles(files);
    pending.reuseFirst = untouched;
    applyFiles(std::move(pending), false);
    return openResult(QStringLiteral("files"));
}

void Application::applyFiles(PendingFiles files, bool skipFirst)
{
    ++m_openBatch;
    const auto original = m_workspace.editingTarget();
    std::optional<application::DocumentId> last;
    const int subtitles = static_cast<int>(files.subtitles.size());
    const int videos = static_cast<int>(files.videos.size());
    for (int i = 0; i < files.count; ++i) {
        if (i == 0 && skipFirst)
            continue;
        std::optional<application::DocumentId> id;
        if (i < subtitles) {
            id = publish(std::move(files.subtitles[static_cast<std::size_t>(i)]),
                         files.subtitlePaths[i], false);
            if (!id)
                break;
        } else {
            id = m_files->createNew();
            m_workspace.add(*id, QCoreApplication::translate("HikariSub.Documents", "Untitled").toStdString());
        }
        if (i == 0 && files.reuseFirst && original && *original != *id) {
            TabMedia media;
            if (const auto it = m_tabMedia.find(original->value); it != m_tabMedia.end())
                media = it->second;
            media.video.clear();
            media.position = 0;
            media.audio = m_audio->hasAudio() ? QDir::toNativeSeparators(m_audio->path()) : QString();
            discardRecovery(*original);
            m_files->close(*original);
            m_workspace.replace(*original, *id);
            openIntoTabArrangement(*original, *id);
            m_comparison.replaced(*original, *id);
            forgetTab(*original);
            m_tabMedia[id->value] = media;
        } else {
            m_tabMedia[id->value];
        }
        if (i < videos) {
            m_tabMedia[id->value].video = files.videos[i];
            m_tabMedia[id->value].position = 0;
        } else if (i < subtitles) {
            offerAssociations(*id);
        }
        last = id;
    }
    if (last)
        m_workspace.setEditingTarget(*last);
    refreshViews();
    QTimer::singleShot(0, this, [this] { checkResolution(); });
    trimAudioCache();
    saveLastSession();
}

}
