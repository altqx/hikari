#pragma once

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

// Reuses released frames' buffers; a same-size resize neither allocates nor
// clears. Thread-safe.
class FramePool : public std::enable_shared_from_this<FramePool> {
public:
    static std::shared_ptr<FramePool> create(std::size_t keep = 4);
    std::shared_ptr<IndexedFrame> acquire();
    // Buffers handed out that were not reused.
    std::uint64_t fresh() const;

private:
    explicit FramePool(std::size_t keep) : m_keep(keep) {}
    void recycle(std::vector<std::byte> buffer);
    mutable std::mutex m_mutex;
    std::vector<std::vector<std::byte>> m_free;
    std::size_t m_keep;
    std::uint64_t m_fresh = 0;
};

// Non-zero alpha pixels; nullopt for none. OverlayFrame::bounds when given,
// which may include clear edges.
std::optional<PixelRect> overlayBounds(const OverlayFrame &overlay);

// Latest wins: only the newest waiting frame and result are kept. The
// renderer's render() must be safe on the worker while the owner prepares.
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
        std::optional<PixelRect> overlayBounds; // nullopt: nothing visible
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

    // No post: every submitted frame is made, rendered and delivered before
    // submit returns.
    PlaybackPipeline(SubtitleRendererPort &renderer, Post post, Deliver deliver);
    ~PlaybackPipeline(); // stops the worker; waiting frames and results are dropped
    PlaybackPipeline(const PlaybackPipeline &) = delete;
    PlaybackPipeline &operator=(const PlaybackPipeline &) = delete;

    void submit(Input input); // any thread
    // Call after each prepare (serial > 0); the owner re-renders results for an older serial.
    void setSubtitles(std::uint64_t serial, bool on);
    // Overlays at the picture's on-screen size in device pixels (nullopt: the
    // frame's size). Any thread.
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
