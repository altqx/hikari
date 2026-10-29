#include "hikari/application/playback_pipeline.h"
#include "hikari/application/yuv_bgra.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <thread>

using namespace hikari;
using namespace hikari::application;

namespace {

using namespace std::chrono_literals;

template <typename F> bool eventually(F done)
{
    const auto until = std::chrono::steady_clock::now() + 5s;
    while (!done()) {
        if (std::chrono::steady_clock::now() > until)
            return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

struct Renderer : SubtitleRendererPort {
    mutable std::mutex mutex;
    std::vector<std::int64_t> renderedAt;
    std::vector<OverlaySize> sizes;
    std::expected<std::uint64_t, RenderError> prepare(RenderSnapshot) override { return 1; }
    std::expected<OverlayFrame, RenderError> renderScaled(core::DocumentTime t, int w, int h, int vw, int vh) override
    {
        {
            std::lock_guard lock(mutex);
            sizes.push_back({w, h, vw, vh});
        }
        return draw(t, w, h);
    }
    std::expected<OverlayFrame, RenderError> render(core::DocumentTime t, int w, int h) override
    {
        {
            std::lock_guard lock(mutex);
            sizes.push_back({w, h, w, h});
        }
        return draw(t, w, h);
    }
    std::expected<OverlayFrame, RenderError> draw(core::DocumentTime t, int w, int h)
    {
        std::lock_guard lock(mutex);
        renderedAt.push_back(t.microseconds());
        OverlayFrame o;
        o.width = w;
        o.height = h;
        o.stride = w * 4;
        OverlayPixels::Bytes pixels(static_cast<std::size_t>(o.stride) * static_cast<std::size_t>(h), 0);
        pixels[4 + 3] = 255;
        o.pixels = std::move(pixels);
        o.empty = false;
        return o;
    }
    std::vector<std::int64_t> times() const
    {
        std::lock_guard lock(mutex);
        return renderedAt;
    }
    std::vector<OverlaySize> sizesDrawn() const
    {
        std::lock_guard lock(mutex);
        return sizes;
    }
};

struct Queue {
    std::mutex mutex;
    std::deque<std::function<void()>> tasks;
    PlaybackPipeline::Post post()
    {
        return [this](std::function<void()> task) {
            std::lock_guard lock(mutex);
            tasks.push_back(std::move(task));
        };
    }
    std::size_t size()
    {
        std::lock_guard lock(mutex);
        return tasks.size();
    }
    void drain()
    {
        for (;;) {
            std::function<void()> task;
            {
                std::lock_guard lock(mutex);
                if (tasks.empty())
                    return;
                task = std::move(tasks.front());
                tasks.pop_front();
            }
            task();
        }
    }
};

struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false, open = false;
    void wait()
    {
        std::unique_lock lock(mutex);
        entered = true;
        cv.notify_all();
        cv.wait(lock, [this] { return open; });
    }
    void waitEntered()
    {
        std::unique_lock lock(mutex);
        cv.wait(lock, [this] { return entered; });
    }
    void release()
    {
        std::lock_guard lock(mutex);
        open = true;
        cv.notify_all();
    }
};

PlaybackPipeline::Make frame(int width = 4, int height = 2, Gate *gate = nullptr)
{
    return [=](FramePool &pool) {
        if (gate)
            gate->wait();
        auto f = pool.acquire();
        f->width = width;
        f->height = height;
        f->stride = width * 4;
        f->bgra.resize(static_cast<std::size_t>(f->stride) * static_cast<std::size_t>(height));
        return f;
    };
}

PlaybackPipeline::Input input(std::int64_t startUs, std::optional<std::int64_t> endUs = std::nullopt,
                              PlaybackPipeline::Make make = frame(), std::uint64_t epoch = 1)
{
    return {epoch, startUs, endUs, std::move(make)};
}

}

TEST(FramePool, ReusesAReleasedBuffer)
{
    const auto pool = FramePool::create(2);
    const std::byte *first = nullptr;
    {
        auto a = pool->acquire();
        a->bgra.resize(1024);
        first = a->bgra.data();
    }
    auto b = pool->acquire();
    EXPECT_EQ(b->bgra.size(), 1024u);
    b->bgra.resize(1024);
    EXPECT_EQ(b->bgra.data(), first);
    auto c = pool->acquire();
    EXPECT_TRUE(c->bgra.empty());
    EXPECT_EQ(pool->fresh(), 2u);
}

TEST(FramePool, AFrameMayOutliveItsPool)
{
    auto pool = FramePool::create();
    auto frame = pool->acquire();
    frame->bgra.resize(16);
    pool.reset();
    frame.reset();
    SUCCEED();
}

TEST(PlaybackPipeline, InlineRendersEachFrameAtItsStart)
{
    Renderer renderer;
    std::vector<PlaybackPipeline::Output> got;
    PlaybackPipeline pipeline(renderer, nullptr, [&](PlaybackPipeline::Output o) { got.push_back(std::move(o)); });
    pipeline.submit(input(40'000));
    pipeline.setSubtitles(3, true);
    pipeline.submit(input(80'000));
    pipeline.setSubtitles(4, false);
    pipeline.submit(input(120'000));
    ASSERT_EQ(got.size(), 3u);
    EXPECT_EQ(got[0].subtitles, 0u);
    EXPECT_FALSE(got[0].overlay);
    EXPECT_EQ(got[1].subtitles, 3u);
    ASSERT_TRUE(got[1].overlay);
    EXPECT_EQ(got[1].overlay->width, 4);
    EXPECT_EQ(got[1].overlayBounds, (PixelRect{1, 0, 1, 1}));
    EXPECT_EQ(got[2].subtitles, 4u);
    EXPECT_FALSE(got[2].overlay);
    EXPECT_EQ(renderer.times(), std::vector<std::int64_t>{80'000});
}

TEST(PlaybackPipeline, OnlyTheNewestFrameWaitsForTheWorker)
{
    Renderer renderer;
    Queue queue;
    std::vector<std::int64_t> shown;
    PlaybackPipeline pipeline(renderer, queue.post(), [&](PlaybackPipeline::Output o) { shown.push_back(o.startUs); });
    Gate gate;
    pipeline.submit(input(0, std::nullopt, frame(4, 2, &gate)));
    gate.waitEntered();
    for (int i = 1; i <= 20; ++i)
        pipeline.submit(input(i * 40'000));
    gate.release();
    ASSERT_TRUE(eventually([&] { return pipeline.counters().submitted == 21 && queue.size() >= 1; }));
    ASSERT_TRUE(eventually([&] {
        queue.drain();
        return !shown.empty() && shown.back() == 800'000;
    }));
    const auto c = pipeline.counters();
    EXPECT_EQ(c.submitted, 21u);
    EXPECT_EQ(c.superseded, 19u);
    EXPECT_LE(shown.size(), 2u);
    EXPECT_EQ(c.delivered, shown.size());
}

TEST(PlaybackPipeline, OnlyTheNewestResultWaitsForTheOwner)
{
    Renderer renderer;
    Queue queue;
    std::vector<std::int64_t> shown;
    PlaybackPipeline pipeline(renderer, queue.post(), [&](PlaybackPipeline::Output o) { shown.push_back(o.startUs); });
    pipeline.submit(input(0));
    ASSERT_TRUE(eventually([&] { return queue.size() == 1; }));
    pipeline.submit(input(40'000));
    ASSERT_TRUE(eventually([&] { return pipeline.counters().replacedResults == 1; }));
    pipeline.submit(input(80'000));
    ASSERT_TRUE(eventually([&] { return pipeline.counters().replacedResults == 2; }));
    EXPECT_EQ(queue.size(), 1u);
    queue.drain();
    EXPECT_EQ(shown, std::vector<std::int64_t>{80'000});
}

TEST(PlaybackPipeline, TheNextFramesOverlayIsRenderedAhead)
{
    Renderer renderer;
    Queue queue;
    std::vector<PlaybackPipeline::Output> got;
    PlaybackPipeline pipeline(renderer, queue.post(), [&](PlaybackPipeline::Output o) { got.push_back(std::move(o)); });
    pipeline.setSubtitles(1, true);
    pipeline.submit(input(0, 40'000));
    ASSERT_TRUE(eventually([&] { return renderer.times().size() == 2; }));
    EXPECT_EQ(renderer.times(), (std::vector<std::int64_t>{0, 40'000}));
    pipeline.submit(input(40'000, 80'000));
    ASSERT_TRUE(eventually([&] { return pipeline.counters().aheadUsed == 1 && queue.size() == 1; }));
    queue.drain();
    ASSERT_FALSE(got.empty());
    EXPECT_EQ(got.back().startUs, 40'000);
    ASSERT_TRUE(got.back().overlay);
    EXPECT_EQ(got.back().overlayBounds, (PixelRect{1, 0, 1, 1}));
    const auto times = renderer.times();
    EXPECT_EQ(std::count(times.begin(), times.end(), 40'000), 1);
}

TEST(PlaybackPipeline, OverlaysAreDrawnAtTheScreenSize)
{
    Renderer renderer;
    Queue queue;
    std::vector<PlaybackPipeline::Output> got;
    PlaybackPipeline pipeline(renderer, queue.post(), [&](PlaybackPipeline::Output o) { got.push_back(std::move(o)); });
    pipeline.setSubtitles(1, true);
    pipeline.setScreenSize(std::pair{8, 4});
    pipeline.submit(input(0, 40'000));
    ASSERT_TRUE(eventually([&] { return renderer.times().size() == 2; }));
    EXPECT_EQ(renderer.sizesDrawn(), (std::vector<OverlaySize>{{8, 4, 4, 2}, {8, 4, 4, 2}}));
    pipeline.submit(input(40'000, 80'000));
    ASSERT_TRUE(eventually([&] { return pipeline.counters().aheadUsed == 1 && queue.size() == 1; }));
    queue.drain();
    ASSERT_TRUE(got.back().overlay);
    EXPECT_EQ(got.back().overlay->width, 8);
    ASSERT_TRUE(eventually([&] { return renderer.times().size() == 3; }));
    pipeline.setScreenSize(std::pair{12, 6});
    pipeline.submit(input(80'000, 120'000));
    ASSERT_TRUE(eventually([&] { return queue.size() == 1; }));
    queue.drain();
    EXPECT_EQ(pipeline.counters().aheadUsed, 1);
    ASSERT_TRUE(got.back().overlay);
    EXPECT_EQ(got.back().overlay->width, 12);
    EXPECT_EQ(got.back().overlay->height, 6);
    ASSERT_TRUE(eventually([&] { return renderer.times().size() == 5; }));
    pipeline.setScreenSize(std::nullopt);
    pipeline.submit(input(120'000, 160'000));
    ASSERT_TRUE(eventually([&] { return queue.size() == 1; }));
    queue.drain();
    ASSERT_TRUE(got.back().overlay);
    EXPECT_EQ(got.back().overlay->width, 4);
    EXPECT_EQ(renderer.sizesDrawn()[5], (OverlaySize{4, 2, 4, 2}));
}

TEST(PlaybackPipeline, AnOverlayRenderedAheadForOlderSubtitlesIsNotUsed)
{
    Renderer renderer;
    Queue queue;
    std::vector<PlaybackPipeline::Output> got;
    PlaybackPipeline pipeline(renderer, queue.post(), [&](PlaybackPipeline::Output o) { got.push_back(std::move(o)); });
    pipeline.setSubtitles(1, true);
    pipeline.submit(input(0, 40'000));
    ASSERT_TRUE(eventually([&] { return renderer.times().size() == 2; }));
    pipeline.setSubtitles(2, true);
    pipeline.submit(input(40'000, 80'000));
    ASSERT_TRUE(eventually([&] {
        queue.drain();
        return !got.empty() && got.back().startUs == 40'000;
    }));
    EXPECT_EQ(pipeline.counters().aheadUsed, 0u);
    EXPECT_EQ(got.back().subtitles, 2u);
    const auto times = renderer.times();
    EXPECT_EQ(std::count(times.begin(), times.end(), 40'000), 2);
}

namespace {

PlaybackPipeline::Counters playOneByOne(const std::vector<std::int64_t> &starts, std::vector<std::int64_t> *rendered = nullptr)
{
    Renderer renderer;
    Queue queue;
    std::size_t shown = 0;
    PlaybackPipeline pipeline(renderer, queue.post(), [&](PlaybackPipeline::Output) { ++shown; });
    pipeline.setSubtitles(1, true);
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const auto end = i + 1 < starts.size() ? std::optional(starts[i + 1]) : std::nullopt;
        pipeline.submit(input(starts[i], end));
        EXPECT_TRUE(eventually([&] {
            queue.drain();
            return shown == i + 1;
        }));
        EXPECT_TRUE(eventually([&] {
            const auto c = pipeline.counters();
            return c.aheadRendered == c.aheadPlanned;
        }));
    }
    if (rendered)
        *rendered = renderer.times();
    return pipeline.counters();
}

}

TEST(PlaybackPipeline, AConstantRateInMillisecondsIsRenderedAheadOnce)
{
    std::vector<std::int64_t> starts;
    for (int n = 0; n < 240; ++n)
        starts.push_back((static_cast<std::int64_t>(n) * 1'001'000 / 24 + 500) / 1000 * 1000);
    std::vector<std::int64_t> rendered;
    const auto c = playOneByOne(starts, &rendered);
    EXPECT_GE(c.aheadUsed, 230u);
    EXPECT_LE(c.aheadUnused, 12u);
    EXPECT_EQ(c.aheadWithheld, 0u);
    EXPECT_LE(rendered.size(), starts.size() + 14);
}

TEST(PlaybackPipeline, AConstantRateInMicrosecondsIsRenderedAheadOnce)
{
    std::vector<std::int64_t> starts;
    for (int n = 0; n < 120; ++n)
        starts.push_back(static_cast<std::int64_t>(n) * 3003 * 1'000'000 / 90'000 + 1);
    const auto c = playOneByOne(starts);
    EXPECT_GE(c.aheadUsed, 115u);
    EXPECT_LE(c.aheadUnused, 3u);
}

TEST(PlaybackPipeline, AnUnpredictablePaceStopsRenderingAhead)
{
    std::mt19937 random(7);
    std::uniform_int_distribution<int> step(15, 70);
    std::vector<std::int64_t> starts{0};
    for (int n = 1; n < 160; ++n)
        starts.push_back(starts.back() + step(random) * 1000);
    std::vector<std::int64_t> rendered;
    const auto c = playOneByOne(starts, &rendered);
    EXPECT_GE(c.aheadWithheld, 130u);
    EXPECT_LE(c.aheadUnused, 10u);
    EXPECT_LE(rendered.size(), starts.size() + 10);
}

TEST(PlaybackPipeline, AFrameWithNothingToShowGivesNoOverlay)
{
    Renderer renderer;
    std::vector<PlaybackPipeline::Output> got;
    PlaybackPipeline pipeline(renderer, nullptr, [&](PlaybackPipeline::Output o) { got.push_back(std::move(o)); });
    pipeline.setSubtitles(1, true);
    pipeline.submit(input(0, std::nullopt, [](FramePool &) { return std::shared_ptr<IndexedFrame>(); }));
    ASSERT_EQ(got.size(), 1u);
    EXPECT_FALSE(got[0].frame);
    EXPECT_TRUE(renderer.times().empty());
}

TEST(PlaybackPipeline, StoppingDropsWaitingWork)
{
    Renderer renderer;
    Queue queue;
    int shown = 0;
    {
        PlaybackPipeline pipeline(renderer, queue.post(), [&](PlaybackPipeline::Output) { ++shown; });
        pipeline.submit(input(0));
        ASSERT_TRUE(eventually([&] { return queue.size() == 1; }));
    }
    queue.drain();
    EXPECT_EQ(shown, 0);
}

TEST(OverlaySize, FollowsTheScreenUpToEightK)
{
    EXPECT_EQ(overlaySizeFor(1920, 1080, std::nullopt), (OverlaySize{1920, 1080, 1920, 1080}));
    EXPECT_FALSE(overlaySizeFor(1920, 1080, std::nullopt).scaled());
    EXPECT_EQ(overlaySizeFor(1920, 1080, std::pair{3840, 2160}), (OverlaySize{3840, 2160, 1920, 1080}));
    EXPECT_TRUE(overlaySizeFor(1920, 1080, std::pair{3840, 2160}).scaled());
    EXPECT_FALSE(overlaySizeFor(1920, 1080, std::pair{1920, 1080}).scaled());
    EXPECT_EQ(overlaySizeFor(1920, 1080, std::pair{15360, 8640}), (OverlaySize{7680, 4320, 1920, 1080}));
    EXPECT_EQ(overlaySizeFor(640, 480, std::pair{10000, 1000}), (OverlaySize{7680, 768, 640, 480}));
    EXPECT_EQ(overlaySizeFor(640, 480, std::pair{0, 0}), (OverlaySize{640, 480, 640, 480}));
}

TEST(OverlayBounds, CoverTheVisiblePixels)
{
    OverlayFrame o;
    o.width = 9;
    o.height = 4;
    o.stride = 40;
    OverlayPixels::Bytes pixels(160, 0);
    o.pixels = pixels;
    o.empty = false;
    EXPECT_FALSE(overlayBounds(o));
    auto set = [&](int x, int y) {
        pixels[static_cast<std::size_t>(y * o.stride + x * 4 + 3)] = 1;
        o.pixels = pixels;
    };
    set(3, 1);
    EXPECT_EQ(overlayBounds(o), (PixelRect{3, 1, 1, 1}));
    set(8, 2);
    set(0, 3);
    EXPECT_EQ(overlayBounds(o), (PixelRect{0, 1, 9, 3}));
    pixels[38] = 9;
    o.pixels = pixels;
    EXPECT_EQ(overlayBounds(o), (PixelRect{0, 1, 9, 3}));
    o.empty = true;
    EXPECT_FALSE(overlayBounds(o));
}

TEST(OverlayBounds, TheRenderersBoundsReplaceTheScan)
{
    OverlayFrame o;
    o.width = 9;
    o.height = 4;
    o.stride = 36;
    OverlayPixels::Bytes pixels(144, 0);
    pixels[static_cast<std::size_t>(2 * o.stride + 3 * 4 + 3)] = 1;
    o.pixels = pixels;
    o.empty = false;
    EXPECT_EQ(overlayBounds(o), (PixelRect{3, 2, 1, 1}));
    o.bounds = PixelRect{2, 1, 4, 3};
    EXPECT_EQ(overlayBounds(o), (PixelRect{2, 1, 4, 3}));
    o.bounds = PixelRect{-2, 2, 20, 9};
    EXPECT_EQ(overlayBounds(o), (PixelRect{0, 2, 9, 2}));
    o.bounds = PixelRect{9, 0, 3, 3};
    EXPECT_FALSE(overlayBounds(o));
    o.bounds = PixelRect{2, 1, 4, 3};
    o.empty = true;
    EXPECT_FALSE(overlayBounds(o));
}

namespace {

void reference(const yuv::Matrix &m, const std::uint8_t *y, const std::uint8_t *u, const std::uint8_t *v, int width,
               std::uint8_t *out)
{
    auto clamp = [](int x) { return static_cast<std::uint8_t>(x < 0 ? 0 : x > 255 ? 255 : x); };
    const int round = 1 << (m.shift - 1);
    for (int x = 0; x < width; ++x) {
        const int c = m.cy * (y[x] - m.yOffset), d = u[x / 2] - 128, e = v[x / 2] - 128;
        out[4 * x + 0] = clamp((c + m.bu * d + round) >> m.shift);
        out[4 * x + 1] = clamp((c + m.gu * d + m.gv * e + round) >> m.shift);
        out[4 * x + 2] = clamp((c + m.rv * e + round) >> m.shift);
        out[4 * x + 3] = 255;
    }
}

}

TEST(YuvRow, MatchesTheFormulaForEveryMatrixAndWidth)
{
    std::vector<yuv::Matrix> matrices;
    for (auto space : {yuv::Space::Bt601, yuv::Space::Bt709, yuv::Space::Bt2020, yuv::Space::Fcc, yuv::Space::Smpte240m})
        for (bool full : {false, true})
            matrices.push_back(yuv::matrix(space, full));
    matrices.push_back({298, 409, -100, -208, 516, 16, 8}); // DirectShow BT.601
    matrices.push_back({298, 459, -55, -136, 541, 16, 8});  // DirectShow BT.709
    std::mt19937 random(7);
    std::uniform_int_distribution<int> byte(0, 255);
    for (const auto &m : matrices)
        for (int width = 1; width <= 67; ++width) {
            std::vector<std::uint8_t> y(static_cast<std::size_t>(width)), u(static_cast<std::size_t>(width / 2 + 9)),
                v(u.size());
            for (auto &s : y)
                s = static_cast<std::uint8_t>(byte(random));
            for (std::size_t i = 0; i < u.size(); ++i) {
                u[i] = static_cast<std::uint8_t>(byte(random));
                v[i] = static_cast<std::uint8_t>(byte(random));
            }
            y[0] = 0;
            u[0] = 255;
            v[0] = 0;
            std::vector<std::uint8_t> got(static_cast<std::size_t>(width) * 4), want(got.size());
            yuv::row(m, y.data(), u.data(), v.data(), width, got.data());
            reference(m, y.data(), u.data(), v.data(), width, want.data());
            ASSERT_EQ(got, want) << "width " << width << " cy " << m.cy;
        }
}

TEST(YuvRow, LimitedRangeBlackAndWhiteAreExact)
{
    for (auto space : {yuv::Space::Bt601, yuv::Space::Bt709, yuv::Space::Bt2020, yuv::Space::Fcc, yuv::Space::Smpte240m}) {
        const auto m = yuv::matrix(space, false);
        std::vector<std::uint8_t> y(32), u(24, 128), v(24, 128), out(128);
        for (int x = 0; x < 32; ++x)
            y[static_cast<std::size_t>(x)] = x % 2 ? 235 : 16;
        yuv::row(m, y.data(), u.data(), v.data(), 32, out.data());
        for (int x = 0; x < 32; ++x)
            for (int c = 0; c < 3; ++c)
                ASSERT_EQ(out[static_cast<std::size_t>(4 * x + c)], x % 2 ? 255 : 0);
    }
    const auto m = yuv::matrix(yuv::Space::Bt709, false);
    const std::uint8_t y = 63, u = 102, v = 240;
    std::uint8_t out[4];
    yuv::row(m, &y, &u, &v, 1, out);
    EXPECT_NEAR(out[2], 255, 1);
    EXPECT_NEAR(out[1], 0, 1);
    EXPECT_NEAR(out[0], 0, 1);
}
