#include "hikari/core/export.h"

#include "hikari/core/style.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace hikari::core {

namespace {

using u8sv = std::u8string_view;

constexpr std::uint32_t bit(ExportLoss loss)
{
    return static_cast<std::uint32_t>(loss);
}

constexpr std::uint32_t kWhite = 0xFFFFFF;

struct Look {
    bool italic = false, bold = false, underline = false, strike = false;
    std::uint32_t colour = kWhite; // 0xRRGGBB
    bool operator==(const Look &) const = default;
};

struct Piece {
    std::u8string text;
    Look look;
    std::int64_t syllableUs = -1; // a karaoke syllable starts here, from the Line's start
};

using Row = std::vector<Piece>;

struct Flat {
    const LineRecord *line = nullptr;
    std::size_t order = 0;
    std::int64_t startUs = 0, endUs = 0;
    std::vector<Row> rows;
    const StyleValues *style = nullptr;
    int alignment = 2;
    std::optional<std::pair<double, double>> pos;
    int marginL = 0, marginR = 0, marginV = 0;
    std::uint32_t dropped = 0; // no format carries these
    bool colourTag = false, formattingTag = false, strikeTag = false, karaoke = false;
    std::uint32_t losses = 0;
};

std::u8string ascii(std::string_view s)
{
    return {reinterpret_cast<const char8_t *>(s.data()), s.size()};
}

u8sv trimmed(u8sv s)
{
    while (!s.empty() && (s.front() == u8' ' || s.front() == u8'\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == u8' ' || s.back() == u8'\t'))
        s.remove_suffix(1);
    return s;
}

std::string_view chars(u8sv s)
{
    return {reinterpret_cast<const char *>(s.data()), s.size()};
}

// Like atoi: spaces, a sign, digits; 0 without digits.
std::int64_t leadingInt(u8sv s)
{
    s = trimmed(s);
    if (!s.empty() && s.front() == u8'+')
        s.remove_prefix(1);
    std::int64_t v = 0;
    const auto text = chars(s);
    std::from_chars(text.data(), text.data() + text.size(), v);
    return v;
}

double leadingDouble(u8sv s)
{
    s = trimmed(s);
    if (!s.empty() && s.front() == u8'+')
        s.remove_prefix(1);
    double v = 0;
    const auto text = chars(s);
    std::from_chars(text.data(), text.data() + text.size(), v);
    return std::isfinite(v) ? v : 0;
}

// ASS &HBBGGRR& as 0xRRGGBB.
std::uint32_t assColour(u8sv s)
{
    s = trimmed(s);
    while (!s.empty() && (s.front() == u8'&' || s.front() == u8'H' || s.front() == u8'h'))
        s.remove_prefix(1);
    std::uint32_t v = 0;
    const auto text = chars(s);
    std::from_chars(text.data(), text.data() + text.size(), v, 16);
    return ((v & 0xFF) << 16) | (v & 0xFF00) | ((v >> 16) & 0xFF);
}

std::uint32_t rgbOf(const Colour &c)
{
    return static_cast<std::uint32_t>(((c.r & 0xFF) << 16) | ((c.g & 0xFF) << 8) | (c.b & 0xFF));
}

Look lookOf(const StyleValues *style)
{
    if (!style)
        return {};
    return {style->italic, style->bold, style->underline, style->strikeOut, rgbOf(style->primary)};
}

std::vector<double> arguments(u8sv args)
{
    std::vector<double> out;
    args = trimmed(args);
    if (!args.empty() && args.front() == u8'(')
        args.remove_prefix(1);
    if (const auto close = args.find(u8')'); close != u8sv::npos)
        args = args.substr(0, close);
    while (!args.empty()) {
        const auto comma = args.find(u8',');
        out.push_back(leadingDouble(args.substr(0, comma)));
        if (comma == u8sv::npos)
            break;
        args.remove_prefix(comma + 1);
    }
    return out;
}

// Longest first where names share a prefix.
constexpr auto kTags = std::to_array<std::u8string_view>({
    u8"xbord", u8"ybord", u8"xshad", u8"yshad", u8"fscx", u8"fscy", u8"fsp", u8"fs", u8"fn", u8"fade",
    u8"fad", u8"fax", u8"fay", u8"frx", u8"fry", u8"frz", u8"fr", u8"fe", u8"alpha", u8"an",
    u8"a", u8"1c", u8"2c", u8"3c", u8"4c", u8"1a", u8"2a", u8"3a", u8"4a", u8"clip",
    u8"c", u8"iclip", u8"i", u8"blur", u8"bord", u8"be", u8"b", u8"kf", u8"ko", u8"kt",
    u8"k", u8"K", u8"move", u8"org", u8"pos", u8"p", u8"q", u8"r", u8"shad", u8"s",
    u8"t", u8"u"});

class Flattener {
public:
    Flattener(const std::map<std::u8string, StyleValues, std::less<>> &styles, bool softBreaks)
        : m_styles(styles), m_softBreaks(softBreaks)
    {
    }

    Flat flatten(const LineRecord &line, std::size_t order)
    {
        Flat flat;
        flat.line = &line;
        flat.order = order;
        flat.startUs = std::max<std::int64_t>(0, line.start.value.microseconds());
        flat.endUs = std::max<std::int64_t>(0, line.end.value.microseconds());
        flat.style = styleNamed(line.style);
        m_base = lookOf(flat.style);
        m_look = m_base;
        m_flat = &flat;
        m_drawing = false;
        m_karaokeUs = 0;
        m_syllable = -1;
        m_alignmentSet = m_posSet = false;
        m_rows.assign(1, {});
        if (flat.style) {
            const auto a = leadingInt(flat.style->alignment);
            flat.alignment = a >= 1 && a <= 9 ? static_cast<int>(a) : 2;
        }
        const auto margin = [&](const IntField &own, const std::u8string *styled) {
            return static_cast<int>(own.value != 0 ? own.value : styled ? leadingInt(*styled) : 0);
        };
        flat.marginL = margin(line.marginLeft, flat.style ? &flat.style->marginLeft : nullptr);
        flat.marginR = margin(line.marginRight, flat.style ? &flat.style->marginRight : nullptr);
        flat.marginV = margin(line.marginVertical, flat.style ? &flat.style->marginVertical : nullptr);
        if (scrolling(line.effect))
            flat.dropped |= bit(ExportLoss::Effect);
        read(line.text);
        flat.rows = finishRows();
        return flat;
    }

private:
    static bool scrolling(u8sv effect)
    {
        const auto starts = [&](u8sv word) {
            if (effect.size() < word.size())
                return false;
            for (std::size_t i = 0; i < word.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(effect[i])) != word[i])
                    return false;
            return true;
        };
        return starts(u8"banner;") || starts(u8"scroll up;") || starts(u8"scroll down;");
    }

    const StyleValues *styleNamed(u8sv name) const
    {
        if (const auto it = m_styles.find(name); it != m_styles.end())
            return &it->second;
        if (const auto it = m_styles.find(u8sv(u8"Default")); it != m_styles.end())
            return &it->second;
        return nullptr;
    }

    void read(u8sv text)
    {
        std::u8string run;
        const auto flush = [&] {
            if (!run.empty())
                put(std::move(run));
            run.clear();
        };
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char8_t c = text[i];
            if (c == u8'{') {
                const auto close = text.find(u8'}', i + 1);
                if (close != u8sv::npos) {
                    flush();
                    block(text.substr(i + 1, close - i - 1));
                    i = close;
                    continue;
                }
            }
            if (c == u8'\\' && i + 1 < text.size()) {
                const char8_t next = text[i + 1];
                if (next == u8'N' || (next == u8'n' && m_softBreaks)) {
                    flush();
                    if (!m_drawing)
                        m_rows.emplace_back();
                    ++i;
                    continue;
                }
                if (next == u8'n') {
                    run += u8' ';
                    ++i;
                    continue;
                }
                if (next == u8'h') {
                    run += u8" ";
                    ++i;
                    continue;
                }
            }
            run += c;
        }
        flush();
    }

    void put(std::u8string text)
    {
        if (m_drawing)
            return;
        m_rows.back().push_back({std::move(text), m_look, m_syllable});
        m_syllable = -1;
    }

    void block(u8sv block)
    {
        std::size_t i = block.find(u8'\\');
        while (i != u8sv::npos && i < block.size()) {
            std::size_t end = i + 1;
            int depth = 0;
            while (end < block.size() && (depth > 0 || block[end] != u8'\\')) {
                if (block[end] == u8'(')
                    ++depth;
                else if (block[end] == u8')' && depth > 0)
                    --depth;
                ++end;
            }
            tag(block.substr(i + 1, end - i - 1));
            i = end;
        }
    }

    void tag(u8sv token)
    {
        const auto name = std::ranges::find_if(kTags, [&](u8sv n) { return token.starts_with(n); });
        if (name == kTags.end())
            return;
        const u8sv n = *name;
        const u8sv arg = token.substr(n.size());
        const bool empty = trimmed(arg).empty();
        Flat &f = *m_flat;
        const auto flag = [&](bool &value, bool styled) {
            value = empty ? styled : leadingInt(arg) != 0;
        };
        if (n == u8"i") {
            flag(m_look.italic, m_base.italic);
            f.formattingTag = true;
        } else if (n == u8"b") {
            const auto v = leadingInt(arg);
            m_look.bold = empty ? m_base.bold : v == 1 || v >= 600;
            f.formattingTag = true;
        } else if (n == u8"u") {
            flag(m_look.underline, m_base.underline);
            f.formattingTag = true;
        } else if (n == u8"s") {
            flag(m_look.strike, m_base.strike);
            f.formattingTag = f.strikeTag = true;
        } else if (n == u8"c" || n == u8"1c") {
            m_look.colour = empty ? m_base.colour : assColour(arg);
            f.colourTag = true;
        } else if (n == u8"r") {
            const auto style = trimmed(arg);
            if (style.empty())
                m_look = m_base;
            else if (const auto it = m_styles.find(style); it != m_styles.end())
                m_look = lookOf(&it->second);
            else
                m_look = m_base;
        } else if (n == u8"an" || n == u8"a") {
            auto v = static_cast<int>(leadingInt(arg));
            if (n == u8"a") {
                const int h = v & 3;
                v = h == 0 ? 0 : h + ((v & 4) ? 6 : (v & 8) ? 3 : 0);
            }
            if (!m_alignmentSet && v >= 1 && v <= 9) {
                f.alignment = v;
                m_alignmentSet = true;
            }
        } else if (n == u8"pos" || n == u8"move") {
            const auto args = arguments(arg);
            if (n == u8"move")
                f.dropped |= bit(ExportLoss::Movement);
            if (!m_posSet && args.size() >= 2) {
                f.pos = std::pair(args[0], args[1]);
                m_posSet = true;
            }
        } else if (n == u8"k" || n == u8"K" || n == u8"kf" || n == u8"ko") {
            m_syllable = m_karaokeUs;
            m_karaokeUs += std::max<std::int64_t>(0, leadingInt(arg)) * 10000;
            f.karaoke = true;
        } else if (n == u8"kt") {
            m_karaokeUs = std::max<std::int64_t>(0, leadingInt(arg)) * 10000;
            f.karaoke = true;
        } else if (n == u8"p") {
            m_drawing = leadingInt(arg) > 0;
            if (m_drawing)
                f.dropped |= bit(ExportLoss::Drawing);
        } else if (n == u8"clip" || n == u8"iclip") {
            f.dropped |= bit(ExportLoss::Clip);
        } else if (n == u8"t") {
            f.dropped |= bit(ExportLoss::Transform);
        } else if (n == u8"fad" || n == u8"fade") {
            f.dropped |= bit(ExportLoss::Fade);
        } else if (n == u8"org") {
            f.dropped |= bit(ExportLoss::Rotation);
        } else if (n.starts_with(u8"fr") || n == u8"fax" || n == u8"fay") {
            if (leadingDouble(arg) != 0)
                f.dropped |= bit(ExportLoss::Rotation);
        } else if (n == u8"fn" || n == u8"fs" || n == u8"fscx" || n == u8"fscy" || n == u8"fsp") {
            f.dropped |= bit(ExportLoss::Font);
        } else if (n != u8"q" && n != u8"fe") {
            f.dropped |= bit(ExportLoss::Outline);
        }
    }

    std::vector<Row> finishRows()
    {
        std::vector<Row> rows;
        for (auto &row : m_rows) {
            Row merged;
            for (auto &piece : row) {
                if (!merged.empty() && merged.back().look == piece.look && piece.syllableUs < 0)
                    merged.back().text += piece.text;
                else
                    merged.push_back(std::move(piece));
            }
            while (!merged.empty()) {
                auto &first = merged.front().text;
                first.erase(0, first.size() - trimmedStart(first).size());
                if (!first.empty())
                    break;
                merged.erase(merged.begin());
            }
            while (!merged.empty()) {
                auto &last = merged.back().text;
                while (!last.empty() && (last.back() == u8' ' || last.back() == u8'\t'))
                    last.pop_back();
                if (!last.empty())
                    break;
                merged.pop_back();
            }
            if (!merged.empty())
                rows.push_back(std::move(merged));
        }
        return rows;
    }

    static u8sv trimmedStart(u8sv s)
    {
        while (!s.empty() && (s.front() == u8' ' || s.front() == u8'\t'))
            s.remove_prefix(1);
        return s;
    }

    const std::map<std::u8string, StyleValues, std::less<>> &m_styles;
    bool m_softBreaks = false;
    Flat *m_flat = nullptr;
    Look m_base, m_look;
    bool m_drawing = false;
    std::int64_t m_karaokeUs = 0;
    std::int64_t m_syllable = -1;
    bool m_alignmentSet = false, m_posSet = false;
    std::vector<Row> m_rows;
};

bool positioned(const Flat &f)
{
    return f.pos || f.alignment != 2;
}

bool uniform(const Row &row, bool Look::*property)
{
    return std::ranges::all_of(row, [&](const Piece &p) { return p.look.*property == row.front().look.*property; });
}

bool uniformColour(const Row &row)
{
    return std::ranges::all_of(row, [&](const Piece &p) { return p.look.colour == row.front().look.colour; });
}

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

std::int64_t roundedDiv(std::int64_t value, std::int64_t by)
{
    return (value + by / 2) / by;
}

std::u8string padded(std::int64_t v, int width)
{
    std::string s = std::to_string(v);
    if (static_cast<int>(s.size()) < width)
        s.insert(0, static_cast<std::size_t>(width) - s.size(), '0');
    return ascii(s);
}

// Hours with `hourDigits` digits, then minutes, seconds and milliseconds.
std::u8string clock(std::int64_t us, int hourDigits, char8_t fraction)
{
    const std::int64_t ms = roundedDiv(us, 1000);
    return padded(ms / 3600000, hourDigits) + u8":" + padded(ms / 60000 % 60, 2) + u8":" + padded(ms / 1000 % 60, 2) +
           std::u8string(1, fraction) + padded(ms % 1000, 3);
}

std::u8string percent(double v)
{
    v = std::clamp(v, 0.0, 100.0);
    std::array<char, 32> buffer{};
    const auto r = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v, std::chars_format::fixed, 2);
    std::string s(buffer.data(), r.ptr);
    while (s.back() == '0')
        s.pop_back();
    if (s.back() == '.')
        s.pop_back();
    return ascii(s) + u8"%";
}

std::u8string hex(std::uint32_t rgb, bool upper)
{
    std::array<char, 8> buffer{};
    const auto r = std::to_chars(buffer.data(), buffer.data() + buffer.size(), rgb, 16);
    std::string s(buffer.data(), r.ptr);
    s.insert(0, 6 - s.size(), '0');
    if (upper)
        std::ranges::transform(s, s.begin(), [](char c) { return static_cast<char>(std::toupper(c)); });
    return ascii(s);
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

std::u8string plainRow(const Row &row)
{
    std::u8string out;
    for (const auto &p : row)
        out += p.text;
    return out;
}

std::u8string joinedRows(const Flat &f, u8sv separator, const std::function<std::u8string(const Row &)> &text)
{
    std::u8string out;
    for (std::size_t r = 0; r < f.rows.size(); ++r)
        out += (r ? std::u8string(separator) : std::u8string()) + text(f.rows[r]);
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

struct PlayRes {
    double x = 384, y = 288;
};

PlayRes playRes(const Document &document)
{
    const auto read = [&](u8sv key) {
        const auto v = document.scriptInfo(key);
        return v ? leadingInt(*v) : 0;
    };
    auto x = read(u8"PlayResX"), y = read(u8"PlayResY");
    if (x <= 0 && y <= 0)
        return {};
    if (x <= 0)
        x = y == 1024 ? 1280 : y * 4 / 3;
    if (y <= 0)
        y = x == 1280 ? 1024 : x * 3 / 4;
    return {static_cast<double>(x), static_cast<double>(y)};
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
        std::u8string name;
        for (const char8_t c : cue.style->name)
            name += (c >= u8'a' && c <= u8'z') || (c >= u8'A' && c <= u8'Z') || (c >= u8'0' && c <= u8'9') ||
                            c == u8'-' || c == u8'_'
                        ? c
                        : u8'_';
        if (name.empty() || (name.front() >= u8'0' && name.front() <= u8'9') || name.front() == u8'-')
            name.insert(0, u8"s");
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
    }
    result.bytes.resize(out.size());
    std::memcpy(result.bytes.data(), out.data(), out.size());
    for (const auto *line : document.lines())
        if (const auto it = losses.find(line->id.value); it != losses.end())
            result.notes.push_back({line->id, it->second});
    return result;
}

}
