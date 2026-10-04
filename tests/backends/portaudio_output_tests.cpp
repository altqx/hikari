// N6 / M50-device: the PortAudio owner against a real stream. Device tests run
// on HIKARI_TEST_AUDIO_DEVICE (a substring of the device name or id) when set, else
// the host default, and skip with the reason when the host has no output
// device. Only silence is written.
#include "hikari/backends/portaudio_output.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <regex>
#include <set>
#include <thread>
#include <vector>

namespace hikari::backends {
namespace {

using application::OutputError;
using application::OutputFormat;
using namespace std::chrono_literals;

bool waitFor(const std::function<bool()> &condition, std::chrono::milliseconds limit = 5s)
{
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

// Keeps the queue topped up with silence until `condition` holds.
bool feedUntil(PortAudioOutput &out, const std::function<bool()> &condition, std::chrono::milliseconds limit = 5s)
{
    const std::vector<float> silence(4096, 0.0f);
    return waitFor([&] {
        out.write(silence);
        return condition();
    }, limit);
}

class PortAudioDevice : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(out.available()) << "PortAudio failed to initialize";
        const auto devices = out.devices();
        const char *wanted = std::getenv("HIKARI_TEST_AUDIO_DEVICE");
        for (const auto &device : devices) {
            if (wanted && *wanted ? device.name.find(wanted) != std::string::npos || device.id.find(wanted) != std::string::npos
                                  : device.isDefault) {
                id = device.id;
                break;
            }
        }
        if (id.empty() && wanted && *wanted)
            FAIL() << "the requested test device \"" << wanted << "\" is not among " << devices.size() << " outputs";
        if (id.empty())
            GTEST_SKIP() << "no audio output device on this host (" << devices.size() << " outputs enumerated"
                         << (wanted ? std::string(", wanted \"") + wanted + "\"" : std::string()) << ")";
        std::printf("audio device under test: %s\n", id.c_str());
    }

    PortAudioOutput out;
    std::string id;
};

TEST(PortAudioOutput, EnumeratesScopedDeviceIds)
{
    PortAudioOutput out;
    ASSERT_TRUE(out.available());
    std::set<std::string> ids;
    int defaults = 0;
    for (const auto &device : out.devices()) {
        std::printf("output: %s (default %d, %d ch, %.0f Hz)\n", device.id.c_str(), device.isDefault,
                    device.maxChannels, device.defaultSampleRate);
        EXPECT_EQ(device.id.rfind(device.hostApi + "/", 0), 0u) << device.id;
        EXPECT_GT(device.maxChannels, 0);
        EXPECT_TRUE(ids.insert(device.id).second) << "duplicate id " << device.id;
        defaults += device.isDefault;
    }
    EXPECT_LE(defaults, 1);
}

TEST(PortAudioOutput, CallsWithoutAStreamFailCleanly)
{
    PortAudioOutput out;
    EXPECT_EQ(out.start().error(), OutputError::NotOpen);
    EXPECT_EQ(out.write(std::vector<float>(8)), 0u);
    EXPECT_FALSE(out.status().open);
    EXPECT_FALSE(out.clock().valid);
    out.stop();
    out.close();
    EXPECT_EQ(out.open("NoSuchApi/0/none", {}).error(), OutputError::DeviceUnavailable);
    EXPECT_EQ(out.open("malformed", {}).error(), OutputError::DeviceUnavailable);
}

TEST_F(PortAudioDevice, RefusesUnsupportedChannelCounts)
{
    EXPECT_EQ(out.open(id, {48000, 0}).error(), OutputError::InvalidFormat);
    EXPECT_EQ(out.open(id, {48000, 4096}).error(), OutputError::InvalidFormat);
}

TEST_F(PortAudioDevice, ConsumesQueuedAudioAndPublishesAClock)
{
    const auto format = out.open(id, {48000, 2});
    ASSERT_TRUE(format) << static_cast<int>(format.error());
    EXPECT_EQ(format->channels, 2);
    EXPECT_GT(out.outputLatency(), 0);
    ASSERT_TRUE(out.start());
    ASSERT_TRUE(feedUntil(out, [&] { return out.status().framesConsumed >= 4800 && out.clock().valid; }))
        << "callbacks " << out.status().callbacks;
    const auto e = out.clock();
    EXPECT_GT(e.bufferFrames, 0u);
    EXPECT_GT(e.uncertaintySeconds, 0);
    if (!e.dacTimeDerived)
        EXPECT_GE(e.dacTimeSeconds, e.streamTimeSeconds) << "the DAC time is not in the past";
    const auto s = out.status();
    EXPECT_TRUE(s.open);
    EXPECT_TRUE(s.running);
    EXPECT_FALSE(s.deviceLost);
    std::printf("callbacks %llu, consumed %llu, silence %llu, underruns %llu, buffer %u frames, latency %.4f s, "
                "dac %s\n",
                static_cast<unsigned long long>(s.callbacks), static_cast<unsigned long long>(s.framesConsumed),
                static_cast<unsigned long long>(s.silenceFrames), static_cast<unsigned long long>(s.underruns),
                e.bufferFrames, out.outputLatency(), e.dacTimeDerived ? "derived" : "reported");

    out.stop();
    EXPECT_FALSE(out.status().running);
    EXPECT_FALSE(out.status().deviceLost);
    EXPECT_FALSE(out.clock().valid) << "stopped: no estimate";
    ASSERT_TRUE(out.start()) << "restart after stop";
    ASSERT_TRUE(feedUntil(out, [&] { return out.clock().valid; }));
    EXPECT_GT(out.clock().epoch, e.epoch) << "restart begins a new epoch";
    out.close();
    EXPECT_FALSE(out.status().open);
}

TEST_F(PortAudioDevice, NegotiatesTheRateOrRefuses)
{
    const auto format = out.open(id, {12345, 2});
    if (!format) {
        EXPECT_EQ(format.error(), OutputError::InvalidFormat);
        return;
    }
    const double deviceRate = [&] {
        for (const auto &d : out.devices())
            if (d.id == id)
                return d.defaultSampleRate;
        return 0.0;
    }();
    EXPECT_TRUE(format->sampleRate == 12345 || format->sampleRate == static_cast<int>(deviceRate))
        << format->sampleRate;
}

TEST_F(PortAudioDevice, ReopensByIdAfterARescan)
{
    ASSERT_TRUE(out.open(id, {48000, 2}));
    out.close();
    const auto devices = out.devices(); // idle: rescans the host
    ASSERT_FALSE(devices.empty());
    ASSERT_TRUE(out.open(id, {48000, 2}));
    EXPECT_EQ(out.openDeviceId(), id);
    out.close();
    // An id whose index moved still names the device by host API and name.
    const auto first = id.find('/'), second = id.find('/', first + 1);
    const std::string moved = id.substr(0, first) + "/9999/" + id.substr(second + 1);
    ASSERT_TRUE(out.open(moved, {48000, 2}));
    EXPECT_EQ(out.openDeviceId(), id);
}

TEST_F(PortAudioDevice, LostStreamInvalidatesAndReopenRecovers)
{
    ASSERT_TRUE(out.open(id, {48000, 2}));
    ASSERT_TRUE(out.start());
    ASSERT_TRUE(feedUntil(out, [&] { return out.clock().valid; }));
    out.simulateDeviceLoss();
    ASSERT_TRUE(waitFor([&] { return out.status().deviceLost; })) << "the host reported no stream end";
    EXPECT_FALSE(out.status().running);
    EXPECT_FALSE(out.clock().valid);
    EXPECT_EQ(out.start().error(), OutputError::DeviceLost);

    ASSERT_TRUE(out.open(id, {48000, 2})) << "reopen";
    EXPECT_FALSE(out.status().deviceLost);
    ASSERT_TRUE(out.start());
    ASSERT_TRUE(feedUntil(out, [&] { return out.status().framesConsumed > 0 && out.clock().valid; }));
}

// M50-device with a real device that goes away and comes back: runs only with
// HIKARI_TEST_AUDIO_HOTPLUG (a regular expression found in the device's name) and optionally
// HIKARI_TEST_AUDIO_HOTPLUG_API (its host API). The test prints HOTPLUG-READY
// once the stream runs; whoever drives it (tools/winix/hotplug.sh unplugs a
// QEMU USB audio device) then removes the device, the test prints
// HOTPLUG-LOST once the loss is seen and the device is gone, and waits for the
// device to come back. Only silence is written.
TEST(PortAudioHotplug, RealDeviceLossInvalidatesAndTheReturnedDeviceReopens)
{
    const char *wanted = std::getenv("HIKARI_TEST_AUDIO_HOTPLUG");
    if (!wanted || !*wanted)
        GTEST_SKIP() << "set HIKARI_TEST_AUDIO_HOTPLUG and unplug the device on HOTPLUG-READY";
    const std::regex pattern(wanted);
    const char *api = std::getenv("HIKARI_TEST_AUDIO_HOTPLUG_API");
    const auto find = [&](PortAudioOutput &out) {
        for (const auto &device : out.devices())
            if (std::regex_search(device.name, pattern) && (!api || !*api || device.hostApi == api))
                return device.id;
        return std::string();
    };
    const auto say = [](const std::string &line) {
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
    };

    PortAudioOutput out;
    ASSERT_TRUE(out.available());
    const std::string id = find(out);
    ASSERT_FALSE(id.empty()) << "no output matching \"" << wanted << "\"";
    ASSERT_TRUE(out.open(id, {48000, 2}));
    ASSERT_TRUE(out.start());
    ASSERT_TRUE(feedUntil(out, [&] { return out.clock().valid; }));
    say("HOTPLUG-READY " + id);

    // The host reports the loss as an unrequested stream end, or its
    // callbacks stop: either way the estimate goes.
    ASSERT_TRUE(feedUntil(out, [&] { return out.status().deviceLost || !out.clock().valid; }, 120s))
        << "the device was not lost within 120 s";
    // Some hosts report the endpoint gone a little after its callbacks stop.
    const auto stalled = std::chrono::steady_clock::now();
    feedUntil(out, [&] { return out.status().deviceLost; }, 10s);
    say("waited for DeviceLost after the stall: " +
        std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - stalled)
                           .count()) +
        " ms");
    const auto lost = out.status();
    say(std::string("loss seen: deviceLost ") + (lost.deviceLost ? "1" : "0") + ", running " +
        (lost.running ? "1" : "0") + ", callbacks " + std::to_string(lost.callbacks));
    EXPECT_FALSE(out.clock().valid);
    // DirectSound keeps an unplugged device's stream active with no error:
    // there the loss is only the stall (HIKARI_TEST_AUDIO_HOTPLUG_STALL_ONLY).
    const char *stallOnly = std::getenv("HIKARI_TEST_AUDIO_HOTPLUG_STALL_ONLY");
    if (!stallOnly || !*stallOnly) {
        EXPECT_TRUE(lost.deviceLost) << "the owner reports the loss as DeviceLost";
        EXPECT_EQ(out.start().error(), application::OutputError::DeviceLost);
    }
    out.close();
    ASSERT_TRUE(waitFor([&] { return find(out).empty(); }, 30s)) << "the device is still listed";
    EXPECT_EQ(out.open(id, {48000, 2}).error(), application::OutputError::DeviceUnavailable);
    // Windows may name the returning device differently ("2- ..."): it is the
    // output of that host API that was not listed while it was gone.
    std::set<std::string> without;
    for (const auto &device : out.devices())
        without.insert(device.name);
    say("HOTPLUG-LOST");

    std::string back;
    const auto returned = [&] {
        for (const auto &device : out.devices())
            if (!without.contains(device.name) && (!api || !*api || device.hostApi == api))
                return device.id;
        return std::string();
    };
    const bool cameBack = waitFor([&] { return !(back = returned()).empty(); }, 120s);
    if (!cameBack)
        for (const auto &device : out.devices())
            say("listed: " + device.id);
    ASSERT_TRUE(cameBack) << "the device did not come back";
    say("returned as " + back);
    // Windows may need a moment after listing it before a stream opens.
    ASSERT_TRUE(waitFor([&] { return static_cast<bool>(out.open(back, {48000, 2})); }, 30s)) << back;
    ASSERT_TRUE(out.start());
    ASSERT_TRUE(feedUntil(out, [&] { return out.status().framesConsumed > 0 && out.clock().valid; }));
    say("HOTPLUG-RECOVERED " + back);
}

TEST_F(PortAudioDevice, RescanWaitsWhileAStreamIsOpen)
{
    ASSERT_TRUE(out.open(id, {48000, 2}));
    ASSERT_TRUE(out.start());
    PortAudioOutput other;
    EXPECT_FALSE(other.devices().empty()) << "enumerates without tearing down the open stream";
    ASSERT_TRUE(feedUntil(out, [&] { return out.status().framesConsumed > 0; }));
    EXPECT_FALSE(out.status().deviceLost);
}

} // namespace
} // namespace hikari::backends
