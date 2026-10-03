#include "hikari/core/clipboard_rows.h"
#include "hikari/core/conversion.h"
#include "hikari/core/text_projection.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/line_formats.h"
#include "hikari/core/srt.h"
#include "text_util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace hikari::core {

using namespace detail;

namespace {

// Legacy Dialogue::Format / SubsTime::form values (styles.h).
enum Format : int { Plain = 0, Ass = 1, Srt = 2, Tmp = 3, Mdvd = 4, Mpl2 = 5, Frame = 10 };

std::u8string str(u8sv s)
{
    return std::u8string(s);
}

std::u8string number(std::int64_t v)
{
    const std::string s = std::to_string(v);
    return std::u8string(s.begin(), s.end());
}

// wxString::Replace(from, to): every occurrence.
void replaceAll(std::u8string &text, u8sv from, u8sv to)
{
    for (std::size_t p = 0; (p = text.find(from, p)) != std::u8string::npos; p += to.size())
        text.replace(p, from.size(), to);
}

void eraseChar(std::u8string &text, char8_t c)
{
    std::erase(text, c);
}

char8_t lower(char8_t c)
{
    return c >= u8'A' && c <= u8'Z' ? static_cast<char8_t>(c - u8'A' + u8'a') : c;
}

bool equalsIgnoreCase(u8sv a, u8sv b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i]))
            return false;
    return true;
}

// wxRegEx ReplaceAll of `open lead ([^close]*) close`: `make` builds the
// replacement from the captured part (`lead` matched case-insensitively when asked).
template <typename Make>
void replaceDelimited(std::u8string &text, char8_t open, u8sv lead, char8_t close, char8_t exclude, bool icase,
                      Make make)
{
    std::u8string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == open && text.size() - i - 1 >= lead.size() &&
            (icase ? equalsIgnoreCase(u8sv(text).substr(i + 1, lead.size()), lead)
                   : u8sv(text).substr(i + 1, lead.size()) == lead)) {
            std::size_t j = i + 1 + lead.size();
            while (j < text.size() && text[j] != close && text[j] != exclude)
                ++j;
            if (j < text.size() && text[j] == close) {
                out += make(u8sv(text).substr(i + 1 + lead.size(), j - i - 1 - lead.size()));
                i = j + 1;
                continue;
            }
        }
        out += text[i++];
    }
    text = std::move(out);
}

// \{[^}]*\} (or <[^>]*>) removed.
void removeBlocks(std::u8string &text, char8_t open, char8_t close)
{
    replaceDelimited(text, open, u8"", close, close, false, [](u8sv) { return std::u8string(); });
}

// SubsTime: integer milliseconds, the original frame and the format it was read in.
struct LegacyTime {
    std::int64_t ms = 0;
    std::int64_t frame = 0;
    int form = Ass;
};

void setRawTime(LegacyTime &t, u8sv rawText, int form)
{
    t.form = form;
    const u8sv raw = trimRight(rawText); // raw.Trim() before everything else
    if (raw.empty()) {
        t.ms = 0;
        t.frame = 0;
        return;
    }
    if (form < Mdvd) {
        const std::size_t colon = raw.find(u8':');
        const std::int64_t hours = legacy::atoi(wxSubString(raw, 0, colon - 1));
        const std::int64_t minutes = legacy::atoi(wxSubString(raw, colon + 1, colon + 2));
        const std::int64_t seconds = legacy::atoi(wxSubString(raw, colon + 4, colon + 5));
        std::int64_t fraction = 0;
        if (form < Srt)
            fraction = legacy::atoi(wxSubString(raw, colon + 7, colon + 8)) * 10;
        else if (form == Srt)
            fraction = legacy::atoi(wxSubString(raw, colon + 7, colon + 9));
        t.ms = hours * 3'600'000 + minutes * 60'000 + seconds * 1'000 + fraction;
        return;
    }
    const auto result = static_cast<int>(legacy::atoi(raw));
    if (form == Frame) {
        t.frame = result;
    } else if (form == Mdvd) {
        t.frame = result;
        t.ms = static_cast<int>((static_cast<float>(result) / 23.976f) * 1000.f);
        if (t.frame < 0)
            t.frame = 0;
    } else {
        t.ms = std::int64_t{result} * 100;
    }
}

std::u8string rawTime(LegacyTime &t, int ft = Plain)
{
    if (ft == Plain)
        ft = t.form;
    if (ft < Srt)
        return legacy::assTimeText(t.ms);
    if (ft == Tmp)
        return legacy::tmpTimeText(t.ms);
    if (ft == Srt)
        return legacy::srtTimeText(t.ms);
    if (ft == Mdvd && !t.frame && t.ms)
        t.frame = static_cast<std::int64_t>(std::ceil(static_cast<float>(t.ms) * (25.f / 1000.f)));
    return number(ft != Mpl2 ? t.frame
                             : static_cast<std::int64_t>(std::ceil(static_cast<float>(t.ms) * (10.0f / 1000.0f))));
}

void changeFormat(LegacyTime &t, int format, float fps)
{
    if (format == t.form)
        return;
    if (format == Ass)
        t.ms = t.ms / 10 * 10; // ZEROIT
    if (format == Mdvd && t.form != Frame)
        t.frame = static_cast<std::int64_t>(std::ceil(static_cast<float>(t.ms) * (fps / 1000)));
    t.form = format;
}

// The legacy Dialogue fields the clipboard reads and writes.
struct Dialogue {
    int format = Ass;
    bool comment = false;
    bool nonDialogue = false;
    std::int64_t layer = 0;
    LegacyTime start;
    LegacyTime end{5000, 0, Ass}; // Dialogue(): End.mstime = 5000
    std::u8string style = u8"Default";
    std::u8string actor;
    bool bookmark = false;
    LineVisibility visibility = LineVisibility::Visible;
    GroupMarker group = GroupMarker::None;
    std::int64_t marginLeft = 0, marginRight = 0, marginVertical = 0;
    std::u8string effect;
    std::u8string text;
    std::u8string translation;
};

// wxStringTokenizer(ldial, ",", wxTOKEN_RET_EMPTY_ALL).
std::vector<u8sv> splitCommas(u8sv s)
{
    std::vector<u8sv> out;
    if (s.empty())
        return out;
    std::size_t from = 0;
    for (std::size_t p; (p = s.find(u8',', from)) != u8sv::npos; from = p + 1)
        out.push_back(s.substr(from, p - from));
    out.push_back(s.substr(from));
    return out;
}

bool replaceMarker(std::u8string &actor, u8sv marker)
{
    bool found = false;
    for (std::size_t p; (p = actor.find(marker)) != std::u8string::npos; found = true)
        actor.erase(p, marker.size());
    return found;
}

// [digits-]+ / [digits-]* (MicroDVD and MPL2 time fields).
std::size_t timeDigits(u8sv s, std::size_t pos)
{
    std::size_t end = pos;
    while (end < s.size() && ((s[end] >= u8'0' && s[end] <= u8'9') || s[end] == u8'-'))
        ++end;
    return end;
}

// ^\{([0-9-]+)\}\{([0-9-]*)\}([^\r\n]*) and the [] form.
bool matchBracketed(u8sv s, char8_t open, char8_t close, u8sv &first, u8sv &second, u8sv &rest)
{
    if (s.empty() || s[0] != open)
        return false;
    const std::size_t a = timeDigits(s, 1);
    if (a == 1 || a >= s.size() || s[a] != close || a + 1 >= s.size() || s[a + 1] != open)
        return false;
    const std::size_t b = timeDigits(s, a + 2);
    if (b >= s.size() || s[b] != close)
        return false;
    first = s.substr(1, a - 1);
    second = s.substr(a + 2, b - a - 2);
    std::size_t e = b + 1;
    while (e < s.size() && s[e] != u8'\r' && s[e] != u8'\n')
        ++e;
    rest = s.substr(b + 1, e - b - 1);
    return true;
}

// ^([0-9]+)[:;]([0-9]+)[:;]([0-9]+)[:;, ]([^\r\n]*)
bool matchTmp(u8sv s, std::u8string &time, u8sv &rest)
{
    std::size_t pos = 0;
    std::u8string out;
    for (int field = 0; field < 3; ++field) {
        const std::size_t from = pos;
        while (pos < s.size() && s[pos] >= u8'0' && s[pos] <= u8'9')
            ++pos;
        if (pos == from || pos >= s.size())
            return false;
        const char8_t sep = s[pos];
        const bool ok = field < 2 ? (sep == u8':' || sep == u8';')
                                  : (sep == u8':' || sep == u8';' || sep == u8',' || sep == u8' ');
        if (!ok)
            return false;
        if (field > 0)
            out += u8':';
        out += s.substr(from, pos - from);
        ++pos;
    }
    std::size_t e = pos;
    while (e < s.size() && s[e] != u8'\r' && s[e] != u8'\n')
        ++e;
    time = std::move(out);
    rest = s.substr(pos, e - pos);
    return true;
}

// Dialogue::SetRaw.
Dialogue setRaw(u8sv ldial)
{
    Dialogue d;
    if (startsWith(ldial, u8"Dialogue") || startsWith(ldial, u8"Comment")) {
        const auto tokens = splitCommas(ldial);
        if (tokens.size() >= 9) {
            d.comment = !startsWith(tokens[0], u8"Dialogue");
            const u8sv first = tokens[0];
            if (first.find(u8"arked=") == u8sv::npos) {
                const auto space = first.find(u8' ');
                d.layer = legacy::atoi(space == u8sv::npos ? u8sv() : first.substr(space + 1));
            } else {
                d.layer = legacy::atoi(first.substr(first.rfind(u8'=') + 1));
            }
            d.format = Ass;
            setRawTime(d.start, tokens[1], Ass);
            setRawTime(d.end, tokens[2], Ass);
            d.style = str(tokens[3]);
            std::u8string actor = str(tokens[4]);
            if (!actor.empty() && actor.front() == u8'[') {
                if (replaceMarker(actor, u8"[bookmark]")) {
                    d.bookmark = true;
                } else if (replaceMarker(actor, u8"[hidden]")) {
                    d.visibility = LineVisibility::Hidden;
                } else if (replaceMarker(actor, u8"[visible]")) {
                    d.visibility = LineVisibility::VisibleBlock;
                } else if (replaceMarker(actor, u8"[tree_closed]")) {
                    d.group = GroupMarker::Closed;
                    d.visibility = LineVisibility::Hidden;
                } else if (replaceMarker(actor, u8"[tree_opened]")) {
                    d.group = GroupMarker::Opened;
                } else if (replaceMarker(actor, u8"[tree_description]")) {
                    d.group = GroupMarker::Description;
                }
            }
            d.actor = str(trim(actor));
            d.marginLeft = legacy::atoi(tokens[5]);
            d.marginRight = legacy::atoi(tokens[6]);
            d.marginVertical = legacy::atoi(tokens[7]);
            d.effect = str(trim(tokens[8]));
            // Text: everything after the ninth comma.
            std::size_t pos = 0;
            for (int i = 0; i < 9 && pos != u8sv::npos; ++i) {
                pos = ldial.find(u8',', pos);
                if (pos != u8sv::npos)
                    ++pos;
            }
            d.text = pos == u8sv::npos ? std::u8string() : str(trim(ldial.substr(pos)));
            return d;
        }
    }
    d.layer = 0;
    d.marginLeft = d.marginRight = d.marginVertical = 0;
    u8sv first, second, rest;
    std::u8string tmpTime;
    if (ldial.find(u8" --> ") != u8sv::npos) {
        d.format = Srt;
        const std::size_t space = ldial.find(u8' ');
        setRawTime(d.start, ldial.substr(0, space), Srt);
        u8sv eend = ldial.substr(space + 1);
        const std::size_t next = eend.find(u8' ');
        eend = next == u8sv::npos ? u8sv() : eend.substr(next + 1);
        const std::size_t newline = eend.find(u8'\n');
        setRawTime(d.end, trimRight(eend.substr(0, newline)), Srt);
        d.text = newline == u8sv::npos ? std::u8string() : str(eend.substr(newline + 1));
        eraseChar(d.text, u8'\r');
        replaceAll(d.text, u8"\n", u8"\\N");
    } else if (matchBracketed(ldial, u8'{', u8'}', first, second, rest)) {
        d.format = Mdvd;
        setRawTime(d.start, first, Mdvd);
        setRawTime(d.end, second, Mdvd);
        d.text = str(trimLeft(rest));
    } else if (matchBracketed(ldial, u8'[', u8']', first, second, rest)) {
        d.format = Mpl2;
        setRawTime(d.start, first, Mpl2);
        setRawTime(d.end, second, Mpl2);
        d.text = str(trimLeft(rest));
    } else if (matchTmp(ldial, tmpTime, rest)) {
        d.format = Tmp;
        setRawTime(d.start, tmpTime, Tmp);
        d.text = str(trimLeft(rest));
    } else if (startsWith(ldial, u8";") ||
               (startsWith(ldial, u8"{") && ldial.ends_with(u8'}') && std::ranges::count(ldial, u8'{') == 1 &&
                std::ranges::count(ldial, u8'}') == 1)) {
        d.nonDialogue = true;
        d.comment = true;
        d.style = u8"Default";
        d.text = str(trimRight(ldial));
        d.format = Ass;
        d.visibility = LineVisibility::Hidden;
    } else {
        d.format = Plain;
        d.style = u8"Default";
        d.text = str(ldial);
        eraseChar(d.text, u8'\r');
        replaceAll(d.text, u8"\n", u8"\\N");
        d.text = str(trimRight(d.text));
    }
    return d;
}

// Dialogue::StartsWith / StartsWithNoBlock / EndsWith (tag blocks and spaces skipped).
bool startsWithChar(u8sv text, char8_t ch, std::size_t &pos, bool skipBlocks)
{
    bool block = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char8_t c = text[i];
        if (skipBlocks && c == u8'{')
            block = true;
        else if (skipBlocks && c == u8'}')
            block = false;
        else if (isSpace(c) || block)
            continue;
        else if (c == ch) {
            pos = i;
            return true;
        } else
            return false;
    }
    return false;
}

bool endsWithChar(u8sv text, char8_t ch, std::size_t &pos)
{
    bool block = false;
    for (std::size_t i = pos; i > 0; --i) {
        const char8_t c = text[i - 1];
        if (c == u8'{')
            block = false;
        else if (c == u8'}')
            block = true;
        else if (isSpace(c) || block)
            continue;
        else if (c == ch) {
            pos = i - 1;
            return true;
        } else
            return false;
    }
    return false;
}

void replaceSlashesToItalics(std::u8string &text)
{
    std::size_t textPos = 0;
    std::u8string copy = text;
    std::int64_t diff = 0;
    std::size_t slashPos = 0;
    while (textPos < text.size()) {
        textPos = copy.find(u8'|');
        if (textPos == std::u8string::npos)
            textPos = copy.size();
        if (startsWithChar(copy, u8'/', slashPos, true)) {
            text.replace(static_cast<std::size_t>(static_cast<std::int64_t>(slashPos) + diff), 1, u8"{\\i1}");
            diff += 4;
        }
        slashPos = textPos;
        if (endsWithChar(copy, u8'/', slashPos)) {
            text.erase(static_cast<std::size_t>(static_cast<std::int64_t>(slashPos) + diff), 1);
            diff -= 1;
        }
        textPos += 1;
        if (textPos >= copy.size())
            break;
        diff += static_cast<std::int64_t>(textPos);
        copy = copy.substr(textPos);
    }
}

void addResetOnMdvdWraps(std::u8string &text, u8sv prefix)
{
    std::size_t textPos = 0;
    std::u8string copy = text;
    std::size_t diff = 0;
    std::size_t seekPos = 0;
    bool needAddPrefix = true;
    const bool addPrefixes = !prefix.empty();
    while (textPos < text.size()) {
        textPos = copy.find(u8'|');
        if (needAddPrefix && addPrefixes) {
            text.insert(diff, u8"{" + str(prefix) + u8"}");
            diff += prefix.size() + 2;
            needAddPrefix = false;
        }
        if (textPos == std::u8string::npos)
            break;
        if (startsWithChar(copy, u8'{', seekPos, false)) {
            text.insert(textPos + diff, u8"{\\r}");
            needAddPrefix = true;
            diff += 4;
        }
        textPos += 1;
        diff += textPos;
        copy = copy.substr(textPos);
    }
}

bool hasDrawing(u8sv text)
{
    // \\p[0-9]+
    for (std::size_t p = 0; (p = text.find(u8"\\p", p)) != u8sv::npos; ++p)
        if (p + 2 < text.size() && text[p + 2] >= u8'0' && text[p + 2] <= u8'9')
            return true;
    return false;
}

// wxRegEx ReplaceAll of a fixed sequence of one-character classes, e.g.
// \{y[:+]([ib])\} as {"{", "y", ":+", "ib", "}"}; `make` gets the character
// matched by the class at `capture`.
template <typename Make>
void replaceSequence(std::u8string &text, const std::vector<u8sv> &classes, std::size_t capture, bool icase, Make make)
{
    std::u8string out;
    std::size_t i = 0;
    auto inClass = [&](char8_t c, u8sv cls) {
        for (char8_t k : cls)
            if (icase ? lower(c) == lower(k) : c == k)
                return true;
        return false;
    };
    while (i < text.size()) {
        bool matched = i + classes.size() <= text.size();
        for (std::size_t k = 0; matched && k < classes.size(); ++k)
            matched = inClass(text[i + k], classes[k]);
        if (matched) {
            out += make(text[i + capture]);
            i += classes.size();
        } else {
            out += text[i++];
        }
    }
    text = std::move(out);
}

// Dialogue::Convert.
void convert(Dialogue &d, int type, const PasteConversion &conversion)
{
    if (d.format == Plain && type == Ass)
        return;
    if (d.format == Tmp && d.end.ms == 0) {
        d.end = d.start;
        d.end.ms += 2000;
    }
    changeFormat(d.start, type, conversion.fps);
    changeFormat(d.end, type, conversion.fps);
    if (type < Srt) {
        d.layer = 0;
        d.style = conversion.style;
        d.actor.clear();
        d.marginLeft = d.marginRight = d.marginVertical = 0;
        d.effect.clear();
        std::u8string tmp = d.text;
        if (d.format != Srt) {
            replaceSequence(tmp, {u8"{", u8"y", u8":+", u8"ib", u8"}"}, 3, true,
                            [](char8_t c) { return u8"{\\" + std::u8string(1, c) + u8"1}"; });
            replaceDelimited(tmp, u8'{', u8"f:", u8'}', u8'}', true,
                             [](u8sv v) { return u8"{\\fn" + str(v) + u8"}"; });
            replaceDelimited(tmp, u8'{', u8"s:", u8'}', u8'}', true,
                             [](u8sv v) { return u8"{\\fs" + str(v) + u8"}"; });
            replaceDelimited(tmp, u8'{', u8"c:$", u8'}', u8'}', true,
                             [](u8sv v) { return u8"{\\1c&H" + str(v) + u8"&}"; });
            d.text = tmp;
            replaceSlashesToItalics(d.text);
            addResetOnMdvdWraps(d.text, conversion.prefix);
            replaceAll(d.text, u8"|", u8"\\N");
            replaceAll(d.text, u8"}{", u8"");
        } else {
            replaceSequence(tmp, {u8"<", u8"ibu", u8">"}, 1, false,
                            [](char8_t c) { return u8"{\\" + std::u8string(1, c) + u8"1}"; });
            replaceSequence(tmp, {u8"<", u8"/", u8"ibu", u8">"}, 2, false,
                            [](char8_t c) { return u8"{\\" + std::u8string(1, c) + u8"0}"; });
            replaceAll(tmp, u8"<br>", u8"\\N");
            d.text = conversion.prefix + tmp;
        }
    } else if (d.format < Srt) {
        std::u8string tmp = d.text;
        replaceAll(tmp, u8"\\h", u8" ");
        if (hasDrawing(tmp)) {
            d.text.clear();
            d.format = type;
            return;
        }
        if (type == Srt) {
            replaceSequence(tmp, {u8"\\", u8"ibu", u8"1"}, 1, false,
                            [](char8_t c) { return u8"}<" + std::u8string(1, c) + u8">{"; });
            replaceSequence(tmp, {u8"\\", u8"ibu", u8"0"}, 1, false,
                            [](char8_t c) { return u8"}</" + std::u8string(1, c) + u8">{"; });
        }
        removeBlocks(tmp, u8'{', u8'}');
        if (type != Srt)
            replaceAll(tmp, u8"\\N", u8"|");
        d.text = tmp;
    } else if (d.format == Srt) {
        std::u8string tmp = d.text;
        replaceAll(tmp, u8"\\N", u8"|");
        replaceAll(tmp, u8"<br>", u8"|");
        if (type == Mdvd) {
            replaceAll(tmp, u8"<i>", u8"{y:i}");
            replaceAll(tmp, u8"<b>", u8"{y:b}");
        } else if (type == Mpl2) {
            replaceAll(tmp, u8"<i>", u8"/");
        }
        removeBlocks(tmp, u8'<', u8'>');
        d.text = tmp;
    } else if (type == Srt) {
        // Legacy also checks for MicroDVD and MPL2 targets here, which never applies.
        replaceAll(d.text, u8"|", u8"\\N");
    } else if (d.format == Mdvd && type == Mpl2) {
        replaceAll(d.text, u8"{y:i}", u8"/");
        removeBlocks(d.text, u8'{', u8'}');
    } else if (d.format == Mpl2 && type == Mdvd) {
        replaceAll(d.text, u8"/", u8"{y:i}");
    } else if (d.format == Mdvd) {
        removeBlocks(d.text, u8'{', u8'}');
    } else if (d.format == Mpl2) {
        eraseChar(d.text, u8'/');
    }
    d.format = type;
}

Dialogue fromLine(const LineRecord &line, SubtitleFormat format)
{
    Dialogue d;
    d.format = static_cast<int>(format);
    d.comment = line.comment;
    d.layer = line.layer.value;
    d.start = LegacyTime{line.start.value.microseconds() / 1000, line.startFrame.value_or(0), d.format};
    d.end = LegacyTime{line.end.value.microseconds() / 1000, line.endFrame.value_or(0), d.format};
    d.style = line.style;
    d.actor = line.actor;
    d.bookmark = line.bookmark;
    d.visibility = line.visibility;
    d.group = line.group;
    d.marginLeft = line.marginLeft.value;
    d.marginRight = line.marginRight.value;
    d.marginVertical = line.marginVertical.value;
    d.effect = line.effect;
    d.text = line.text;
    d.translation = line.translation;
    return d;
}

LineRecord toLine(const Dialogue &d, SubtitleFormat format)
{
    LineRecord line;
    line.comment = d.comment;
    line.layer.value = d.layer;
    line.start.value = DocumentTime(d.start.ms * 1000);
    line.end.value = DocumentTime(d.end.ms * 1000);
    if (format == SubtitleFormat::MicroDvd) {
        line.startFrame = d.start.frame;
        line.endFrame = d.end.frame;
    }
    line.style = d.style;
    line.actor = d.actor;
    line.bookmark = d.bookmark;
    line.visibility = d.visibility;
    line.group = d.group;
    line.marginLeft.value = d.marginLeft;
    line.marginRight.value = d.marginRight;
    line.marginVertical.value = d.marginVertical;
    line.effect = d.effect;
    line.text = d.text;
    line.translation = d.translation;
    return line;
}

// Dialogue::GetRaw, line terminator included.
std::u8string getRaw(Dialogue d, const LineRecord &line, bool translation)
{
    std::u8string out;
    if (d.format < Srt) {
        LineRecord copy = line;
        if (translation)
            copy.text = line.translation;
        out = legacy::assLineText(copy);
    } else if (d.format == Mdvd) {
        out = u8"{" + rawTime(d.start, Mdvd) + u8"}{" + rawTime(d.end, Mdvd) + u8"}" + d.text;
    } else if (d.format == Mpl2) {
        out = u8"[" + rawTime(d.start, Mpl2) + u8"][" + rawTime(d.end, Mpl2) + u8"]" + d.text;
    } else if (d.format == Tmp) {
        out = rawTime(d.start, Tmp) + u8":" + d.text;
    } else {
        std::u8string text = d.text;
        replaceAll(text, u8"\\N", u8"\r\n");
        out = rawTime(d.start, Srt) + u8" --> " + rawTime(d.end, Srt) + u8"\r\n" + text + u8"\r\n";
    }
    return out + u8"\r\n";
}

// Dialogue::GetCols.
std::u8string getCols(Dialogue d, int cols, bool translation)
{
    std::u8string text = translation ? d.translation : d.text;
    if (cols & column::TextWithoutTags) {
        // \{[^\{]*\}
        replaceDelimited(text, u8'{', u8"", u8'}', u8'{', false, [](u8sv) { return std::u8string(); });
        cols |= column::Text;
    }
    std::u8string out;
    if (d.format < Srt) {
        if (cols & column::Layer)
            out += number(d.layer) + u8",";
        if (cols & column::Start)
            out += rawTime(d.start) + u8",";
        if (cols & column::End)
            out += rawTime(d.end) + u8",";
        if (cols & column::Style)
            out += d.style + u8",";
        if (cols & column::Actor)
            out += d.actor + u8",";
        if (cols & column::MarginLeft)
            out += number(d.marginLeft) + u8",";
        if (cols & column::MarginRight)
            out += number(d.marginRight) + u8",";
        if (cols & column::MarginVertical)
            out += number(d.marginVertical) + u8",";
        if (cols & column::Effect)
            out += d.effect + u8",";
        if (cols & column::Text)
            out += text;
    } else if (d.format == Mdvd || d.format == Mpl2) {
        const char8_t open = d.format == Mdvd ? u8'{' : u8'[', close = d.format == Mdvd ? u8'}' : u8']';
        if (cols & column::Start)
            out += open + rawTime(d.start) + close;
        if (cols & column::End)
            out += open + rawTime(d.end) + close;
        if (cols & column::Text)
            out += text;
    } else if (d.format == Tmp) {
        if (cols & column::Start)
            out += rawTime(d.start) + u8":";
        if (cols & column::Text)
            out += text;
    } else {
        replaceAll(text, u8"\\N", u8"\r\n");
        if (cols & column::Start)
            out += rawTime(d.start);
        if (cols & column::End) {
            if (cols & column::Start)
                out += u8" --> ";
            out += rawTime(d.end);
            if (cols & column::Text)
                out += u8"\r\n";
        }
        if (cols & column::Text)
            out += text;
    }
    return out + u8"\r\n";
}

bool isNumber(u8sv s)
{
    for (char8_t c : s)
        if (c < u8'0' || c > u8'9')
            return false;
    return true;
}

std::unordered_map<std::uint64_t, std::size_t> rowsOf(const Document &document)
{
    std::unordered_map<std::uint64_t, std::size_t> rows;
    const auto lines = document.lines();
    for (std::size_t i = 0; i < lines.size(); ++i)
        rows.emplace(lines[i]->id.value, i);
    return rows;
}

const LineRecord *find(const Document &document, LineId id)
{
    for (const auto *line : document.lines())
        if (line->id == id)
            return line;
    return nullptr;
}

} // namespace

std::u8string clipboardRows(const Document &document, const std::vector<LineId> &lines, bool translationMode,
                            bool numberSrtCues)
{
    const auto rows = rowsOf(document);
    std::u8string out;
    for (const auto id : lines) {
        const LineRecord *line = find(document, id);
        if (!line)
            continue;
        // Legacy numbers SRT cues by Document row.
        if (numberSrtCues && document.format() == SubtitleFormat::Srt)
            out += number(static_cast<std::int64_t>(rows.at(id.value)) + 1) + u8"\r\n";
        const bool translation = translationMode && !line->translation.empty();
        out += getRaw(fromLine(*line, document.format()), *line, translation);
    }
    return out;
}

std::u8string clipboardColumns(const Document &document, const std::vector<LineId> &lines, int columns,
                               bool translationMode)
{
    std::u8string out;
    for (const auto id : lines)
        if (const LineRecord *line = find(document, id))
            out += getCols(fromLine(*line, document.format()), columns, translationMode && !line->translation.empty());
    return out;
}

std::vector<LineRecord> parseClipboardRows(std::u8string_view text, SubtitleFormat format,
                                           const PasteConversion &conversion, bool intoTranslation)
{
    // wxStringTokenizer(text, "\n\r", wxTOKEN_STRTOK): no empty tokens.
    std::vector<u8sv> tokens;
    for (std::size_t i = 0; i < text.size();) {
        std::size_t j = i;
        while (j < text.size() && text[j] != u8'\n' && text[j] != u8'\r')
            ++j;
        if (j > i)
            tokens.push_back(text.substr(i, j - i));
        i = j + 1;
    }
    std::vector<LineRecord> out;
    std::size_t next = 0;
    std::u8string pending; // the token read ahead while gathering an SRT block
    const int target = static_cast<int>(format);
    while (next < tokens.size()) {
        std::u8string token = pending.empty() ? str(trim(tokens[next++])) : pending;
        if (isNumber(token)) {
            // A cue number: the lines up to the next number are one SRT block.
            token.clear();
            while (next < tokens.size()) {
                pending = str(trim(tokens[next++]));
                if (isNumber(pending))
                    break;
                token += u8"\r\n" + pending;
            }
        }
        Dialogue d = setRaw(token);
        if (intoTranslation)
            d.translation = d.text;
        if (d.format != target)
            convert(d, target, conversion);
        if (d.nonDialogue) {
            d.nonDialogue = false;
            d.comment = false;
        }
        out.push_back(toLine(d, format));
    }
    return out;
}

LineRecord dialogueFromRaw(std::u8string_view raw, SubtitleFormat format, const PasteConversion &conversion)
{
    Dialogue d = setRaw(raw);
    if (d.format != static_cast<int>(format))
        convert(d, static_cast<int>(format), conversion);
    return toLine(d, format);
}


std::optional<ConversionResult> convertDocument(const Document &source, SubtitleFormat target,
                                                const ConversionOptions &options, const ConversionCollate &collate)
{
    const int type = static_cast<int>(target);
    const int from = static_cast<int>(source.format());
    if (target == source.format() || target == SubtitleFormat::PlainText || options.fps < 1)
        return std::nullopt;
    ConversionReport report;
    PasteConversion conversion;
    conversion.style = options.style.name;
    conversion.prefix = options.prefix;
    conversion.fps = static_cast<float>(options.fps);

    struct Item {
        LineId id;
        Dialogue d;
    };
    std::vector<Item> items;
    for (const auto *line : source.lines())
        items.push_back({line->id, fromLine(*line, source.format())});

    // SubsGrid::Convert's loop.
    std::vector<Item> out;
    std::size_t lastIndex = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (type > Ass && from < Srt && items[i].d.comment) {
            ++report.commentsRemoved;
            continue;
        }
        Item item = items[i];
        const Dialogue before = item.d;
        convert(item.d, type, conversion);
        if (item.d.text != before.text)
            ++report.textChanged;
        if (from < Srt && type >= Srt) {
            if (before.text.find(u8"\\p") != std::u8string::npos && hasDrawing(before.text) && item.d.text.empty())
                ++report.drawingsCleared;
            if (before.layer || before.style != u8"Default" || !before.actor.empty() || before.marginLeft ||
                before.marginRight || before.marginVertical || !before.effect.empty())
                ++report.fieldsDropped;
            if (before.bookmark || before.visibility != LineVisibility::Visible || before.group != GroupMarker::None)
                ++report.markersDropped;
        } else if (type < Srt) {
            ++report.fieldsDropped; // every Line gets the conversion Style
        }
        if ((options.newEndTimes && type != Tmp) || from == Tmp) {
            if (lastIndex != static_cast<std::size_t>(-1)) {
                Dialogue &last = out[lastIndex].d;
                if (last.end.ms > item.d.start.ms)
                    last.end = item.d.start;
            }
            const auto length = static_cast<std::int64_t>(toUtf16(item.d.text).size());
            std::int64_t newEnd = options.timePerCharacter * length;
            if (newEnd < 1000)
                newEnd = 1000;
            newEnd += item.d.start.ms;
            // SubsTime::NewTime: MicroDVD frames at the video's rate.
            item.d.end.ms = newEnd < 0 ? 0 : newEnd;
            if (item.d.end.form == Mdvd)
                item.d.end.frame = static_cast<std::int64_t>(
                    std::ceil(static_cast<float>(item.d.end.ms) * (static_cast<float>(options.videoFps) / 1000.f)));
        }
        out.push_back(std::move(item));
        lastIndex = out.size() - 1;
    }
    for (std::size_t i = 0, j = 0; i < items.size() && j < out.size(); ++i) {
        if (items[i].id != out[j].id)
            continue;
        if (items[i].d.start.ms != out[j].d.start.ms || items[i].d.end.ms != out[j].d.end.ms)
            ++report.timesChanged;
        ++j;
    }

    if (from == Ass) {
        report.headerDropped = type != Ass;
        const auto before = out;
        std::stable_sort(out.begin(), out.end(), [&](const Item &a, const Item &b) {
            if (a.d.start.ms != b.d.start.ms)
                return a.d.start.ms < b.d.start.ms;
            if (a.d.end.ms != b.d.end.ms)
                return a.d.end.ms < b.d.end.ms;
            return collate ? collate(a.d.text, b.d.text) < 0 : a.d.text < b.d.text;
        });
        for (std::size_t i = 0; i < out.size(); ++i)
            report.reordered = report.reordered || out[i].id != before[i].id;
        // Equal neighbours: the earlier one goes; empty texts go (not the first Line).
        std::size_t last = 0, i = 1;
        while (i < out.size()) {
            if (out[last].d.start.ms == out[i].d.start.ms && out[last].d.end.ms == out[i].d.end.ms &&
                out[last].d.text == out[i].d.text) {
                out.erase(out.begin() + static_cast<std::ptrdiff_t>(i - 1));
                ++report.duplicatesRemoved;
                last = i - 1;
                continue;
            }
            if (out[i].d.text.empty()) {
                out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
                ++report.emptyRemoved;
                continue;
            }
            last = i;
            ++i;
        }
    }

    // The file legacy SaveFile writes for the converted Lines.
    std::u8string text;
    if (type == Ass) {
        // LoadDefault, then CONVERT_RESOLUTION_* (legacy sets the width when
        // the height is empty) and the conversion Style.
        std::u8string resx = options.resolutionWidth, resy = options.resolutionHeight;
        if (resx.empty())
            resx = u8"1280";
        if (resy.empty())
            resx = u8"720";
        text = u8"[Script Info]\r\nTitle: HikariSub Ass File\r\nPlayResX: " + resx + u8"\r\nPlayResY: " + resy +
               u8"\r\nScaledBorderAndShadow: yes\r\nWrapStyle: 0\r\nScriptType: v4.00+\r\n"
               u8"Last Style Storage: Default\r\nYCbCr Matrix: TV.601\r\n\r\n[V4+ Styles]\r\n"
               u8"Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
               u8"Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
               u8"MarginL, MarginR, MarginV, Encoding\r\nStyle: ";
        const auto fields = legacy::styleRawFields(options.style);
        for (std::size_t i = 0; i < fields.size(); ++i)
            text += (i ? u8"," : u8"") + fields[i];
        text += u8"\r\n\r\n[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (type == Srt)
            text += number(static_cast<std::int64_t>(i + 1)) + u8"\r\n";
        const LineRecord line = toLine(out[i].d, target);
        text += getRaw(out[i].d, line, false);
    }
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    LoadResult loaded = type == Ass ? loadAss(bytes) : type == Srt ? loadSrt(bytes) : loadLineFormats(bytes);
    Document &document = loaded.document;
    if (target != SubtitleFormat::Ass && target != SubtitleFormat::Srt)
        DocumentBuilder::setFormat(document, target);
    if (target == SubtitleFormat::MicroDvd)
        if (const auto rate = FrameRate::make(std::llround(options.fps * 1000), 1000))
            document.setFrameRate(*rate);
    // The surviving Lines keep their LineIds when the file reads back one to one.
    std::uint64_t next = DocumentBuilder::peekNextLineId(source);
    std::size_t k = 0;
    std::vector<LineRecord *> readBack;
    for (auto &section : DocumentBuilder::sections(document))
        for (auto &record : section.records)
            if (auto *line = std::get_if<LineRecord>(&record))
                readBack.push_back(line);
    if (readBack.size() == out.size())
        for (auto *line : readBack)
            line->id = out[k++].id;
    else
        for (auto *line : readBack)
            line->id = LineId{next++};
    DocumentBuilder::setNextLineId(document, next);
    return ConversionResult{std::move(document), report};
}

} // namespace hikari::core
