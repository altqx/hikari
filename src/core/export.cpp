#include "hikari/core/export.h"

#include "export_flat.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace hikari::core {

namespace {

using namespace detail;

std::uint32_t lossesFor(ExportFormat format, const ExportOptions &o, const Flat &f)
{
    std::uint32_t l = f.dropped;
    const auto lose = [&](bool when, ExportLoss loss) {
        if (when)
            l |= bit(loss);
    };
    const bool carriesKaraoke = format == ExportFormat::WebVtt && o.vttKaraoke;
    lose(f.karaoke && !carriesKaraoke, ExportLoss::Karaoke);
    switch (format) {
    case ExportFormat::Srt:
        lose(f.strikeTag, ExportLoss::Formatting);
        lose(f.colourTag && !o.srtFontColour, ExportLoss::Colour);
        lose(f.pos.has_value() || (f.alignment != 2 && !o.srtAlignmentTags), ExportLoss::Position);
        break;
    case ExportFormat::WebVtt:
        lose(f.strikeTag, ExportLoss::Formatting);
        lose(f.colourTag && !o.vttStyles, ExportLoss::Colour);
        lose(positioned(f) && !o.vttPositions, ExportLoss::Position);
        break;
    case ExportFormat::MicroDvd:
        lose(f.formattingTag && std::ranges::any_of(f.rows, [](const Row &r) {
                 return !uniform(r, &Look::italic) || !uniform(r, &Look::bold) || !uniform(r, &Look::underline) ||
                        !uniform(r, &Look::strike);
             }),
             ExportLoss::Formatting);
        lose(f.colourTag && !std::ranges::all_of(f.rows, uniformColour), ExportLoss::Colour);
        lose(positioned(f), ExportLoss::Position);
        break;
    case ExportFormat::Mpl2:
        lose(f.formattingTag && std::ranges::any_of(f.rows, [](const Row &r) {
                 return !uniform(r, &Look::italic) || std::ranges::any_of(r, [](const Piece &p) {
                            return p.look.bold || p.look.underline || p.look.strike;
                        });
             }),
             ExportLoss::Formatting);
        lose(f.colourTag, ExportLoss::Colour);
        lose(positioned(f), ExportLoss::Position);
        break;
    case ExportFormat::Sbv:
    case ExportFormat::PlainText:
    case ExportFormat::TMPlayer:
        lose(f.formattingTag, ExportLoss::Formatting);
        lose(f.colourTag, ExportLoss::Colour);
        lose(positioned(f), ExportLoss::Position);
        break;
    case ExportFormat::Ttml:
    case ExportFormat::Dfxp:
        return ttmlLosses(o, f);
    case ExportFormat::Srv3:
        break;
    }
    return l;
}

int band(const Flat &f)
{
    return f.alignment >= 7 ? 0 : f.alignment >= 4 ? 1 : 2;
}

// One cue per stretch of time with the same Lines on screen; top Lines first.
std::vector<Flat> merged(std::vector<Flat> cues, std::map<std::uint64_t, std::uint32_t> &losses)
{
    std::vector<Flat> out;
    std::size_t i = 0;
    while (i < cues.size()) {
        std::size_t j = i + 1;
        std::int64_t end = cues[i].endUs;
        while (j < cues.size() && cues[j].startUs < end) {
            end = std::max(end, cues[j].endUs);
            ++j;
        }
        if (j == i + 1) {
            out.push_back(std::move(cues[i]));
            ++i;
            continue;
        }
        std::vector<std::int64_t> edges;
        for (std::size_t k = i; k < j; ++k) {
            edges.push_back(cues[k].startUs);
            edges.push_back(cues[k].endUs);
            losses[cues[k].line->id.value] |= bit(ExportLoss::Merged);
        }
        std::ranges::sort(edges);
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
        for (std::size_t e = 0; e + 1 < edges.size(); ++e) {
            std::vector<const Flat *> on;
            for (std::size_t k = i; k < j; ++k)
                if (cues[k].startUs <= edges[e] && cues[k].endUs >= edges[e + 1])
                    on.push_back(&cues[k]);
            if (on.empty())
                continue;
            std::ranges::stable_sort(on, [](const Flat *a, const Flat *b) {
                return std::pair(band(*a), a->order) < std::pair(band(*b), b->order);
            });
            Flat cue = *on.front();
            cue.startUs = edges[e];
            cue.endUs = edges[e + 1];
            cue.alignment = 2;
            cue.pos.reset();
            cue.rows.clear();
            for (const Flat *f : on)
                for (const Row &row : f->rows) {
                    Row copy = row;
                    for (auto &p : copy)
                        p.syllableUs = -1;
                    cue.rows.push_back(std::move(copy));
                }
            out.push_back(std::move(cue));
        }
        i = j;
    }
    return out;
}

std::u8string percent(double v)
{
    return decimal(std::clamp(v, 0.0, 100.0)) + u8"%";
}

// Closes and reopens only what changes, so tags always nest.
class Markup {
public:
    struct Tag {
        std::u8string open, close;
        bool operator==(const Tag &) const = default;
    };

    void to(std::u8string &out, const std::vector<Tag> &want)
    {
        std::size_t same = 0;
        while (same < m_open.size() && same < want.size() && m_open[same] == want[same])
            ++same;
        for (std::size_t k = m_open.size(); k > same; --k)
            out += m_open[k - 1].close;
        for (std::size_t k = same; k < want.size(); ++k)
            out += want[k].open;
        m_open = want;
    }
    void close(std::u8string &out) { to(out, {}); }

private:
    std::vector<Tag> m_open;
};

std::u8string escapedVtt(u8sv text)
{
    std::u8string out;
    for (const char8_t c : text) {
        if (c == u8'&')
            out += u8"&amp;";
        else if (c == u8'<')
            out += u8"&lt;";
        else if (c == u8'>')
            out += u8"&gt;";
        else
            out += c;
    }
    return out;
}

void writeSrt(std::u8string &out, const std::vector<Flat> &cues, const ExportOptions &o)
{
    int number = 0;
    for (const auto &cue : cues) {
        out += ascii(std::to_string(++number)) + u8"\r\n" + clock(cue.startUs, 2, u8',') + u8" --> " +
               clock(cue.endUs, 2, u8',') + u8"\r\n";
        if (o.srtAlignmentTags && cue.alignment != 2)
            out += u8"{\\an" + ascii(std::to_string(cue.alignment)) + u8"}";
        out += joinedRows(cue, u8"\r\n", [&](const Row &row) {
            std::u8string text;
            Markup markup;
            for (const auto &p : row) {
                std::vector<Markup::Tag> want;
                if (p.look.bold)
                    want.push_back({u8"<b>", u8"</b>"});
                if (p.look.italic)
                    want.push_back({u8"<i>", u8"</i>"});
                if (p.look.underline)
                    want.push_back({u8"<u>", u8"</u>"});
                if (o.srtFontColour && p.look.colour != kWhite)
                    want.push_back({u8"<font color=\"#" + hex(p.look.colour, true) + u8"\">", u8"</font>"});
                markup.to(text, want);
                text += p.text;
            }
            markup.close(text);
            return text;
        });
        out += u8"\r\n\r\n";
    }
}

std::u8string vttSettings(const Flat &f, PlayRes res)
{
    const int h = (f.alignment - 1) % 3; // 0 left, 1 centre, 2 right
    const int v = (f.alignment - 1) / 3; // 0 bottom, 1 middle, 2 top
    if (f.pos) {
        static constexpr std::array<u8sv, 3> kLine = {u8"end", u8"center", u8"start"};
        static constexpr std::array<u8sv, 3> kPosition = {u8"line-left", u8"center", u8"line-right"};
        static constexpr std::array<u8sv, 3> kAlign = {u8"left", u8"center", u8"right"};
        return u8" line:" + percent(f.pos->second * 100 / res.y) + u8"," + std::u8string(kLine[v]) + u8" position:" +
               percent(f.pos->first * 100 / res.x) + u8"," + std::u8string(kPosition[h]) + u8" align:" +
               std::u8string(kAlign[h]);
    }
    if (f.alignment == 2)
        return {};
    std::u8string s;
    if (v == 0)
        s += u8" line:" + percent(100 - f.marginV * 100 / res.y) + u8",end";
    else if (v == 1)
        s += u8" line:50%,center";
    else
        s += u8" line:" + percent(f.marginV * 100 / res.y) + u8",start";
    if (h == 0)
        s += u8" position:" + percent(f.marginL * 100 / res.x) + u8",line-left align:left";
    else if (h == 2)
        s += u8" position:" + percent(100 - f.marginR * 100 / res.x) + u8",line-right align:right";
    return s;
}

std::u8string cssColour(const Colour &c)
{
    if ((c.a & 0xFF) == 0)
        return u8"#" + hex(rgbOf(c), true);
    std::array<char, 16> buffer{};
    const double alpha = static_cast<double>(255 - (c.a & 0xFF)) / 255.0;
    const auto r = std::to_chars(buffer.data(), buffer.data() + buffer.size(), alpha, std::chars_format::fixed, 2);
    return ascii("rgba(" + std::to_string(c.r & 0xFF) + ", " + std::to_string(c.g & 0xFF) + ", " +
                 std::to_string(c.b & 0xFF) + ", " + std::string(buffer.data(), r.ptr) + ")");
}

std::u8string cssString(u8sv text)
{
    std::u8string out = u8"\"";
    for (const char8_t c : text) {
        if (c == u8'"' || c == u8'\\')
            out += u8'\\';
        out += c;
    }
    return out + u8"\"";
}

// CSS class names: letters, digits, '-' and '_'; never two Styles on one name.
std::map<const StyleValues *, std::u8string> vttClasses(const std::vector<Flat> &cues)
{
    std::map<const StyleValues *, std::u8string> classes;
    std::map<std::u8string, int> taken;
    for (const auto &cue : cues) {
        if (!cue.style || classes.contains(cue.style))
            continue;
        std::u8string name = identifierFor(cue.style->name);
        if (const int n = taken[name]++)
            name += u8"_" + ascii(std::to_string(n + 1));
        classes.emplace(cue.style, std::move(name));
    }
    return classes;
}

std::u8string vttStyleRule(const StyleValues &style, u8sv name)
{
    std::u8string rule = u8"::cue(." + std::u8string(name) + u8") { color: " + cssColour(style.primary) + u8";";
    if (!style.fontname.empty())
        rule += u8" font-family: " + cssString(style.fontname) + u8";";
    if (style.bold)
        rule += u8" font-weight: bold;";
    if (style.italic)
        rule += u8" font-style: italic;";
    if (style.underline || style.strikeOut)
        rule += std::u8string(u8" text-decoration:") + (style.underline ? u8" underline" : u8"") +
                (style.strikeOut ? u8" line-through" : u8"") + u8";";
    if (style.borderStyle == 3 || style.borderStyle == 4) {
        rule += u8" background-color: " + cssColour(style.outline) + u8";";
    } else {
        rule += u8" background-color: transparent;";
        if (leadingDouble(style.outlineWidth) > 0) {
            const auto c = cssColour(style.outline);
            rule += u8" text-shadow: -1px -1px 0 " + c + u8", 1px -1px 0 " + c + u8", -1px 1px 0 " + c +
                    u8", 1px 1px 0 " + c + u8";";
        }
    }
    return rule + u8" }";
}

void writeVtt(std::u8string &out, const std::vector<Flat> &cues, const ExportOptions &o, PlayRes res)
{
    const auto classes = vttClasses(cues);
    std::map<std::uint32_t, bool> colours;
    std::u8string body;
    for (const auto &cue : cues) {
        body += u8"\n" + clock(cue.startUs, 2, u8'.') + u8" --> " + clock(cue.endUs, 2, u8'.');
        if (o.vttPositions)
            body += vttSettings(cue, res);
        body += u8"\n";
        const Look base = lookOf(cue.style);
        const auto styleClass = classes.find(cue.style);
        std::int64_t lastStamp = cue.startUs;
        body += joinedRows(cue, u8"\n", [&](const Row &row) {
            std::u8string text;
            const bool wrap = o.vttStyles && styleClass != classes.end();
            if (wrap)
                text += u8"<c." + styleClass->second + u8">";
            Markup markup;
            for (const auto &p : row) {
                if (o.vttKaraoke && p.syllableUs >= 0) {
                    const std::int64_t at = cue.startUs + p.syllableUs;
                    if (at > lastStamp && at < cue.endUs) {
                        markup.close(text);
                        text += u8"<" + clock(at, 2, u8'.') + u8">";
                        lastStamp = at;
                    }
                }
                std::vector<Markup::Tag> want;
                const auto styled = [&](bool on, bool inStyle) { return wrap ? on != inStyle && on : on; };
                if (styled(p.look.bold, base.bold))
                    want.push_back({u8"<b>", u8"</b>"});
                if (styled(p.look.italic, base.italic))
                    want.push_back({u8"<i>", u8"</i>"});
                if (styled(p.look.underline, base.underline))
                    want.push_back({u8"<u>", u8"</u>"});
                if (o.vttStyles && p.look.colour != base.colour) {
                    colours[p.look.colour] = true;
                    want.push_back({u8"<c.c" + hex(p.look.colour, false) + u8">", u8"</c>"});
                }
                markup.to(text, want);
                text += escapedVtt(p.text);
            }
            markup.close(text);
            if (wrap)
                text += u8"</c>";
            return text;
        });
        body += u8"\n";
    }
    out += u8"WEBVTT\n";
    if (o.vttStyles && (!classes.empty() || !colours.empty())) {
        out += u8"\nSTYLE\n";
        std::vector<std::pair<std::u8string, const StyleValues *>> sorted;
        for (const auto &[style, name] : classes)
            sorted.emplace_back(name, style);
        std::ranges::sort(sorted);
        for (const auto &[name, style] : sorted)
            out += vttStyleRule(*style, name) + u8"\n";
        for (const auto &[colour, used] : colours)
            out += u8"::cue(.c" + hex(colour, false) + u8") { color: #" + hex(colour, true) + u8"; }\n";
    }
    out += body;
}

void writeSbv(std::u8string &out, const std::vector<Flat> &cues)
{
    for (std::size_t i = 0; i < cues.size(); ++i) {
        if (i)
            out += u8"\r\n";
        out += clock(cues[i].startUs, 1, u8'.') + u8"," + clock(cues[i].endUs, 1, u8'.') + u8"\r\n" +
               joinedRows(cues[i], u8"\r\n", plainRow) + u8"\r\n";
    }
}

void writePlainText(std::u8string &out, const std::vector<Flat> &lines, const ExportOptions &o)
{
    for (const auto &f : lines) {
        if (o.textTimes)
            out += clock(f.startUs, 1, u8'.') + u8"\t" + clock(f.endUs, 1, u8'.') + u8"\t";
        if (o.textActors && !f.line->actor.empty())
            out += f.line->actor + u8": ";
        out += joinedRows(f, u8" ", plainRow) + u8"\n";
    }
}

std::u8string microDvdRow(const Row &row)
{
    const Look &first = row.front().look;
    std::u8string codes;
    if (uniform(row, &Look::italic) && first.italic)
        codes += u8"{y:i}";
    if (uniform(row, &Look::bold) && first.bold)
        codes += u8"{y:b}";
    if (uniform(row, &Look::underline) && first.underline)
        codes += u8"{y:u}";
    if (uniform(row, &Look::strike) && first.strike)
        codes += u8"{y:s}";
    if (uniformColour(row) && first.colour != kWhite) {
        const std::uint32_t c = first.colour;
        codes += u8"{c:$" + hex(((c & 0xFF) << 16) | (c & 0xFF00) | (c >> 16), true) + u8"}";
    }
    return codes + plainRow(row);
}

void writeMicroDvd(std::u8string &out, const std::vector<Flat> &cues, double fps)
{
    const auto frame = [&](std::int64_t us) {
        return std::llround(static_cast<double>(us) * fps / 1e6);
    };
    for (const auto &cue : cues)
        out += u8"{" + ascii(std::to_string(frame(cue.startUs))) + u8"}{" + ascii(std::to_string(frame(cue.endUs))) +
               u8"}" + joinedRows(cue, u8"|", microDvdRow) + u8"\r\n";
}

void writeMpl2(std::u8string &out, const std::vector<Flat> &cues)
{
    for (const auto &cue : cues)
        out += u8"[" + ascii(std::to_string(roundedDiv(cue.startUs, 100000))) + u8"][" +
               ascii(std::to_string(roundedDiv(cue.endUs, 100000))) + u8"]" +
               joinedRows(cue, u8"|",
                          [](const Row &row) {
                              return (uniform(row, &Look::italic) && row.front().look.italic ? u8"/" : u8"") +
                                     plainRow(row);
                          }) +
               u8"\r\n";
}

std::u8string tmpClock(std::int64_t seconds)
{
    return padded(seconds / 3600, 2) + u8":" + padded(seconds / 60 % 60, 2) + u8":" + padded(seconds % 60, 2) + u8":";
}

// No end times: an empty cue clears the screen when nothing follows at once.
void writeTmPlayer(std::u8string &out, const std::vector<Flat> &cues)
{
    for (std::size_t i = 0; i < cues.size(); ++i) {
        const std::int64_t start = roundedDiv(cues[i].startUs, 1000000);
        const std::int64_t end = roundedDiv(cues[i].endUs, 1000000);
        out += tmpClock(start) + joinedRows(cues[i], u8"|", plainRow) + u8"\r\n";
        const bool followed = i + 1 < cues.size() && roundedDiv(cues[i + 1].startUs, 1000000) <= end;
        if (!followed && end > start)
            out += tmpClock(end) + u8"\r\n";
    }
}

bool merges(ExportFormat format, const ExportOptions &o)
{
    switch (format) {
    case ExportFormat::TMPlayer:
        return true;
    case ExportFormat::Srt:
    case ExportFormat::Sbv:
    case ExportFormat::MicroDvd:
    case ExportFormat::Mpl2:
        return o.mergeOverlaps;
    default:
        return false;
    }
}

}

ExportResult exportDocument(const Document &document, ExportFormat format, const ExportOptions &options)
{
    if (format == ExportFormat::Srv3)
        return exportSrv3(document, options);
    ExportResult result;
    std::map<std::u8string, StyleValues, std::less<>> styles;
    for (auto &style : decodeStyles(document))
        styles.try_emplace(style.name, std::move(style));
    const auto wrap = document.scriptInfo(u8"WrapStyle");
    Flattener flattener(styles, wrap && leadingInt(*wrap) == 2);

    std::vector<Flat> flats;
    std::map<std::uint64_t, std::uint32_t> losses;
    std::size_t order = 0;
    for (const auto *line : document.lines()) {
        if (options.lines && !options.lines->contains(line->id))
            continue;
        if (line->comment || line->unparsed) {
            ++result.commentsSkipped;
            continue;
        }
        Flat flat = flattener.flatten(*line, order++);
        const bool timed = format != ExportFormat::PlainText;
        if (flat.rows.empty() || (timed && flat.endUs <= flat.startUs)) {
            ++result.emptySkipped;
            if (flat.dropped)
                losses[line->id.value] |= flat.dropped;
            continue;
        }
        if (const auto l = lossesFor(format, options, flat))
            losses[line->id.value] |= l;
        flats.push_back(std::move(flat));
    }
    result.linesWritten = static_cast<int>(flats.size());
    if (format != ExportFormat::PlainText)
        std::ranges::stable_sort(flats, [](const Flat &a, const Flat &b) { return a.startUs < b.startUs; });
    if (merges(format, options))
        flats = merged(std::move(flats), losses);

    std::u8string out;
    switch (format) {
    case ExportFormat::Srt:
        writeSrt(out, flats, options);
        break;
    case ExportFormat::WebVtt:
        writeVtt(out, flats, options, playRes(document));
        break;
    case ExportFormat::Sbv:
        writeSbv(out, flats);
        break;
    case ExportFormat::PlainText:
        writePlainText(out, flats, options);
        break;
    case ExportFormat::MicroDvd:
        writeMicroDvd(out, flats, std::isfinite(options.fps) && options.fps >= 1 ? options.fps : 24000.0 / 1001.0);
        break;
    case ExportFormat::Mpl2:
        writeMpl2(out, flats);
        break;
    case ExportFormat::TMPlayer:
        writeTmPlayer(out, flats);
        break;
    case ExportFormat::Ttml:
    case ExportFormat::Dfxp:
        writeTtml(out, flats, options, playRes(document), format == ExportFormat::Ttml, losses);
        break;
    case ExportFormat::Srv3:
        break;
    }
    result.bytes.resize(out.size());
    std::memcpy(result.bytes.data(), out.data(), out.size());
    for (const auto *line : document.lines())
        if (const auto it = losses.find(line->id.value); it != losses.end())
            result.notes.push_back({line->id, it->second});
    return result;
}

}
