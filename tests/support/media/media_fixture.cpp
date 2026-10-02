// Writes synthetic video fixtures whose frames identify themselves (M50-seek,
// N1). Each frame shows its index as a 4x4 barcode of 16 large blocks (white
// = 1), which survives MPEG-4 quantization; decoders read it back by
// sampling block centres.
//
//   hikari_media_fixture <out> <kind>
//   kinds: cfr       24000/1001, 48 frames, I/P only, GOP 12
//          vfr       48 frames with irregular timestamps (1 ms timebase)
//          bframes   24000/1001, 48 frames, 2 B-frames per anchor, GOP 12
//          longgop   24000/1001, 300 frames, one keyframe
//          audio     the cfr video plus 2 s of 48 kHz stereo PCM whose left
//                    channel is the sample index modulo 32768
//          tracks    the audio fixture with a second audio track (languages
//                    eng and jpn, titled), a SubRip track with one cue
//                    "Hello" from 0.5 s to 1.5 s, and two chapters (N5)
//          unknown   the cfr video written as a live stream: no duration
//          color601, color709, color709full
//                    12 frames of four flat Y'CbCr quadrants (kColorPatches),
//                    tagged BT.601 limited, BT.709 limited and BT.709 full
//                    range (I2: the decoder must convert with the tags)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/dict.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
}

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr int kWidth = 320, kHeight = 240, kBlocks = 4;

void drawIndex(AVFrame *f, int index)
{
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) {
            const int bit = (y * kBlocks / kHeight) * kBlocks + (x * kBlocks / kWidth);
            f->data[0][y * f->linesize[0] + x] = static_cast<std::uint8_t>((index >> bit) & 1 ? 235 : 16);
        }
    for (int p = 1; p < 3; ++p)
        for (int y = 0; y < kHeight / 2; ++y)
            std::memset(f->data[p] + y * f->linesize[p], 128, kWidth / 2);
}

// Y', Cb, Cr per quadrant (top-left, top-right, bottom-left, bottom-right),
// in the 8-bit code values of the tagged range.
constexpr int kColorPatches[4][3] = {{180, 60, 200}, {81, 90, 240}, {145, 54, 34}, {41, 240, 110}};

void drawColor(AVFrame *f)
{
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x)
            f->data[0][y * f->linesize[0] + x] = std::uint8_t(kColorPatches[(y >= kHeight / 2) * 2 + (x >= kWidth / 2)][0]);
    for (int p = 1; p < 3; ++p)
        for (int y = 0; y < kHeight / 2; ++y)
            for (int x = 0; x < kWidth / 2; ++x)
                f->data[p][y * f->linesize[p] + x] =
                    std::uint8_t(kColorPatches[(y >= kHeight / 4) * 2 + (x >= kWidth / 4)][p]);
}

int fail(const char *what)
{
    std::fprintf(stderr, "media_fixture: %s\n", what);
    return 1;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 3)
        return fail("usage: <out> <cfr|vfr|bframes|longgop|audio|tracks|unknown|color601|color709|color709full>");
    const std::string out = argv[1], kind = argv[2];
    const bool vfr = kind == "vfr";
    const bool tracks = kind == "tracks";
    const bool color = kind.starts_with("color");
    const int frames = kind == "longgop" ? 300 : color ? 12 : 48;

    AVFormatContext *fmt = nullptr;
    if (avformat_alloc_output_context2(&fmt, nullptr, "matroska", out.c_str()) < 0)
        return fail("output context");
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (!codec)
        return fail("no MPEG-4 encoder");
    AVCodecContext *enc = avcodec_alloc_context3(codec);
    enc->width = kWidth;
    enc->height = kHeight;
    enc->pix_fmt = AV_PIX_FMT_YUV420P;
    enc->time_base = vfr ? AVRational{1, 1000} : AVRational{1001, 24000};
    enc->framerate = AVRational{24000, 1001};
    enc->gop_size = kind == "longgop" ? 600 : 12; // 600 is MPEG-4's largest interval
    enc->max_b_frames = kind == "bframes" ? 2 : 0;
    enc->bit_rate = 2'000'000;
    if (color) {
        enc->colorspace = kind == "color601" ? AVCOL_SPC_SMPTE170M : AVCOL_SPC_BT709;
        enc->color_primaries = kind == "color601" ? AVCOL_PRI_SMPTE170M : AVCOL_PRI_BT709;
        enc->color_trc = kind == "color601" ? AVCOL_TRC_SMPTE170M : AVCOL_TRC_BT709;
        enc->color_range = kind == "color709full" ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
        enc->bit_rate = 8'000'000; // flat patches survive quantization
    }
    if (fmt->oformat->flags & AVFMT_GLOBALHEADER)
        enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(enc, codec, nullptr) < 0)
        return fail("video encoder");
    AVStream *vs = avformat_new_stream(fmt, nullptr);
    avcodec_parameters_from_context(vs->codecpar, enc);
    vs->time_base = enc->time_base;

    AVCodecContext *aenc = nullptr;
    AVStream *as = nullptr, *as2 = nullptr, *ss = nullptr;
    if (kind == "audio" || tracks) {
        const AVCodec *acodec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
        aenc = avcodec_alloc_context3(acodec);
        aenc->sample_rate = 48000;
        aenc->sample_fmt = AV_SAMPLE_FMT_S16;
        av_channel_layout_default(&aenc->ch_layout, 2);
        aenc->time_base = AVRational{1, 48000};
        if (avcodec_open2(aenc, acodec, nullptr) < 0)
            return fail("audio encoder");
        as = avformat_new_stream(fmt, nullptr);
        avcodec_parameters_from_context(as->codecpar, aenc);
        as->time_base = aenc->time_base;
    }
    if (tracks) {
        av_dict_set(&as->metadata, "language", "eng", 0);
        av_dict_set(&as->metadata, "title", "Main", 0);
        as2 = avformat_new_stream(fmt, nullptr);
        avcodec_parameters_from_context(as2->codecpar, aenc);
        as2->time_base = aenc->time_base;
        av_dict_set(&as2->metadata, "language", "jpn", 0);
        av_dict_set(&as2->metadata, "title", "Commentary", 0);
        ss = avformat_new_stream(fmt, nullptr);
        ss->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
        ss->codecpar->codec_id = AV_CODEC_ID_SUBRIP;
        ss->time_base = AVRational{1, 1000};
        av_dict_set(&ss->metadata, "language", "eng", 0);
        fmt->chapters = static_cast<AVChapter **>(av_calloc(2, sizeof(AVChapter *)));
        fmt->nb_chapters = 2;
        for (int c = 0; c < 2; ++c) {
            auto *chapter = static_cast<AVChapter *>(av_mallocz(sizeof(AVChapter)));
            chapter->id = c + 1;
            chapter->time_base = AVRational{1, 1000};
            chapter->start = c * 1000;
            chapter->end = (c + 1) * 1000;
            av_dict_set(&chapter->metadata, "title", c == 0 ? "Opening" : "Second", 0);
            fmt->chapters[c] = chapter;
        }
    }
    AVDictionary *muxerOptions = nullptr;
    if (kind == "unknown")
        av_dict_set(&muxerOptions, "live", "1", 0); // no Duration element, no cues
    if (avio_open(&fmt->pb, out.c_str(), AVIO_FLAG_WRITE) < 0 || avformat_write_header(fmt, &muxerOptions) < 0)
        return fail("open output");
    av_dict_free(&muxerOptions);

    AVPacket *pkt = av_packet_alloc();
    auto drain = [&](AVCodecContext *c, AVStream *s, AVFrame *f) {
        if (avcodec_send_frame(c, f) < 0)
            return false;
        while (avcodec_receive_packet(c, pkt) == 0) {
            av_packet_rescale_ts(pkt, c->time_base, s->time_base);
            pkt->stream_index = s->index;
            av_interleaved_write_frame(fmt, pkt);
        }
        return true;
    };

    AVFrame *frame = av_frame_alloc();
    frame->format = enc->pix_fmt;
    frame->width = kWidth;
    frame->height = kHeight;
    av_frame_get_buffer(frame, 0);
    std::int64_t pts = 0;
    for (int i = 0; i < frames; ++i) {
        av_frame_make_writable(frame);
        if (color)
            drawColor(frame);
        else
            drawIndex(frame, i);
        // VFR: durations cycle 30, 50, 70 ms, so frame boundaries are irregular.
        frame->pts = vfr ? pts : i;
        pts += 30 + 20 * (i % 3);
        if (!drain(enc, vs, frame))
            return fail("encode video");
    }
    drain(enc, vs, nullptr);

    if (aenc) {
        AVFrame *a = av_frame_alloc();
        a->format = aenc->sample_fmt;
        av_channel_layout_copy(&a->ch_layout, &aenc->ch_layout);
        a->sample_rate = 48000;
        a->nb_samples = 1024;
        av_frame_get_buffer(a, 0);
        for (std::int64_t s = 0; s < 96000; s += 1024) {
            av_frame_make_writable(a);
            auto *samples = reinterpret_cast<std::int16_t *>(a->data[0]);
            for (int k = 0; k < 1024; ++k) {
                const auto v = static_cast<std::int16_t>((s + k) % 32768);
                samples[2 * k] = v;
                samples[2 * k + 1] = static_cast<std::int16_t>(-v);
            }
            a->pts = s;
            if (!drain(aenc, as, a))
                return fail("encode audio");
        }
        drain(aenc, as, nullptr);
        if (as2) {
            // The same PCM, packet for packet, on the second track.
            avcodec_free_context(&aenc);
            const AVCodec *acodec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
            aenc = avcodec_alloc_context3(acodec);
            aenc->sample_rate = 48000;
            aenc->sample_fmt = AV_SAMPLE_FMT_S16;
            av_channel_layout_default(&aenc->ch_layout, 2);
            aenc->time_base = AVRational{1, 48000};
            avcodec_open2(aenc, acodec, nullptr);
            for (std::int64_t s = 0; s < 96000; s += 1024) {
                av_frame_make_writable(a);
                std::memset(a->data[0], 0, 1024 * 4);
                a->pts = s;
                if (!drain(aenc, as2, a))
                    return fail("encode second audio");
            }
            drain(aenc, as2, nullptr);
        }
        av_frame_free(&a);
        avcodec_free_context(&aenc);
    }
    if (ss) {
        const char cue[] = "Hello";
        AVPacket *sp = av_packet_alloc();
        av_new_packet(sp, sizeof cue - 1);
        std::memcpy(sp->data, cue, sizeof cue - 1);
        sp->pts = sp->dts = 500;
        sp->duration = 1000;
        sp->stream_index = ss->index;
        av_packet_rescale_ts(sp, AVRational{1, 1000}, ss->time_base);
        av_interleaved_write_frame(fmt, sp);
        av_packet_free(&sp);
    }
    av_write_trailer(fmt);
    avio_closep(&fmt->pb);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&enc);
    avformat_free_context(fmt);
    return 0;
}
