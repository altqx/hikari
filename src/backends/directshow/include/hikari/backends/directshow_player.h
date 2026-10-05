#pragma once

// W1: legacy's DirectShow player (DShowPlayer, dshowplayer.cpp, with its
// video renderer CD2DVideoRender, DshowRenderer.cpp) as an optional Windows
// adapter of the general player port. It is chosen explicitly
// (video.playbackPlayer), never as a fallback, and Linux builds do not
// contain it.
//
// The graph is legacy's: the file's source filter, the HikariSub video
// renderer and a DirectSound renderer, the source's pins connected by their
// major type (a byte stream through the splitter the graph inserts, whose
// first audio pin then goes to DirectSound). The graph's other filters
// (splitters, decoders) are whatever the system has registered; none ships
// with the adapter. Each video sample the renderer receives is a delivered
// frame: converted to BGRA (legacy's DXVA conversion: limited range in, the
// script's matrix, BT.601 unless "TV.709"), it acknowledges a seek and
// anchors the clock. Callbacks and frames arrive through `post`, on the
// thread that owns the player.

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
    // Runs a task on the owner's thread (a queued call).
    using Post = std::function<void(std::function<void()>)>;
    // A delivered frame (index -1, BGRA, top-down) and its start time.
    using FrameSink = std::function<void(application::IndexedFrame frame, std::int64_t startUs)>;

    // A graph filter as legacy's Filters menu lists it (EnumFilters): its
    // name, enabled when it has property pages.
    struct Filter {
        std::string name; // UTF-8
        bool hasPropertyPages = false;
    };
    // The renderer's negotiated format (legacy VideoInf, SetMediaType).
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
    // The playback or media status changed.
    void setStateObserver(std::function<void()> changed);
    // An open failed: legacy's log line for the step that failed (HR's
    // HikariLogSilent), e.g. "Source filter not added".
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
    // Legacy SetVolume(-(pos * pos)): IBasicAudio::put_Volume in hundredths
    // of a decibel, so the linear gain becomes 2000 * log10(gain).
    void setVolume(double linear) override;

    // Legacy RendererDirectShow::SetColorSpace: "TV.709" converts with
    // BT.709, anything else with BT.601.
    void setColorSpace(const std::string &matrix);
    // Legacy EnumFilters (the graph in enumeration order) and FilterConfig:
    // the named filter's property pages in a modal property frame owned by
    // `ownerWindow` (an HWND) at (x, y) in its client area. False without
    // a graph, such a filter or property pages.
    std::vector<Filter> filters() const;
    bool showFilterProperties(const std::string &name, std::uintptr_t ownerWindow, int x, int y);

    VideoFormat videoFormat() const;
    std::uint64_t deliveredFrames() const;
    // Legacy GetStreams(): IAMStreamSelect's names, each with " 1" when
    // enabled or " 0" (empty without the interface).
    std::vector<std::string> streams() const;
    // The volume last given to IBasicAudio (hundredths of a dB), if any.
    long appliedVolume() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace hikari::backends
