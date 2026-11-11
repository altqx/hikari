#include "hikari/application/video_analysis.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <set>

namespace hikari::application {

namespace {

std::u8string u8(std::string_view s)
{
    return {s.begin(), s.end()};
}

std::u8string number(std::int64_t n)
{
    return u8(std::to_string(n));
}

bool isHex(char8_t c)
{
    return (c >= u8'0' && c <= u8'9') || (c >= u8'a' && c <= u8'f') || (c >= u8'A' && c <= u8'F');
}

// The length of `\<name>&H<hex>[&]` at `at`, or 0.
std::size_t hexTagAt(std::u8string_view text, std::size_t at, std::u8string_view name)
{
    std::size_t i = at + 1 + name.size();
    if (text.substr(at, 1) != u8"\\" || text.substr(at + 1, name.size()) != name || text.substr(i, 2) != u8"&H")
        return 0;
    i += 2;
    const std::size_t digits = i;
    while (i < text.size() && isHex(text[i]))
        ++i;
    if (i == digits)
        return 0;
    if (i < text.size() && text[i] == u8'&')
        ++i;
    return i - at;
}

// Lua's gsub of `\\<name>&H[%x]+&?` with "" over the whole of `block`.
std::u8string removeHexTag(std::u8string_view block, std::u8string_view name)
{
    std::u8string out;
    for (std::size_t i = 0; i < block.size();) {
        if (const auto n = hexTagAt(block, i, name)) {
            i += n;
            continue;
        }
        out += block[i++];
    }
    return out;
}

double brightness(KeyColour c)
{
    return 0.299 * c.r + 0.587 * c.g + 0.114 * c.b;
}

constexpr std::array<std::u8string_view, 4> kColourTags{u8"\\c", u8"\\2c", u8"\\3c", u8"\\4c"};
constexpr std::array<std::u8string_view, 4> kAlphaTags{u8"\\1a", u8"\\2a", u8"\\3a", u8"\\4a"};

std::u8string valueTags(const ColourTrackOptions &options, std::u8string_view value)
{
    std::u8string out;
    const auto add = [&](std::u8string_view tag, std::u8string_view prefix, std::u8string_view suffix) {
        out.append(tag).append(prefix).append(value).append(suffix);
    };
    if (!options.alpha) {
        for (std::size_t i = 0; i < 4; ++i)
            if (options.colours[i])
                add(kColourTags[i], u8"", u8"");
        return out;
    }
    if (options.allAlpha)
        add(u8"\\alpha", u8"&H", u8"&");
    else
        for (std::size_t i = 0; i < 4; ++i)
            if (options.alphas[i])
                add(kAlphaTags[i], u8"&H", u8"&");
    return out;
}

std::optional<double> parseNumber(std::u8string_view text, std::size_t &at)
{
    while (at < text.size() && text[at] == u8' ')
        ++at;
    const auto *begin = reinterpret_cast<const char *>(text.data()) + at;
    const auto *end = reinterpret_cast<const char *>(text.data()) + text.size();
    double value = 0;
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr == begin)
        return std::nullopt;
    at += static_cast<std::size_t>(ptr - begin);
    return value;
}

struct Span {
    std::size_t at = 0, size = 0;
};

// Every \clip(…) and \iclip(…) inside override blocks.
std::vector<Span> clipTags(std::u8string_view text)
{
    std::vector<Span> out;
    bool inBlock = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == u8'{')
            inBlock = true;
        else if (text[i] == u8'}')
            inBlock = false;
        if (!inBlock || text[i] != u8'\\')
            continue;
        const auto rest = text.substr(i + 1);
        const std::size_t name = rest.starts_with(u8"clip(") ? 5 : rest.starts_with(u8"iclip(") ? 6 : 0;
        if (!name)
            continue;
        const auto close = text.find(u8')', i + 1 + name);
        if (close == std::u8string_view::npos)
            continue;
        out.push_back({i, close + 1 - i});
    }
    return out;
}

bool isVectorClip(std::u8string_view tag)
{
    return tag.find(u8',') == std::u8string_view::npos;
}

struct FadeTag {
    Span span;
    std::int64_t in = 0, out = 0;
};

std::optional<FadeTag> fadeTag(std::u8string_view text)
{
    for (std::size_t at = text.find(u8"\\fad("); at != std::u8string_view::npos; at = text.find(u8"\\fad(", at + 1)) {
        std::size_t i = at + 5;
        const auto in = parseNumber(text, i);
        while (i < text.size() && text[i] == u8' ')
            ++i;
        if (!in || i >= text.size() || text[i] != u8',')
            continue;
        ++i;
        const auto out = parseNumber(text, i);
        while (i < text.size() && text[i] == u8' ')
            ++i;
        if (!out || i >= text.size() || text[i] != u8')')
            continue;
        return FadeTag{{at, i + 1 - at}, static_cast<std::int64_t>(*in), static_cast<std::int64_t>(*out)};
    }
    return std::nullopt;
}

}

LineFrames lineFrames(const AegisubFrameTimes &times, std::int64_t startMs, std::int64_t endMs)
{
    const auto first = times.frameFromMs(startMs);
    return {first, std::max<std::int64_t>(0, times.frameFromMs(endMs) - first)};
}

PointPath::PointPath(FramePoint picked, MotionTrack track, std::int64_t trackFirst, std::int64_t reference)
    : m_picked(picked), m_track(std::move(track)), m_trackFirst(trackFirst), m_reference(reference)
{
}

std::optional<FramePoint> PointPath::at(std::int64_t frame) const
{
    if (!m_track)
        return m_picked;
    const auto rows = static_cast<std::int64_t>(m_track->frames());
    const auto row = frame - m_trackFirst;
    if (rows == 0 || row < 0 || row >= rows)
        return std::nullopt;
    const auto reference = static_cast<std::size_t>(std::clamp<std::int64_t>(m_reference - m_trackFirst, 0, rows - 1));
    const auto r = static_cast<std::size_t>(row);
    return FramePoint{static_cast<int>(std::floor(m_picked.x + m_track->x[r] - m_track->x[reference])),
                      static_cast<int>(std::floor(m_picked.y + m_track->y[r] - m_track->y[reference]))};
}

std::u8string assColour(KeyColour colour)
{
    char text[16];
    std::snprintf(text, sizeof text, "&H%02X%02X%02X&", colour.b, colour.g, colour.r);
    return u8(text);
}

std::u8string trackedAlpha(KeyColour sign, KeyColour background, KeyColour current)
{
    const double s = brightness(sign), b = brightness(background), c = brightness(current);
    int alpha = 0;
    if (c == s)
        alpha = 0;
    else if (c == b)
        alpha = 255;
    else {
        const double raw = 255 * (b - c) / (b - s);
        alpha = static_cast<int>(255 - (std::isnan(raw) ? 0 : std::clamp(raw, 0.0, 255.0)));
    }
    char text[8];
    std::snprintf(text, sizeof text, "%02X", alpha);
    return u8(text);
}

std::u8string colourTrackTags(std::span<const std::optional<KeyColour>> colours,
                              std::span<const std::int64_t> offsetsMs, const ColourTrackOptions &options)
{
    const auto value = [&](KeyColour c) {
        return options.alpha ? trackedAlpha(options.sign, options.background, c) : assColour(c);
    };
    std::u8string out;
    std::optional<std::u8string> last;
    for (std::size_t i = 0; i < colours.size() && i < offsetsMs.size(); ++i) {
        if (!colours[i])
            continue;
        auto v = value(*colours[i]);
        if (i == 0) {
            out += valueTags(options, v);
        } else if (!(options.skipRepeats && last == v)) {
            const auto t = number(offsetsMs[i]);
            out.append(u8"\\t(").append(t).append(u8",").append(t).append(u8",");
            out.append(valueTags(options, v)).append(u8")");
        }
        last = std::move(v);
    }
    return out;
}

std::u8string withColourTrack(std::u8string_view text, std::u8string_view tags, const ColourTrackOptions &options)
{
    std::u8string out(text);
    const auto open = out.find(u8'{');
    const auto close = open == std::u8string::npos ? std::u8string::npos : out.find(u8'}', open);
    if (close == std::u8string::npos)
        return u8"{" + std::u8string(tags) + u8"}" + out;
    std::u8string block = out.substr(open, close + 1 - open);
    if (!options.alpha) {
        if (options.colours[0])
            block = removeHexTag(removeHexTag(block, u8"1c"), u8"c");
        for (std::size_t i = 1; i < 4; ++i)
            if (options.colours[i])
                block = removeHexTag(block, kColourTags[i].substr(1));
    } else {
        if (options.allAlpha)
            block = removeHexTag(block, u8"alpha");
        for (std::size_t i = 0; i < 4; ++i)
            if (options.allAlpha || options.alphas[i])
                block = removeHexTag(block, kAlphaTags[i].substr(1));
    }
    out.replace(open, close + 1 - open, block);
    out.insert(out.find(u8'}'), tags);
    return out;
}

double colourDistance(KeyColour a, KeyColour b)
{
    const auto d = [](int x, int y) { return static_cast<double>(x - y) * (x - y); };
    return std::sqrt(d(a.b, b.b) + d(a.g, b.g) + d(a.r, b.r));
}

std::expected<FadeTimes, FadeProblem> findFade(const AegisubFrameTimes &times,
                                               std::span<const std::optional<double>> distances,
                                               std::int64_t first, std::int64_t current, std::int64_t startMs,
                                               std::int64_t endMs, const FadeOptions &options)
{
    const auto ms = [&](std::int64_t frame) { return times.msFromFrame(frame); };
    const double limit = static_cast<double>(ms(2) - ms(1)) / 2;
    const auto middle = [&](std::int64_t a, std::int64_t b) {
        return static_cast<std::int64_t>(std::floor(static_cast<double>(ms(a) + ms(b)) / 2));
    };
    const auto distance = [&](std::int64_t frame) -> std::optional<double> {
        const auto i = frame - first;
        return i >= 0 && i < static_cast<std::int64_t>(distances.size()) ? distances[static_cast<std::size_t>(i)]
                                                                          : std::nullopt;
    };
    const auto last = first + static_cast<std::int64_t>(distances.size()) - 1;
    FadeTimes out;
    if (options.in) {
        for (auto f = first; f <= current && f <= last; ++f)
            if (const auto d = distance(f); d && *d < options.tolerance) {
                const auto fade = middle(f + 1, f) - startMs;
                out.inMs = fade < limit ? 0 : fade;
                out.inFrame = f;
                break;
            }
        if (!out.inMs)
            return std::unexpected(FadeProblem::InNotFound);
    }
    if (options.out) {
        for (auto f = current; f <= last; ++f)
            if (const auto d = distance(f); d && *d > options.tolerance) {
                const auto fade = endMs - middle(f - 1, f);
                out.outMs = fade < limit ? 0 : fade;
                out.outFrame = f;
                break;
            }
        if (!out.outMs)
            return std::unexpected(FadeProblem::OutNotFound);
    }
    return out;
}

std::optional<FramePoint> pointClip(std::u8string_view text)
{
    for (const auto &clip : clipTags(text)) {
        const auto tag = text.substr(clip.at, clip.size);
        std::size_t i = tag.find(u8'(') + 1;
        while (i < tag.size() && tag[i] == u8' ')
            ++i;
        if (i >= tag.size() || tag[i] != u8'm')
            continue;
        ++i;
        const auto x = parseNumber(tag, i);
        const auto y = x ? parseNumber(tag, i) : std::nullopt;
        while (i < tag.size() && tag[i] == u8' ')
            ++i;
        if (x && y && *x >= 0 && *y >= 0 && i + 1 == tag.size())
            return FramePoint{static_cast<int>(*x), static_cast<int>(*y)};
    }
    return std::nullopt;
}

std::u8string withFade(std::u8string_view text, std::optional<std::int64_t> inMs, std::optional<std::int64_t> outMs,
                       bool dropClip)
{
    std::u8string out(text);
    if (dropClip) {
        const auto clips = clipTags(out);
        for (auto it = clips.rbegin(); it != clips.rend(); ++it)
            if (isVectorClip(std::u8string_view(out).substr(it->at, it->size)))
                out.erase(it->at, it->size);
        for (auto empty = out.find(u8"{}"); empty != std::u8string::npos; empty = out.find(u8"{}"))
            out.erase(empty, 2);
    }
    const auto existing = fadeTag(out);
    const auto in = inMs.value_or(existing ? existing->in : 0);
    const auto fadeOut = outMs.value_or(existing ? existing->out : 0);
    if (in == 0 && fadeOut == 0)
        return out;
    const auto tag = u8"\\fad(" + number(in) + u8"," + number(fadeOut) + u8")";
    if (existing)
        out.replace(existing->span.at, existing->span.size, tag);
    else if (out.starts_with(u8'{') && out.find(u8'}') != std::u8string::npos)
        out.insert(1, tag);
    else
        out = u8"{" + tag + u8"}" + out;
    return out;
}

std::expected<void, CommandRefusal> replaceTexts(EditSession &session, const std::string &history,
                                                 const std::vector<std::pair<core::LineId, std::u8string>> &texts)
{
    if (session.draftLine())
        session.commitDraft();
    std::set<core::LineId> touches;
    for (const auto &[id, text] : texts)
        for (const auto *line : session.document().lines())
            if (line->id == id && line->text != text)
                touches.insert(id);
    if (touches.empty())
        return {};
    const auto selection = session.selection();
    const auto ran = session.run(Command{history, session.revision(), touches, [&](core::Document &d) {
                                             bool changed = false;
                                             for (const auto &[id, text] : texts)
                                                 if (touches.contains(id))
                                                     changed = d.editLine(id, [&](core::LineRecord &l) { l.text = text; }) ||
                                                               changed;
                                             return changed;
                                         }});
    if (!ran)
        return std::unexpected(ran.error());
    session.setSelection(selection);
    return {};
}

}
