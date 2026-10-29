#pragma once

#include "directshow_frames.h"

#include "hikari/application/playback_pipeline.h"

#include <streams.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace hikari::backends::dshow {

struct Delivery {
    std::uint64_t serial = 0; // the seek the frame follows (Bridge::requestedSerial)
    std::int64_t startUs = 0;
    std::optional<std::int64_t> endUs;
    std::shared_ptr<application::IndexedFrame> frame; // from the bridge's pool
};

// Shared by the player (its thread) and the renderer (streaming threads).
struct Bridge {
    std::function<void(Delivery)> deliver; // posts to the player's thread (the newest only)
    std::shared_ptr<application::FramePool> pool = application::FramePool::create();
    std::function<void()> endOfStream;     // likewise
    std::atomic<bool> bt709{false};
    std::atomic<bool> ycbcr{false};
    std::atomic<std::uint64_t> requestedSerial{0};
};

struct VideoInf {
    int width = 1280;
    int height = 720;
    float fps = 23.976f;
    int aspectX = 16;
    int aspectY = 9;
    Subtype subtype = Subtype::RGB32;
    bool bottomUp = false;
};

class VideoRenderer : public CBaseVideoRenderer {
public:
    VideoRenderer(std::shared_ptr<Bridge> bridge, HRESULT *phr);

    HRESULT CheckMediaType(const CMediaType *pmt) override;
    HRESULT SetMediaType(const CMediaType *pmt) override;
    HRESULT DoRenderSample(IMediaSample *) override { return S_OK; }
    HRESULT Render(IMediaSample *sample) override;
    void OnReceiveFirstSample(IMediaSample *sample) override;
    HRESULT EndOfStream() override;
    HRESULT EndFlush() override;

    VideoInf info() const;
    bool connected() { return m_pInputPin && m_pInputPin->IsConnected(); }
    // A seek on a stopped graph flushes nothing, so its first frame follows it directly.
    void adoptSerial(std::uint64_t serial) { m_serial = serial; }

private:
    void deliver(IMediaSample *sample);

    std::shared_ptr<Bridge> m_bridge;
    mutable std::mutex m_infoMutex;
    VideoInf m_info;
    std::atomic<std::uint64_t> m_serial{0};
    // OnReceiveFirstSample's sample, which Render then gets again and skips.
    IMediaSample *m_shownFirst = nullptr;
    REFERENCE_TIME m_shownFirstStart = 0;
};

}
