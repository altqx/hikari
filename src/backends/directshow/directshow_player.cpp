#include "hikari/backends/directshow_player.h"

#include "directshow_renderer.h"

#include <dvdmedia.h>
#include <olectl.h>
#include <wrl/client.h>

#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

namespace hikari::backends {

using application::MediaDescription;
using application::MediaStatus;
using application::PlaybackState;
using application::PlayerClock;
using application::PlayerError;
using application::PlayerTrack;
using application::SeekResult;
using Microsoft::WRL::ComPtr;

namespace {

double monotonicSeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string utf8(const wchar_t *text)
{
    if (!text || !*text)
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1)
        return {};
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring wide(const std::string &text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

// ISO 639-2 of a stream's LCID (IAMStreamSelect::Info), empty when unknown.
std::string languageOf(LCID lcid)
{
    if (lcid == 0)
        return {};
    wchar_t name[16] = {};
    if (GetLocaleInfoW(lcid, LOCALE_SISO639LANGNAME2, name, 16) <= 0)
        return {};
    return utf8(name);
}

// MEDIATYPE_Subtitle (not in every SDK's uuids.h).
const GUID kMediaTypeSubtitle = {0xE487EB08, 0x6B26, 0x4BE9, {0x9D, 0xD3, 0x99, 0x34, 0x34, 0xD3, 0x13, 0xFD}};

bool isSubtitleType(const GUID &major)
{
    return major == kMediaTypeSubtitle || major == MEDIATYPE_Text;
}

} // namespace

struct DirectShowPlayer::Impl {
    enum class State { None, Stopped, Playing, Paused }; // legacy m_state

    Post post;
    FrameSink sink;
    std::function<void()> observer;
    std::function<void(const std::string &)> openFailed;
    bool comInitialized = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::shared_ptr<dshow::Bridge> bridge = std::make_shared<dshow::Bridge>();

    ComPtr<IGraphBuilder> graph;
    ComPtr<IMediaControl> control;
    ComPtr<IMediaSeeking> seeking;
    ComPtr<IBasicAudio> audio;
    ComPtr<IAMStreamSelect> streamSelect;
    ComPtr<IBaseFilter> rendererFilter;
    dshow::VideoRenderer *renderer = nullptr; // owned through rendererFilter
    bool audioConnected = false;

    State state = State::None;
    MediaStatus status = MediaStatus::NoMedia;
    std::uint64_t generation = 0;
    std::uint64_t epoch = 0;
    std::uint64_t serial = 0;
    std::uint64_t delivered = 0;
    PlayerClock clockState;
    struct PendingSeek {
        std::uint64_t generation;
        std::uint64_t serial;
        std::int64_t requestedUs;
        Seeked done;
    };
    std::optional<PendingSeek> pendingSeek;
    double volume = 1.0;
    long appliedVolume = 0;
    bool volumeApplied = false;
    std::string colorSpace;

    void changed()
    {
        if (observer)
            observer();
    }
    void tearDown();
    bool build(const std::string &path, std::string &error);
    void onDelivery(dshow::Delivery d);
    void applyVolume();
    MediaDescription describe() const;
    // IAMStreamSelect's streams of one major type: their stream indices.
    std::vector<long> streamsOf(bool audioType, bool subtitleType) const;
};

void DirectShowPlayer::Impl::tearDown()
{
    // Legacy TearDownGraph.
    if (control && state != State::Stopped)
        control->Stop();
    streamSelect.Reset();
    control.Reset();
    audio.Reset();
    seeking.Reset();
    rendererFilter.Reset();
    renderer = nullptr;
    graph.Reset();
    audioConnected = false;
    state = State::None;
}

bool DirectShowPlayer::Impl::build(const std::string &path, std::string &error)
{
    // Legacy DShowPlayer::InitializeGraph and OpenFile; each failing step
    // reports legacy's message (HR / PTR) and fails the open.
#define W1_HR(what, message)                                                                                          \
    if (FAILED(what)) {                                                                                                \
        error = message;                                                                                               \
        return false;                                                                                                  \
    }
    tearDown();
    W1_HR(CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph)),
          "Cannot create filters interface");
    W1_HR(graph.As(&control), "Cannot create controller");
    W1_HR(graph.As(&seeking), "Cannot create search interface");
    W1_HR(graph.As(&audio), "Cannot create audio interface");

    ComPtr<IBaseFilter> source;
    const std::wstring file = wide(path);
    W1_HR(graph->AddSourceFilter(file.c_str(), L"Source Filter", &source), "Source filter not added");

    HRESULT hr = S_OK;
    auto *videoRenderer = new dshow::VideoRenderer(bridge, &hr);
    videoRenderer->QueryInterface(IID_PPV_ARGS(&rendererFilter)); // the filter now holds the only reference
    if (FAILED(hr) || !rendererFilter) {
        error = "Cannot add video renderer";
        return false;
    }
    renderer = videoRenderer;
    W1_HR(graph->AddFilter(rendererFilter.Get(), L"HikariSub video Renderer"), "Cannot add video renderer");

    ComPtr<IBaseFilter> audioRenderer;
    W1_HR(CoCreateInstance(CLSID_DSoundRender, nullptr, CLSCTX_INPROC, IID_PPV_ARGS(&audioRenderer)),
          "Cannot create audio renderer instance");
    W1_HR(graph->AddFilter(audioRenderer.Get(), L"Direct Sound Renderer"), "Cannot add Direct Sound renderer");

    // Legacy's non-vobsub branch: connect the source's pins by major type.
    ComPtr<IEnumPins> sourcePins, rendererPins, audioPins;
    W1_HR(source->EnumPins(&sourcePins), "Cannot enumerate source pins");
    W1_HR(rendererFilter->EnumPins(&rendererPins), "Cannot enumerate renderer pins");
    W1_HR(audioRenderer->EnumPins(&audioPins), "Cannot enumerate Direct Sound pins");
    ComPtr<IPin> rendererPin, audioPin;
    if (rendererPins->Next(1, &rendererPin, nullptr) != S_OK) {
        error = "Cannot get renderer pin";
        return false;
    }
    if (audioPins->Next(1, &audioPin, nullptr) != S_OK) {
        error = "Cannot get dsound pin";
        return false;
    }
    bool hasStream = false;
    ComPtr<IPin> sourcePin;
    while (sourcePins->Next(1, &sourcePin, nullptr) == S_OK) {
        ComPtr<IEnumMediaTypes> types;
        W1_HR(sourcePin->EnumMediaTypes(&types), "No IMediaTypes");
        // Legacy HR failed only on an error: a pin without types (S_FALSE)
        // went on with the previous pin's type, which here means skipping it.
        AM_MEDIA_TYPE *info = nullptr;
        W1_HR(types->Next(1, &info, nullptr), "No track type info");
        if (!info) {
            sourcePin.Reset();
            continue;
        }
        const GUID major = info->majortype;
        DeleteMediaType(info);
        if (major == MEDIATYPE_Video) {
            W1_HR(graph->Connect(sourcePin.Get(), rendererPin.Get()), "Cannot connect source pin to video renderer");
        } else if (major == MEDIATYPE_Audio) {
            W1_HR(graph->Connect(sourcePin.Get(), audioPin.Get()), "Cannot connect source pin to audio renderer");
            audioConnected = true;
        } else if (major == MEDIATYPE_Stream) {
            // A byte stream: the graph inserts the splitter (and decoders) to
            // reach the video renderer; the splitter's first audio pin goes to
            // DirectSound, and it gives the stream selection.
            W1_HR(graph->Connect(sourcePin.Get(), rendererPin.Get()),
                  "Cannot connect source pin to audio1 renderer");
            ComPtr<IPin> splitterInput;
            W1_HR(sourcePin->ConnectedTo(&splitterInput), "Cannot find connected source pin");
            PIN_INFO pinInfo{};
            W1_HR(splitterInput->QueryPinInfo(&pinInfo), "Cannot get splitter pin info");
            ComPtr<IBaseFilter> splitter;
            splitter.Attach(pinInfo.pFilter);
            ComPtr<IEnumPins> splitterPins;
            W1_HR(splitter->EnumPins(&splitterPins), "Cannot enumerate splitter pins");
            ComPtr<IPin> splitterPin;
            while (splitterPins->Next(1, &splitterPin, nullptr) == S_OK) {
                ComPtr<IEnumMediaTypes> splitterTypes;
                W1_HR(splitterPin->EnumMediaTypes(&splitterTypes), "No IMediaTypes");
                // The splitter's input pin lists no types (S_FALSE): skipped,
                // as legacy's HR let it through with the previous type.
                AM_MEDIA_TYPE *splitterInfo = nullptr;
                W1_HR(splitterTypes->Next(1, &splitterInfo, nullptr), "No track type info");
                if (!splitterInfo) {
                    splitterPin.Reset();
                    continue;
                }
                const bool isAudio = splitterInfo->majortype == MEDIATYPE_Audio;
                DeleteMediaType(splitterInfo);
                if (isAudio) {
                    W1_HR(graph->Connect(splitterPin.Get(), audioPin.Get()),
                          "Cannot connect source pin to video2 renderer");
                    audioConnected = true;
                    break;
                }
                splitterPin.Reset();
            }
            splitter.As(&streamSelect);
            hasStream = true;
            break;
        }
        sourcePin.Reset();
    }
    seeking->SetTimeFormat(&TIME_FORMAT_MEDIA_TIME);
    state = State::Stopped;
    if (!hasStream && FAILED(source.As(&streamSelect))) {
        // The splitter behind the source's first pin (legacy: no message,
        // it "will spam on avi/wmv").
        ComPtr<IEnumPins> pins;
        ComPtr<IPin> first, connected;
        if (SUCCEEDED(source->EnumPins(&pins)) && pins->Next(1, &first, nullptr) == S_OK &&
            SUCCEEDED(first->ConnectedTo(&connected))) {
            PIN_INFO pinInfo{};
            if (SUCCEEDED(connected->QueryPinInfo(&pinInfo)) && pinInfo.pFilter) {
                ComPtr<IBaseFilter> splitter;
                splitter.Attach(pinInfo.pFilter);
                splitter.As(&streamSelect);
            }
        }
    }
#undef W1_HR
    return true;
}

std::vector<long> DirectShowPlayer::Impl::streamsOf(bool audioType, bool subtitleType) const
{
    std::vector<long> out;
    if (!streamSelect)
        return out;
    DWORD count = 0;
    if (FAILED(streamSelect->Count(&count)))
        return out;
    for (DWORD i = 0; i < count; ++i) {
        AM_MEDIA_TYPE *type = nullptr;
        if (FAILED(streamSelect->Info(static_cast<long>(i), &type, nullptr, nullptr, nullptr, nullptr, nullptr,
                                      nullptr)) ||
            !type)
            continue;
        const bool audio = type->majortype == MEDIATYPE_Audio;
        const bool subtitle = isSubtitleType(type->majortype);
        DeleteMediaType(type);
        if ((audioType && audio) || (subtitleType && subtitle))
            out.push_back(static_cast<long>(i));
    }
    return out;
}

MediaDescription DirectShowPlayer::Impl::describe() const
{
    MediaDescription d;
    d.generation = generation;
    if (!graph)
        return d;
    LONGLONG duration = 0;
    if (seeking && SUCCEEDED(seeking->GetDuration(&duration)) && duration > 0)
        d.durationUs = duration / 10;
    // The video's own seeking: the graph's capabilities are those every
    // renderer shares, and legacy's DirectSound renderer stays in the graph
    // unconnected when the file has no audio.
    DWORD caps = 0;
    ComPtr<IMediaSeeking> videoSeeking;
    if (rendererFilter && SUCCEEDED(rendererFilter.As(&videoSeeking)) && SUCCEEDED(videoSeeking->GetCapabilities(&caps)))
        d.seekable = (caps & AM_SEEKING_CanSeekAbsolute) != 0;
    else
        d.seekable = seeking && SUCCEEDED(seeking->GetCapabilities(&caps)) && (caps & AM_SEEKING_CanSeekAbsolute);
    d.audioOutput = audioConnected;
    DWORD count = 0;
    if (streamSelect && SUCCEEDED(streamSelect->Count(&count)) && count > 0) {
        for (DWORD i = 0; i < count; ++i) {
            AM_MEDIA_TYPE *type = nullptr;
            DWORD flags = 0;
            LCID lcid = 0;
            WCHAR *name = nullptr;
            if (FAILED(streamSelect->Info(static_cast<long>(i), &type, &flags, &lcid, nullptr, &name, nullptr,
                                          nullptr)))
                continue;
            PlayerTrack track;
            track.language = languageOf(lcid);
            track.title = utf8(name);
            if (name)
                CoTaskMemFree(name);
            const bool enabled = flags != 0;
            const GUID major = type ? type->majortype : GUID_NULL;
            if (type)
                DeleteMediaType(type);
            if (major == MEDIATYPE_Video) {
                if (enabled)
                    d.activeVideo = static_cast<int>(d.videoTracks.size());
                d.videoTracks.push_back(std::move(track));
            } else if (major == MEDIATYPE_Audio) {
                if (enabled)
                    d.activeAudio = static_cast<int>(d.audioTracks.size());
                d.audioTracks.push_back(std::move(track));
            } else if (isSubtitleType(major)) {
                if (enabled)
                    d.activeSubtitle = static_cast<int>(d.subtitleTracks.size());
                d.subtitleTracks.push_back(std::move(track));
            }
        }
    }
    // Without stream selection: the connected video and audio.
    if (d.videoTracks.empty() && renderer && renderer->connected()) {
        d.videoTracks.emplace_back();
        d.activeVideo = 0;
    }
    if (d.audioTracks.empty() && audioConnected) {
        d.audioTracks.emplace_back();
        d.activeAudio = 0;
    }
    return d;
}

void DirectShowPlayer::Impl::applyVolume()
{
    if (!audio || state == State::None)
        return;
    // Legacy SetVolume(-(pos * pos)) through videoVolumeGain: 2000 * log10.
    long value = volume <= 0 ? -10000 : static_cast<long>(std::lround(2000.0 * std::log10(volume)));
    value = value < -10000 ? -10000 : value > 0 ? 0 : value;
    if (SUCCEEDED(audio->put_Volume(value))) {
        appliedVolume = value;
        volumeApplied = true;
    }
}

void DirectShowPlayer::Impl::onDelivery(dshow::Delivery d)
{
    ++delivered;
    clockState.epoch = epoch;
    clockState.mediaUs = d.startUs;
    clockState.monotonicSeconds = monotonicSeconds();
    clockState.rate = 1.0;
    clockState.uncertaintyUs = d.endUs ? *d.endUs - d.startUs : 0;
    clockState.valid = state == State::Playing;
    if (pendingSeek && d.serial >= pendingSeek->serial) {
        auto pending = std::move(*pendingSeek);
        pendingSeek.reset();
        SeekResult r;
        r.generation = pending.generation;
        r.requestedUs = pending.requestedUs;
        r.deliveredStartUs = d.startUs;
        r.deliveredEndUs = d.endUs;
        pending.done(r);
    }
    if (sink)
        sink(std::move(d.frame), d.startUs);
}

DirectShowPlayer::DirectShowPlayer(Post post) : d(std::make_unique<Impl>())
{
    d->post = std::move(post);
    // Legacy: CoInitialize in the player's constructor, CoUninitialize in
    // its destructor.
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    d->comInitialized = SUCCEEDED(hr);
    const std::weak_ptr<bool> alive = d->alive;
    Impl *impl = d.get();
    d->bridge->deliver = [post = d->post, alive, impl](dshow::Delivery delivery) {
        auto shared = std::make_shared<dshow::Delivery>(std::move(delivery));
        post([alive, impl, shared] {
            if (alive.expired())
                return;
            impl->onDelivery(std::move(*shared));
        });
    };
    d->bridge->endOfStream = [post = d->post, alive, impl] {
        post([alive, impl] {
            if (alive.expired())
                return;
            impl->status = MediaStatus::EndOfMedia;
            impl->changed();
        });
    };
}

DirectShowPlayer::~DirectShowPlayer()
{
    d->alive.reset(); // queued deliveries are dropped
    d->tearDown();
    d->bridge->deliver = nullptr;
    d->bridge->endOfStream = nullptr;
    if (d->comInitialized)
        CoUninitialize();
}

void DirectShowPlayer::setFrameSink(FrameSink sink)
{
    d->sink = std::move(sink);
}

void DirectShowPlayer::setStateObserver(std::function<void()> changed)
{
    d->observer = std::move(changed);
}

void DirectShowPlayer::setOpenFailed(std::function<void(const std::string &)> failed)
{
    d->openFailed = std::move(failed);
}

void DirectShowPlayer::open(const std::string &path, Opened done)
{
    ++d->generation;
    ++d->epoch;
    d->clockState = {};
    if (auto seek = std::exchange(d->pendingSeek, std::nullopt))
        seek->done(std::unexpected(PlayerError::Stale));
    std::string error;
    // A delivery from the previous graph carries an older serial; it never
    // answers a seek on this one.
    d->bridge->requestedSerial = ++d->serial;
    const bool built = d->build(path, error);
    std::expected<MediaDescription, PlayerError> result = std::unexpected(PlayerError::BackendFailure);
    if (built) {
        d->renderer->adoptSerial(d->serial);
        d->status = MediaStatus::Loaded;
        d->applyVolume();
        result = d->describe();
    } else {
        d->tearDown();
        d->status = MediaStatus::Invalid;
        if (d->openFailed)
            d->openFailed(error);
    }
    d->changed();
    // As the Qt player, the answer comes later, on the owner's thread.
    const std::weak_ptr<bool> alive = d->alive;
    const std::uint64_t generation = d->generation;
    Impl *impl = d.get();
    d->post([alive, impl, generation, result = std::move(result), done = std::move(done)]() mutable {
        if (alive.expired())
            return;
        if (generation != impl->generation)
            return done(std::unexpected(PlayerError::Stale));
        done(std::move(result));
    });
}

void DirectShowPlayer::seek(std::int64_t us, Seeked done)
{
    if (!d->graph || d->state == Impl::State::None || !d->seeking)
        return done(std::unexpected(PlayerError::NotOpen));
    // Legacy asked for no capabilities: SetPositions answers (below).
    if (us < 0)
        return done(std::unexpected(PlayerError::InvalidInput));
    if (auto previous = std::exchange(d->pendingSeek, std::nullopt))
        previous->done(std::unexpected(PlayerError::Stale));
    ++d->epoch;
    d->clockState.valid = false;
    const std::uint64_t serial = ++d->serial;
    d->bridge->requestedSerial = serial;
    d->pendingSeek = Impl::PendingSeek{d->generation, serial, us, std::move(done)};
    if (d->status == MediaStatus::EndOfMedia)
        d->status = MediaStatus::Loaded;
    // Legacy DShowPlayer::SetPosition, in 100 ns units.
    LONGLONG position = us * 10;
    const bool stopped = d->state == Impl::State::Stopped;
    if (stopped)
        d->renderer->adoptSerial(serial); // a stopped graph flushes nothing
    const HRESULT hr =
        d->seeking->SetPositions(&position, AM_SEEKING_AbsolutePositioning, nullptr, AM_SEEKING_NoPositioning);
    if (FAILED(hr)) {
        if (auto pending = std::exchange(d->pendingSeek, std::nullopt))
            pending->done(std::unexpected(PlayerError::BackendFailure));
        return;
    }
    // A stopped graph shows the new frame by pausing until it is ready, then
    // stopping again (IMediaControl::StopWhenReady).
    if (stopped)
        d->control->StopWhenReady();
}

void DirectShowPlayer::play()
{
    // Legacy DShowPlayer::Play.
    if (d->state == Impl::State::None || !d->graph)
        return;
    if (d->state == Impl::State::Paused || d->state == Impl::State::Stopped) {
        if (SUCCEEDED(d->control->Run())) {
            d->state = Impl::State::Playing;
            ++d->epoch;
            d->clockState.valid = false;
            d->changed();
        }
    }
}

void DirectShowPlayer::pause()
{
    // Legacy DShowPlayer::Pause's paused branch; the port's pause always pauses.
    if (!d->graph || d->state == Impl::State::None || d->state == Impl::State::Paused)
        return;
    if (SUCCEEDED(d->control->Pause())) {
        d->state = Impl::State::Paused;
        ++d->epoch;
        d->clockState.valid = false;
        d->changed();
    }
}

void DirectShowPlayer::stop()
{
    // Legacy DShowPlayer::Stop.
    if (auto seek = std::exchange(d->pendingSeek, std::nullopt))
        seek->done(std::unexpected(PlayerError::Stale));
    if (d->state != Impl::State::Playing && d->state != Impl::State::Paused)
        return;
    if (d->control && SUCCEEDED(d->control->Stop())) {
        d->state = Impl::State::Stopped;
        ++d->epoch;
        d->clockState.valid = false;
        d->changed();
    }
}

bool DirectShowPlayer::selectAudioTrack(int index)
{
    // Legacy RendererDirectShow::EnableStream on the audio streams.
    const auto audio = d->streamsOf(true, false);
    if (audio.empty())
        return index == 0 && d->audioConnected;
    if (index < 0 || index >= static_cast<int>(audio.size()))
        return false;
    return SUCCEEDED(d->streamSelect->Enable(audio[static_cast<std::size_t>(index)], AMSTREAMSELECTENABLE_ENABLE));
}

bool DirectShowPlayer::selectSubtitleTrack(int index)
{
    const auto subtitles = d->streamsOf(false, true);
    if (index < -1 || index >= static_cast<int>(subtitles.size()))
        return false;
    if (index == -1) {
        for (const long stream : subtitles)
            d->streamSelect->Enable(stream, 0);
        return true;
    }
    return SUCCEEDED(
        d->streamSelect->Enable(subtitles[static_cast<std::size_t>(index)], AMSTREAMSELECTENABLE_ENABLE));
}

PlaybackState DirectShowPlayer::playbackState() const
{
    switch (d->state) {
    case Impl::State::Playing: return PlaybackState::Playing;
    case Impl::State::Paused: return PlaybackState::Paused;
    default: return PlaybackState::Stopped;
    }
}

MediaStatus DirectShowPlayer::mediaStatus() const
{
    return d->status;
}

double DirectShowPlayer::bufferProgress() const
{
    return d->graph ? 1.0 : 0.0;
}

PlayerClock DirectShowPlayer::clock() const
{
    PlayerClock c = d->clockState;
    c.valid = c.valid && c.epoch == d->epoch && d->state == Impl::State::Playing;
    return c;
}

MediaDescription DirectShowPlayer::description() const
{
    return d->describe();
}

std::uint64_t DirectShowPlayer::generation() const
{
    return d->generation;
}

void DirectShowPlayer::setVolume(double linear)
{
    d->volume = linear;
    d->applyVolume();
}

void DirectShowPlayer::setColorSpace(const std::string &matrix)
{
    d->colorSpace = matrix;
    d->bridge->bt709 = matrix == "TV.709";
}

std::vector<DirectShowPlayer::Filter> DirectShowPlayer::filters() const
{
    // Legacy DShowPlayer::EnumFilters.
    std::vector<Filter> out;
    if (!d->graph)
        return out;
    ComPtr<IEnumFilters> filters;
    if (FAILED(d->graph->EnumFilters(&filters)))
        return out;
    ComPtr<IBaseFilter> filter;
    while (filters->Next(1, &filter, nullptr) == S_OK) {
        FILTER_INFO info{};
        if (SUCCEEDED(filter->QueryFilterInfo(&info))) {
            ComPtr<ISpecifyPropertyPages> pages;
            Filter f;
            f.name = utf8(info.achName);
            f.hasPropertyPages = SUCCEEDED(filter.As(&pages)) && pages;
            out.push_back(std::move(f));
            if (info.pGraph)
                info.pGraph->Release();
        }
        filter.Reset();
    }
    return out;
}

bool DirectShowPlayer::showFilterProperties(const std::string &name, std::uintptr_t ownerWindow, int x, int y)
{
    // Legacy DShowPlayer::FilterConfig.
    if (!d->graph)
        return false;
    ComPtr<IBaseFilter> filter;
    const std::wstring wname = wide(name);
    if (FAILED(d->graph->FindFilterByName(wname.c_str(), &filter)) || !filter)
        return false;
    ComPtr<ISpecifyPropertyPages> pages;
    if (FAILED(filter.As(&pages)) || !pages)
        return false;
    CAUUID guids{};
    if (FAILED(pages->GetPages(&guids)))
        return false;
    ComPtr<IUnknown> object;
    pages.As(&object);
    IUnknown *objects[] = {object.Get()};
    try {
        OleCreatePropertyFrame(reinterpret_cast<HWND>(ownerWindow), static_cast<UINT>(x), static_cast<UINT>(y),
                               wname.c_str(), 1, objects, guids.cElems, guids.pElems, 0, 0, nullptr);
    } catch (...) {
    }
    if (guids.pElems)
        CoTaskMemFree(guids.pElems);
    return true;
}

DirectShowPlayer::VideoFormat DirectShowPlayer::videoFormat() const
{
    VideoFormat f;
    if (!d->renderer)
        return f;
    const auto info = d->renderer->info();
    f.width = info.width;
    f.height = info.height;
    f.fps = info.fps;
    f.aspectX = info.aspectX;
    f.aspectY = info.aspectY;
    f.subtype = dshow::subtypeName(info.subtype);
    return f;
}

std::uint64_t DirectShowPlayer::deliveredFrames() const
{
    return d->delivered;
}

std::vector<std::string> DirectShowPlayer::streams() const
{
    // Legacy DShowPlayer::GetStreams.
    std::vector<std::string> out;
    if (!d->streamSelect)
        return out;
    DWORD count = 0;
    if (FAILED(d->streamSelect->Count(&count)))
        return out;
    for (DWORD i = 0; i < count; ++i) {
        DWORD flags = 0;
        WCHAR *name = nullptr;
        if (FAILED(d->streamSelect->Info(static_cast<long>(i), nullptr, &flags, nullptr, nullptr, &name, nullptr,
                                         nullptr)))
            continue;
        out.push_back(utf8(name) + (flags != 0 ? " 1" : " 0"));
        if (name)
            CoTaskMemFree(name);
    }
    return out;
}

long DirectShowPlayer::appliedVolume() const
{
    return d->volumeApplied ? d->appliedVolume : 0;
}

} // namespace hikari::backends
