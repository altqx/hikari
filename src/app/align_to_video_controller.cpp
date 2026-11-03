#include "hikari/app/align_to_video_controller.h"

#include "settings_store.h"

#include <QQmlEngine>
#include <QQuickImageProvider>

#include <algorithm>

namespace hikari::app {

namespace {

constexpr const char *kTolerance = "video.alignTolerance";

QImage imageOf(const application::IndexedFrame &frame)
{
    if (frame.ycbcr || frame.width <= 0 || frame.height <= 0)
        return {};
    return QImage(reinterpret_cast<const uchar *>(frame.bgra.data()), frame.width, frame.height, frame.stride,
                  QImage::Format_RGB32)
        .copy();
}

class AlignStillProvider : public QQuickImageProvider {
public:
    explicit AlignStillProvider(AlignToVideoController &controller)
        : QQuickImageProvider(QQuickImageProvider::Image), m_controller(controller)
    {
    }
    QImage requestImage(const QString &, QSize *size, const QSize &) override
    {
        const QImage image = m_controller.still();
        if (size)
            *size = image.size();
        return image;
    }

private:
    AlignToVideoController &m_controller;
};

}

AlignToVideoController::AlignToVideoController(ui::SettingsStore &settings, Hooks hooks, QObject *parent)
    : QObject(parent), m_settings(settings), m_hooks(std::move(hooks))
{
}

AlignToVideoController::~AlignToVideoController() = default;

int AlignToVideoController::tolerance() const
{
    return std::clamp(m_settings.integer(kTolerance), 0, application::kMaxAlignTolerance);
}

void AlignToVideoController::setTolerance(int tolerance)
{
    tolerance = std::clamp(tolerance, 0, application::kMaxAlignTolerance);
    if (tolerance == this->tolerance())
        return;
    m_settings.set(kTolerance, tolerance);
    emit changed();
}

bool AlignToVideoController::open()
{
    close();
    auto *video = m_hooks.video();
    auto *target = m_hooks.target();
    if (!video || !video->reportedFrame()) {
        stop(QStringLiteral("noVideo"));
        return false;
    }
    if (!target || !target->selection().active) {
        stop(QStringLiteral("noLine"));
        return false;
    }
    m_hooks.pause();
    const auto generation = ++m_generation;
    video->requestFrame(*video->reportedFrame(), false,
                        [this, alive = std::weak_ptr<bool>(m_alive), generation](std::shared_ptr<const application::IndexedFrame> frame) {
                            if (alive.expired() || generation != m_generation)
                                return;
                            if (!frame || (m_still = imageOf(*frame)).isNull())
                                return stop(QStringLiteral("decodeFailed"));
                            m_frame = std::move(frame);
                            ++m_stillKey;
                            pick(m_point.x, m_point.y);
                        });
    return true;
}

void AlignToVideoController::pick(int x, int y)
{
    if (!m_frame)
        return;
    m_point.x = std::clamp(x, 0, m_frame->width - 1);
    m_point.y = std::clamp(y, 0, m_frame->height - 1);
    m_point.colour = application::pixelColour(*m_frame, m_point.x, m_point.y).value_or(application::KeyColour{});
    emit changed();
}

bool AlignToVideoController::align()
{
    auto *video = m_hooks.video();
    auto *target = m_hooks.target();
    if (!m_frame || m_search)
        return false;
    if (!video) {
        stop(QStringLiteral("noVideo"));
        return false;
    }
    if (!target || !target->selection().active) {
        stop(QStringLiteral("noLine"));
        return false;
    }
    m_point.tolerance = tolerance();
    const auto shape = application::keyShape(*m_frame, m_point);
    if (!shape)
        return false;
    m_shape = *shape;
    m_problem.clear();
    ++m_generation;
    m_search.emplace(m_frame->index, video->frameCount());
    emit changed();
    checkNext();
    return true;
}

void AlignToVideoController::checkNext()
{
    const auto next = m_search->next();
    if (!next)
        return found();
    auto *video = m_hooks.video();
    if (!video)
        return stop(QStringLiteral("noVideo"));
    const auto generation = m_generation;
    video->requestFrame(*next, false,
                        [this, alive = std::weak_ptr<bool>(m_alive), generation](std::shared_ptr<const application::IndexedFrame> frame) {
                            if (alive.expired() || generation != m_generation || !m_search)
                                return;
                            if (!frame)
                                return stop(QStringLiteral("decodeFailed"));
                            m_search->report(application::keyPointHolds(*frame, m_point, m_shape));
                            emit changed();
                            QMetaObject::invokeMethod(
                                this,
                                [this, generation] {
                                    if (generation == m_generation && m_search)
                                        checkNext();
                                },
                                Qt::QueuedConnection);
                        });
}

void AlignToVideoController::found()
{
    const int first = m_search->first(), last = m_search->last();
    m_search.reset();
    auto *video = m_hooks.video();
    auto *target = m_hooks.target();
    if (!video || !target)
        return stop(!video ? QStringLiteral("noVideo") : QStringLiteral("noLine"));
    const auto aligned = application::alignActiveLine(*target, video->legacyTimebase(), first, last);
    if (!aligned) {
        const bool locked = aligned.error() == application::CommandRefusal::ReadOnly ||
                            aligned.error() == application::CommandRefusal::Protected;
        return stop(locked ? QStringLiteral("readOnly") : QStringLiteral("noLine"));
    }
    m_hooks.refresh();
    emit changed();
    emit aligned();
}

void AlignToVideoController::stop(const QString &problem)
{
    ++m_generation;
    m_search.reset();
    m_problem = problem;
    emit changed();
}

void AlignToVideoController::cancel()
{
    if (!m_search)
        return;
    ++m_generation;
    m_search.reset();
    emit changed();
}

void AlignToVideoController::close()
{
    ++m_generation;
    m_search.reset();
    m_frame.reset();
    m_still = {};
    m_problem.clear();
    emit changed();
}

void attachAlignToVideoStill(QQmlEngine &engine, AlignToVideoController &controller)
{
    engine.addImageProvider(QStringLiteral("alignframe"), new AlignStillProvider(controller));
}

}
