// N6: the editor output's real-time half without a device. These pin the
// callback constraints (no allocation; silence when nothing is ready), the
// counters, and when clock estimates are invalidated (M50-device: underrun,
// suspend/stall, stop, loss).
#include "hikari/backends/audio_output_engine.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>

namespace {

// Allocation counter for the calling thread. Replacing the global operators
// covers every allocation in this executable; only flagged regions count.
thread_local bool t_counting = false;
thread_local int t_allocations = 0;

} // namespace

void *operator new(std::size_t size)
{
    if (t_counting)
        ++t_allocations;
    if (void *p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }

namespace hikari::backends {
namespace {

using application::OutputFormat;

constexpr OutputFormat kStereo{48000, 2};
constexpr std::size_t kFrames = 256;
constexpr double kBuffer = static_cast<double>(kFrames) / 48000;

std::vector<float> ramp(std::size_t samples, float first = 1)
{
    std::vector<float> v(samples);
    for (std::size_t i = 0; i < samples; ++i)
        v[i] = first + static_cast<float>(i);
    return v;
}

// Drives render() like a host: consecutive buffers on a regular stream clock.
struct Host {
    OutputEngine &engine;
    std::vector<float> out = std::vector<float>(kFrames * 2);
    int index = 0;
    double streamOffset = 0;

    bool tick(bool underflow = false)
    {
        const double stream = 10 + streamOffset + index * kBuffer;
        ++index;
        return engine.render(out, {stream + 0.05, stream, stream, underflow});
    }
};

TEST(AudioRing, CapacityIsAPowerOfTwoAndFullRingRefusesMore)
{
    AudioRing ring(100);
    EXPECT_EQ(ring.capacity(), 128u);
    const auto data = ramp(200);
    EXPECT_EQ(ring.push(data), 128u);
    EXPECT_EQ(ring.push(data), 0u);
    EXPECT_EQ(ring.readable(), 128u);
    EXPECT_EQ(ring.writable(), 0u);
}

TEST(AudioRing, PopWrapsAndFillsTheRestWithSilence)
{
    AudioRing ring(8);
    std::vector<float> out(6);
    ring.push(ramp(6));
    EXPECT_EQ(ring.pop(out), 6u);
    ring.push(ramp(5, 100)); // wraps past the end of the buffer
    std::vector<float> wide(8, -1);
    EXPECT_EQ(ring.pop(wide), 5u);
    EXPECT_EQ(wide, (std::vector<float>{100, 101, 102, 103, 104, 0, 0, 0}));
}

TEST(AudioRing, ConcurrentProducerAndConsumerKeepOrder)
{
    AudioRing ring(1024);
    constexpr std::size_t kTotal = 2'000'000;
    std::thread producer([&] {
        std::size_t next = 0;
        std::vector<float> chunk;
        while (next < kTotal) {
            const std::size_t n = std::min<std::size_t>(1 + next % 97, kTotal - next);
            chunk.resize(n);
            for (std::size_t i = 0; i < n; ++i)
                chunk[i] = static_cast<float>((next + i) % 1'000'000 + 1);
            next += ring.push(chunk);
        }
    });
    std::size_t seen = 0;
    std::vector<float> out(64);
    bool ordered = true;
    while (seen < kTotal) {
        const std::size_t n = ring.pop(out);
        for (std::size_t i = 0; i < n; ++i)
            ordered = ordered && out[i] == static_cast<float>((seen + i) % 1'000'000 + 1);
        for (std::size_t i = n; i < out.size(); ++i)
            ordered = ordered && out[i] == 0;
        seen += n;
    }
    producer.join();
    EXPECT_TRUE(ordered);
}

TEST(OutputEngine, RenderNeverAllocates)
{
    OutputEngine engine(kStereo, 0.02, 0.1);
    engine.started();
    Host host{engine};
    const auto data = ramp(kFrames * 2 * 3);
    engine.write(data);
    t_allocations = 0;
    t_counting = true;
    // Every path: full buffers, an underrun, starvation, a host underflow, a
    // clock jump, then an injected loss.
    for (int i = 0; i < 6; ++i)
        host.tick();
    host.tick(true);
    host.streamOffset = 5;
    host.tick();
    engine.injectLoss();
    const bool continued = host.tick();
    t_counting = false;
    EXPECT_EQ(t_allocations, 0);
    EXPECT_FALSE(continued);
}

TEST(OutputEngine, WritesWholeFramesAndFillsSilence)
{
    OutputEngine engine(kStereo, 0.02, 0.1);
    engine.started();
    EXPECT_EQ(engine.write(ramp(201)), 200u); // the odd sample is not a frame
    Host host{engine};
    host.tick();
    for (std::size_t i = 0; i < 200; ++i)
        ASSERT_EQ(host.out[i], 1 + static_cast<float>(i));
    for (std::size_t i = 200; i < host.out.size(); ++i)
        ASSERT_EQ(host.out[i], 0.0f);
    const auto s = engine.status();
    EXPECT_EQ(s.callbacks, 1u);
    EXPECT_EQ(s.framesConsumed, 100u);
    EXPECT_EQ(s.silenceFrames, kFrames - 100);
    EXPECT_EQ(s.underruns, 0u) << "not flowing yet: starting short is not an underrun";
}

TEST(OutputEngine, ClockIsAnEstimateWithItsUncertainty)
{
    OutputEngine engine(kStereo, 0.02, 0.1);
    engine.started();
    EXPECT_FALSE(engine.clock(10).valid) << "no callback has run";
    engine.write(ramp(kFrames * 4));
    Host host{engine};
    host.tick();
    host.tick();
    const auto e = engine.clock(10 + kBuffer);
    ASSERT_TRUE(e.valid);
    EXPECT_EQ(e.domain, application::ClockDomain::StreamTime);
    EXPECT_EQ(e.framesConsumed, kFrames) << "frames queued before the latest buffer";
    EXPECT_EQ(e.readyFrames, kFrames);
    EXPECT_EQ(e.bufferFrames, kFrames);
    EXPECT_DOUBLE_EQ(e.streamTimeSeconds, 10 + kBuffer);
    EXPECT_DOUBLE_EQ(e.dacTimeSeconds, 10 + kBuffer + 0.05);
    EXPECT_FALSE(e.dacTimeDerived);
    EXPECT_DOUBLE_EQ(e.uncertaintySeconds, kBuffer);
}

TEST(OutputEngine, MissingDacTimeIsDerivedFromLatency)
{
    OutputEngine engine(kStereo, 0.03, 0.1);
    engine.started();
    std::vector<float> out(kFrames * 2);
    engine.render(out, {0, 4, 4, false});
    const auto e = engine.clock(4);
    ASSERT_TRUE(e.valid);
    EXPECT_TRUE(e.dacTimeDerived);
    EXPECT_DOUBLE_EQ(e.dacTimeSeconds, 4.03);
    EXPECT_DOUBLE_EQ(e.uncertaintySeconds, kBuffer + 0.03);
}

TEST(OutputEngine, UnderrunStartsANewEpoch)
{
    OutputEngine engine(kStereo, 0.02, 0.1);
    engine.started();
    engine.write(ramp(kFrames * 2 * 2 + 20));
    Host host{engine};
    host.tick();
    host.tick();
    const auto before = engine.clock(10).epoch;
    host.tick(); // 10 frames ready of 256
    const auto s = engine.status();
    EXPECT_EQ(s.underruns, 1u);
    const auto after = engine.clock(10 + 2 * kBuffer);
    EXPECT_TRUE(after.valid) << "the new epoch's estimate is usable";
    EXPECT_GT(after.epoch, before);
    EXPECT_EQ(after.readyFrames, 10u);
    host.tick(); // still starved: one underrun, not one per callback
    EXPECT_EQ(engine.status().underruns, 1u);
    EXPECT_EQ(engine.clock(10 + 3 * kBuffer).epoch, after.epoch);
}

TEST(OutputEngine, HostUnderflowStartsANewEpoch)
{
    OutputEngine engine(kStereo, 0.02, 0.1);
    engine.started();
    Host host{engine};
    host.tick();
    const auto before = engine.clock(10).epoch;
    host.tick(true);
    EXPECT_EQ(engine.status().underruns, 1u);
    EXPECT_GT(engine.clock(10 + kBuffer).epoch, before);
}

TEST(OutputEngine, StreamClockJumpIsADiscontinuity)
{
    OutputEngine engine(kStereo, 0.02, 0.1);
    engine.started();
    Host host{engine};
    host.tick();
    host.tick();
    const auto before = engine.clock(10).epoch;
    host.streamOffset = 30; // resumed after a suspend
    host.tick();
    EXPECT_EQ(engine.status().discontinuities, 1u);
    EXPECT_GT(engine.clock(40 + 2 * kBuffer).epoch, before);
}

TEST(OutputEngine, StalledCallbacksInvalidateTheEstimate)
{
    OutputEngine engine(kStereo, 0.02, 0.1);
    engine.started();
    Host host{engine};
    host.tick();
    EXPECT_TRUE(engine.clock(10 + engine.stallLimit()).valid);
    EXPECT_FALSE(engine.clock(10 + engine.stallLimit() + 0.01).valid) << "suspended: never extrapolate";
}

TEST(OutputEngine, StopDiscardsQueuedAudioAndInvalidates)
{
    OutputEngine engine(kStereo, 0.02, 0.1);
    engine.started();
    engine.write(ramp(kFrames * 2 * 2));
    Host host{engine};
    host.tick();
    const auto epoch = engine.clock(10).epoch;
    engine.stopRequested();
    engine.streamFinished();
    engine.stopped();
    EXPECT_FALSE(engine.status().running);
    EXPECT_FALSE(engine.status().deviceLost) << "a requested stop is not a loss";
    EXPECT_FALSE(engine.clock(10).valid);
    engine.started();
    host.tick();
    EXPECT_GT(engine.clock(10 + kBuffer).epoch, epoch);
    EXPECT_EQ(host.out[0], 0.0f) << "the queue was flushed";
    EXPECT_EQ(engine.status().framesConsumed, kFrames);
}

TEST(OutputEngine, UnrequestedStreamEndIsDeviceLoss)
{
    OutputEngine engine(kStereo, 0.02, 0.1);
    engine.started();
    Host host{engine};
    host.tick();
    engine.injectLoss();
    EXPECT_FALSE(host.tick());
    engine.streamFinished();
    const auto s = engine.status();
    EXPECT_TRUE(s.deviceLost);
    EXPECT_FALSE(s.running);
    EXPECT_FALSE(engine.clock(10).valid);
}

TEST(OutputEngine, ClockSnapshotsAreNeverTorn)
{
    OutputEngine engine(kStereo, 0.02, 0.1);
    engine.started();
    std::atomic<bool> done{false};
    std::thread callback([&] {
        std::vector<float> out(kFrames * 2);
        for (int k = 1; k <= 300'000; ++k)
            engine.render(out, {k + 0.5, double(k), double(k), false});
        done = true;
    });
    int reads = 0, torn = 0;
    while (!done) {
        const auto e = engine.clock(0);
        if (e.epoch == 0 && e.streamTimeSeconds == 0)
            continue;
        ++reads;
        if (e.dacTimeSeconds != e.streamTimeSeconds + 0.5 || e.monotonicSeconds != e.streamTimeSeconds
            || e.bufferFrames != kFrames)
            ++torn;
    }
    callback.join();
    EXPECT_EQ(torn, 0) << "of " << reads << " reads";
}

} // namespace
} // namespace hikari::backends
