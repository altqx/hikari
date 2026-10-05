#include "directshow_renderer.h"

#include <dvdmedia.h>

#include <cstdlib>

namespace hikari::backends::dshow {

namespace {

// Legacy CLSID_KVideoRenderer.
const GUID kRendererClsid = {0x269ba141, 0x1fde, 0x494b, {0x91, 0x24, 0x45, 0x3a, 0x17, 0x83, 0x8b, 0x9f}};

} // namespace

VideoRenderer::VideoRenderer(std::shared_ptr<Bridge> bridge, HRESULT *phr)
    : CBaseVideoRenderer(kRendererClsid, L"video Renderer", nullptr, phr), m_bridge(std::move(bridge))
{
}

HRESULT VideoRenderer::CheckMediaType(const CMediaType *pmt)
{
    // Legacy CD2DVideoRender::CheckMediaType.
    CheckPointer(pmt, E_POINTER);
    if (pmt->majortype != MEDIATYPE_Video)
        return E_FAIL;
    const GUID *subtype = pmt->Subtype();
    if (subtype == nullptr)
        return E_FAIL;
    if (*subtype != MEDIASUBTYPE_YV12 && *subtype != MEDIASUBTYPE_NV12 && *subtype != MEDIASUBTYPE_YUY2 &&
        *subtype != MEDIASUBTYPE_RGB32)
        return E_FAIL;
    if (pmt->formattype != FORMAT_VideoInfo && pmt->formattype != FORMAT_VideoInfo2)
        return E_FAIL;
    return S_OK;
}

HRESULT VideoRenderer::SetMediaType(const CMediaType *pmt)
{
    // Legacy CD2DVideoRender::SetMediaType.
    CheckPointer(pmt, E_POINTER);
    CAutoLock lock(m_pLock);
    VideoInf info;
    double timePerFrame = 0;
    LONG height = 0;
    if (pmt->formattype == FORMAT_VideoInfo) {
        const auto *vih = reinterpret_cast<const VIDEOINFOHEADER *>(pmt->pbFormat);
        if (vih == nullptr || pmt->cbFormat < sizeof(VIDEOINFOHEADER) || vih->bmiHeader.biSize != sizeof(BITMAPINFOHEADER))
            return E_INVALIDARG;
        timePerFrame = static_cast<double>(vih->AvgTimePerFrame);
        info.width = vih->bmiHeader.biWidth;
        height = vih->bmiHeader.biHeight;
        info.aspectX = info.width;
        info.aspectY = static_cast<int>(height);
    } else if (pmt->formattype == FORMAT_VideoInfo2) {
        const auto *vih = reinterpret_cast<const VIDEOINFOHEADER2 *>(pmt->pbFormat);
        if (vih == nullptr || pmt->cbFormat < sizeof(VIDEOINFOHEADER2) || vih->bmiHeader.biSize != sizeof(BITMAPINFOHEADER))
            return E_INVALIDARG;
        timePerFrame = static_cast<double>(vih->AvgTimePerFrame);
        info.width = vih->bmiHeader.biWidth;
        height = vih->bmiHeader.biHeight;
        info.aspectX = static_cast<int>(vih->dwPictAspectRatioX);
        info.aspectY = static_cast<int>(vih->dwPictAspectRatioY);
    } else {
        return E_INVALIDARG;
    }
    const GUID subtype = *pmt->Subtype();
    if (subtype == MEDIASUBTYPE_YV12)
        info.subtype = Subtype::YV12;
    else if (subtype == MEDIASUBTYPE_NV12)
        info.subtype = Subtype::NV12;
    else if (subtype == MEDIASUBTYPE_YUY2)
        info.subtype = Subtype::YUY2;
    else
        info.subtype = Subtype::RGB32;
    // A positive height is a bottom-up DIB for RGB (legacy m_SwapFrame).
    info.bottomUp = info.subtype == Subtype::RGB32 && height > 0;
    info.height = std::abs(static_cast<int>(height));
    if (timePerFrame < 1)
        timePerFrame = 417083;
    info.fps = static_cast<float>(10000000.0 / timePerFrame);
    std::lock_guard guard(m_infoMutex);
    m_info = info;
    return S_OK;
}

VideoInf VideoRenderer::info() const
{
    std::lock_guard guard(m_infoMutex);
    return m_info;
}

void VideoRenderer::deliver(IMediaSample *sample)
{
    BYTE *buffer = nullptr;
    if (FAILED(sample->GetPointer(&buffer)) || !buffer)
        return;
    REFERENCE_TIME start = 0, end = 0;
    const HRESULT timed = sample->GetTime(&start, &end);
    // A format the decoder changed with this sample (a new stride or size).
    AM_MEDIA_TYPE *changed = nullptr;
    if (sample->GetMediaType(&changed) == S_OK && changed) {
        CMediaType type(*changed);
        SetMediaType(&type);
        DeleteMediaType(changed);
    }
    const VideoInf info = this->info();
    SampleFormat format;
    format.subtype = info.subtype;
    format.width = info.width % 2 ? info.width + 1 : info.width; // legacy m_Width++
    format.height = info.height;
    format.bottomUp = info.bottomUp;
    Delivery d;
    d.serial = m_serial;
    if (!toBgra(format, buffer, static_cast<std::size_t>(sample->GetActualDataLength()), m_bridge->bt709,
                d.frame))
        return;
    d.frame.index = -1;
    const REFERENCE_TIME segment = m_pInputPin ? m_pInputPin->CurrentStartTime() : 0;
    if (SUCCEEDED(timed)) {
        d.startUs = (segment + start) / 10;
        d.frame.pts = d.startUs;
        if (timed == S_OK && end > start)
            d.endUs = (segment + end) / 10;
    } else {
        d.startUs = segment / 10;
    }
    if (m_bridge->deliver)
        m_bridge->deliver(std::move(d));
}

void VideoRenderer::OnReceiveFirstSample(IMediaSample *sample)
{
    // Legacy: the paused graph's first sample is drawn now; Render skips it.
    if (!sample || sample->IsPreroll() == S_OK)
        return;
    REFERENCE_TIME start = 0, end = 0;
    sample->GetTime(&start, &end);
    m_shownFirst = sample;
    m_shownFirstStart = start;
    deliver(sample);
}

HRESULT VideoRenderer::Render(IMediaSample *sample)
{
    // Legacy CD2DVideoRender::Render.
    if (!sample || m_bStreaming == FALSE)
        return E_POINTER;
    if (sample->IsPreroll() == S_OK)
        return S_OK;
    REFERENCE_TIME start = 0, end = 0;
    sample->GetTime(&start, &end);
    if (sample == m_shownFirst && start == m_shownFirstStart) {
        m_shownFirst = nullptr;
        return S_OK;
    }
    m_shownFirst = nullptr;
    deliver(sample);
    return S_OK;
}

HRESULT VideoRenderer::EndOfStream()
{
    const HRESULT hr = CBaseRenderer::EndOfStream();
    if (m_bridge->endOfStream)
        m_bridge->endOfStream();
    return hr;
}

HRESULT VideoRenderer::EndFlush()
{
    // Samples after a flush follow the latest seek; any rendered before it
    // carry the earlier serial.
    m_serial = m_bridge->requestedSerial.load();
    m_shownFirst = nullptr;
    return CBaseVideoRenderer::EndFlush();
}

} // namespace hikari::backends::dshow
