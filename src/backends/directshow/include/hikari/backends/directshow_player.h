#pragma once

// Callbacks and frames arrive through `post`, on the thread that owns the player.

#include "hikari/application/general_player.h"
#include "hikari/application/indexed_source.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hikari::backends {

class DirectShowPlayer final : public application::GeneralPlayerPort {
public:
    // Runs a task on the owner's thread.
    using Post = std::function<void(std::function<void()>)>;
    // A frame (index -1, BGRA, top-down; its buffer is pooled) and its start time. A newer
    // delivery replaces one still waiting for the owner's thread.
    using FrameSink = std::function<void(std::shared_ptr<application::IndexedFrame> frame, std::int64_t startUs)>;

    struct Filter {
        std::string name; // UTF-8
        bool hasPropertyPages = false;
    };
    struct VideoFormat {
        int width = 0, height = 0;
        double fps = 0;
        int aspectX = 0, aspectY = 0;
        std::string subtype; // "YV12", "NV12", "YUY2" or "RGB32"
    };

    explicit DirectShowPlayer(Post post);
    ~DirectShowPlayer() override;
    DirectShowPlayer(const DirectShowPlayer &) = delete;
    DirectShowPlayer &operator=(const DirectShowPlayer &) = delete;

    void setFrameSink(FrameSink sink);
    void setStateObserver(std::function<void()> changed);
    // `message` names the step that failed, e.g. "Source filter not added".
    void setOpenFailed(std::function<void(const std::string &message)> failed);

    void open(const std::string &path, Opened done) override;
    void seek(std::int64_t us, Seeked done) override;
    void play() override;
    void pause() override;
    void stop() override;
    bool selectAudioTrack(int index) override;
    bool selectSubtitleTrack(int index) override;
    application::PlaybackState playbackState() const override;
    application::MediaStatus mediaStatus() const override;
    double bufferProgress() const override;
    application::PlayerClock clock() const override;
    application::MediaDescription description() const override;
    std::uint64_t generation() const override;
    // IBasicAudio::put_Volume takes hundredths of a decibel: 2000 * log10(linear).
    void setVolume(double linear) override;

    // "TV.709" converts with BT.709, anything else with BT.601.
    void setColorSpace(const std::string &matrix);
    // Delivers YV12 and NV12 as IndexedFrame::ycbcr planes instead of BGRA. Any thread;
    // the next sample follows it.
    void setYcbcrFrames(bool planes);
    // In the graph's enumeration order.
    std::vector<Filter> filters() const;
    // Opens the filter's property pages, modal, over `ownerWindow` (an HWND) at (x, y) in its
    // client area. False without a graph, such a filter or property pages.
    bool showFilterProperties(const std::string &name, std::uintptr_t ownerWindow, int x, int y);

    VideoFormat videoFormat() const;
    std::uint64_t deliveredFrames() const;
    // IAMStreamSelect's names, each with " 1" when enabled or " 0"; empty without it.
    std::vector<std::string> streams() const;
    // The volume last given to IBasicAudio (hundredths of a dB), if any.
    long appliedVolume() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace hikari::backends
