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
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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
    // A1: the audio box's audio, in legacy's decode format (S16, 1 or 2
    // channels), in the box's own helper.
    std::unique_ptr<FFMS_AudioSource, AudioDeleter> display;
    std::int64_t displaySamples = 0;
    int displayChannels = 0;
    // A1: legacy DiskCache's `data`, the block buffer every block is decoded
    // into; it starts zeroed with each open and keeps what the last read left.
    std::vector<std::byte> block;
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

// Paths arrive as UTF-8 (FFMS2 takes UTF-8 on every platform).
std::filesystem::path fsPath(const std::string &utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

// Legacy ProviderFFMS2::Init's index cache: an index file is used when it is
// not older than the source (FFMS2 matches an index only by size and a hash
// of the file's ends) and FFMS2 says it belongs to the file.
FFMS_Index *readIndexFile(const std::string &path, const std::string &indexFile)
{
    if (indexFile.empty())
        return nullptr;
    std::error_code ec;
    const auto index = fsPath(indexFile);
    if (!std::filesystem::exists(index, ec))
        return nullptr;
    std::error_code sourceError, indexError;
    const auto sourceTime = std::filesystem::last_write_time(fsPath(path), sourceError);
    const auto indexTime = std::filesystem::last_write_time(index, indexError);
    if (!sourceError && !indexError && sourceTime > indexTime)
        return nullptr; // legacy: a source changed after indexing is indexed again
    char buffer[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    FFMS_Index *read = FFMS_ReadIndex(indexFile.c_str(), &err);
    if (read && FFMS_IndexBelongsToFile(read, path.c_str(), &err) != 0) {
        FFMS_DestroyIndex(read);
        return nullptr;
    }
    return read;
}

// Legacy: the folder is made when missing; a failed write is a debug message
// ("Cannot save index, error %s occurred") and the open goes on. True when
// the file was written.
bool writeIndexFile(const std::string &indexFile, FFMS_Index *index)
{
    if (indexFile.empty())
        return false;
    std::error_code ec;
    std::filesystem::create_directories(fsPath(indexFile).parent_path(), ec);
    char buffer[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    if (FFMS_WriteIndex(indexFile.c_str(), index, &err) != 0) {
        std::fprintf(stderr, "Cannot save index, error %s occurred\n", errorText(err).c_str());
        return false;
    }
    return true;
}

// A1: legacy's box shared the video's index in memory. When the index file
// cannot be written, the index goes to the host's temporary file instead, so
// the box's own helper reads it rather than indexing the track again.
bool handOff(const std::string &handoffFile, FFMS_Index *index)
{
    if (handoffFile.empty())
        return false;
    char buffer[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    if (FFMS_WriteIndex(handoffFile.c_str(), index, &err) != 0) {
        std::fprintf(stderr, "Cannot hand the index over, error %s occurred\n", errorText(err).c_str());
        return false;
    }
    return true;
}

constexpr int kEveryAudioTrack = -2;

void open(Source &source, Reader &in, Responder &r)
{
    const std::string path = in.str();
    const int audioTrack = in.i32();
    const std::string indexFile = in.str();
    const std::string handoffFile = in.str();
    if (!in.ok())
        return r.terminal(Outcome::InvalidInput, bytesOf("malformed open"));
    source = {};
    char buffer[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    FFMS_Indexer *indexer = FFMS_CreateIndexer(path.c_str(), &err);
    if (!indexer)
        return r.terminal(Outcome::InvalidInput, bytesOf(errorText(err)));
    std::unique_ptr<FFMS_Index, IndexDeleter> index(readIndexFile(path, indexFile));
    const bool newIndex = !index;
    bool handedOff = false;
    if (index) {
        FFMS_CancelIndexing(indexer); // legacy: the index file stands in for indexing
    } else {
        Progress progress{&r};
        FFMS_SetProgressCallback(indexer, onProgress, &progress);
        // audio is not indexed by default; legacy indexes the chosen track alone
        if (audioTrack == kEveryAudioTrack)
            FFMS_TrackTypeIndexSettings(indexer, FFMS_TYPE_AUDIO, 1, 0);
        else if (audioTrack >= 0 && audioTrack < FFMS_GetNumTracksI(indexer) &&
                 FFMS_GetTrackTypeI(indexer, audioTrack) == FFMS_TYPE_AUDIO)
            FFMS_TrackIndexSettings(indexer, audioTrack, 1, 0);
        index.reset(FFMS_DoIndexing2(indexer, FFMS_IEH_ABORT, &err));
        if (!index)
            return r.terminal(r.cancelled() ? Outcome::Cancelled : Outcome::Failed, bytesOf(errorText(err)));
        if (!writeIndexFile(indexFile, index.get()) && !indexFile.empty())
            handedOff = handOff(handoffFile, index.get());
    }
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
    // V4: legacy ProviderFFMS2::Init's m_CS and m_CR, read from frame 0
    // before the output format is set (ProviderFFMS2.cpp:368-369, 378);
    // setting it resets the frame's colour fields.
    const int firstColorSpace = first->ColorSpace, firstColorRange = first->ColorRange;
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
    std::vector<int> audioTracks;
    for (int i = 0; i < FFMS_GetNumTracks(index.get()); ++i)
        if (FFMS_GetTrackType(FFMS_GetTrackFromIndex(index.get(), i)) == FFMS_TYPE_AUDIO)
            audioTracks.push_back(i);
    out.i32(static_cast<std::int32_t>(audioTracks.size()));
    for (int t : audioTracks)
        out.i32(t);
    std::vector<int> keyframes; // V2: legacy ProviderFFMS2 reads FFMS_FrameInfo::KeyFrame
    for (int i = 0; i < props->NumFrames; ++i)
        if (FFMS_GetFrameInfo(t, i)->KeyFrame)
            keyframes.push_back(i);
    out.i32(static_cast<std::int32_t>(keyframes.size()));
    for (int k : keyframes)
        out.i32(k);
    out.u8(newIndex ? 1 : 0).u8(handedOff ? 1 : 0);
    // T1: legacy ProviderFFMS2::Init's frame size and SAR (ProviderFFMS2.cpp:362-366).
    out.i32(first->EncodedWidth).i32(first->EncodedHeight).i32(props->SARNum).i32(props->SARDen);
    out.i32(firstColorSpace).i32(firstColorRange); // V4
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

// V4: legacy ProviderFFMS2's FFMS_SetInputFormatV (ProviderFFMS2.cpp:401,
// 407, 958, 960): the matrix and range, the source's pixel format kept.
void inputMatrix(Source &source, Reader &in, Responder &r)
{
    const int colorSpace = in.i32();
    const int colorRange = in.i32();
    if (!in.ok() || !source.video)
        return r.terminal(Outcome::InvalidInput, bytesOf("not open"));
    char buffer[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    if (FFMS_SetInputFormatV(source.video.get(), colorSpace, colorRange, FFMS_GetPixFmt(""), &err) != 0)
        return r.terminal(Outcome::Failed, bytesOf(errorText(err)));
    r.terminal(Outcome::Ok, {});
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

// A1: failures of the audio box's requests name their stage and carry
// FFMS2's text (media_protocol.h).
enum class Stage : std::uint8_t { Indexer = 0, Indexing = 1, Source = 2, Convert = 3, Read = 4, Host = 5 };

void failDisplay(Responder &r, Outcome outcome, Stage stage, const std::string &text)
{
    r.terminal(outcome, Writer().u8(static_cast<std::uint8_t>(stage)).str(text).take());
}

std::string orEmpty(const char *text)
{
    return text ? std::string(text) : std::string();
}

// A1: a file's tracks as legacy ProviderFFMS2::Init lists them from the
// indexer, before anything is indexed.
void probe(Reader &in, Responder &r)
{
    const std::string path = in.str();
    if (!in.ok())
        return failDisplay(r, Outcome::InvalidInput, Stage::Host, "malformed probe");
    char buffer[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    FFMS_Indexer *indexer = FFMS_CreateIndexer(path.c_str(), &err);
    if (!indexer)
        return failDisplay(r, Outcome::InvalidInput, Stage::Indexer, errorText(err));
    bool hasVideo = false;
    Writer tracks;
    std::int32_t count = 0;
    for (int i = 0; i < FFMS_GetNumTracksI(indexer); ++i) {
        const int type = FFMS_GetTrackTypeI(indexer, i);
        if (type == FFMS_TYPE_VIDEO)
            hasVideo = true;
        if (type != FFMS_TYPE_AUDIO)
            continue;
        const char *name = FFMS_GetTrackName(indexer, i);
        const char *language = FFMS_GetTrackLanguage(indexer, i);
        tracks.i32(i).u8(name ? 1 : 0).str(orEmpty(name)).u8(language ? 1 : 0).str(orEmpty(language))
            .str(orEmpty(FFMS_GetCodecNameI(indexer, i)));
        ++count;
    }
    FFMS_CancelIndexing(indexer);
    auto list = tracks.take();
    Writer out;
    out.u8(hasVideo ? 1 : 0).i32(count);
    auto head = out.take();
    head.insert(head.end(), list.begin(), list.end());
    r.terminal(Outcome::Ok, std::move(head));
}

// Legacy's decode format on an audio source: S16, front left and right for
// more than one channel, else front centre. Writes the Ok reply.
void finishDisplay(Source &source, std::unique_ptr<FFMS_AudioSource, AudioDeleter> audio, int track, bool newIndex,
                   Responder &r)
{
    char buffer[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    const bool stereo = FFMS_GetAudioProperties(audio.get())->Channels > 1;
    FFMS_ResampleOptions *options = FFMS_CreateResampleOptions(audio.get());
    options->ChannelLayout = stereo ? (FFMS_CH_FRONT_LEFT | FFMS_CH_FRONT_RIGHT) : FFMS_CH_FRONT_CENTER;
    options->SampleFormat = FFMS_FMT_S16;
    const std::int64_t layout = options->ChannelLayout;
    const int converted = FFMS_SetOutputFormatA(audio.get(), options, &err);
    FFMS_DestroyResampleOptions(options);
    if (converted != 0)
        return failDisplay(r, Outcome::Unsupported, Stage::Convert, errorText(err));
    // The properties keep the source's channels; the output is what was asked.
    const FFMS_AudioProperties *a = FFMS_GetAudioProperties(audio.get());
    source.displayChannels = stereo ? 2 : 1;
    source.displaySamples = a->NumSamples;
    source.display = std::move(audio);
    r.terminal(Outcome::Ok, Writer()
                                .i32(track)
                                .i32(FFMS_FMT_S16)
                                .i32(a->SampleRate)
                                .i32(16)
                                .i32(source.displayChannels)
                                .i64(layout)
                                .i64(source.displaySamples)
                                .i64(0)
                                .u8(newIndex ? 1 : 0)
                                .take());
}

// A1: the audio box's file, as legacy ProviderFFMS2 opens it: the index file
// when it can be used (an open video wrote it for the same track), else the
// chosen audio track indexed (decode errors ignored, FFMS_IEH_IGNORE) and the
// index written; sample 0 at the first video frame
// (FFMS_DELAY_FIRST_VIDEO_TRACK).
void openDisplayAudio(Source &source, Reader &in, Responder &r)
{
    const std::string path = in.str();
    const int track = in.i32();
    const std::string indexFile = in.str();
    if (!in.ok())
        return failDisplay(r, Outcome::InvalidInput, Stage::Host, "malformed open");
    source = {};
    char buffer[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof buffer, buffer};
    FFMS_Indexer *indexer = FFMS_CreateIndexer(path.c_str(), &err);
    if (!indexer)
        return failDisplay(r, Outcome::InvalidInput, Stage::Indexer, errorText(err));
    if (track < 0 || track >= FFMS_GetNumTracksI(indexer) || FFMS_GetTrackTypeI(indexer, track) != FFMS_TYPE_AUDIO) {
        FFMS_CancelIndexing(indexer);
        return failDisplay(r, Outcome::Unsupported, Stage::Host, "no audio track");
    }
    std::unique_ptr<FFMS_Index, IndexDeleter> index(readIndexFile(path, indexFile));
    const bool newIndex = !index;
    if (index) {
        FFMS_CancelIndexing(indexer);
    } else {
        Progress progress{&r};
        FFMS_SetProgressCallback(indexer, onProgress, &progress);
        FFMS_TrackIndexSettings(indexer, track, 1, 0);
        index.reset(FFMS_DoIndexing2(indexer, FFMS_IEH_IGNORE, &err));
        if (!index)
            return failDisplay(r, r.cancelled() ? Outcome::Cancelled : Outcome::Failed, Stage::Indexing,
                               errorText(err));
        writeIndexFile(indexFile, index.get());
    }
    std::unique_ptr<FFMS_AudioSource, AudioDeleter> audio(
        FFMS_CreateAudioSource(path.c_str(), track, index.get(), FFMS_DELAY_FIRST_VIDEO_TRACK, &err));
    if (!audio)
        return failDisplay(r, Outcome::Unsupported, Stage::Source, errorText(err));
    source.path = path;
    source.index = std::move(index);
    finishDisplay(source, std::move(audio), track, newIndex, r);
}

// A1: legacy ProviderFFMS2::GetAudio into a cache's buffer (media_protocol.h).
void displayRead(Source &source, Reader &in, Responder &r)
{
    const std::int64_t start = in.i64();
    const std::int64_t frames = in.i64();
    const std::int64_t decode = in.i64();
    const bool fresh = in.u8() != 0;
    if (!in.ok() || !source.display || frames < 0 || decode < 0 || decode > frames)
        return failDisplay(r, Outcome::InvalidInput, Stage::Host, "not open");
    if (decode > 0 && (start < 0 || start >= source.displaySamples))
        return failDisplay(r, Outcome::InvalidInput, Stage::Host, "EOF");
    const std::size_t bytes = static_cast<std::size_t>(frames) * 2 * static_cast<std::size_t>(source.displayChannels);
    std::vector<std::byte> newBlock;
    std::vector<std::byte> &buffer = fresh ? newBlock : source.block;
    if (buffer.size() < bytes)
        buffer.resize(bytes); // the new part zeroed, as legacy's vector began
    // legacy "Fill beyond with zero": short samples [decode, frames), so in
    // stereo half of the frames past the decoded ones
    auto *samples = reinterpret_cast<std::int16_t *>(buffer.data());
    for (std::int64_t i = decode; i < frames; ++i)
        samples[i] = 0;
    char text[1024];
    FFMS_ErrorInfo err{FFMS_ERROR_SUCCESS, FFMS_ERROR_SUCCESS, sizeof text, text};
    // FFMS2 copies each decoded piece into the buffer as it goes, so a failure
    // leaves the frames it decoded first followed by what the buffer held.
    if (decode > 0 && FFMS_GetAudio(source.display.get(), buffer.data(), start, decode, &err) != 0)
        return r.terminal(Outcome::Failed, Writer()
                                               .u8(static_cast<std::uint8_t>(Stage::Read))
                                               .str(errorText(err))
                                               .raw(buffer.data(), bytes)
                                               .take());
    r.terminal(Outcome::Ok, Writer().i64(start).i64(frames).raw(buffer.data(), bytes).take());
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
        case media::Command::OpenDisplayAudio:
            session.reset();
            return openDisplayAudio(source, in, r);
        case media::Command::Probe:
            return probe(in, r);
        case media::Command::InputMatrix:
            return inputMatrix(source, in, r);
        case media::Command::DisplayRead:
            return displayRead(source, in, r);
        }
        r.terminal(Outcome::Unsupported, bytesOf("unknown command"));
    });
}
