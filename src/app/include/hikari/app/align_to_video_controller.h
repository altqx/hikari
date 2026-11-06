#pragma once

#include "hikari/application/align_to_video.h"
#include "hikari/application/video_session.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class QQmlEngine;

namespace hikari::ui {
class SettingsStore;
}

namespace hikari::app {

// Times the active Line to the run of frames around the video's frame where a
// picked pixel keeps its colour and shape.
class AlignToVideoController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(int frameWidth READ frameWidth NOTIFY changed)
    Q_PROPERTY(int frameHeight READ frameHeight NOTIFY changed)
    // Bumped with each new still, for image://alignframe/<stillKey>.
    Q_PROPERTY(int stillKey READ stillKey NOTIFY changed)
    Q_PROPERTY(int pointX READ pointX NOTIFY changed)
    Q_PROPERTY(int pointY READ pointY NOTIFY changed)
    Q_PROPERTY(QColor colour READ colour NOTIFY changed)
    Q_PROPERTY(int tolerance READ tolerance WRITE setTolerance NOTIFY changed)
    Q_PROPERTY(int maxTolerance READ maxTolerance CONSTANT)
    Q_PROPERTY(bool searching READ searching NOTIFY changed)
    Q_PROPERTY(int checked READ checked NOTIFY changed)
    // "", "noVideo", "noLine", "decodeFailed" or "readOnly".
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
public:
    struct Hooks {
        std::function<application::VideoSession *()> video; // nullptr without video
        std::function<application::EditSession *()> target;
        std::function<void()> pause;
        std::function<void()> refresh; // after the Document changed
    };
    AlignToVideoController(ui::SettingsStore &settings, Hooks hooks, QObject *parent = nullptr);
    ~AlignToVideoController() override;

    bool ready() const { return !m_still.isNull(); }
    int frameWidth() const { return m_still.width(); }
    int frameHeight() const { return m_still.height(); }
    int stillKey() const { return m_stillKey; }
    int pointX() const { return m_point.x; }
    int pointY() const { return m_point.y; }
    QColor colour() const { return QColor(m_point.colour.r, m_point.colour.g, m_point.colour.b); }
    int tolerance() const;
    void setTolerance(int tolerance);
    int maxTolerance() const { return application::kMaxAlignTolerance; }
    bool searching() const { return m_search.has_value(); }
    int checked() const { return m_search ? m_search->checked() : 0; }
    QString problem() const { return m_problem; }
    QImage still() const { return m_still; }

    // Pauses the video and decodes its frame without subtitles; false when
    // there is no video or no active Line.
    Q_INVOKABLE bool open();
    // Clamped to the frame.
    Q_INVOKABLE void pick(int x, int y);
    // Searches, then times the active Line; aligned() once it is timed.
    Q_INVOKABLE bool align();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void close();

signals:
    void changed();
    void aligned();

private:
    void checkNext();
    void found();
    void stop(const QString &problem);

    ui::SettingsStore &m_settings;
    Hooks m_hooks;
    std::shared_ptr<const application::IndexedFrame> m_frame;
    QImage m_still;
    int m_stillKey = 0;
    application::KeyPoint m_point;
    application::KeyShape m_shape;
    std::optional<application::AlignSearch> m_search;
    std::uint64_t m_generation = 0;
    QString m_problem;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

void attachAlignToVideoStill(QQmlEngine &engine, AlignToVideoController &controller);

}
