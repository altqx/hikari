#pragma once

#include "hikari/application/video_analysis.h"
#include "hikari/application/video_session.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QQmlEngine;

namespace hikari::app {

// Track colour and Find fade: read a picked point on each frame of the Lines, preview the result
// on the video and apply it in one step.
class VideoAnalysisController : public QObject {
    Q_OBJECT
    // "track" or "fade".
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY changed)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY changed)
    // Bumped with each new still, for image://analysisframe/<stillKey>.
    Q_PROPERTY(int stillKey READ stillKey NOTIFY changed)
    Q_PROPERTY(int pointX READ pointX NOTIFY changed)
    Q_PROPERTY(int pointY READ pointY NOTIFY changed)
    Q_PROPERTY(QColor colour READ colour NOTIFY changed)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY changed)
    Q_PROPERTY(int lineNumber READ lineNumber NOTIFY changed)
    Q_PROPERTY(int startMs READ startMs NOTIFY changed)
    Q_PROPERTY(int endMs READ endMs NOTIFY changed)
    Q_PROPERTY(int lineFrames READ lineFrames NOTIFY changed)
    Q_PROPERTY(bool follow READ follow WRITE setFollow NOTIFY changed)
    Q_PROPERTY(int trackFrames READ trackFrames NOTIFY changed)
    Q_PROPERTY(bool alpha READ alpha WRITE setAlpha NOTIFY changed)
    Q_PROPERTY(QVariantList colourTargets READ colourTargets NOTIFY changed)
    Q_PROPERTY(bool allAlpha READ allAlpha WRITE setAllAlpha NOTIFY changed)
    Q_PROPERTY(QVariantList alphaTargets READ alphaTargets NOTIFY changed)
    Q_PROPERTY(QColor signColour READ signColour WRITE setSignColour NOTIFY changed)
    Q_PROPERTY(QColor backgroundColour READ backgroundColour WRITE setBackgroundColour NOTIFY changed)
    Q_PROPERTY(bool skipRepeats READ skipRepeats WRITE setSkipRepeats NOTIFY changed)
    Q_PROPERTY(bool fadeIn READ fadeIn WRITE setFadeIn NOTIFY changed)
    Q_PROPERTY(bool fadeOut READ fadeOut WRITE setFadeOut NOTIFY changed)
    Q_PROPERTY(int tolerance READ tolerance WRITE setTolerance NOTIFY changed)
    Q_PROPERTY(bool reading READ reading NOTIFY changed)
    Q_PROPERTY(int framesRead READ framesRead NOTIFY changed)
    Q_PROPERTY(int framesToRead READ framesToRead NOTIFY changed)
    // Track: the first Line's colour on each of its frames, an invalid colour where none was read.
    Q_PROPERTY(QVariantList swatches READ swatches NOTIFY changed)
    // Fade: the distance from the colour on each frame of the Line and the frame after it, -1 where none.
    Q_PROPERTY(QVariantList distances READ distances NOTIFY changed)
    // Indexes into distances, -1 when not found.
    Q_PROPERTY(int fadeInIndex READ fadeInIndex NOTIFY changed)
    Q_PROPERTY(int fadeOutIndex READ fadeOutIndex NOTIFY changed)
    // The first target Line's text as Apply would write it.
    Q_PROPERTY(QString result READ result NOTIFY changed)
    // "", "noVideo", "noLine", "decodeFailed", "readOnly", "outsideLine", "noTrack", "trackLength",
    // "fadeInNotFound" or "fadeOutNotFound".
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
public:
    struct Hooks {
        std::function<application::VideoSession *()> video; // nullptr without video
        std::function<application::EditSession *()> target;
        std::function<void()> pause;
        std::function<void()> refresh; // after the Document changed
        std::function<QString()> clipboard;
    };
    explicit VideoAnalysisController(Hooks hooks, QObject *parent = nullptr);
    ~VideoAnalysisController() override;

    QString mode() const { return m_fade ? QStringLiteral("fade") : QStringLiteral("track"); }
    bool ready() const { return !m_still.isNull(); }
    int frameWidth() const { return m_still.width(); }
    int frameHeight() const { return m_still.height(); }
    int stillKey() const { return m_stillKey; }
    int pointX() const { return m_point.x; }
    int pointY() const { return m_point.y; }
    QColor colour() const;
    int lineCount() const { return static_cast<int>(m_lines.size()); }
    int lineNumber() const { return m_lineNumber; }
    int startMs() const;
    int endMs() const;
    int lineFrames() const;
    bool follow() const { return m_track.has_value(); }
    void setFollow(bool on);
    int trackFrames() const { return m_track ? m_track->frames() : 0; }
    bool alpha() const { return m_options.alpha; }
    void setAlpha(bool on);
    QVariantList colourTargets() const;
    Q_INVOKABLE void setColourTarget(int index, bool on);
    bool allAlpha() const { return m_options.allAlpha; }
    void setAllAlpha(bool on);
    QVariantList alphaTargets() const;
    Q_INVOKABLE void setAlphaTarget(int index, bool on);
    QColor signColour() const;
    void setSignColour(const QColor &colour);
    QColor backgroundColour() const;
    void setBackgroundColour(const QColor &colour);
    bool skipRepeats() const { return m_options.skipRepeats; }
    void setSkipRepeats(bool on);
    bool fadeIn() const { return m_fadeOptions.in; }
    void setFadeIn(bool on);
    bool fadeOut() const { return m_fadeOptions.out; }
    void setFadeOut(bool on);
    int tolerance() const { return static_cast<int>(m_fadeOptions.tolerance); }
    void setTolerance(int tolerance);
    bool reading() const { return m_reading; }
    int framesRead() const { return m_framesRead; }
    int framesToRead() const { return static_cast<int>(m_frames.size()); }
    QVariantList swatches() const;
    QVariantList distances() const;
    int fadeInIndex() const { return m_fadeInIndex; }
    int fadeOutIndex() const { return m_fadeOutIndex; }
    QString result() const { return m_result; }
    QString problem() const { return m_problem; }
    QImage still() const { return m_still; }

    // Pauses the video and decodes its frame without subtitles; false when it can't start.
    Q_INVOKABLE bool open(const QString &mode);
    // Clamped to the frame; the frames are read again.
    Q_INVOKABLE void pick(int x, int y);
    // One step, "Track colour" or "Find fade"; applied() once done.
    Q_INVOKABLE bool apply();
    Q_INVOKABLE void close();

    bool videoPreview() const { return !m_previewTexts.empty(); }
    // `document` with the targets' new texts, while a result is shown.
    std::optional<core::Document> videoDocument(const core::Document &document) const;

signals:
    void changed();
    void applied();
    void previewChanged();

private:
    struct Line {
        core::LineId id;
        application::LineFrames frames;
        std::int64_t startMs = 0, endMs = 0;
    };
    application::PointPath path() const;
    void readFrames();
    void readNext(std::uint64_t generation);
    void recompute();
    void stop(const QString &problem);

    Hooks m_hooks;
    bool m_fade = false;
    std::shared_ptr<const application::IndexedFrame> m_frame;
    QImage m_still;
    int m_stillKey = 0;
    application::FramePoint m_point;
    bool m_pointFromClip = false;
    std::vector<Line> m_lines;
    int m_lineNumber = 0;
    std::int64_t m_current = 0;
    std::optional<application::MotionTrack> m_track;
    application::ColourTrackOptions m_options;
    application::FadeOptions m_fadeOptions;
    std::vector<std::int64_t> m_frames;
    std::map<std::int64_t, std::optional<application::KeyColour>> m_colours;
    bool m_reading = false;
    int m_framesRead = 0;
    int m_fadeInIndex = -1, m_fadeOutIndex = -1;
    std::vector<std::pair<core::LineId, std::u8string>> m_previewTexts;
    QString m_result;
    QString m_problem;
    std::uint64_t m_generation = 0;
    QTimer m_readTimer;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

void attachVideoAnalysisStill(QQmlEngine &engine, VideoAnalysisController &controller);

}
