#pragma once

#include "hikari/core/frame_shrink.h"

#include <ffms.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace hikari::media {

// What a FrameBatch decoder copies from the open video so its frames convert as Frame's do.
struct BatchVideo {
    std::string path;
    // MUST KILL: borrowed from the helper's Source, which must outlive the batcher
    FFMS_Index *index = nullptr;
    int track = 0;
    int width = 0, height = 0;
    int frames = 0;
    std::vector<int> keyframeOf;
    std::optional<std::pair<int, int>> inputMatrix;
};

struct BatchRequest {
    int from = 0, to = 0, stride = 1, maxEdge = 64;
    bool luma = true;
    int decoders = 1;
    int idleMs = 10000;
};

// Decodes frames for FrameBatch on worker threads, one FFMS2 source and one thread each, whole
// keyframe-to-keyframe segments at a time, and reads ahead of the last request.
class FrameBatcher {
public:
    explicit FrameBatcher(BatchVideo video);
    ~FrameBatcher();
    FrameBatcher(const FrameBatcher &) = delete;
    FrameBatcher &operator=(const FrameBatcher &) = delete;

    // Emits each frame of the request in order; the error text when a frame fails or it was cancelled.
    std::optional<std::string> serve(const BatchRequest &request,
                                     const std::function<void(int, const core::ShrunkFrame &)> &emit,
                                     const std::function<bool()> &cancelled);
    void setInputMatrix(int colorSpace, int colorRange);
    int openDecoders();

private:
    struct Segment {
        std::uint64_t generation = 0;
        std::vector<int> frames;
    };
    struct Key {
        int stride = 0, maxEdge = 0;
        bool luma = false;
        bool operator==(const Key &) const = default;
    };

    void work();
    void restart(const Key &key, int from);
    void plan(int until);
    std::size_t window() const;

    BatchVideo m_video;
    std::mutex m_mutex;
    std::condition_variable m_wake, m_ready;
    std::vector<std::thread> m_workers;
    bool m_stop = false;
    std::uint64_t m_generation = 0, m_matrixGeneration = 0;
    Key m_key;
    int m_start = 0, m_kept = 0, m_next = 0;
    std::deque<Segment> m_queue;
    std::map<int, core::ShrunkFrame> m_done;
    std::map<int, std::string> m_failed;
    std::chrono::milliseconds m_idle{10000};
    std::chrono::steady_clock::time_point m_lastRequest;
    int m_open = 0;
};

}
