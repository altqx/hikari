// AV1 (#238): the audio panel's output device menu. It lists what Settings >
// Audio's device choice lists (H2, decision 60), marks the stored choice and
// stores a new one the way the dialog's OK does, so the two never disagree;
// the routing then moves both audio paths. When the chosen device goes away
// and the system default plays instead, the status bar says so.
#include "hikari/app/application.h"
#include "hikari/application/audio_box.h"

#include <QCoreApplication>
#include <QDir>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace hikari::app {

QVariantList Application::audioOutputMenu() const
{
    const auto &settings = m_settings->settings();
    const std::string chosen = settings.text("audio.outputDevice");
    QVariantList rows;
    for (const auto &choice :
         application::outputDeviceChoices(m_audioDevices->outputs(), chosen, settings.text("audio.outputDeviceName")))
        rows << QVariantMap{{QStringLiteral("label"), outputDeviceLabel(choice)},
                            {QStringLiteral("checked"), choice.id == chosen},
                            {QStringLiteral("id"), QString::fromStdString(choice.id)},
                            {QStringLiteral("name"), QString::fromStdString(choice.name)}};
    return rows;
}

// By the entry's id, not its row: a device plugged in or out while the menu
// is open moves the rows under it.
void Application::chooseAudioOutput(const QString &id, const QString &name)
{
    // The name first: the id's change moves the outputs (settingChanged).
    m_settings->set("audio.outputDeviceName", name);
    m_settings->set("audio.outputDevice", id);
}

QString Application::audioOutputName() const
{
    const auto route = m_audioRouting->route();
    return QString::fromStdString(route.name);
}

void Application::noticeAudioRoute(const application::OutputRoute &before, const application::OutputRoute &after)
{
    if (before == after)
        return;
    emit audioOutputChanged();
    // A notice of a device that is back (or replaced) no longer holds.
    if (!m_audioRouteNotice.isEmpty() && m_shell->statusText() == std::exchange(m_audioRouteNotice, QString()))
        m_shell->setStatusText(QString());
    // The chosen device went away (the choice is still stored): the default plays.
    if (before.systemDefault || !after.systemDefault || m_settings->settings().text("audio.outputDevice").empty())
        return;
    const QString name = QString::fromStdString(m_settings->settings().text("audio.outputDeviceName"));
    QString text;
    if (after.id.empty())
        //: The output device chosen on the audio panel or in Settings > Audio was unplugged and no other is connected; %1 is its name
        text = QCoreApplication::translate("HikariSub.Audio", "%1 is not connected. No output device is available.").arg(name);
    else
        //: The output device chosen on the audio panel or in Settings > Audio was unplugged; %1 is its name, %2 the system default that plays instead
        text = QCoreApplication::translate("HikariSub.Audio", "%1 is not connected. Audio plays through %2.")
                   .arg(name, QString::fromStdString(after.name));
    m_shell->setStatusText(text);
    m_audioRouteNotice = text;
    // As legacy's status messages, it goes after 5 s unless something replaced
    // it (a later notice of the same words too: it has its own 5 s).
    QTimer::singleShot(5000, Qt::PreciseTimer, this, [this, text, serial = ++m_audioRouteNoticeSerial] {
        if (serial != m_audioRouteNoticeSerial)
            return;
        if (m_shell->statusText() == text)
            m_shell->setStatusText(QString());
        m_audioRouteNotice.clear();
    });
}

// #384: Snap to vocal onsets reads the whole audio before anything snaps;
// the status bar says so while it reads, and when it is done (for 5 s, as
// legacy's status messages).
void Application::noticeOnsets()
{
    //: Status bar while Snap to vocal onsets (the audio panel's Tap menu, Karaoke mode) reads the audio
    const QString finding = QCoreApplication::translate("HikariSub.Audio", "Finding vocal onsets…");
    if (m_audio->findingOnsets()) {
        m_shell->setStatusText(finding);
        return;
    }
    if (m_shell->statusText() != finding)
        return; // replaced by another message
    if (!m_audio->onsetsFound()) {
        m_shell->setStatusText(QString()); // stopped before the end
        return;
    }
    //: Status bar once Snap to vocal onsets has read the audio and boundaries snap
    const QString found = QCoreApplication::translate("HikariSub.Audio", "Vocal onsets found");
    m_shell->setStatusText(found);
    QTimer::singleShot(5000, Qt::PreciseTimer, this, [this, found] {
        if (m_shell->statusText() == found)
            m_shell->setStatusText(QString());
    });
}

QString Application::dummyVideoAudio(const QString &videoPath)
{
    const std::string path = videoPath.toStdString();
    const auto dummy = application::isDummyVideo(path) ? application::parseDummyVideo(path) : std::nullopt;
    if (!dummy)
        return {};
    return QString::fromStdString(application::AudioBox::dummyName(
        application::dummyVideoAudioFrames(*dummy, application::AudioBox::kDummyRate)));
}

bool Application::isVideoAudio(const QString &audio, const QString &videoPath)
{
    if (audio.isEmpty() || videoPath.isEmpty())
        return false;
    return QDir::toNativeSeparators(audio) == QDir::toNativeSeparators(videoPath) || audio == dummyVideoAudio(videoPath);
}

// AV2 (#239): the video's side of the sync toggle, on each change of the
// video. While it plays, the audio cursor is at its time (legacy Tell(): the
// last delivered frame's); a frame the playing box wanted while another was
// on its way is asked for once that one arrived.
void Application::syncAudioWithVideo()
{
    const auto &video = m_video->session();
    const bool ready = video.state() == application::VideoSession::State::Ready;
    m_audio->setPlayingVideoTime(ready && video.playing() ? std::optional(video.tellMs()) : std::nullopt);
    // Off, the box's playback asks for no frame (a queued one is dropped).
    if (!ready || video.playing() || !m_audio->playing() || !m_audio->syncVideo()) {
        m_audioWantsFrame.reset();
        return;
    }
    if (m_audioWantsFrame && video.requestedFrame() == video.shownFrame()) {
        const int frame = *std::exchange(m_audioWantsFrame, std::nullopt);
        if (frame != video.requestedFrame())
            m_video->session().showFrame(frame);
    }
}

// AV2: the playing box's time on each tick: the paused video shows the frame
// there (legacy FrameShownAt). A playing video keeps its own clock; the box
// paused it when it started anyway (AudioDisplay::Play).
void Application::showVideoAtAudio(int ms)
{
    auto &video = m_video->session();
    // the box's timebase over this video (followVideoInAudio keeps it)
    if (video.state() != application::VideoSession::State::Ready || video.playing() || video.frameCount() <= 0
        || !m_audioTimebase || m_audioTimebasePath != video.path())
        return;
    const int frame = std::clamp(m_audioTimebase->frameShownAt(ms), 0, video.frameCount() - 1);
    if (frame == video.requestedFrame()) {
        m_audioWantsFrame.reset();
        return;
    }
    if (video.requestedFrame() != video.shownFrame()) {
        m_audioWantsFrame = frame; // one decode at a time; the newest waits
        return;
    }
    m_audioWantsFrame.reset();
    video.showFrame(frame);
}

} // namespace hikari::app
