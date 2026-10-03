#pragma once

// The Video panel's presenter-side controller (I1). It offers the editing
// target's associated video the way legacy did ("Associated files", load or
// not), opens video on request, follows the active Line and steps frames.
// The video never gates subtitle editing.

#include "hikari/application/media_association.h"
#include "hikari/application/video_session.h"

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class VideoController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(int frame READ frame NOTIFY changed)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY changed)
    Q_PROPERTY(bool offering READ offering NOTIFY changed)
    Q_PROPERTY(QString offer READ offer NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    // V2: the legacy times field ("00:00:01,001;  24;  0;  1 ms, -999 ms":
    // time, frame, frames from the active Line's start, then ms from its
    // start and end) and whether the shown frame is a keyframe.
    Q_PROPERTY(QString times READ times NOTIFY changed)
    Q_PROPERTY(bool keyframeShown READ keyframeShown NOTIFY changed)
public:
    VideoController(application::IndexedSourcePort &source, application::SubtitleRendererPort &renderer,
                    QObject *parent = nullptr);

    application::VideoSession &session() { return m_session; }

    // The editing target changed: closes the video and offers the new
    // target's resolved association, if any.
    void offer(const application::MediaAssociations &associations);
    void withdrawOffer();

    bool hasVideo() const { return m_session.state() == application::VideoSession::State::Ready; }
    QString status() const;
    int frame() const { return m_session.requestedFrame().value_or(-1); }
    int frameCount() const { return m_session.frameCount(); }
    bool offering() const { return !m_offeredVideo.isEmpty(); }
    QString offer() const;

    // The panel's VideoPresenter item.
    Q_INVOKABLE void attachPresenter(QObject *presenter);
    Q_INVOKABLE void loadAssociated();
    Q_INVOKABLE void dismissOffer();
    Q_INVOKABLE void openVideo(const QString &path);
    Q_INVOKABLE void openVideoUrl(const QUrl &url) { openVideo(url.toLocalFile()); }
    Q_INVOKABLE bool stepFrames(int frames);
    // V1: legacy Play / Pause (VIDEO_PLAY_PAUSE, Space) and Stop.
    bool playing() const { return m_session.playing(); }
    Q_INVOKABLE bool play();
    Q_INVOKABLE bool pause();
    Q_INVOKABLE bool togglePlay() { return playing() ? pause() : play(); }
    Q_INVOKABLE bool stop();
    // V2 navigation.
    QString times() const;
    bool keyframeShown() const;
    Q_INVOKABLE bool showFrameAt(int frame); // the seek bar
    Q_INVOKABLE bool seekBy(int ms);
    Q_INVOKABLE bool nextKeyframe() { return m_session.nextKeyframe(); }
    Q_INVOKABLE bool previousKeyframe() { return m_session.previousKeyframe(); }
    // GLOBAL_SET_VIDEO_AT_START_TIME / _END_TIME for the active Line.
    Q_INVOKABLE bool goToLineStart();
    Q_INVOKABLE bool goToLineEnd();
    // The active Line's times, for the times field and the go-to commands.
    void setActiveLineTimes(std::optional<std::pair<core::DocumentTime, core::DocumentTime>> times);

signals:
    void changed();

private:
    application::VideoSession m_session;
    QString m_offeredVideo;
    std::optional<std::pair<core::DocumentTime, core::DocumentTime>> m_lineTimes;
};

} // namespace hikari::ui
