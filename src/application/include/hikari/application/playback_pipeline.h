#pragma once

// V1 playback frames off the owner's (GUI) thread (docs/media.md, video
// pipeline). A player hands each delivered frame over from any thread; only
// the newest one waits, and a newer one replaces it (latest wins), so a slow
// moment never leaves a backlog that the video then falls behind on. A worker
// takes the waiting frame, makes its BGRA (into pooled buffers: no fresh
// allocation or zero fill per frame), renders its subtitle overlay at its
// start time, and posts the result to the owner's thread, where again only
// the newest result waits. With no input waiting the worker renders the
// overlay of the next frame ahead of time, once, at the millisecond it
// guesses the frame starts in (renderers draw to the millisecond,
// subtitle_render.h): one frame on at the constant pace the starts so far
// allow (exactly, even when a millisecond timebase rounds every start; the
// first frame's guess is its end). While half or more of the last 16
// guesses missed (a variable frame rate) it renders nothing ahead, so
// guessing costs at most one render a frame and usually none.
//
// The renderer port's render() must be safe to call from the worker while
// the owner prepares new subtitles (SubtitleRendererPort's adapters serialize
// their native renderer). Overlays carry the subtitles serial they were
// rendered for: setSubtitles is called after each prepare, and a result for
// an older serial is rendered again by the owner.

#include "hikari/application/indexed_source.h"
#include "hikari/application/subtitle_render.h"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace hikari::application {

// Frames whose pixel buffers are reused: a frame taken from the pool gets the
// buffer of a released one when there is one (resizing it to the same size
// neither allocates nor clears), and gives its buffer back when its last
// reference goes. Thread-safe.
class FramePool : public std::enable_shared_from_this<FramePool> {
public:
    static std::shared_ptr<FramePool> create(std::size_t keep = 4);
    std::shared_ptr<IndexedFrame> acquire();
    // Buffers handed out that were not reused ones (tests, benchmarks).
    std::uint64_t fresh() const;

private:
    explicit FramePool(std::size_t keep) : m_keep(keep) {}
    void recycle(std::vector<std::byte> buffer);
    mutable std::mutex m_mutex;
    std::vector<std::vector<std::byte>> m_free;
    std::size_t m_keep;
    std::uint64_t m_fresh = 0;
};

// The bounds of an overlay's visible (non-zero alpha) pixels; nullopt when
// there are none. The renderer's OverlayFrame::bounds when it gave them
// (they may also hold clear pixels at their edges), else a scan.
std::optional<PixelRect> overlayBounds(const OverlayFrame &overlay);

class PlaybackPipeline {
public:
    // Runs a task on the owner's thread.
    using Post = std::function<void(std::function<void()>)>;
    // The frame's BGRA, made on the worker (nullptr: nothing to show).
    using Make = std::function<std::shared_ptr<IndexedFrame>(FramePool &)>;
    struct Input {
        std::uint64_t epoch = 0; // the owner's, given back with the result
        std::int64_t startUs = 0;
        std::optional<std::int64_t> endUs;
        Make make;
    };
    struct Output {
        std::uint64_t epoch = 0;
        std::int64_t startUs = 0;
        std::shared_ptr<const IndexedFrame> frame;
        // Rendered for `subtitles` (0: none was rendered); null when that
        // serial had no subtitles or the render failed.
        std::shared_ptr<const OverlayFrame> overlay;
        std::uint64_t subtitles = 0;
        std::optional<PixelRect> overlayBounds; // with an overlay: its visible pixels (none: nothing visible)
    };
    using Deliver = std::function<void(Output)>;
    struct Counters {
        std::uint64_t submitted = 0;
        std::uint64_t superseded = 0; // replaced while waiting for the worker
        std::uint64_t delivered = 0;
        std::uint64_t replacedResults = 0; // a newer result replaced one the owner had not taken
        std::uint64_t aheadPlanned = 0;    // overlays asked to be rendered ahead
        std::uint64_t aheadRendered = 0;   // ... and rendered (the others: a frame came first)
        std::uint64_t aheadUsed = 0;       // overlays rendered ahead that a frame then used
        std::uint64_t aheadUnused = 0;     // overlays rendered ahead that no frame used
        std::uint64_t aheadWithheld = 0;   // frames after which nothing was rendered ahead (unreliable pace)
    };

    // No post: inline, every submitted frame is made, rendered and delivered
    // before submit returns (tests).
    PlaybackPipeline(SubtitleRendererPort &renderer, Post post, Deliver deliver);
    ~PlaybackPipeline(); // stops the worker; waiting frames and results are dropped
    PlaybackPipeline(const PlaybackPipeline &) = delete;
    PlaybackPipeline &operator=(const PlaybackPipeline &) = delete;

    void submit(Input input); // any thread
    // The owner's subtitles after a prepare (serial > 0), or none.
    void setSubtitles(std::uint64_t serial, bool on);
    // SV1-screen-size-subtitles: overlays drawn at the picture's size on
    // screen, in device pixels (none: at the frame's size). Any thread.
    void setScreenSize(std::optional<std::pair<int, int>> size);
    Counters counters() const;
    FramePool &pool() { return *m_pool; }

private:
    struct State;
    Output process(Input input);
    void run();
    SubtitleRendererPort &m_renderer;
    std::shared_ptr<FramePool> m_pool = FramePool::create();
    std::shared_ptr<State> m_state;
    std::thread m_worker;
};

} // namespace hikari::application
