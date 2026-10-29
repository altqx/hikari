#pragma once

#include "hikari/application/display_audio_port.h"
#include "hikari/application/general_player.h"
#include "hikari/application/media_association.h"
#include "hikari/application/seek_throttle.h"
#include "hikari/application/video_session.h"
#include "hikari/application/video_sources.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace hikari::ui {

class VideoPresenter;

class VideoController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application composition")
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY textsChanged)
    Q_PROPERTY(int frame READ frame NOTIFY textsChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY changed)
    Q_PROPERTY(bool offering READ offering NOTIFY changed)
    Q_PROPERTY(QString offer READ offer NOTIFY textsChanged)
    Q_PROPERTY(QString offerAssociatedLabel READ offerAssociatedLabel NOTIFY changed)
    Q_PROPERTY(QString offerDirectoryLabel READ offerDirectoryLabel NOTIFY changed)
    Q_PROPERTY(bool offerApplyToAll READ offerApplyToAll WRITE setOfferApplyToAll NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool looping READ looping NOTIFY changed)
    Q_PROPERTY(QString times READ times NOTIFY textsChanged)
    Q_PROPERTY(bool keyframeShown READ keyframeShown NOTIFY textsChanged)
    Q_PROPERTY(bool indexing READ indexing NOTIFY changed)
    Q_PROPERTY(double indexingProgress READ indexingProgress NOTIFY changed)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(bool dummy READ dummy NOTIFY changed)
    Q_PROPERTY(int streamCount READ streamCount NOTIFY changed)
    Q_PROPERTY(int chapterCount READ chapterCount NOTIFY changed)
public:
    VideoController(application::IndexedSourcePort &source, application::SubtitleRendererPort &renderer,
                    QObject *parent = nullptr);

    application::VideoSession &session() { return m_session; }
    using OpenFilter = std::function<void(const QString &path, std::function<void(application::IndexRequest)> asVideo)>;
    void setOpenFilter(OpenFilter filter) { m_filter = std::move(filter); }
    void setBeforeOpen(std::function<void()> before) { m_beforeOpen = std::move(before); }

    void resetForTarget();
    void setOffer(const QString &text, const QString &associatedLabel, const QString &directoryLabel);
    void withdrawOffer();

    bool hasVideo() const { return m_session.state() == application::VideoSession::State::Ready; }
    QString status() const;
    int frame() const { return m_session.currentFrame().value_or(-1); }
    int frameCount() const { return m_session.frameCount(); }
    bool offering() const { return !m_offer.isEmpty(); }
    QString offer() const { return m_offer; }
    QString offerAssociatedLabel() const { return m_offerAssociated; }
    QString offerDirectoryLabel() const { return m_offerDirectory; }
    bool offerApplyToAll() const { return m_offerApplyToAll; }
    void setOfferApplyToAll(bool on);

    Q_INVOKABLE void attachPresenter(QObject *presenter);
    void setSubtitlesAtScreenSize(bool on);
    bool subtitlesAtScreenSize() const { return m_screenSizeSubtitles; }
    Q_INVOKABLE void loadAssociated();
    Q_INVOKABLE void loadFromDirectory();
    Q_INVOKABLE void dismissOffer();
    Q_INVOKABLE void openVideo(const QString &path);
    // MUST KILL: opening() and openRequest() return one generation counter that cancel, unload and target resets bump too
    Q_INVOKABLE qulonglong opening() const { return m_openRequest; }
    std::uint64_t openRequest() const { return m_openRequest; }
    const QString &requestedVideo() const { return m_requestedVideo; }
    Q_INVOKABLE void openVideoUrl(const QUrl &url) { openVideo(url.toLocalFile()); }
    Q_INVOKABLE bool stepFrames(int frames);
    bool playing() const { return m_session.playing(); }
    Q_INVOKABLE bool play();
    Q_INVOKABLE bool pause();
    Q_INVOKABLE bool togglePlay() { return playing() ? pause() : play(); }
    Q_INVOKABLE bool stop();
    Q_INVOKABLE bool playActualLine();
    Q_INVOKABLE bool loopRange(int startMs, int endMs);
    bool looping() const { return m_session.looping(); }
    QString times() const;
    bool keyframeShown() const;
    Q_INVOKABLE bool showFrameAt(int frame);
    Q_INVOKABLE void seekBarMoved(int frame, bool held);
    Q_INVOKABLE void seekBarReleased();
    Q_INVOKABLE bool seekBy(int ms);
    Q_INVOKABLE bool nextKeyframe() { return m_session.nextKeyframe(); }
    Q_INVOKABLE bool previousKeyframe() { return m_session.previousKeyframe(); }
    Q_INVOKABLE bool goToLineStart();
    Q_INVOKABLE bool goToLineEnd();
    void setActiveLineTimes(std::optional<std::pair<core::DocumentTime, core::DocumentTime>> times);

    void setMediaInfo(application::DisplayAudioPort *tracks, application::ChapterPort *chapters);
    bool indexing() const { return m_session.state() == application::VideoSession::State::Opening; }
    double indexingProgress() const;
    Q_INVOKABLE void cancelIndexing();
    bool loaded() const { return m_session.state() != application::VideoSession::State::Closed; }
    bool dummy() const { return m_session.dummy(); }
    Q_INVOKABLE bool unloadVideo();
    int streamCount() const { return static_cast<int>(m_session.audioTracks().size()); }
    Q_INVOKABLE QVariantList streams() const;
    Q_INVOKABLE bool selectStream(int index);
    int chapterCount() const { return static_cast<int>(m_chapters.size()); }
    Q_INVOKABLE QVariantList chapters() const;
    Q_INVOKABLE bool seekChapter(int index);
    Q_INVOKABLE bool nextChapter();
    Q_INVOKABLE bool previousChapter();

    void retranslate() { emit textsChanged(); }

signals:
    void changed();
    void unloaded();
    void textsChanged();
    // MUST KILL: answer is a magic int (0 load associated, 1 load from directory, 2 no)
    void offerAnswered(int answer, bool applyToAll);
    void offerWithdrawn();
    void seekBarSought(int frame);

private:
    void open(const QString &path);

    application::VideoSession m_session;
    QPointer<VideoPresenter> m_presenter;
    bool m_screenSizeSubtitles = false;
    void followScreenSize();
    OpenFilter m_filter;
    std::function<void()> m_beforeOpen;
    std::uint64_t m_openRequest = 0;
    QString m_requestedVideo;
    QString m_offer;
    QString m_offerAssociated;
    QString m_offerDirectory;
    bool m_offerApplyToAll = false;
    void answer(int answer);
    std::optional<std::pair<core::DocumentTime, core::DocumentTime>> m_lineTimes;
    void sessionChanged();
    void textsMayChange();
    void textsNow();
    std::uint64_t m_textsEpoch = 0;
    QTimer *m_textsTimer = nullptr;
    application::SeekThrottle m_seekThrottle;
    QTimer *m_seekTimer = nullptr;
    void seekBarSeek(std::optional<int> frame);
    void seekBarTimer();
    std::vector<int> chapterStarts() const;
    application::DisplayAudioPort *m_trackPort = nullptr;
    application::ChapterPort *m_chapterPort = nullptr;
    std::string m_mediaInfoPath;
    std::vector<application::AudioTrack> m_trackInfo;
    std::vector<application::Chapter> m_chapters;
    int m_prevChapter = -1;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

}
