#include "operations.h"

#include "hikari/application/audio_display.h"

#include <algorithm>
#include <limits>

namespace hikari::api::detail {

namespace {

using application::MacroMediaPort;

constexpr std::int64_t kMaxRawSamples = 20'000'000;

std::unexpected<Error> noVideo()
{
    return fail(errors::kUnavailable, "No video is open.");
}

std::unexpected<Error> noAudio()
{
    return fail(errors::kUnavailable, "No audio is open.");
}

bool videoOpen(const Context &context)
{
    return context.host.media && context.host.media->videoSize();
}

Result videoInfo(const Context &context, const Json &)
{
    const MacroMediaPort *media = context.host.media;
    const auto size = media ? media->videoSize() : std::nullopt;
    if (!size)
        return Json::object({{"open", Json(false)}});
    Json keyframes = Json::array();
    for (const std::int64_t frame : media->keyframes().value_or(std::vector<std::int64_t>{}))
        keyframes.push(Json(frame));
    const auto project = media->project();
    return Json::object({{"open", Json(true)},
                         {"width", Json(size->width)},
                         {"height", Json(size->height)},
                         {"aspect", Json::array({Json(size->arX), Json(size->arY)})},
                         {"frameShown", Json(project ? project->videoFrameShown : 0)},
                         {"keyframes", std::move(keyframes)}});
}

Result frameAtTime(const Context &context, const Json &params)
{
    const auto frame = context.host.media ? context.host.media->frameFromMs(params.find("time")->asInt()) : std::nullopt;
    if (!frame)
        return noVideo();
    return Json::object({{"frame", Json(*frame)}});
}

Result timeOfFrame(const Context &context, const Json &params)
{
    const auto time = context.host.media ? context.host.media->msFromFrame(params.find("frame")->asInt()) : std::nullopt;
    if (!time)
        return noVideo();
    return Json::object({{"time", Json(*time)}});
}

struct Rect {
    int x = 0, y = 0, width = 0, height = 0;
};

std::optional<Rect> cropOf(const Json &params, int width, int height)
{
    const Json *crop = params.find("crop");
    if (!crop)
        return Rect{0, 0, width, height};
    const auto at = [&](const char *key) { return crop->find(key)->asInt(); };
    const std::int64_t x = at("x"), y = at("y");
    const std::int64_t right = std::min<std::int64_t>(x + at("width"), width);
    const std::int64_t bottom = std::min<std::int64_t>(y + at("height"), height);
    if (x >= right || y >= bottom)
        return std::nullopt;
    return Rect{int(x), int(y), int(right - x), int(bottom - y)};
}

std::string scaled(const MacroMediaPort::Frame &frame, Rect crop, int outWidth, int outHeight)
{
    std::string out(static_cast<std::size_t>(outWidth) * outHeight * 4, '\0');
    for (int y = 0; y < outHeight; ++y) {
        const int sy = crop.y + int((std::int64_t(y) * 2 + 1) * crop.height / (std::int64_t(outHeight) * 2));
        for (int x = 0; x < outWidth; ++x) {
            const int sx = crop.x + int((std::int64_t(x) * 2 + 1) * crop.width / (std::int64_t(outWidth) * 2));
            const std::byte *from = frame.bgra.data() + (static_cast<std::size_t>(sy) * frame.width + sx) * 4;
            std::copy_n(reinterpret_cast<const char *>(from), 4, out.data() + (static_cast<std::size_t>(y) * outWidth + x) * 4);
        }
    }
    return out;
}

void videoFrame(const Context &context, const Json &params, Reply reply)
{
    MacroMediaPort *media = context.host.media;
    if (!videoOpen(context))
        return reply(noVideo());
    const auto project = media->project();
    const std::int64_t index = params.find("frame") ? params.find("frame")->asInt() : project ? project->videoFrameShown : 0;
    const bool subtitles = params.find("subtitles") ? params.find("subtitles")->asBool() : true;
    const int maxEdge = params.find("maxEdge") ? int(params.find("maxEdge")->asInt()) : 1024;
    media->frame(index, subtitles, [params, index, maxEdge, reply](std::optional<MacroMediaPort::Frame> frame) {
        if (!frame || frame->width <= 0 || frame->height <= 0)
            return reply(fail(errors::kUnavailable, "Frame " + std::to_string(index) + " could not be decoded.",
                              Json::object({{"frame", Json(index)}})));
        const auto crop = cropOf(params, frame->width, frame->height);
        if (!crop)
            return reply(fail(errors::kInvalidParams, "Parameter /crop: it lies outside the frame.",
                              Json::object({{"pointer", Json("/crop")}})));
        const double scale = std::min(1.0, double(maxEdge) / std::max(crop->width, crop->height));
        const int width = std::max(1, int(crop->width * scale));
        const int height = std::max(1, int(crop->height * scale));
        reply(Json::object({{"frame", Json(index)},
                            {"width", Json(width)},
                            {"height", Json(height)},
                            {"format", Json("bgra")},
                            {"data", Json(base64(scaled(*frame, *crop, width, height)))}}));
    });
}

Result audioInfo(const Context &context, const Json &)
{
    const auto audio = context.host.media ? context.host.media->peakAudio() : std::nullopt;
    if (!audio)
        return Json::object({{"open", Json(false)}});
    if (!*audio || (*audio)->sampleRate() <= 0)
        return Json::object({{"open", Json(true)}, {"ready", Json(false)}});
    const application::DisplayAudio &a = **audio;
    return Json::object({{"open", Json(true)},
                         {"ready", Json(a.finished())},
                         {"duration", Json(a.sampleCount() * 1000 / a.sampleRate())},
                         {"sampleRate", Json(a.sampleRate())},
                         {"channels", Json(a.channels())}});
}

Result audioPeaks(const Context &context, const Json &params)
{
    const auto audio = context.host.media ? context.host.media->peakAudio() : std::nullopt;
    if (!audio)
        return noAudio();
    if (!*audio || (*audio)->sampleRate() <= 0)
        return fail(errors::kRefused, "The audio is still loading.", Json::object({{"reason", Json("loading")}}));
    const application::DisplayAudio &a = **audio;
    const std::int64_t start = params.find("start")->asInt(), end = params.find("end")->asInt();
    if (end <= start)
        return fail(errors::kInvalidParams, "Parameter /end: it must be after start.",
                    Json::object({{"pointer", Json("/end")}}));
    const int buckets = params.find("buckets") ? int(params.find("buckets")->asInt()) : 256;
    const std::int64_t first = start * a.sampleRate() / 1000;
    const std::int64_t count = std::max<std::int64_t>(1, (end - start) * a.sampleRate() / 1000);
    const application::WaveformPeaks *table = a.peaks();
    const bool fromTable = table && count / buckets >= table->blockSamples();
    if (!fromTable && count > kMaxRawSamples)
        return fail(errors::kRefused, "The audio is still loading; ask for a shorter range or wait.",
                    Json::object({{"reason", Json("loading")}}));
    std::vector<std::int16_t> raw;
    if (!fromTable) {
        raw.resize(static_cast<std::size_t>(count));
        a.read(first, count, raw.data());
    }
    Json lows = Json::array(), highs = Json::array();
    for (int i = 0; i < buckets; ++i) {
        const std::int64_t from = count * i / buckets, to = std::max(from + 1, count * (i + 1) / buckets);
        std::int16_t lo = 0, hi = 0;
        if (fromTable) {
            table->range(first + from, to - from, &lo, &hi);
        } else {
            const auto [mn, mx] = std::minmax_element(raw.begin() + from, raw.begin() + std::min(to, count));
            lo = *mn;
            hi = *mx;
        }
        lows.push(Json(lo));
        highs.push(Json(hi));
    }
    return Json::object({{"start", Json(start)},
                         {"end", Json(end)},
                         {"min", std::move(lows)},
                         {"max", std::move(highs)}});
}

}

void addMediaOperations(Registry &registry)
{
    addOperation(registry, Access::Read, "video.info",
                 "Whether a video is open, its size and aspect ratio, the frame shown and the keyframes.",
                 R"({"type": "object", "additionalProperties": false})",
                 R"({"type": "object", "properties": {"open": {"type": "boolean"},
                     "width": {"type": "integer", "minimum": 1}, "height": {"type": "integer", "minimum": 1},
                     "aspect": {"type": "array", "items": {"type": "integer", "minimum": 1}, "minItems": 2, "maxItems": 2},
                     "frameShown": {"type": "integer", "minimum": 0},
                     "keyframes": {"type": "array", "items": {"type": "integer", "minimum": 0}}},
                     "required": ["open"], "additionalProperties": false})",
                 now(videoInfo));
    addOperation(registry, Access::Read, "video.frame_at_time", "The video frame shown at a time.",
                 R"({"type": "object", "properties": {"time": {"type": "integer", "minimum": 0,
                     "description": "Milliseconds."}}, "required": ["time"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {"frame": {"type": "integer", "minimum": 0}},
                     "required": ["frame"], "additionalProperties": false})",
                 now(frameAtTime));
    addOperation(registry, Access::Read, "video.time_of_frame", "The time a video frame starts, in milliseconds.",
                 R"({"type": "object", "properties": {"frame": {"type": "integer", "minimum": 0}},
                     "required": ["frame"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {"time": {"type": "integer"}},
                     "required": ["time"], "additionalProperties": false})",
                 now(timeOfFrame));
    addOperation(registry, Access::Read, "video.frame",
                 "One video frame as BGRA pixels in base64, optionally cropped, scaled to fit maxEdge.",
                 R"({"type": "object", "properties": {
                     "frame": {"type": "integer", "minimum": 0, "description": "Default: the frame shown."},
                     "subtitles": {"type": "boolean", "description": "Render the subtitles on it. Default true."},
                     "crop": {"type": "object", "properties": {"x": {"type": "integer", "minimum": 0},
                         "y": {"type": "integer", "minimum": 0}, "width": {"type": "integer", "minimum": 1},
                         "height": {"type": "integer", "minimum": 1}},
                         "required": ["x", "y", "width", "height"], "additionalProperties": false},
                     "maxEdge": {"type": "integer", "minimum": 16, "maximum": 4096, "description": "Default 1024."}},
                     "additionalProperties": false})",
                 R"({"type": "object", "properties": {"frame": {"type": "integer", "minimum": 0},
                     "width": {"type": "integer", "minimum": 1}, "height": {"type": "integer", "minimum": 1},
                     "format": {"const": "bgra"},
                     "data": {"type": "string", "description": "width * 4 bytes per row, top row first, base64."}},
                     "required": ["frame", "width", "height", "format", "data"], "additionalProperties": false})",
                 videoFrame);
    addOperation(registry, Access::Read, "audio.info", "Whether audio is open and loaded, its duration and sample rate.",
                 R"({"type": "object", "additionalProperties": false})",
                 R"({"type": "object", "properties": {"open": {"type": "boolean"}, "ready": {"type": "boolean"},
                     "duration": {"type": "integer", "minimum": 0}, "sampleRate": {"type": "integer", "minimum": 1},
                     "channels": {"type": "integer", "minimum": 1}},
                     "required": ["open"], "additionalProperties": false})",
                 now(audioInfo));
    addOperation(registry, Access::Read, "audio.peaks",
                 "The lowest and highest 16-bit sample in each of `buckets` equal slices of a time range.",
                 R"({"type": "object", "properties": {"start": {"type": "integer", "minimum": 0},
                     "end": {"type": "integer", "minimum": 1},
                     "buckets": {"type": "integer", "minimum": 1, "maximum": 4096, "description": "Default 256."}},
                     "required": ["start", "end"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {"start": {"type": "integer"}, "end": {"type": "integer"},
                     "min": {"type": "array", "items": {"type": "integer", "minimum": -32768, "maximum": 32767}},
                     "max": {"type": "array", "items": {"type": "integer", "minimum": -32768, "maximum": 32767}}},
                     "required": ["start", "end", "min", "max"], "additionalProperties": false})",
                 now(audioPeaks));
}

}
