#include "export_flat.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>

namespace hikari::core::detail {

namespace {

u8sv trimmed(u8sv s)
{
    while (!s.empty() && (s.front() == u8' ' || s.front() == u8'\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == u8' ' || s.back() == u8'\t'))
        s.remove_suffix(1);
    return s;
}

u8sv trimmedStart(u8sv s)
{
    while (!s.empty() && (s.front() == u8' ' || s.front() == u8'\t'))
        s.remove_prefix(1);
    return s;
}

std::string_view chars(u8sv s)
{
    return {reinterpret_cast<const char *>(s.data()), s.size()};
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

bool scrolling(u8sv effect)
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

// Longest first where names share a prefix.
constexpr auto kTags = std::to_array<std::u8string_view>({
    u8"xbord", u8"ybord", u8"xshad", u8"yshad", u8"fscx", u8"fscy", u8"fsp", u8"fs", u8"fn", u8"fade",
    u8"fad", u8"fax", u8"fay", u8"frx", u8"fry", u8"frz", u8"fr", u8"fe", u8"alpha", u8"an",
    u8"a", u8"1c", u8"2c", u8"3c", u8"4c", u8"1a", u8"2a", u8"3a", u8"4a", u8"clip",
    u8"c", u8"iclip", u8"i", u8"blur", u8"bord", u8"be", u8"b", u8"kf", u8"ko", u8"kt",
    u8"k", u8"K", u8"move", u8"org", u8"pos", u8"p", u8"q", u8"r", u8"shad", u8"s",
    u8"t", u8"u"});

}

std::u8string ascii(std::string_view s)
{
    return {reinterpret_cast<const char8_t *>(s.data()), s.size()};
}

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

Flat Flattener::flatten(const LineRecord &line, std::size_t order)
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

const StyleValues *Flattener::styleNamed(u8sv name) const
{
    if (const auto it = m_styles.find(name); it != m_styles.end())
        return &it->second;
    if (const auto it = m_styles.find(u8sv(u8"Default")); it != m_styles.end())
        return &it->second;
    return nullptr;
}

void Flattener::read(u8sv text)
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
                run += u8"\u00A0";
                ++i;
                continue;
            }
        }
        run += c;
    }
    flush();
}

void Flattener::put(std::u8string text)
{
    if (m_drawing)
        return;
    m_rows.back().push_back({std::move(text), m_look, m_syllable});
    m_syllable = -1;
}

void Flattener::block(u8sv block)
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

void Flattener::tag(u8sv token)
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

std::vector<Row> Flattener::finishRows()
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

std::u8string clock(std::int64_t us, int hourDigits, char8_t fraction)
{
    const std::int64_t ms = roundedDiv(us, 1000);
    return padded(ms / 3600000, hourDigits) + u8":" + padded(ms / 60000 % 60, 2) + u8":" + padded(ms / 1000 % 60, 2) +
           std::u8string(1, fraction) + padded(ms % 1000, 3);
}

std::u8string decimal(double v)
{
    std::array<char, 32> buffer{};
    const auto r = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v, std::chars_format::fixed, 2);
    std::string s(buffer.data(), r.ptr);
    while (s.back() == '0')
        s.pop_back();
    if (s.back() == '.')
        s.pop_back();
    if (s == "-0")
        s = "0";
    return ascii(s);
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

std::u8string identifierFor(u8sv name)
{
    std::u8string id;
    for (const char8_t c : name)
        id += (c >= u8'a' && c <= u8'z') || (c >= u8'A' && c <= u8'Z') || (c >= u8'0' && c <= u8'9') || c == u8'-' ||
                      c == u8'_'
                  ? c
                  : u8'_';
    if (id.empty() || (id.front() >= u8'0' && id.front() <= u8'9') || id.front() == u8'-')
        id.insert(0, u8"s");
    return id;
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

}
