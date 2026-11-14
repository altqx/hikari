#include "frame_batch.h"

extern "C" {
#include <libavutil/pixfmt.h>
}

#include <algorithm>
#include <expected>
#include <memory>

namespace hikari::media {

namespace {

struct VideoDeleter {
    void operator()(FFMS_VideoSource *v) const { FFMS_DestroyVideoSource(v); }
};
using Video = std::unique_ptr<FFMS_VideoSource, VideoDeleter>;

constexpr std::size_t kMostAheadBytes = 64u << 20;
constexpr std::size_t kMostAheadFrames = 1024;

std::mutex &openingMutex()
{
    static std::mutex m;
    return m;
}

std::string errorText(const FFMS_ErrorInfo &e)
{
    return e.Buffer ? std::string(e.Buffer) : std::string("unknown FFMS2 error");
}

std::expected<Video, std::string> openDecoder(const BatchVideo &video)
{
    char buffer[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    Video source;
    {
        const std::lock_guard lock(openingMutex());
        source.reset(FFMS_CreateVideoSource(video.path.c_str(), video.track, video.index, 1, FFMS_SEEK_NORMAL, &err));
    }
    if (!source || !FFMS_GetFrame(source.get(), 0, &err))
        return std::unexpected(errorText(err));
    const int formats[] = {AV_PIX_FMT_BGRA, -1};
    if (FFMS_SetOutputFormatV2(source.get(), formats, video.width, video.height, FFMS_RESIZER_BICUBIC, &err) != 0)
        return std::unexpected(errorText(err));
    if (video.inputMatrix &&
        FFMS_SetInputFormatV(source.get(), video.inputMatrix->first, video.inputMatrix->second, FFMS_GetPixFmt(""), &err) != 0)
        return std::unexpected(errorText(err));
    return source;
}

}

FrameBatcher::FrameBatcher(BatchVideo video) : m_video(std::move(video)) {}

FrameBatcher::~FrameBatcher()
{
    {
        const std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    for (std::thread &t : m_workers)
        t.join();
}

std::size_t FrameBatcher::window() const
{
    const int edge = std::max(m_video.width, m_video.height);
    const std::size_t side = std::size_t(std::min(edge, m_key.maxEdge));
    const std::size_t bytes = std::max<std::size_t>(1, side * side * (m_key.luma ? 1 : 4));
    return std::clamp<std::size_t>(kMostAheadBytes / bytes, 1, kMostAheadFrames);
}

void FrameBatcher::restart(const Key &key, int from)
{
    ++m_generation;
    m_key = key;
    m_start = m_kept = m_next = from;
    m_queue.clear();
    m_done.clear();
    m_failed.clear();
}

void FrameBatcher::plan(int until)
{
    until = std::min(until, m_video.frames - 1);
    while (m_next <= until) {
        Segment segment{m_generation, {}};
        const int keyframe = m_video.keyframeOf[std::size_t(m_next)];
        for (; m_next <= until && m_video.keyframeOf[std::size_t(m_next)] == keyframe; m_next += m_key.stride)
            segment.frames.push_back(m_next);
        m_queue.push_back(std::move(segment));
    }
}

std::optional<std::string> FrameBatcher::serve(const BatchRequest &request,
                                               const std::function<void(int, const core::ShrunkFrame &)> &emit,
                                               const std::function<bool()> &cancelled)
{
    std::unique_lock lock(m_mutex);
    m_lastRequest = std::chrono::steady_clock::now();
    m_idle = std::chrono::milliseconds(std::max(0, request.idleMs));
    const Key key{request.stride, request.maxEdge, request.luma};
    const bool continues = key == m_key && request.from >= m_kept && request.from <= m_next &&
                           (request.from - m_start) % request.stride == 0;
    if (!continues)
        restart(key, request.from);
    m_kept = request.from;
    std::erase_if(m_done, [&](const auto &f) { return f.first < request.from; });
    std::erase_if(m_failed, [&](const auto &f) { return f.first < request.from; });
    plan(request.to + int(window()) * request.stride);
    while (int(m_workers.size()) < std::clamp(request.decoders, 1, 16))
        m_workers.emplace_back([this] { work(); });
    m_wake.notify_all();

    for (int frame = request.from; frame <= request.to; frame += request.stride) {
        while (!m_done.contains(frame) && !m_failed.contains(frame) && !cancelled())
            m_ready.wait_for(lock, std::chrono::milliseconds(100));
        if (const auto failed = m_failed.find(frame); failed != m_failed.end())
            return "frame " + std::to_string(frame) + ": " + failed->second;
        const auto done = m_done.find(frame);
        if (done == m_done.end())
            return "cancelled";
        core::ShrunkFrame shrunk = std::move(done->second);
        m_done.erase(done);
        m_kept = frame + request.stride;
        m_lastRequest = std::chrono::steady_clock::now();
        lock.unlock();
        emit(frame, shrunk);
        lock.lock();
    }
    m_lastRequest = std::chrono::steady_clock::now();
    return std::nullopt;
}

void FrameBatcher::setInputMatrix(int colorSpace, int colorRange)
{
    const std::lock_guard lock(m_mutex);
    m_video.inputMatrix = std::pair{colorSpace, colorRange};
    ++m_matrixGeneration;
    restart(Key{}, 0);
}

int FrameBatcher::openDecoders()
{
    const std::lock_guard lock(m_mutex);
    return m_open;
}

void FrameBatcher::work()
{
    Video source;
    std::uint64_t matrix = 0;
    std::unique_lock lock(m_mutex);
    while (!m_stop) {
        if (m_queue.empty()) {
            if (!source) {
                m_wake.wait(lock, [&] { return m_stop || !m_queue.empty(); });
            } else if (!m_wake.wait_until(lock, m_lastRequest + m_idle, [&] { return m_stop || !m_queue.empty(); }) &&
                       std::chrono::steady_clock::now() >= m_lastRequest + m_idle) {
                source.reset();
                --m_open;
            }
            continue;
        }
        Segment segment = std::move(m_queue.front());
        m_queue.pop_front();
        const Key key = m_key;
        const BatchVideo video = m_video;
        const bool stale = source && matrix != m_matrixGeneration;
        lock.unlock();

        if (stale) {
            source.reset();
            lock.lock();
            --m_open;
            lock.unlock();
        }
        std::string failure;
        if (!source) {
            auto opened = openDecoder(video);
            lock.lock();
            if (opened) {
                source = std::move(*opened);
                matrix = m_matrixGeneration;
                ++m_open;
            } else {
                failure = opened.error();
            }
            lock.unlock();
        }
        char buffer[1024];
        FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
        for (const int frame : segment.frames) {
            std::optional<core::ShrunkFrame> shrunk;
            std::string error = failure;
            if (source) {
                if (const FFMS_Frame *f = FFMS_GetFrame(source.get(), frame, &err)) {
                    const int width = f->ScaledWidth > 0 ? f->ScaledWidth : f->EncodedWidth;
                    const int height = f->ScaledHeight > 0 ? f->ScaledHeight : f->EncodedHeight;
                    shrunk = core::shrinkFrame(reinterpret_cast<const std::byte *>(f->Data[0]), width, height,
                                               std::size_t(f->Linesize[0]), key.maxEdge, key.luma);
                } else {
                    error = errorText(err);
                }
            }
            lock.lock();
            const bool current = segment.generation == m_generation;
            if (current && shrunk)
                m_done.emplace(frame, std::move(*shrunk));
            else if (current)
                m_failed.emplace(frame, error);
            lock.unlock();
            m_ready.notify_all();
            if (!current)
                break;
        }
        lock.lock();
    }
    if (source)
        --m_open;
}

}
