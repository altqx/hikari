#include "operations.h"

#include "hikari/application/audio_display.h"
#include "hikari/application/document_overlay.h"
#include "hikari/core/frame_shrink.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace hikari::api::detail {

using application::MacroMediaPort;

std::unexpected<Error> noVideo()
{
    return fail(errors::kUnavailable, "No video is open.");
}

namespace {

std::unexpected<Error> noAudio()
{
    return fail(errors::kUnavailable, "No audio is open.");
}

}

bool videoOpen(const Context &context)
{
    return context.host.media && context.host.media->videoSize();
}

namespace {

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

Json rectJson(Rect r)
{
    return Json::object({{"x", Json(r.x)}, {"y", Json(r.y)}, {"width", Json(r.width)}, {"height", Json(r.height)}});
}

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

}

const core::LineRecord *findLine(const core::Document &document, core::LineId id)
{
    for (const core::LineRecord *line : document.lines())
        if (line->id.value == id.value)
            return line;
    return nullptr;
}

std::unexpected<Error> noLine(core::LineId id)
{
    return fail(errors::kNotFound, "No Line has id " + std::to_string(id.value) + ".",
                Json::object({{"line", Json(std::int64_t(id.value))}}));
}

std::unexpected<Error> cannotDraw()
{
    return fail(errors::kUnavailable, "Subtitles cannot be drawn apart from the video here.");
}

std::unexpected<Error> notDrawn(std::int64_t frame)
{
    return fail(errors::kUnavailable, "The subtitles of frame " + std::to_string(frame) + " could not be drawn.",
                Json::object({{"frame", Json(frame)}}));
}

namespace {

struct Drawn {
    Target document;
    std::optional<core::LineId> line;
};

std::expected<Drawn, Error> drawn(const Context &context, const Json &params)
{
    auto document = target(context, params);
    if (!document)
        return std::unexpected(document.error());
    const Json *line = params.find("line");
    if (!line)
        return Drawn{*document, std::nullopt};
    const core::LineId id{std::uint64_t(line->asInt())};
    if (!findLine(document->session->document(), id))
        return noLine(id);
    return Drawn{*document, id};
}

}

std::int64_t frameOf(const MacroMediaPort &media, const Json &params, const core::LineRecord *record)
{
    if (const Json *frame = params.find("frame"))
        return frame->asInt();
    const auto project = media.project();
    const std::int64_t shown = project ? project->videoFrameShown : 0;
    if (!record)
        return shown;
    const std::int64_t start = record->start.value.microseconds() / 1000, end = record->end.value.microseconds() / 1000;
    if (const auto at = media.msFromFrame(shown); at && *at >= start && *at < end)
        return shown;
    std::int64_t first = media.frameFromMs(start).value_or(0);
    if (const auto at = media.msFromFrame(first); at && *at < start)
        ++first;
    return first;
}

namespace {

std::optional<application::OverlayFrame> drawLines(const Host &host, application::DocumentId document,
                                                   std::optional<core::LineId> line, std::int64_t frame, int width,
                                                   int height)
{
    const application::EditSession *session = host.documents ? host.documents->session(document) : nullptr;
    if (!session)
        return std::nullopt;
    if (!line)
        return host.render->render(document, session->document(), frame, width, height);
    return host.render->render(document, application::withOnlyLine(session->document(), *line), frame, width, height);
}

void videoFrame(const Context &context, const Json &params, Reply reply)
{
    MacroMediaPort *media = context.host.media;
    if (!videoOpen(context))
        return reply(noVideo());
    const bool subtitles = params.find("subtitles") ? params.find("subtitles")->asBool() : true;
    std::optional<Drawn> only;
    if (params.find("line")) {
        if (!subtitles)
            return reply(fail(errors::kInvalidParams, "Parameter /line: subtitles is false, so no Line is drawn.",
                              Json::object({{"pointer", Json("/line")}})));
        if (!context.host.render)
            return reply(cannotDraw());
        auto found = drawn(context, params);
        if (!found)
            return reply(std::unexpected(found.error()));
        only = *found;
    }
    const std::int64_t index =
        frameOf(*media, params, only ? findLine(only->document.session->document(), *only->line) : nullptr);
    const int maxEdge = params.find("maxEdge") ? int(params.find("maxEdge")->asInt()) : 1024;
    const std::int64_t maxPixels = params.find("maxPixels") ? params.find("maxPixels")->asInt() : 0;
    const std::optional<application::DocumentId> document = only ? std::optional(only->document.id) : std::nullopt;
    const std::optional<core::LineId> line = only ? only->line : std::nullopt;
    media->frame(index, subtitles && !only, [host = context.host, params, index, maxEdge, maxPixels, document, line,
                                             reply](std::optional<MacroMediaPort::Frame> frame) {
        if (!frame || frame->width <= 0 || frame->height <= 0)
            return reply(fail(errors::kUnavailable, "Frame " + std::to_string(index) + " could not be decoded.",
                              Json::object({{"frame", Json(index)}})));
        if (line) {
            const auto overlay = drawLines(host, *document, line, index, frame->width, frame->height);
            if (!overlay)
                return reply(notDrawn(index));
            application::compositeOverlay(*overlay, reinterpret_cast<std::uint8_t *>(frame->bgra.data()), frame->width,
                                          frame->height, std::ptrdiff_t(frame->width) * 4);
        }
        const auto crop = cropOf(params, frame->width, frame->height);
        if (!crop)
            return reply(fail(errors::kInvalidParams, "Parameter /crop: it lies outside the frame.",
                              Json::object({{"pointer", Json("/crop")}})));
        double scale = std::min(1.0, double(maxEdge) / std::max(crop->width, crop->height));
        if (maxPixels > 0)
            scale = std::min(scale, std::sqrt(double(maxPixels) / (double(crop->width) * crop->height)));
        const int width = std::max(1, int(crop->width * scale));
        const int height = std::max(1, int(crop->height * scale));
        reply(Json::object({{"frame", Json(index)},
                            {"width", Json(width)},
                            {"height", Json(height)},
                            {"crop", rectJson(*crop)},
                            {"format", Json("bgra")},
                            {"data", Json(base64(scaled(*frame, *crop, width, height)))}}));
    });
}

Result inkBounds(const Context &context, const Json &params)
{
    MacroMediaPort *media = context.host.media;
    if (!videoOpen(context))
        return noVideo();
    if (!context.host.render)
        return cannotDraw();
    const auto found = drawn(context, params);
    if (!found)
        return std::unexpected(found.error());
    const std::int64_t index =
        frameOf(*media, params, found->line ? findLine(found->document.session->document(), *found->line) : nullptr);
    const auto size = media->videoSize();
    const auto overlay = drawLines(context.host, found->document.id, found->line, index, size->width, size->height);
    if (!overlay)
        return notDrawn(index);
    const auto bounds = application::inkBounds(*overlay);
    return Json::object({{"frame", Json(index)},
                         {"bounds", bounds ? rectJson({bounds->x, bounds->y, bounds->width, bounds->height}) : Json()}});
}

std::string assColour(int r, int g, int b)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out = "&H";
    for (const int channel : {b, g, r}) {
        out += kHex[channel >> 4];
        out += kHex[channel & 15];
    }
    return out + "&";
}

void videoColours(const Context &context, const Json &params, Reply reply)
{
    MacroMediaPort *media = context.host.media;
    if (!videoOpen(context))
        return reply(noVideo());
    const auto size = media->videoSize();
    const auto &points = params.find("points")->asArray();
    for (std::size_t i = 0; i < points.size(); ++i)
        if (points[i].find("x")->asInt() >= size->width || points[i].find("y")->asInt() >= size->height) {
            const std::string pointer = "/points/" + std::to_string(i);
            return reply(fail(errors::kInvalidParams, "Parameter " + pointer + ": it lies outside the frame.",
                              Json::object({{"pointer", Json(pointer)}})));
        }
    const auto project = media->project();
    const std::int64_t index = params.find("frame") ? params.find("frame")->asInt() : project ? project->videoFrameShown : 0;
    const int radius = params.find("radius") ? int(params.find("radius")->asInt()) : 0;
    const bool subtitles = params.find("subtitles") && params.find("subtitles")->asBool();
    media->frame(index, subtitles, [points, index, radius, reply](std::optional<MacroMediaPort::Frame> frame) {
        if (!frame || frame->width <= 0 || frame->height <= 0)
            return reply(fail(errors::kUnavailable, "Frame " + std::to_string(index) + " could not be decoded.",
                              Json::object({{"frame", Json(index)}})));
        Json colours = Json::array();
        for (const Json &point : points) {
            const int x = int(point.find("x")->asInt()), y = int(point.find("y")->asInt());
            std::int64_t sum[3] = {0, 0, 0}, count = 0;
            for (int sy = std::max(0, y - radius); sy <= std::min(frame->height - 1, y + radius); ++sy)
                for (int sx = std::max(0, x - radius); sx <= std::min(frame->width - 1, x + radius); ++sx) {
                    const std::byte *p = frame->bgra.data() + (static_cast<std::size_t>(sy) * frame->width + sx) * 4;
                    for (int c = 0; c < 3; ++c)
                        sum[c] += std::to_integer<int>(p[c]);
                    ++count;
                }
            const auto mean = [&](int c) { return int((sum[c] * 2 + count) / (count * 2)); };
            colours.push(Json::object({{"x", Json(x)}, {"y", Json(y)}, {"colour", Json(assColour(mean(2), mean(1), mean(0)))}}));
        }
        reply(Json::object({{"frame", Json(index)}, {"colours", std::move(colours)}}));
    });
}

constexpr std::size_t kMaxKeyframes = 500;

Result videoKeyframes(const Context &context, const Json &params)
{
    MacroMediaPort *media = context.host.media;
    if (!videoOpen(context))
        return noVideo();
    const std::int64_t from = params.find("from") ? params.find("from")->asInt() : 0;
    const std::int64_t to = params.find("to") ? params.find("to")->asInt() : std::numeric_limits<std::int64_t>::max();
    Json keyframes = Json::array();
    std::int64_t total = 0;
    for (const std::int64_t frame : media->keyframes().value_or(std::vector<std::int64_t>{})) {
        const std::int64_t time = media->msFromFrame(frame).value_or(0);
        if (time < from || time > to)
            continue;
        if (std::size_t(total++) < kMaxKeyframes)
            keyframes.push(Json::object({{"frame", Json(frame)}, {"time", Json(time)}}));
    }
    return Json::object({{"keyframes", std::move(keyframes)}, {"total", Json(total)}});
}

constexpr std::int64_t kMostFrames = 256;
constexpr std::int64_t kMostFrameBytes = 16 * 1024 * 1024;

struct FrameBatch : std::enable_shared_from_this<FrameBatch> {
    MacroMediaPort *media = nullptr;
    std::int64_t to = 0, stride = 1;
    int maxEdge = 64;
    bool luma = true, subtitles = false;
    Reply reply;
    Json frames = Json::array();
    int width = 0, height = 0;

    void step(std::int64_t index)
    {
        if (index > to)
            return reply(Json::object({{"width", Json(width)},
                                       {"height", Json(height)},
                                       {"format", Json(luma ? "luma" : "bgra")},
                                       {"frames", std::move(frames)}}));
        media->frame(index, subtitles, [self = shared_from_this(), index](std::optional<MacroMediaPort::Frame> frame) {
            if (!frame || frame->width <= 0 || frame->height <= 0)
                return self->reply(fail(errors::kUnavailable, "Frame " + std::to_string(index) + " could not be decoded.",
                                        Json::object({{"frame", Json(index)}})));
            const core::ShrunkFrame shrunk = core::shrinkFrame(frame->bgra.data(), frame->width, frame->height,
                                                               std::size_t(frame->width) * 4, self->maxEdge, self->luma);
            self->width = shrunk.width;
            self->height = shrunk.height;
            self->frames.push(Json::object({{"frame", Json(index)}, {"data", Json(base64(shrunk.bytes))}}));
            self->step(index + self->stride);
        });
    }
};

void videoFrames(const Context &context, const Json &params, Reply reply)
{
    MacroMediaPort *media = context.host.media;
    if (!videoOpen(context))
        return reply(noVideo());
    const std::int64_t from = params.find("from")->asInt(), to = params.find("to")->asInt();
    const std::int64_t stride = params.find("stride") ? params.find("stride")->asInt() : 1;
    const int maxEdge = params.find("maxEdge") ? int(params.find("maxEdge")->asInt()) : 64;
    const bool luma = !params.find("format") || params.find("format")->asString() == "luma";
    const bool subtitles = params.find("subtitles") && params.find("subtitles")->asBool();
    if (to < from)
        return reply(fail(errors::kInvalidParams, "Parameter /to: it must not be before from.", Json::object({{"pointer", Json("/to")}})));
    const std::int64_t count = (to - from) / stride + 1;
    const auto size = media->videoSize();
    const std::int64_t edge = size ? std::max(size->width, size->height) : maxEdge;
    const std::int64_t scale = std::min<std::int64_t>(maxEdge, edge);
    const std::int64_t perFrame = scale * scale * (luma ? 1 : 4);
    if (count > kMostFrames || count * perFrame > kMostFrameBytes)
        return reply(fail(errors::kInvalidParams,
                          "Parameter /to: at most 256 frames and 16 MiB of pixels come in one call; ask for fewer or a larger stride.",
                          Json::object({{"pointer", Json("/to")}})));
    auto batch = std::make_shared<FrameBatch>();
    batch->media = media;
    batch->to = to;
    batch->stride = stride;
    batch->maxEdge = maxEdge;
    batch->luma = luma;
    batch->subtitles = subtitles;
    batch->reply = std::move(reply);
    const MacroMediaPort::SmallFramesRequest request{from, to, stride, maxEdge, luma};
    const bool offered = !subtitles && media->smallFrames(request, [batch, from](std::optional<MacroMediaPort::SmallFrames> small) {
        if (!small)
            return batch->step(from);
        Json frames = Json::array();
        for (std::size_t i = 0; i < small->pixels.size(); ++i)
            frames.push(Json::object({{"frame", Json(from + std::int64_t(i) * batch->stride)}, {"data", Json(base64(small->pixels[i]))}}));
        batch->reply(Json::object({{"width", Json(small->width)},
                                   {"height", Json(small->height)},
                                   {"format", Json(batch->luma ? "luma" : "bgra")},
                                   {"frames", std::move(frames)}}));
    });
    if (!offered)
        batch->step(from);
}

constexpr std::size_t kMostKeyframes = 100000;

Result setKeyframes(const Context &context, const Json &params)
{
    if (!videoOpen(context))
        return noVideo();
    if (!context.host.keyframes)
        return fail(errors::kUnavailable, "Keyframes can't be set here.");
    std::vector<std::int64_t> frames;
    for (const Json &f : params.find("frames")->asArray())
        frames.push_back(f.asInt());
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    if (frames.size() > kMostKeyframes)
        return fail(errors::kInvalidParams, "Parameter /frames: at most 100000 keyframes.", Json::object({{"pointer", Json("/frames")}}));
    std::optional<std::string> path;
    Json written;
    if (const Json *file = params.find("file")) {
        const std::string &handle = file->asString();
        const auto granted = std::find_if(context.files.begin(), context.files.end(),
                                          [&](const GrantedFile &g) { return g.handle == handle; });
        if (granted == context.files.end())
            return fail(errors::kNotGranted, "The file isn't granted to the caller.", Json::object({{"file", Json(handle)}}));
        if (!granted->writable)
            return fail(errors::kReadOnly, "The file is granted read-only.", Json::object({{"file", Json(handle)}}));
        path = granted->path;
        written = Json(handle);
    }
    if (const auto problem = context.host.keyframes->setKeyframes(frames, path))
        return fail(errors::kRefused, *problem, Json::object({{"reason", Json("not_set")}}));
    Json result = Json::object({{"count", Json(std::int64_t(frames.size()))}});
    if (path)
        result["file"] = written;
    return result;
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
    const auto peaks = application::audioPeaks(a, start, end, buckets);
    if (!peaks)
        return fail(errors::kRefused, "The audio is still loading; ask for a shorter range or wait.",
                    Json::object({{"reason", Json("loading")}}));
    Json lows = Json::array(), highs = Json::array();
    for (std::size_t i = 0; i < peaks->min.size(); ++i) {
        lows.push(Json(peaks->min[i]));
        highs.push(Json(peaks->max[i]));
    }
    return Json::object({{"start", Json(start)},
                         {"end", Json(end)},
                         {"min", std::move(lows)},
                         {"max", std::move(highs)}});
}

constexpr std::string_view kRect = R"({"type": "object", "properties": {"x": {"type": "integer", "minimum": 0},
    "y": {"type": "integer", "minimum": 0}, "width": {"type": "integer", "minimum": 1},
    "height": {"type": "integer", "minimum": 1}}, "required": ["x", "y", "width", "height"], "additionalProperties": false})";

std::string withRects(std::string_view text)
{
    std::string out(text);
    for (std::size_t at = out.find("@rect"); at != std::string::npos; at = out.find("@rect", at + kRect.size()))
        out.replace(at, 5, kRect);
    return out;
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
                 "One video frame as BGRA pixels in base64, with every Line, one Line or none drawn on it, optionally "
                 "cropped, scaled to fit maxEdge and maxPixels.",
                 withRects(R"({"type": "object", "properties": {
                     "frame": {"type": "integer", "minimum": 0,
                         "description": "Default: the frame shown, or with line the Line's first frame when it is not on the frame shown."},
                     "subtitles": {"type": "boolean", "description": "Render the subtitles on it. Default true."},
                     "document": {"$ref": "#/$defs/DocumentId", "description": "line's Document. Defaults to the active Document."},
                     "line": {"$ref": "#/$defs/LineId", "description": "Draw this Line alone."},
                     "crop": @rect,
                     "maxEdge": {"type": "integer", "minimum": 16, "maximum": 4096, "description": "Default 1024."},
                     "maxPixels": {"type": "integer", "minimum": 256, "maximum": 16777216,
                         "description": "The most pixels the image may have. No limit when omitted."}},
                     "additionalProperties": false})"),
                 withRects(R"({"type": "object", "properties": {"frame": {"type": "integer", "minimum": 0},
                     "width": {"type": "integer", "minimum": 1}, "height": {"type": "integer", "minimum": 1},
                     "crop": @rect,
                     "format": {"const": "bgra"},
                     "data": {"type": "string", "description": "width * 4 bytes per row, top row first, base64."}},
                     "required": ["frame", "width", "height", "crop", "format", "data"], "additionalProperties": false})"),
                 videoFrame);
    addOperation(registry, Access::Read, "video.ink_bounds",
                 "Where one Line's ink, or every Line's, lies on a video frame, in video pixels.",
                 R"({"type": "object", "properties": {
                     "frame": {"type": "integer", "minimum": 0,
                         "description": "Default: the frame shown, or with line the Line's first frame when it is not on the frame shown."},
                     "document": {"$ref": "#/$defs/DocumentId", "description": "Defaults to the active Document."},
                     "line": {"$ref": "#/$defs/LineId", "description": "This Line alone. Every Line when omitted."}},
                     "additionalProperties": false})",
                 withRects(R"({"type": "object", "properties": {"frame": {"type": "integer", "minimum": 0},
                     "bounds": {"anyOf": [@rect, {"type": "null"}], "description": "Null when nothing is drawn."}},
                     "required": ["frame", "bounds"], "additionalProperties": false})"),
                 now(inkBounds));
    addOperation(registry, Access::Read, "video.colours",
                 "The colours of a video frame at points, as ASS &HBBGGRR&, each the mean of the square radius pixels around it.",
                 R"({"type": "object", "properties": {
                     "frame": {"type": "integer", "minimum": 0, "description": "Default: the frame shown."},
                     "points": {"type": "array", "minItems": 1, "maxItems": 256, "items": {"type": "object",
                         "properties": {"x": {"type": "integer", "minimum": 0}, "y": {"type": "integer", "minimum": 0}},
                         "required": ["x", "y"], "additionalProperties": false}},
                     "radius": {"type": "integer", "minimum": 0, "maximum": 32, "description": "Default 0, the pixel alone."},
                     "subtitles": {"type": "boolean", "description": "Sample with the subtitles drawn. Default false."}},
                     "required": ["points"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {"frame": {"type": "integer", "minimum": 0},
                     "colours": {"type": "array", "items": {"type": "object", "properties": {
                         "x": {"type": "integer"}, "y": {"type": "integer"},
                         "colour": {"type": "string", "pattern": "^&H[0-9A-F]{6}&$"}},
                         "required": ["x", "y", "colour"], "additionalProperties": false}}},
                     "required": ["frame", "colours"], "additionalProperties": false})",
                 videoColours);
    addOperation(registry, Access::Read, "video.keyframes", "The keyframes from one time to another, each with its start time.",
                 R"({"type": "object", "properties": {
                     "from": {"type": "integer", "minimum": 0, "description": "Milliseconds. Default 0."},
                     "to": {"type": "integer", "minimum": 0, "description": "Milliseconds. Default: the end."}},
                     "additionalProperties": false})",
                 R"({"type": "object", "properties": {
                     "keyframes": {"type": "array", "maxItems": 500, "items": {"type": "object", "properties": {
                         "frame": {"type": "integer", "minimum": 0}, "time": {"type": "integer"}},
                         "required": ["frame", "time"], "additionalProperties": false}},
                     "total": {"type": "integer", "minimum": 0, "description": "Every keyframe in the range; the first 500 are listed."}},
                     "required": ["keyframes", "total"], "additionalProperties": false})",
                 now(videoKeyframes));
    addOperation(registry, Access::Read, "video.frames",
                 "Video frames from one to another, every stride-th, scaled to fit maxEdge, as luma or BGRA in base64. At "
                 "most 256 frames and 16 MiB of pixels come in one call.",
                 R"({"type": "object", "properties": {
                     "from": {"type": "integer", "minimum": 0}, "to": {"type": "integer", "minimum": 0},
                     "stride": {"type": "integer", "minimum": 1, "description": "Default 1, every frame."},
                     "maxEdge": {"type": "integer", "minimum": 16, "maximum": 512, "description": "Default 64."},
                     "format": {"enum": ["luma", "bgra"], "description": "Default luma, one byte per pixel."},
                     "subtitles": {"type": "boolean", "description": "Render the subtitles on them. Default false."}},
                     "required": ["from", "to"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {
                     "width": {"type": "integer", "minimum": 1}, "height": {"type": "integer", "minimum": 1},
                     "format": {"enum": ["luma", "bgra"]},
                     "frames": {"type": "array", "items": {"type": "object", "properties": {
                         "frame": {"type": "integer", "minimum": 0},
                         "data": {"type": "string", "description": "width bytes per row for luma, width * 4 for bgra; top row first; base64."}},
                         "required": ["frame", "data"], "additionalProperties": false}}},
                     "required": ["width", "height", "format", "frames"], "additionalProperties": false})",
                 videoFrames);
    addOperation(registry, Access::Write, "video.set_keyframes",
                 "Replaces the video's keyframes as Open keyframes does. With file, writes them there as an Aegisub "
                 "keyframes file first and opens it. Not an undo step.",
                 R"({"type": "object", "properties": {
                     "frames": {"type": "array", "maxItems": 100000, "items": {"type": "integer", "minimum": 0}},
                     "file": {"type": "string", "description": "A writable file handle, from files.pick or files.open."}},
                     "required": ["frames"], "additionalProperties": false})",
                 R"({"type": "object", "properties": {"count": {"type": "integer", "minimum": 0},
                     "file": {"type": "string"}}, "required": ["count"], "additionalProperties": false})",
                 now(setKeyframes));
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
