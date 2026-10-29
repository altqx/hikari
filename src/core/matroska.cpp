#include "hikari/core/matroska.h"

#include "hikari/core/ass_load.h"
#include "hikari/core/ass_save.h"
#include "hikari/core/clipboard_rows.h"
#include "hikari/core/srt.h"
#include "hikari/core/style.h"
#include "text_util.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace hikari::core {

using namespace detail;

namespace {

std::u8string str(u8sv s)
{
    return std::u8string(s);
}

long find(u8sv s, char8_t c)
{
    const std::size_t p = s.find(c);
    return p == u8sv::npos ? -1 : static_cast<long>(p);
}

u8sv left(u8sv s, long pos)
{
    return s.substr(0, static_cast<std::size_t>(pos));
}

u8sv mid(u8sv s, long pos)
{
    const auto p = static_cast<std::size_t>(pos);
    return p > s.size() ? u8sv() : s.substr(p);
}

void toLong(u8sv s, long &val)
{
    const std::string text(reinterpret_cast<const char *>(s.data()), s.size());
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || errno == ERANGE)
        return;
    val = parsed;
}

std::int32_t toInt(std::int64_t v)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(v)));
}

std::int64_t newTime(std::int32_t ms)
{
    return ms < 0 ? 0 : ms;
}

struct ScriptInfo {
    std::u8string key, value;
};

void addSInfo(std::vector<ScriptInfo> &info, std::u8string key, std::u8string value)
{
    for (auto &i : info)
        if (i.key == key) {
            i.value = std::move(value);
            return;
        }
    info.push_back({std::move(key), std::move(value)});
}

void addSInfo(std::vector<ScriptInfo> &info, u8sv raw)
{
    const std::size_t colon = raw.find(u8':');
    const u8sv key = colon == u8sv::npos ? raw : raw.substr(0, colon);
    const u8sv value = colon == u8sv::npos ? u8sv() : raw.substr(colon + 1);
    addSInfo(info, str(trim(key)), str(trim(value)));
}

bool sameLine(const LineRecord &a, const LineRecord &b)
{
    return a.comment == b.comment && a.layer.value == b.layer.value && a.start.value == b.start.value &&
           a.end.value == b.end.value && a.style == b.style && a.actor == b.actor && a.bookmark == b.bookmark &&
           a.visibility == b.visibility && a.group == b.group && a.marginLeft.value == b.marginLeft.value &&
           a.marginRight.value == b.marginRight.value && a.marginVertical.value == b.marginVertical.value &&
           a.effect == b.effect && a.text == b.text && a.translation == b.translation;
}

void copyFields(LineRecord &to, const LineRecord &from)
{
    to.comment = from.comment;
    to.layer = from.layer;
    to.start = from.start;
    to.end = from.end;
    to.style = from.style;
    to.actor = from.actor;
    to.bookmark = from.bookmark;
    to.visibility = from.visibility;
    to.group = from.group;
    to.marginLeft = from.marginLeft;
    to.marginRight = from.marginRight;
    to.marginVertical = from.marginVertical;
    to.effect = from.effect;
    to.text = from.text;
    to.translation = from.translation;
}

}

namespace legacy {

std::u8string utf8OrEmpty(std::string_view bytes)
{
    if (!validUtf8(reinterpret_cast<const std::byte *>(bytes.data()), bytes.size()))
        return {};
    return std::u8string(reinterpret_cast<const char8_t *>(bytes.data()), bytes.size());
}

}

MatroskaCodec matroskaCodec(std::u8string_view codecName)
{
    if (codecName == u8"ass")
        return MatroskaCodec::Ass;
    if (codecName == u8"ssa")
        return MatroskaCodec::Ssa;
    return MatroskaCodec::Text;
}

std::u8string matroskaLine(std::int64_t start, std::int64_t duration, std::string_view line, MatroskaCodec codec)
{
    std::u8string blockString = legacy::utf8OrEmpty(line);
    std::int32_t startTime = toInt(start);
    std::int32_t endTime = toInt(static_cast<std::int64_t>(static_cast<std::uint64_t>(std::int64_t{startTime}) +
                                                         static_cast<std::uint64_t>(duration)));
    const bool ass = codec != MatroskaCodec::Text;
    if (ass) {
        startTime = toInt(std::int64_t{startTime} + 5);
        endTime = toInt(std::int64_t{endTime} + 5);
        startTime = startTime / 10 * 10;
        endTime = endTime / 10 * 10;
    }
    const std::int64_t subStart = newTime(startTime), subEnd = newTime(endTime);
    if (!ass)
        return legacy::srtTimeText(subStart) + u8" --> " + legacy::srtTimeText(subEnd) + u8"\r\n" + blockString;
    long pos = find(blockString, u8',');
    blockString = str(mid(blockString, pos + 1));
    pos = find(blockString, u8',');
    long layer = 0;
    // MUST KILL: pos -1 (no comma) passes, and left/mid read it as the whole string
    if (pos) {
        toLong(left(blockString, pos), layer);
        blockString = str(mid(blockString, pos + 1));
    }
    if (blockString.empty())
        blockString = u8"Default,,0000,0000,0000,,";
    if (!startsWith(blockString, u8","))
        blockString.insert(0, u8",");
    const std::string number = std::to_string(layer);
    return u8"Dialogue: " + std::u8string(number.begin(), number.end()) + u8"," + legacy::assTimeText(subStart) +
           u8"," + legacy::assTimeText(subEnd) + blockString;
}

Document matroskaDocument(std::string_view codecPrivate, const std::vector<std::u8string> &lines, MatroskaCodec codec)
{
    std::vector<ScriptInfo> info;
    std::vector<std::u8string> styles;
    std::vector<LegacyDialogue> dialogues;
    if (codec != MatroskaCodec::Text) {
        const std::u8string priv = legacy::utf8OrEmpty(codecPrivate);
        int type = 0;
        for (std::size_t i = 0; i < priv.size();) {
            std::size_t j = i;
            while (j < priv.size() && priv[j] != u8'\r' && priv[j] != u8'\n')
                ++j;
            const u8sv next = u8sv(priv).substr(i, j - i);
            i = j + 1;
            if (next.empty())
                continue;
            if (startsWith(next, u8"Style:")) {
                const auto fields = legacy::styleRawFields(legacy::decodeStyle(next, codec == MatroskaCodec::Ssa));
                std::u8string raw = u8"Style: ";
                for (std::size_t k = 0; k < fields.size(); ++k)
                    raw += (k ? u8"," : u8"") + fields[k];
                styles.push_back(raw + u8"\r\n");
                type = 1;
            } else if (startsWith(next, u8"Comment:")) {
                dialogues.push_back(legacyDialogue(next));
                type = 2;
            } else if (type == 0 && !startsWith(next, u8";") && !startsWith(next, u8"[") &&
                       !startsWith(next, u8"Format:")) {
                addSInfo(info, next);
            }
        }
    }
    for (const auto &line : lines)
        dialogues.push_back(legacyDialogue(line));
    if (codec == MatroskaCodec::Ass) {
        std::u8string matrix;
        for (const auto &i : info)
            if (i.key == u8"YCbCr Matrix") {
                matrix = i.value;
                break;
            }
        if (matrix.empty() || matrix == u8"None")
            addSInfo(info, u8"YCbCr Matrix", u8"TV.601");
    }
    int format = 1;
    for (const auto &d : dialogues)
        if (!d.nonDialogue && d.format != 0) {
            format = d.format;
            break;
        }
    const bool srt = format == 2;
    std::u8string text;
    if (!srt) {
        text = u8"[Script Info]\r\n";
        for (const auto &i : info)
            text += i.key + u8": " + i.value + u8"\r\n";
        text += u8"\r\n[V4+ Styles]\r\n"
                u8"Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, "
                u8"Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, "
                u8"MarginL, MarginR, MarginV, Encoding\r\n";
        for (const auto &s : styles)
            text += s;
        text += u8"\r\n[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\n";
    }
    for (std::size_t i = 0; i < dialogues.size(); ++i) {
        if (srt) {
            const std::string n = std::to_string(i + 1);
            text += std::u8string(n.begin(), n.end()) + u8"\r\n";
        }
        text += dialogues[i].raw;
    }
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    Document document = srt ? loadSrt(bytes).document : loadAss(bytes).document;
    // MUST KILL: writes a file, loads it, then patches or replaces its Lines with the parsed ones
    std::vector<LineId> loaded;
    std::vector<bool> same;
    for (const auto *l : document.lines()) {
        same.push_back(loaded.size() < dialogues.size() && sameLine(*l, dialogues[loaded.size()].line));
        loaded.push_back(l->id);
    }
    if (loaded.size() == dialogues.size()) {
        for (std::size_t i = 0; i < loaded.size(); ++i)
            if (!same[i])
                document.editLine(loaded[i], [&](LineRecord &l) { copyFields(l, dialogues[i].line); });
    } else {
        for (const auto id : loaded)
            document.removeLine(id);
        for (const auto &d : dialogues)
            document.appendLine(d.line);
    }
    return document;
}

}
