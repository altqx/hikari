#pragma once

// W1: legacy's video renderer filter (CD2DVideoRender, DshowRenderer.cpp)
// on the DirectShow base classes. It accepts legacy's formats (YV12, NV12,
// YUY2, RGB32 in VIDEOINFOHEADER or VIDEOINFOHEADER2), records the format as
// legacy's SetMediaType did, and hands every sample it shows to the player as
// a BGRA frame: the paused state's first sample (OnReceiveFirstSample) and
// each sample the clock releases (Render), once each, preroll samples skipped
// as legacy's Render skipped them. A sample's time is the segment's start
// plus its own (legacy added the sample time to the position it sought).

#include "directshow_frames.h"

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
    application::IndexedFrame frame;
};

// Shared by the player (its thread) and the renderer (streaming threads).
struct Bridge {
    std::function<void(Delivery)> deliver; // posts to the player's thread
    std::function<void()> endOfStream;     // likewise
    std::atomic<bool> bt709{false};
    std::atomic<std::uint64_t> requestedSerial{0};
};

// Legacy VideoInf with its defaults (CD2DVideoRender's constructor).
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
    // A seek while the graph is stopped flushes nothing: its first frame
    // follows that seek directly.
    void adoptSerial(std::uint64_t serial) { m_serial = serial; }

private:
    void deliver(IMediaSample *sample);

    std::shared_ptr<Bridge> m_bridge;
    mutable std::mutex m_infoMutex;
    VideoInf m_info;
    std::atomic<std::uint64_t> m_serial{0};
    // The sample OnReceiveFirstSample showed: Render then skips it (legacy norender).
    IMediaSample *m_shownFirst = nullptr;
    REFERENCE_TIME m_shownFirstStart = 0;
};

} // namespace hikari::backends::dshow
