// hikari-media-helper: the isolated FFMS2 media worker (N1; ADR 0016). One
// process owns one indexed source; requests run one at a time, so FFMS2 calls
// are serialized. Frames leave the process as owned BGRA copies.

#include "hikari/backends/helper_endpoint.h"
#include "hikari/backends/media_protocol.h"

#include <ffms.h>

extern "C" {
#include <libavutil/pixfmt.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

using namespace hikari::backends;
using namespace hikari::backends::helper;

namespace {

struct IndexDeleter {
    void operator()(FFMS_Index *i) const { FFMS_DestroyIndex(i); }
};
struct VideoDeleter {
    void operator()(FFMS_VideoSource *v) const { FFMS_DestroyVideoSource(v); }
};
struct AudioDeleter {
    void operator()(FFMS_AudioSource *a) const { FFMS_DestroyAudioSource(a); }
};

struct Source {
    std::string path;
    std::unique_ptr<FFMS_Index, IndexDeleter> index;
    std::unique_ptr<FFMS_VideoSource, VideoDeleter> video;
    std::unique_ptr<FFMS_AudioSource, AudioDeleter> audio;
    int frames = 0;
    std::int64_t samples = 0;
    int bytesPerFrame = 0; // one sample on every channel
    int sampleFormat = 0;  // FFMS_FMT_*
    int sampleRate = 0;
    int channels = 0;
    std::int64_t channelLayout = 0;
};

// One resampled stream over a source range (I3). Chunks continue the same
// resampler, so they join without seams.
struct PcmSession {
    SwrContext *swr = nullptr;
    std::int64_t cursor = 0, end = 0;
    std::int64_t expected = 0, produced = 0;
    int outChannels = 0;
    bool flushed = false;
    ~PcmSession() { swr_free(&swr); }
};

std::string errorText(const FFMS_ErrorInfo &e)
{
    return e.Buffer ? std::string(e.Buffer) : std::string("unknown FFMS2 error");
}

struct Progress {
    Responder *responder;
};

int FFMS_CC onProgress(int64_t current, int64_t total, void *opaque)
{
    auto *p = static_cast<Progress *>(opaque);
    p->responder->progress(Writer().i64(current).i64(total).take());
    return p->responder->cancelled() ? 1 : 0; // non-zero cancels indexing
}

void open(Source &source, Reader &in, Responder &r)
{
    const std::string path = in.str();
    if (!in.ok())
        return r.terminal(Outcome::InvalidInput, bytesOf("malformed open"));
    source = {};
    char buffer[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    FFMS_Indexer *indexer = FFMS_CreateIndexer(path.c_str(), &err);
    if (!indexer)
        return r.terminal(Outcome::InvalidInput, bytesOf(errorText(err)));
    Progress progress{&r};
    FFMS_SetProgressCallback(indexer, onProgress, &progress);
    FFMS_TrackTypeIndexSettings(indexer, FFMS_TYPE_AUDIO, 1, 0); // audio is not indexed by default
    std::unique_ptr<FFMS_Index, IndexDeleter> index(FFMS_DoIndexing2(indexer, FFMS_IEH_ABORT, &err));
    if (!index)
        return r.terminal(r.cancelled() ? Outcome::Cancelled : Outcome::Failed, bytesOf(errorText(err)));
    const int track = FFMS_GetFirstTrackOfType(index.get(), FFMS_TYPE_VIDEO, &err);
    if (track < 0)
        return r.terminal(Outcome::Unsupported, bytesOf("no video track"));
    // FFMS_SEEK_NORMAL: the accepted seek mode for exact indexed frames.
    std::unique_ptr<FFMS_VideoSource, VideoDeleter> video(
        FFMS_CreateVideoSource(path.c_str(), track, index.get(), 1, FFMS_SEEK_NORMAL, &err));
    if (!video)
        return r.terminal(Outcome::Failed, bytesOf(errorText(err)));
    const FFMS_VideoProperties *props = FFMS_GetVideoProperties(video.get());
    const FFMS_Frame *first = FFMS_GetFrame(video.get(), 0, &err);
    if (!first)
        return r.terminal(Outcome::Failed, bytesOf(errorText(err)));
    const int formats[] = {AV_PIX_FMT_BGRA, -1};
    if (FFMS_SetOutputFormatV2(video.get(), formats, first->EncodedWidth, first->EncodedHeight,
                               FFMS_RESIZER_BICUBIC, &err) != 0)
        return r.terminal(Outcome::Failed, bytesOf(errorText(err)));
    FFMS_Track *t = FFMS_GetTrackFromVideo(video.get());
    const FFMS_TrackTimeBase *tb = FFMS_GetTimeBase(t);
    Writer out;
    out.i32(track).i64(props->FPSNumerator).i64(props->FPSDenominator).i64(tb->Num).i64(tb->Den).i32(props->NumFrames);
    for (int i = 0; i < props->NumFrames; ++i)
        out.i64(FFMS_GetFrameInfo(t, i)->PTS);
    FFMS_ErrorInfo audioErr{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    out.i32(FFMS_GetFirstTrackOfType(index.get(), FFMS_TYPE_AUDIO, &audioErr)); // -1 when none
    source.path = path;
    source.index = std::move(index);
    source.video = std::move(video);
    source.frames = props->NumFrames;
    r.terminal(Outcome::Ok, out.take());
}

void frame(Source &source, Reader &in, Responder &r)
{
    const int n = in.i32();
    if (!in.ok() || !source.video)
        return r.terminal(Outcome::InvalidInput, bytesOf("not open"));
    if (n < 0 || n >= source.frames)
        return r.terminal(Outcome::InvalidInput, bytesOf("EOF"));
    char buffer[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    const FFMS_Frame *f = FFMS_GetFrame(source.video.get(), n, &err);
    if (!f)
        return r.terminal(Outcome::Failed, bytesOf(errorText(err)));
    const int width = f->ScaledWidth > 0 ? f->ScaledWidth : f->EncodedWidth;
    const int height = f->ScaledHeight > 0 ? f->ScaledHeight : f->EncodedHeight;
    // An owned, tightly packed copy: FFMS2's frame is valid only until its next call.
    std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
        std::memcpy(pixels.data() + static_cast<std::size_t>(y) * width * 4, f->Data[0] + y * f->Linesize[0],
                    static_cast<std::size_t>(width) * 4);
    FFMS_Track *t = FFMS_GetTrackFromVideo(source.video.get());
    Writer out;
    out.i32(width).i32(height).i32(width * 4).i64(FFMS_GetFrameInfo(t, n)->PTS).bytes(pixels);
    r.terminal(Outcome::Ok, out.take());
}

void openAudio(Source &source, Reader &in, Responder &r)
{
    const int track = in.i32();
    if (!in.ok() || !source.index)
        return r.terminal(Outcome::InvalidInput, bytesOf("not open"));
    char buffer[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    // FFMS_DELAY_NO_SHIFT: sample 0 is the first decoded sample; its time is
    // reported as the origin instead of being shifted or padded.
    std::unique_ptr<FFMS_AudioSource, AudioDeleter> audio(
        FFMS_CreateAudioSource(source.path.c_str(), track, source.index.get(), FFMS_DELAY_NO_SHIFT, &err));
    if (!audio)
        return r.terminal(Outcome::Unsupported, bytesOf(errorText(err)));
    const FFMS_AudioProperties *a = FFMS_GetAudioProperties(audio.get());
    const int bytesPerSample = a->BitsPerSample / 8;
    source.bytesPerFrame = bytesPerSample * a->Channels;
    source.samples = a->NumSamples;
    source.sampleFormat = a->SampleFormat;
    source.sampleRate = a->SampleRate;
    source.channels = a->Channels;
    source.channelLayout = a->ChannelLayout;
    source.audio = std::move(audio);
    r.terminal(Outcome::Ok, Writer()
                                .i32(track)
                                .i32(a->SampleFormat)
                                .i32(a->SampleRate)
                                .i32(a->BitsPerSample)
                                .i32(a->Channels)
                                .i64(a->ChannelLayout)
                                .i64(a->NumSamples)
                                .i64(static_cast<std::int64_t>(a->FirstTime * 1'000'000.0 + (a->FirstTime < 0 ? -0.5 : 0.5)))
                                .take());
}

void audio(Source &source, Reader &in, Responder &r)
{
    const std::int64_t start = in.i64();
    std::int64_t count = in.i64();
    if (!in.ok() || !source.audio || count < 0)
        return r.terminal(Outcome::InvalidInput, bytesOf("not open"));
    if (start < 0 || start >= source.samples)
        return r.terminal(Outcome::InvalidInput, bytesOf("EOF"));
    count = std::min(count, source.samples - start);
    std::vector<std::byte> samples(static_cast<std::size_t>(count) * source.bytesPerFrame);
    char buffer[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    if (count > 0 && FFMS_GetAudio(source.audio.get(), samples.data(), start, count, &err) != 0)
        return r.terminal(Outcome::Failed, bytesOf(errorText(err)));
    r.terminal(Outcome::Ok, Writer().i64(start).i64(count).bytes(samples).take());
}

AVSampleFormat avFormat(int ffms)
{
    switch (ffms) {
    case FFMS_FMT_U8: return AV_SAMPLE_FMT_U8;
    case FFMS_FMT_S16: return AV_SAMPLE_FMT_S16;
    case FFMS_FMT_S32: return AV_SAMPLE_FMT_S32;
    case FFMS_FMT_FLT: return AV_SAMPLE_FMT_FLT;
    case FFMS_FMT_DBL: return AV_SAMPLE_FMT_DBL;
    default: return AV_SAMPLE_FMT_NONE;
    }
}

void pcmBegin(Source &source, std::unique_ptr<PcmSession> &session, Reader &in, Responder &r)
{
    const std::int64_t start = in.i64();
    std::int64_t count = in.i64();
    const int outRate = in.i32();
    const int outChannels = in.i32();
    if (!in.ok() || !source.audio || count < 0 || outRate <= 0 || outChannels < 1 || outChannels > 8)
        return r.terminal(Outcome::InvalidInput, bytesOf("not open or invalid format"));
    if (start < 0 || start >= source.samples)
        return r.terminal(Outcome::InvalidInput, bytesOf("EOF"));
    count = std::min(count, source.samples - start);
    auto next = std::make_unique<PcmSession>();
    AVChannelLayout inLayout{}, outLayout{};
    if (source.channelLayout == 0 ||
        av_channel_layout_from_mask(&inLayout, static_cast<std::uint64_t>(source.channelLayout)) < 0 ||
        inLayout.nb_channels != source.channels) {
        av_channel_layout_uninit(&inLayout);
        av_channel_layout_default(&inLayout, source.channels);
    }
    av_channel_layout_default(&outLayout, outChannels);
    const int rc = swr_alloc_set_opts2(&next->swr, &outLayout, AV_SAMPLE_FMT_FLT, outRate, &inLayout,
                                       avFormat(source.sampleFormat), source.sampleRate, 0, nullptr);
    av_channel_layout_uninit(&inLayout);
    av_channel_layout_uninit(&outLayout);
    if (rc < 0 || swr_init(next->swr) < 0)
        return r.terminal(Outcome::Unsupported, bytesOf("cannot convert this audio"));
    next->cursor = start;
    next->end = start + count;
    // Exactly the range's duration at the output rate, rounded to a frame.
    next->expected = (count * outRate * 2 + source.sampleRate) / (2 * static_cast<std::int64_t>(source.sampleRate));
    next->outChannels = outChannels;
    session = std::move(next);
    r.terminal(Outcome::Ok, Writer().i64(start).i64(count).i64(session->expected).take());
}

void pcmNext(Source &source, std::unique_ptr<PcmSession> &session, Reader &in, Responder &r)
{
    const std::int64_t maxFrames = in.i64();
    if (!in.ok() || !session || !source.audio || maxFrames <= 0)
        return r.terminal(Outcome::InvalidInput, bytesOf("no PCM session"));
    PcmSession &p = *session;
    const std::int64_t want = std::min(maxFrames, p.expected - p.produced);
    std::vector<float> out(static_cast<std::size_t>(std::max<std::int64_t>(want, 0)) * p.outChannels);
    std::int64_t got = 0;
    std::vector<std::byte> input;
    char buffer[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    while (got < want) {
        auto *dst = reinterpret_cast<std::uint8_t *>(out.data() + got * p.outChannels);
        int n = 0;
        if (p.cursor < p.end) {
            const std::int64_t chunk = std::min<std::int64_t>(p.end - p.cursor, 4096);
            input.resize(static_cast<std::size_t>(chunk) * source.bytesPerFrame);
            if (FFMS_GetAudio(source.audio.get(), input.data(), p.cursor, chunk, &err) != 0)
                return r.terminal(Outcome::Failed, bytesOf(errorText(err)));
            const auto *src = reinterpret_cast<const std::uint8_t *>(input.data());
            n = swr_convert(p.swr, &dst, static_cast<int>(want - got), &src, static_cast<int>(chunk));
            p.cursor += chunk;
        } else if (!p.flushed) {
            n = swr_convert(p.swr, &dst, static_cast<int>(want - got), nullptr, 0);
            if (n == 0)
                p.flushed = true;
        } else {
            break; // the resampler is empty; pad below
        }
        if (n < 0)
            return r.terminal(Outcome::Failed, bytesOf("resampling failed"));
        got += n;
    }
    // A short resampler tail is padded with silence to the exact length.
    if (p.cursor >= p.end && p.flushed && got < want)
        got = want;
    p.produced += got;
    out.resize(static_cast<std::size_t>(got) * p.outChannels);
    r.terminal(Outcome::Ok, Writer()
                                .i64(got)
                                .u8(p.produced >= p.expected ? 1 : 0)
                                .raw(out.data(), out.size() * sizeof(float))
                                .take());
}

} // namespace

// Container chapters for general playback (N5), read with libavformat in this
// process so FFmpeg stays out of the application.
void chapters(Reader &in, Responder &r)
{
    const std::string path = in.str();
    if (!in.ok() || path.empty())
        return r.terminal(Outcome::InvalidInput, bytesOf("malformed Chapters"));
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0)
        return r.terminal(Outcome::Failed, bytesOf("cannot open " + path));
    Writer w;
    w.i32(static_cast<std::int32_t>(fmt->nb_chapters));
    for (unsigned i = 0; i < fmt->nb_chapters; ++i) {
        const AVChapter *c = fmt->chapters[i];
        const AVDictionaryEntry *title = av_dict_get(c->metadata, "title", nullptr, 0);
        w.i64(av_rescale_q(c->start, c->time_base, AVRational{1, 1'000'000}))
            .i64(av_rescale_q(c->end, c->time_base, AVRational{1, 1'000'000}))
            .str(title ? title->value : "");
    }
    avformat_close_input(&fmt);
    r.terminal(Outcome::Ok, w.take());
}

int main()
{
    FFMS_Init(0, 0);
    Source source;
    std::unique_ptr<PcmSession> session;
    return runHelper(media::kHelperName, media::kProtocolVersion, [&](const Frame &request, Responder &r) {
        Reader in(request.payload);
        switch (static_cast<media::Command>(in.u8())) {
        case media::Command::Open:
            return open(source, in, r);
        case media::Command::Frame:
            return frame(source, in, r);
        case media::Command::OpenAudio:
            return openAudio(source, in, r);
        case media::Command::Audio:
            return audio(source, in, r);
        case media::Command::Chapters:
            return chapters(in, r);
        case media::Command::PcmBegin:
            return pcmBegin(source, session, in, r);
        case media::Command::PcmNext:
            return pcmNext(source, session, in, r);
        }
        r.terminal(Outcome::Unsupported, bytesOf("unknown command"));
    });
}
