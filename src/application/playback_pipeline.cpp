#include "hikari/application/playback_pipeline.h"

#include <algorithm>
#include <bit>
#include <limits>
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
    if (o.bounds) {
        const int x0 = std::max(0, o.bounds->x), y0 = std::max(0, o.bounds->y);
        const int x1 = std::min(o.width, o.bounds->x + o.bounds->width);
        const int y1 = std::min(o.height, o.bounds->y + o.bounds->height);
        if (x0 >= x1 || y0 >= y1)
            return std::nullopt;
        return PixelRect{x0, y0, x1 - x0, y1 - y0};
    }
    int top = -1, bottom = -1, left = o.width, right = -1;
    const int pairs = o.width / 2;
    for (int y = 0; y < o.height; ++y) {
        const std::uint8_t *row = o.pixels.data() + static_cast<std::size_t>(y) * o.stride;
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

namespace {

// Renderers draw to the millisecond.
std::int64_t millisecond(std::int64_t us)
{
    return us / 1000;
}

// Guesses the next frame's start from a constant pace fitted to the starts so
// far. A millisecond timebase (Matroska) rounds each start half up.
class Pace {
public:
    // The millisecond to render ahead for the next frame; nullopt with
    // `withheld` set while the guesses keep missing.
    std::optional<std::int64_t> next(std::uint64_t epoch, std::int64_t startUs, std::optional<std::int64_t> endUs,
                                     bool &withheld)
    {
        withheld = false;
        const bool ms = startUs % 1000 == 0;
        const double half = ms ? 500.0 : 1.0;
        if (epoch != m_epoch || !m_last || startUs <= *m_last) {
            *this = Pace{};
            m_epoch = epoch;
            restart(startUs, half);
        } else {
            if (m_guess)
                record(*m_guess == millisecond(startUs));
            double low = 0, high = 0;
            span(1.0, 0.0, low, high);
            const double middle = (low + high) / 2;
            const std::int64_t k =
                m_k + (m_k > 0 ? std::max<std::int64_t>(1, std::llround(static_cast<double>(startUs - *m_last) / middle)) : 1);
            if (!cut(k, startUs, half)) {
                restart(*m_last, half);
                cut(1, startUs, half);
                m_k = 1;
            } else {
                m_k = k;
            }
        }
        m_last = startUs;
        m_guess.reset();
        if (m_k == 0) {
            if (endUs && *endUs > startUs)
                m_guess = millisecond(*endUs);
            return m_guess;
        }
        double lo = 0, hi = 0;
        span(static_cast<double>(m_k + 1), 1.0, lo, hi);
        lo += static_cast<double>(m_anchor);
        hi += static_cast<double>(m_anchor);
        // The millisecond lo falls in, or the next one if more of the range is past it.
        const auto key = [ms](double us) {
            return static_cast<std::int64_t>(ms ? std::floor((us + 500.0) / 1000.0) : std::trunc(us / 1000.0));
        };
        const std::int64_t first = key(lo);
        const double boundary = static_cast<double>(first + 1) * 1000.0 - (ms ? 500.0 : 0.0);
        m_guess = hi <= boundary || boundary - lo >= hi - boundary ? first : first + 1;
        if (unreliable()) {
            withheld = true;
            return std::nullopt;
        }
        return m_guess;
    }

private:
    struct Point {
        double a, p; // the anchor's exact start offset and the interval, in us
    };
    void restart(std::int64_t anchor, double half)
    {
        m_anchor = anchor;
        m_k = 0;
        // 1 to 1000 frames a second.
        m_polygon = {{-half, 1e3}, {half, 1e3}, {half, 1e6}, {-half, 1e6}};
    }
    // Keeps (a, p) with ca * a + cp * p <= c (Sutherland-Hodgman).
    void clip(double ca, double cp, double c)
    {
        std::vector<Point> out;
        const std::size_t n = m_polygon.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Point &x = m_polygon[i], &y = m_polygon[(i + 1) % n];
            const double fx = ca * x.a + cp * x.p - c, fy = ca * y.a + cp * y.p - c;
            if (fx <= 0)
                out.push_back(x);
            if ((fx < 0 && fy > 0) || (fx > 0 && fy < 0)) {
                const double t = fx / (fx - fy);
                out.push_back({x.a + t * (y.a - x.a), x.p + t * (y.p - x.p)});
            }
        }
        std::vector<Point> kept;
        const auto same = [](const Point &x, const Point &y) {
            return std::abs(x.a - y.a) <= 1e-6 && std::abs(x.p - y.p) <= 1e-9;
        };
        for (const Point &q : out)
            if (kept.empty() || !same(kept.back(), q))
                kept.push_back(q);
        if (kept.size() > 1 && same(kept.front(), kept.back()))
            kept.pop_back();
        m_polygon = std::move(kept);
    }
    // False when no pace allows frame k to start at `startUs`.
    bool cut(std::int64_t k, std::int64_t startUs, double half)
    {
        const double x = static_cast<double>(startUs - m_anchor), kd = static_cast<double>(k);
        clip(1.0, kd, x + half);
        if (m_polygon.size() >= 3)
            clip(-1.0, -kd, -(x - half));
        return m_polygon.size() >= 3;
    }
    // MUST KILL: span takes (cp, ca) where clip takes (ca, cp)
    // The range of a * ca + p * cp over the polygon.
    void span(double cp, double ca, double &lo, double &hi) const
    {
        lo = std::numeric_limits<double>::infinity();
        hi = -lo;
        for (const Point &q : m_polygon) {
            const double v = ca * q.a + cp * q.p;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
    void record(bool hit)
    {
        m_misses = ((m_misses << 1) | (hit ? 0u : 1u)) & 0xffffu;
        m_recorded = std::min(m_recorded + 1, 16);
    }
    bool unreliable() const { return m_recorded > 0 && std::popcount(m_misses) * 2 >= std::max(m_recorded, 8); }

    std::uint64_t m_epoch = 0;
    std::optional<std::int64_t> m_last;
    std::int64_t m_anchor = 0;
    std::int64_t m_k = 0; // frames from the anchor to the last start
    std::vector<Point> m_polygon;
    std::optional<std::int64_t> m_guess;
    std::uint32_t m_misses = 0;          // the last guesses, newest in bit 0 (1: missed)
    int m_recorded = 0;
};

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
    std::optional<std::pair<int, int>> screen; // nullopt: the video's size
    Counters counters;
    struct AheadRequest {
        std::int64_t timeUs;
        OverlaySize size;
    };
    std::vector<AheadRequest> aheadRequests; // the likeliest first
    struct Ahead {
        AheadRequest at;
        std::uint64_t serial;
        std::shared_ptr<const OverlayFrame> overlay;
        std::optional<PixelRect> bounds;
    };
    // Worker-only.
    std::vector<Ahead> ahead;
    Pace pace;
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

void PlaybackPipeline::setScreenSize(std::optional<std::pair<int, int>> size)
{
    std::lock_guard lock(m_state->mutex);
    m_state->screen = size;
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
    const auto unused = [this, &ahead](bool taken) {
        const std::size_t n = ahead.size() - (taken ? 1 : 0);
        if (n == 0)
            return;
        std::lock_guard lock(m_state->mutex);
        m_state->counters.aheadUnused += n;
    };
    if (!out.frame) {
        unused(false);
        return out;
    }
    std::uint64_t serial = 0;
    bool on = false;
    std::optional<std::pair<int, int>> screen;
    {
        std::lock_guard lock(m_state->mutex);
        serial = m_state->serial;
        on = m_state->on;
        screen = m_state->screen;
    }
    if (!serial) {
        unused(false);
        return out;
    }
    out.subtitles = serial;
    if (!on) {
        unused(false);
        return out;
    }
    const OverlaySize size = overlaySizeFor(out.frame->width, out.frame->height, screen);
    for (auto &made : ahead)
        if (made.serial == serial && millisecond(made.at.timeUs) == millisecond(input.startUs) && made.at.size == size) {
            out.overlay = std::move(made.overlay);
            out.overlayBounds = made.bounds;
            unused(true);
            std::lock_guard lock(m_state->mutex);
            ++m_state->counters.aheadUsed;
            return out;
        }
    unused(false);
    if (auto overlay = renderOverlay(m_renderer, core::DocumentTime(input.startUs), size)) {
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
            if (auto overlay = renderOverlay(m_renderer, core::DocumentTime(ahead->timeUs), ahead->size)) {
                made.bounds = overlayBounds(*overlay);
                made.overlay = std::make_shared<const OverlayFrame>(std::move(*overlay));
            }
            state->ahead.push_back(std::move(made));
            {
                std::lock_guard lock(state->mutex);
                ++state->counters.aheadRendered;
            }
            continue;
        }
        bool withheld = false;
        const std::optional<std::int64_t> next = state->pace.next(input->epoch, input->startUs, input->endUs, withheld);
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
            if (shown && !state->input) {
                if (next) {
                    state->aheadRequests.push_back({*next * 1000, overlaySizeFor(width, height, state->screen)});
                    ++state->counters.aheadPlanned;
                }
                if (withheld)
                    ++state->counters.aheadWithheld;
            }
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

}
