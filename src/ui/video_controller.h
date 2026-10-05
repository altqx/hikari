#pragma once

// The Video panel's presenter-side controller (I1). It offers the editing
// target's associated video the way legacy did ("Associated files", load or
// not), opens video on request, follows the active Line and steps frames.
// The video never gates subtitle editing.

#include "hikari/application/display_audio_port.h"
#include "hikari/application/general_player.h"
#include "hikari/application/media_association.h"
#include "hikari/application/video_session.h"
#include "hikari/application/video_sources.h"

#include <QObject>

#include <functional>
#include <QString>
#include <QUrl>
#include <QVariantList>
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
    // V3: indexing with its progress (0 to 1; -1 before the first report)
    // and Cancel (legacy ProgressSink "Indexing video").
    Q_PROPERTY(bool indexing READ indexing NOTIFY changed)
    Q_PROPERTY(double indexingProgress READ indexingProgress NOTIFY changed)
    // V3: an open video or dummy video, or a failed one (legacy GetState() != None).
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool dummy READ dummy NOTIFY changed)
    // V3: the stream menu's audio entries and the chapter menu (each
    // {label, checked}); counts for enabling the menus.
    Q_PROPERTY(int streamCount READ streamCount NOTIFY changed)
    Q_PROPERTY(int chapterCount READ chapterCount NOTIFY changed)
public:
    VideoController(application::IndexedSourcePort &source, application::SubtitleRendererPort &renderer,
                    QObject *parent = nullptr);

    application::VideoSession &session() { return m_session; }
    // A1: legacy RendererFFMS2::OpenFile gives a file with audio but no video
    // to the audio box, and its provider chooses the audio track before it
    // indexes; either way the current video stays until the new one opens.
    // The filter sees every file before the video session does and calls
    // `asVideo` with the track and index file to open it as video; when it
    // never calls it (audio only, "Choose the track" cancelled), the current
    // video stays. A later open supersedes one the filter has not let through.
    using OpenFilter = std::function<void(const QString &path, std::function<void(application::IndexRequest)> asVideo)>;
    void setOpenFilter(OpenFilter filter) { m_filter = std::move(filter); }

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
    // A4: GLOBAL_PLAY_ACTUAL_LINE (legacy VideoBox's "Play the current
    // line"): the active Line from its start to the frame before its end.
    Q_INVOKABLE bool playActualLine();
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

    // V3: where the stream menu's track names and the chapters come from
    // (the media helper's probe and chapter list); none: no names, no chapters.
    void setMediaInfo(application::DisplayAudioPort *tracks, application::ChapterPort *chapters);
    bool indexing() const { return m_session.state() == application::VideoSession::State::Opening; }
    double indexingProgress() const;
    Q_INVOKABLE void cancelIndexing();
    bool loaded() const { return m_session.state() != application::VideoSession::State::Closed; }
    bool dummy() const { return m_session.dummy(); }
    // V3-unload-video: VIDEO_DELETE_FILE as "Unload video": the panel goes
    // back to no video; the file is never touched and the audio box stays.
    Q_INVOKABLE bool unloadVideo();
    // V3: the stream menu: the video's audio tracks ("A: <description>
    // (<codec>)"), the one general playback plays checked; choosing one
    // switches it (legacy RendererVideo::EnableStream).
    int streamCount() const { return static_cast<int>(m_session.audioTracks().size()); }
    Q_INVOKABLE QVariantList streams() const;
    Q_INVOKABLE bool selectStream(int index);
    // V3: the chapters (legacy m_Chapters; "<name>  [H:MM:SS.CC]", the
    // current one checked), VIDEO_NEXT_CHAPTER / VIDEO_PREVIOUS_CHAPTER and a
    // chapter chosen in the menu (Seek to its start).
    int chapterCount() const { return static_cast<int>(m_chapters.size()); }
    Q_INVOKABLE QVariantList chapters() const;
    Q_INVOKABLE bool seekChapter(int index);
    Q_INVOKABLE bool nextChapter();
    Q_INVOKABLE bool previousChapter();

signals:
    void changed();
    // V3: Unload video emptied the panel.
    void unloaded();

private:
    void open(const QString &path);

    application::VideoSession m_session;
    OpenFilter m_filter;
    std::uint64_t m_openRequest = 0;
    QString m_offeredVideo;
    std::optional<std::pair<core::DocumentTime, core::DocumentTime>> m_lineTimes;
    // V3
    void sessionChanged();
    std::vector<int> chapterStarts() const;
    application::DisplayAudioPort *m_trackPort = nullptr;
    application::ChapterPort *m_chapterPort = nullptr;
    std::string m_mediaInfoPath; // the video the tracks and chapters were asked for
    std::vector<application::AudioTrack> m_trackInfo;
    std::vector<application::Chapter> m_chapters;
    int m_prevChapter = -1; // legacy prevchap
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace hikari::ui
