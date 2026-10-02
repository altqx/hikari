// hikari-media-helper: the isolated FFMS2 media worker (N1; ADR 0016). One
// process owns one indexed source; requests run one at a time, so FFMS2 calls
// are serialized. Frames leave the process as owned BGRA copies.

#include "hikari/backends/helper_endpoint.h"
#include "hikari/backends/media_protocol.h"

#include <ffms.h>

extern "C" {
#include <libavutil/pixfmt.h>
#include <libavformat/avformat.h>
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
        }
        r.terminal(Outcome::Unsupported, bytesOf("unknown command"));
    });
}
