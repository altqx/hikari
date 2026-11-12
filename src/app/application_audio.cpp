#include "hikari/app/application.h"
#include "hikari/application/audio_box.h"

#include <QCoreApplication>
#include <QDir>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace hikari::app {

QVariantList Application::audioOutputMenu()
{
    ensureAudioRoute();
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

void Application::chooseAudioOutput(const QString &id, const QString &name)
{
    // MUST KILL: the name must be set before the id; the id's change reads it.
    m_settings->set("audio.outputDeviceName", name);
    m_settings->set("audio.outputDevice", id);
}

QString Application::audioOutputName() const
{
    if (m_audioRouting)
        return QString::fromStdString(m_audioRouting->route().name);
    const auto &settings = m_settings->settings();
    if (settings.text("audio.outputDevice").empty())
        return outputDeviceLabel({});
    return QString::fromStdString(settings.text("audio.outputDeviceName"));
}

void Application::noticeAudioRoute(const application::OutputRoute &before, const application::OutputRoute &after)
{
    if (before == after)
        return;
    emit audioOutputChanged();
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
    m_log->notice(ui::LogController::Warning, QCoreApplication::translate("HikariSub.Options", "Audio"), text);
}

void Application::noticeOnsets()
{
    //: Status bar while Snap to vocal onsets (the audio panel's Tap menu, Karaoke mode) reads the audio
    const QString finding = QCoreApplication::translate("HikariSub.Audio", "Finding vocal onsets…");
    if (activeMedia().audio->findingOnsets()) {
        shell().setStatusText(finding);
        return;
    }
    if (shell().statusText() != finding)
        return;
    if (!activeMedia().audio->onsetsFound()) {
        shell().setStatusText(QString());
        return;
    }
    //: Status bar once Snap to vocal onsets has read the audio and boundaries snap
    const QString found = QCoreApplication::translate("HikariSub.Audio", "Vocal onsets found");
    shell().setStatusText(found);
    QTimer::singleShot(5000, Qt::PreciseTimer, this, [this, found] {
        if (shell().statusText() == found)
            shell().setStatusText(QString());
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

void Application::syncAudioWithVideo(WindowMedia &media)
{
    const auto &video = media.video->session();
    const bool ready = video.state() == application::VideoSession::State::Ready;
    media.audio->setPlayingVideoTime(ready && video.playing() ? std::optional(video.tellMs()) : std::nullopt);
    if (!ready || video.playing() || !media.audio->playing() || !media.audio->syncVideo()) {
        media.audioWantsFrame.reset();
        return;
    }
    if (media.audioWantsFrame && video.requestedFrame() == video.shownFrame()) {
        const int frame = *std::exchange(media.audioWantsFrame, std::nullopt);
        if (frame != video.requestedFrame())
            media.video->session().showFrame(frame);
    }
}

void Application::showVideoAtAudio(WindowMedia &media, int ms)
{
    auto &video = media.video->session();
    if (video.state() != application::VideoSession::State::Ready || video.playing() || video.frameCount() <= 0
        || !media.audioTimebase || media.audioTimebasePath != video.path()
        || media.audioTimebaseSerial != video.frameTimesSerial())
        return;
    const int frame = std::clamp(media.audioTimebase->frameShownAt(ms), 0, video.frameCount() - 1);
    if (frame == video.requestedFrame()) {
        media.audioWantsFrame.reset();
        return;
    }
    if (video.requestedFrame() != video.shownFrame()) {
        media.audioWantsFrame = frame;
        return;
    }
    media.audioWantsFrame.reset();
    video.showFrame(frame);
}

}
