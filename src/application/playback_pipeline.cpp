#include "hikari/application/playback_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace hikari::application {

std::shared_ptr<FramePool> FramePool::create(std::size_t keep)
{
    return std::shared_ptr<FramePool>(new FramePool(keep));
}

std::shared_ptr<IndexedFrame> FramePool::acquire()
{
    auto frame = std::make_unique<IndexedFrame>();
    {
        std::lock_guard lock(m_mutex);
        if (!m_free.empty()) {
            frame->bgra = std::move(m_free.back());
            m_free.pop_back();
        } else {
            ++m_fresh;
        }
    }
    const std::weak_ptr<FramePool> pool = weak_from_this();
    return std::shared_ptr<IndexedFrame>(frame.release(), [pool](IndexedFrame *f) {
        if (const auto owner = pool.lock())
            owner->recycle(std::move(f->bgra));
        delete f;
    });
}

std::uint64_t FramePool::fresh() const
{
    std::lock_guard lock(m_mutex);
    return m_fresh;
}

void FramePool::recycle(std::vector<std::byte> buffer)
{
    if (buffer.empty())
        return;
    std::lock_guard lock(m_mutex);
    if (m_free.size() < m_keep)
        m_free.push_back(std::move(buffer));
}

std::optional<PixelRect> overlayBounds(const OverlayFrame &o)
{
    if (o.empty || o.width <= 0 || o.height <= 0 || o.stride < o.width * 4 ||
        o.pixels.size() < static_cast<std::size_t>(o.stride) * static_cast<std::size_t>(o.height))
        return std::nullopt;
    int top = -1, bottom = -1, left = o.width, right = -1;
    const int pairs = o.width / 2;
    for (int y = 0; y < o.height; ++y) {
        const std::uint8_t *row = o.pixels.data() + static_cast<std::size_t>(y) * o.stride;
        // Whole rows first, two pixels at a time.
        std::uint64_t any = 0;
        for (int i = 0; i < pairs; ++i) {
            std::uint64_t two;
            std::memcpy(&two, row + i * 8, 8);
            any |= two;
        }
        if (o.width % 2) {
            std::uint32_t last;
            std::memcpy(&last, row + (o.width - 1) * 4, 4);
            any |= last;
        }
        if (!any)
            continue;
        if (top < 0)
            top = y;
        bottom = y;
        auto visible = [row](int x) {
            std::uint32_t p;
            std::memcpy(&p, row + x * 4, 4);
            return p != 0;
        };
        for (int x = 0; x < left; ++x)
            if (visible(x)) {
                left = x;
                break;
            }
        for (int x = o.width - 1; x > right; --x)
            if (visible(x)) {
                right = x;
                break;
            }
    }
    if (top < 0)
        return std::nullopt;
    return PixelRect{left, top, right - left + 1, bottom - top + 1};
}

struct PlaybackPipeline::State {
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false;
    Post post;
    Deliver deliver;
    std::optional<Input> input;
    std::optional<Output> output; // waiting for the owner
    bool posted = false;          // a task to take it is on the owner's queue
    std::uint64_t serial = 0;
    bool on = false;
    Counters counters;
    // The next frame's overlay, rendered ahead (requested, then made).
    struct AheadRequest {
        std::int64_t timeUs;
        int width, height;
    };
    std::vector<AheadRequest> aheadRequests; // the likeliest first
    struct Ahead {
        AheadRequest at;
        std::uint64_t serial;
        std::shared_ptr<const OverlayFrame> overlay;
        std::optional<PixelRect> bounds;
    };
    // The worker's own: what it rendered ahead, and the frames' pace.
    std::vector<Ahead> ahead;
    std::uint64_t paceEpoch = 0;
    std::optional<std::int64_t> lastStartUs;
    double intervalUs = 0;
    // Where the next frame may start: one frame on at the pace seen so far
    // (the frame's own duration until then). In a millisecond timebase
    // (Matroska) a constant-rate frame starts at its exact time rounded to
    // the millisecond, either side of the pace: both are tried.
    std::vector<std::int64_t> nextStarts(std::uint64_t epoch, std::int64_t startUs, std::optional<std::int64_t> endUs)
    {
        if (epoch != paceEpoch || !lastStartUs || startUs <= *lastStartUs) {
            paceEpoch = epoch;
            intervalUs = 0;
        } else {
            const double step = static_cast<double>(startUs - *lastStartUs);
            if (intervalUs <= 0)
                intervalUs = step;
            else if (step < intervalUs * 1.5) // not across dropped frames
                intervalUs = intervalUs * 0.9 + step * 0.1;
        }
        lastStartUs = startUs;
        const double pace = intervalUs > 0 ? intervalUs : endUs ? static_cast<double>(*endUs - startUs) : 0;
        if (pace <= 0)
            return {};
        const double next = static_cast<double>(startUs) + pace;
        if (startUs % 1000 != 0)
            return {endUs && intervalUs <= 0 ? *endUs : std::llround(next)};
        const std::int64_t below = static_cast<std::int64_t>(std::floor(next / 1000.0)) * 1000;
        const bool nearerBelow = next - static_cast<double>(below) < 500.0;
        return nearerBelow ? std::vector<std::int64_t>{below, below + 1000} : std::vector<std::int64_t>{below + 1000, below};
    }
};

PlaybackPipeline::PlaybackPipeline(SubtitleRendererPort &renderer, Post post, Deliver deliver)
    : m_renderer(renderer), m_state(std::make_shared<State>())
{
    m_state->post = std::move(post);
    m_state->deliver = std::move(deliver);
    if (m_state->post)
        m_worker = std::thread([this] { run(); });
}

PlaybackPipeline::~PlaybackPipeline()
{
    {
        std::lock_guard lock(m_state->mutex);
        m_state->stop = true;
        m_state->input.reset();
        m_state->output.reset();
    }
    m_state->wake.notify_all();
    if (m_worker.joinable())
        m_worker.join();
}

void PlaybackPipeline::submit(Input input)
{
    if (!m_state->post) {
        {
            std::lock_guard lock(m_state->mutex);
            ++m_state->counters.submitted;
        }
        Output out = process(std::move(input));
        {
            std::lock_guard lock(m_state->mutex);
            ++m_state->counters.delivered;
        }
        if (m_state->deliver)
            m_state->deliver(std::move(out));
        return;
    }
    {
        std::lock_guard lock(m_state->mutex);
        if (m_state->stop)
            return;
        ++m_state->counters.submitted;
        if (m_state->input)
            ++m_state->counters.superseded;
        m_state->input = std::move(input);
        m_state->aheadRequests.clear(); // a real frame first
    }
    m_state->wake.notify_one();
}

void PlaybackPipeline::setSubtitles(std::uint64_t serial, bool on)
{
    std::lock_guard lock(m_state->mutex);
    m_state->serial = serial;
    m_state->on = on;
}

PlaybackPipeline::Counters PlaybackPipeline::counters() const
{
    std::lock_guard lock(m_state->mutex);
    return m_state->counters;
}

PlaybackPipeline::Output PlaybackPipeline::process(Input input)
{
    Output out;
    out.epoch = input.epoch;
    out.startUs = input.startUs;
    out.frame = input.make ? input.make(*m_pool) : nullptr;
    auto ahead = std::exchange(m_state->ahead, {});
    if (!out.frame)
        return out;
    std::uint64_t serial = 0;
    bool on = false;
    {
        std::lock_guard lock(m_state->mutex);
        serial = m_state->serial;
        on = m_state->on;
    }
    if (!serial)
        return out;
    out.subtitles = serial;
    if (!on)
        return out;
    const int width = out.frame->width, height = out.frame->height;
    for (auto &made : ahead)
        if (made.serial == serial && made.at.timeUs == input.startUs && made.at.width == width &&
            made.at.height == height) {
            out.overlay = std::move(made.overlay);
            out.overlayBounds = made.bounds;
            std::lock_guard lock(m_state->mutex);
            ++m_state->counters.aheadUsed;
            return out;
        }
    if (auto overlay = m_renderer.render(core::DocumentTime(input.startUs), width, height)) {
        out.overlayBounds = overlayBounds(*overlay);
        out.overlay = std::make_shared<const OverlayFrame>(std::move(*overlay));
    }
    return out;
}

void PlaybackPipeline::run()
{
    const std::shared_ptr<State> state = m_state;
    const std::weak_ptr<State> weak = state;
    for (;;) {
        std::optional<Input> input;
        std::optional<State::AheadRequest> ahead;
        std::uint64_t serial = 0;
        {
            std::unique_lock lock(state->mutex);
            state->wake.wait(lock, [&] { return state->stop || state->input || !state->aheadRequests.empty(); });
            if (state->stop)
                return;
            if (state->input) {
                input = std::exchange(state->input, std::nullopt);
            } else {
                ahead = state->aheadRequests.front();
                state->aheadRequests.erase(state->aheadRequests.begin());
                serial = state->on ? state->serial : 0;
            }
        }
        if (ahead) {
            if (!serial)
                continue;
            State::Ahead made{*ahead, serial, nullptr, std::nullopt};
            if (auto overlay = m_renderer.render(core::DocumentTime(ahead->timeUs), ahead->width, ahead->height)) {
                made.bounds = overlayBounds(*overlay);
                made.overlay = std::make_shared<const OverlayFrame>(std::move(*overlay));
            }
            state->ahead.push_back(std::move(made));
            continue;
        }
        const std::vector<std::int64_t> next = state->nextStarts(input->epoch, input->startUs, input->endUs);
        Output out = process(std::move(*input));
        const bool shown = out.frame != nullptr;
        const int width = shown ? out.frame->width : 0, height = shown ? out.frame->height : 0;
        bool post = false;
        {
            std::lock_guard lock(state->mutex);
            if (state->stop)
                return;
            if (state->output)
                ++state->counters.replacedResults;
            state->output = std::move(out);
            if (!state->posted)
                post = state->posted = true;
            if (shown && !state->input)
                for (const std::int64_t at : next)
                    state->aheadRequests.push_back({at, width, height});
        }
        if (post)
            state->post([weak] {
                const auto s = weak.lock();
                if (!s)
                    return;
                std::optional<Output> taken;
                {
                    std::lock_guard lock(s->mutex);
                    s->posted = false;
                    if (s->stop)
                        return;
                    taken = std::exchange(s->output, std::nullopt);
                    if (taken)
                        ++s->counters.delivered;
                }
                if (taken && s->deliver)
                    s->deliver(std::move(*taken));
            });
    }
}

} // namespace hikari::application
