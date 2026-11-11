#include "hikari/app/video_analysis_controller.h"

#include <QQmlEngine>
#include <QQuickImageProvider>

#include <algorithm>
#include <limits>

namespace hikari::app {

namespace {

QImage imageOf(const application::IndexedFrame &frame)
{
    if (frame.ycbcr || frame.width <= 0 || frame.height <= 0)
        return {};
    return QImage(reinterpret_cast<const uchar *>(frame.bgra.data()), frame.width, frame.height, frame.stride,
                  QImage::Format_RGB32)
        .copy();
}

QColor qColour(application::KeyColour c)
{
    return QColor(c.r, c.g, c.b);
}

application::KeyColour keyColour(const QColor &c)
{
    return {static_cast<std::uint8_t>(c.red()), static_cast<std::uint8_t>(c.green()), static_cast<std::uint8_t>(c.blue())};
}

std::int64_t msOf(core::DocumentTime t)
{
    return t.microseconds() / 1000;
}

QVariantList boolList(const std::array<bool, 4> &values)
{
    QVariantList out;
    for (const bool v : values)
        out.append(v);
    return out;
}

class AnalysisStillProvider : public QQuickImageProvider {
public:
    explicit AnalysisStillProvider(VideoAnalysisController &controller)
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
    VideoAnalysisController &m_controller;
};

}

VideoAnalysisController::VideoAnalysisController(Hooks hooks, QObject *parent) : QObject(parent), m_hooks(std::move(hooks))
{
    m_readTimer.setSingleShot(true);
    m_readTimer.setInterval(150);
    connect(&m_readTimer, &QTimer::timeout, this, &VideoAnalysisController::readFrames);
}

VideoAnalysisController::~VideoAnalysisController() = default;

QColor VideoAnalysisController::colour() const
{
    const auto c = m_frame ? application::pixelColour(*m_frame, m_point.x, m_point.y) : std::nullopt;
    return c ? qColour(*c) : QColor();
}

int VideoAnalysisController::startMs() const
{
    std::int64_t out = std::numeric_limits<std::int64_t>::max();
    for (const auto &l : m_lines)
        out = std::min(out, l.startMs);
    return m_lines.empty() ? 0 : static_cast<int>(out);
}

int VideoAnalysisController::endMs() const
{
    std::int64_t out = 0;
    for (const auto &l : m_lines)
        out = std::max(out, l.endMs);
    return static_cast<int>(out);
}

int VideoAnalysisController::lineFrames() const
{
    if (m_fade)
        return m_lines.empty() ? 0 : static_cast<int>(m_lines.front().frames.count);
    return static_cast<int>(m_frames.size());
}

void VideoAnalysisController::setFollow(bool on)
{
    if (on == follow() || m_lines.empty())
        return;
    m_track.reset();
    m_problem.clear();
    if (on) {
        const auto text = m_hooks.clipboard ? m_hooks.clipboard().toStdString() : std::string();
        auto track = application::parseMotionData(std::u8string(text.begin(), text.end()));
        if (!track) {
            m_problem = QStringLiteral("noTrack");
            emit changed();
            return;
        }
        if (track->frames() != m_lines.front().frames.count) {
            m_problem = QStringLiteral("trackLength");
            emit changed();
            return;
        }
        m_track = std::move(*track);
    }
    readFrames();
}

void VideoAnalysisController::setAlpha(bool on)
{
    if (on == m_options.alpha)
        return;
    m_options.alpha = on;
    recompute();
}

QVariantList VideoAnalysisController::colourTargets() const
{
    return boolList(m_options.colours);
}

void VideoAnalysisController::setColourTarget(int index, bool on)
{
    if (index < 0 || index >= 4 || m_options.colours[static_cast<std::size_t>(index)] == on)
        return;
    m_options.colours[static_cast<std::size_t>(index)] = on;
    recompute();
}

void VideoAnalysisController::setAllAlpha(bool on)
{
    if (on == m_options.allAlpha)
        return;
    m_options.allAlpha = on;
    recompute();
}

QVariantList VideoAnalysisController::alphaTargets() const
{
    return boolList(m_options.alphas);
}

void VideoAnalysisController::setAlphaTarget(int index, bool on)
{
    if (index < 0 || index >= 4 || m_options.alphas[static_cast<std::size_t>(index)] == on)
        return;
    m_options.alphas[static_cast<std::size_t>(index)] = on;
    recompute();
}

QColor VideoAnalysisController::signColour() const
{
    return qColour(m_options.sign);
}

void VideoAnalysisController::setSignColour(const QColor &colour)
{
    if (keyColour(colour) == m_options.sign)
        return;
    m_options.sign = keyColour(colour);
    recompute();
}

QColor VideoAnalysisController::backgroundColour() const
{
    return qColour(m_options.background);
}

void VideoAnalysisController::setBackgroundColour(const QColor &colour)
{
    if (keyColour(colour) == m_options.background)
        return;
    m_options.background = keyColour(colour);
    recompute();
}

void VideoAnalysisController::setSkipRepeats(bool on)
{
    if (on == m_options.skipRepeats)
        return;
    m_options.skipRepeats = on;
    recompute();
}

void VideoAnalysisController::setFadeIn(bool on)
{
    if (on == m_fadeOptions.in)
        return;
    m_fadeOptions.in = on;
    recompute();
}

void VideoAnalysisController::setFadeOut(bool on)
{
    if (on == m_fadeOptions.out)
        return;
    m_fadeOptions.out = on;
    recompute();
}

void VideoAnalysisController::setTolerance(int tolerance)
{
    tolerance = std::clamp(tolerance, 0, 442);
    if (tolerance == this->tolerance())
        return;
    m_fadeOptions.tolerance = tolerance;
    recompute();
}

QVariantList VideoAnalysisController::swatches() const
{
    QVariantList out;
    if (m_fade || m_lines.empty() || m_reading)
        return out;
    const auto &frames = m_lines.front().frames;
    for (auto f = frames.first; f < frames.first + frames.count; ++f) {
        const auto it = m_colours.find(f);
        out.append(it != m_colours.end() && it->second ? qColour(*it->second) : QColor());
    }
    return out;
}

QVariantList VideoAnalysisController::distances() const
{
    QVariantList out;
    const auto target = m_frame ? application::pixelColour(*m_frame, m_point.x, m_point.y) : std::nullopt;
    if (!m_fade || m_reading || !target)
        return out;
    for (const auto f : m_frames) {
        const auto it = m_colours.find(f);
        out.append(it != m_colours.end() && it->second ? application::colourDistance(*it->second, *target) : -1.0);
    }
    return out;
}

bool VideoAnalysisController::open(const QString &mode)
{
    close();
    m_fade = mode == QLatin1String("fade");
    auto *video = m_hooks.video();
    auto *target = m_hooks.target();
    const auto times = video ? video->aegisubFrameTimes() : std::nullopt;
    if (!video || !video->reportedFrame() || !times) {
        stop(QStringLiteral("noVideo"));
        return false;
    }
    if (!target || !target->selection().active) {
        stop(QStringLiteral("noLine"));
        return false;
    }
    const auto selection = target->selection();
    const auto lines = target->document().lines();
    m_current = *video->reportedFrame();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto *l = lines[i];
        if (m_fade ? l->id != *selection.active : !selection.selected.contains(l->id))
            continue;
        const auto start = msOf(l->start.value), end = msOf(l->end.value);
        Line line{l->id, application::lineFrames(*times, start, end), start, end};
        if (l->id == *selection.active || m_lines.empty())
            m_lineNumber = static_cast<int>(i) + 1;
        if (l->id == *selection.active)
            m_lines.insert(m_lines.begin(), line);
        else
            m_lines.push_back(line);
        if (m_fade) {
            if (const auto clip = application::pointClip(l->text)) {
                m_point = *clip;
                m_pointFromClip = true;
            }
        }
    }
    if (m_lines.empty()) {
        stop(QStringLiteral("noLine"));
        return false;
    }
    const auto &active = m_lines.front().frames;
    if (m_fade && (m_current < active.first || m_current > active.first + active.count)) {
        m_lines.clear();
        stop(QStringLiteral("outsideLine"));
        return false;
    }
    std::vector<std::int64_t> frames;
    for (const auto &l : m_lines)
        for (auto f = l.frames.first; f < l.frames.first + l.frames.count + (m_fade ? 1 : 0); ++f)
            frames.push_back(f);
    std::erase_if(frames, [&](std::int64_t f) { return f < 0 || f >= video->frameCount(); });
    std::ranges::sort(frames);
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    m_frames = std::move(frames);
    m_hooks.pause();
    const auto generation = ++m_generation;
    video->requestFrame(static_cast<int>(m_current), false,
                        [this, alive = std::weak_ptr<bool>(m_alive), generation](std::shared_ptr<const application::IndexedFrame> frame) {
                            if (alive.expired() || generation != m_generation)
                                return;
                            if (!frame || (m_still = imageOf(*frame)).isNull())
                                return stop(QStringLiteral("decodeFailed"));
                            m_frame = std::move(frame);
                            ++m_stillKey;
                            pick(m_point.x, m_point.y);
                        });
    emit changed();
    return true;
}

void VideoAnalysisController::pick(int x, int y)
{
    if (!m_frame)
        return;
    const application::FramePoint point{std::clamp(x, 0, m_frame->width - 1), std::clamp(y, 0, m_frame->height - 1)};
    if (point == m_point && (m_reading || !m_colours.empty()))
        return;
    m_point = point;
    ++m_generation;
    m_reading = !m_frames.empty();
    m_colours.clear();
    m_framesRead = 0;
    recompute();
    m_readTimer.start();
}

application::PointPath VideoAnalysisController::path() const
{
    if (!m_track || m_lines.empty())
        return application::PointPath(m_point);
    return application::PointPath(m_point, *m_track, m_lines.front().frames.first, m_current);
}

void VideoAnalysisController::readFrames()
{
    m_readTimer.stop();
    ++m_generation;
    m_colours.clear();
    m_framesRead = 0;
    m_reading = !m_frames.empty();
    if (m_problem != QLatin1String("noTrack") && m_problem != QLatin1String("trackLength"))
        m_problem.clear();
    recompute();
    readNext(m_generation);
}

void VideoAnalysisController::readNext(std::uint64_t generation)
{
    if (generation != m_generation)
        return;
    if (m_framesRead >= static_cast<int>(m_frames.size())) {
        m_reading = false;
        return recompute();
    }
    auto *video = m_hooks.video();
    if (!video)
        return stop(QStringLiteral("noVideo"));
    const auto frame = m_frames[static_cast<std::size_t>(m_framesRead)];
    video->requestFrame(static_cast<int>(frame), false,
                        [this, alive = std::weak_ptr<bool>(m_alive), generation,
                         frame](std::shared_ptr<const application::IndexedFrame> decoded) {
                            if (alive.expired() || generation != m_generation)
                                return;
                            if (!decoded)
                                return stop(QStringLiteral("decodeFailed"));
                            const auto at = path().at(frame);
                            m_colours[frame] = at ? application::pixelColour(*decoded, at->x, at->y) : std::nullopt;
                            ++m_framesRead;
                            emit changed();
                            QMetaObject::invokeMethod(this, [this, generation] { readNext(generation); }, Qt::QueuedConnection);
                        });
}

void VideoAnalysisController::recompute()
{
    const bool hadPreview = videoPreview();
    m_previewTexts.clear();
    m_result.clear();
    m_fadeInIndex = m_fadeOutIndex = -1;
    if (m_problem.startsWith(QLatin1String("fade")))
        m_problem.clear();
    auto *video = m_hooks.video();
    auto *target = m_hooks.target();
    const auto times = video ? video->aegisubFrameTimes() : std::nullopt;
    if (!m_reading && m_frame && times && target && !m_lines.empty() && !m_colours.empty()) {
        const auto lines = target->document().lines();
        const auto textOf = [&](core::LineId id) -> const std::u8string * {
            for (const auto *l : lines)
                if (l->id == id)
                    return &l->text;
            return nullptr;
        };
        if (!m_fade) {
            for (const auto &l : m_lines) {
                const auto *text = textOf(l.id);
                if (!text || l.frames.count == 0)
                    continue;
                std::vector<std::optional<application::KeyColour>> colours;
                std::vector<std::int64_t> offsets;
                const auto origin = times->msFromFrame(l.frames.first);
                for (auto f = l.frames.first; f < l.frames.first + l.frames.count; ++f) {
                    const auto it = m_colours.find(f);
                    colours.push_back(it != m_colours.end() ? it->second : std::nullopt);
                    offsets.push_back(times->msFromFrame(f) - origin);
                }
                const auto tags = application::colourTrackTags(colours, offsets, m_options);
                m_previewTexts.emplace_back(l.id, application::withColourTrack(*text, tags, m_options));
            }
        } else if (const auto *text = textOf(m_lines.front().id)) {
            const auto &l = m_lines.front();
            const auto targetColour = application::pixelColour(*m_frame, m_point.x, m_point.y);
            std::vector<std::optional<double>> distances;
            for (const auto f : m_frames) {
                const auto it = m_colours.find(f);
                distances.push_back(targetColour && it != m_colours.end() && it->second
                                        ? std::optional(application::colourDistance(*it->second, *targetColour))
                                        : std::nullopt);
            }
            const auto fade = application::findFade(*times, distances, l.frames.first, m_current, l.startMs, l.endMs,
                                                    m_fadeOptions);
            if (!fade) {
                m_problem = fade.error() == application::FadeProblem::InNotFound ? QStringLiteral("fadeInNotFound")
                                                                                 : QStringLiteral("fadeOutNotFound");
            } else if (m_fadeOptions.in || m_fadeOptions.out) {
                if (fade->inFrame)
                    m_fadeInIndex = static_cast<int>(*fade->inFrame - l.frames.first);
                if (fade->outFrame)
                    m_fadeOutIndex = static_cast<int>(*fade->outFrame - l.frames.first);
                m_previewTexts.emplace_back(l.id, application::withFade(*text, fade->inMs, fade->outMs, m_pointFromClip));
            }
        }
        if (!m_previewTexts.empty())
            m_result = QString::fromUtf8(reinterpret_cast<const char *>(m_previewTexts.front().second.data()),
                                         static_cast<qsizetype>(m_previewTexts.front().second.size()));
    }
    emit changed();
    if (hadPreview || videoPreview())
        emit previewChanged();
}

bool VideoAnalysisController::apply()
{
    auto *target = m_hooks.target();
    if (m_reading || !target || m_previewTexts.empty())
        return false;
    if (target->draftLine())
        target->commitDraft();
    recompute();
    const auto done = application::replaceTexts(*target, m_fade ? "Find fade" : "Track colour", m_previewTexts);
    if (!done) {
        stop(QStringLiteral("readOnly"));
        return false;
    }
    m_hooks.refresh();
    emit applied();
    return true;
}

std::optional<core::Document> VideoAnalysisController::videoDocument(const core::Document &document) const
{
    if (!videoPreview())
        return std::nullopt;
    core::Document out = document;
    for (const auto &[id, text] : m_previewTexts)
        out.editLine(id, [&](core::LineRecord &l) { l.text = text; });
    return out;
}

void VideoAnalysisController::stop(const QString &problem)
{
    ++m_generation;
    m_reading = false;
    m_problem = problem;
    emit changed();
}

void VideoAnalysisController::close()
{
    const bool hadPreview = videoPreview();
    m_readTimer.stop();
    ++m_generation;
    m_frame.reset();
    m_still = {};
    m_lines.clear();
    m_frames.clear();
    m_colours.clear();
    m_track.reset();
    m_pointFromClip = false;
    m_reading = false;
    m_framesRead = 0;
    m_fadeInIndex = m_fadeOutIndex = -1;
    m_previewTexts.clear();
    m_result.clear();
    m_problem.clear();
    emit changed();
    if (hadPreview)
        emit previewChanged();
}

void attachVideoAnalysisStill(QQmlEngine &engine, VideoAnalysisController &controller)
{
    engine.addImageProvider(QStringLiteral("analysisframe"), new AnalysisStillProvider(controller));
}

}
