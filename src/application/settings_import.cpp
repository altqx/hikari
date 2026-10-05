#include "hikari/application/settings_import.h"

#include "hikari/application/automation_hotkeys.h"
#include "hikari/application/update_check.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

namespace hikari::application::settings_import {

namespace {

constexpr std::string_view kWindowLetters = "GSEVA";
constexpr std::string_view kCrashMarker = "___Program Crashed___";
// The legacy program name the converters write in their header line; only
// its presence matters (it is one more record SetRawOptions counts).
constexpr std::string_view kConverterProgname = "HikariSub";

// ---- text helpers

bool isSpace(char c)
{
    // wxString::Trim: wxSafeIsspace (" \t\n\v\f\r").
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

std::string_view trimLeft(std::string_view s)
{
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    return s;
}

std::string_view trimRight(std::string_view s)
{
    while (!s.empty() && isSpace(s.back()))
        s.remove_suffix(1);
    return s;
}

std::string_view trim(std::string_view s)
{
    return trimRight(trimLeft(s));
}

// wxString::BeforeFirst / AfterFirst: the whole text / "" without `c`.
std::string_view beforeFirst(std::string_view s, char c)
{
    const auto at = s.find(c);
    return at == std::string_view::npos ? s : s.substr(0, at);
}

std::string_view afterFirst(std::string_view s, char c)
{
    const auto at = s.find(c);
    return at == std::string_view::npos ? std::string_view() : s.substr(at + 1);
}

// The code points of UTF-8 text (wxString's characters on Linux; on Windows
// they are UTF-16 units, which differ only beyond the BMP).
std::size_t codePoints(std::string_view s)
{
    return std::size_t(std::count_if(s.begin(), s.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

// The byte offset of code point `n` (or the end).
std::size_t offsetOf(std::string_view s, std::size_t n)
{
    std::size_t i = 0;
    for (; i < s.size() && n > 0; ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80 && i > 0)
            --n;
        if (n == 0)
            return i;
    }
    // Skip to the start of the next code point.
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
        ++i;
    return i;
}

// wxString::Mid(first, count) in code points.
std::string_view mid(std::string_view s, std::size_t first, std::size_t count = std::string_view::npos)
{
    const std::size_t begin = first == 0 ? 0 : offsetOf(s, first);
    if (begin >= s.size())
        return {};
    std::string_view rest = s.substr(begin);
    if (count == std::string_view::npos)
        return rest;
    return rest.substr(0, count == 0 ? 0 : offsetOf(rest, count));
}

struct Token {
    int line = 0;
    std::string text;
};

// wxStringTokenizer(text, "\n") in wxTOKEN_STRTOK mode (the default for
// whitespace delimiters): the non-empty pieces, with their line numbers.
std::vector<Token> strtokLines(std::string_view text, int firstLine)
{
    std::vector<Token> out;
    int line = firstLine;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const auto end = text.find('\n', pos);
        const std::string_view piece = text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
        if (!piece.empty())
            out.push_back({line, std::string(piece)});
        if (end == std::string_view::npos)
            break;
        pos = end + 1;
        ++line;
    }
    return out;
}

// ---- encodings

bool validUtf8(std::string_view s)
{
    std::size_t i = 0;
    auto cont = [&](std::size_t at) { return at < s.size() && (static_cast<unsigned char>(s[at]) & 0xC0) == 0x80; };
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            ++i;
        } else if (c >= 0xC2 && c <= 0xDF) {
            if (!cont(i + 1))
                return false;
            i += 2;
        } else if (c >= 0xE0 && c <= 0xEF) {
            if (!cont(i + 1) || !cont(i + 2))
                return false;
            const auto c1 = static_cast<unsigned char>(s[i + 1]);
            if ((c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 > 0x9F))
                return false;
            i += 3;
        } else if (c >= 0xF0 && c <= 0xF4) {
            if (!cont(i + 1) || !cont(i + 2) || !cont(i + 3))
                return false;
            const auto c1 = static_cast<unsigned char>(s[i + 1]);
            if ((c == 0xF0 && c1 < 0x90) || (c == 0xF4 && c1 > 0x8F))
                return false;
            i += 4;
        } else {
            return false;
        }
    }
    return true;
}

void appendUtf8(std::string &out, char32_t cp)
{
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

std::optional<std::string> fromUtf16(std::string_view bytes, bool bigEndian)
{
    if (bytes.size() % 2 != 0)
        return std::nullopt;
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 2) {
        auto unit = [&](std::size_t at) {
            const auto a = static_cast<unsigned char>(bytes[at]), b = static_cast<unsigned char>(bytes[at + 1]);
            return char16_t(bigEndian ? (a << 8 | b) : (b << 8 | a));
        };
        const char16_t u = unit(i);
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 2 >= bytes.size())
                return std::nullopt;
            const char16_t low = unit(i + 2);
            if (low < 0xDC00 || low > 0xDFFF)
                return std::nullopt;
            appendUtf8(out, 0x10000 + ((char32_t(u) - 0xD800) << 10) + (low - 0xDC00));
            i += 2;
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            return std::nullopt;
        } else {
            appendUtf8(out, u);
        }
    }
    return out;
}

// Windows-1252's 0x80-0x9F (the five undefined bytes map to the C1 control
// of the same value, as Windows' MultiByteToWideChar does).
constexpr std::array<char16_t, 32> kCp1252High = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

std::string fromSingleByte(std::string_view bytes, Interpretation interpretation)
{
    std::string out;
    for (const char ch : bytes) {
        const auto c = static_cast<unsigned char>(ch);
        if (interpretation == Interpretation::Windows1252 && c >= 0x80 && c <= 0x9F)
            appendUtf8(out, kCp1252High[c - 0x80]);
        else
            appendUtf8(out, c);
    }
    return out;
}

// ---- values

std::string listText(const std::vector<std::string> &list)
{
    std::string out;
    for (const auto &e : list) {
        if (!out.empty())
            out += " | ";
        out += e;
    }
    return out;
}

SettingValue effectiveValue(const SettingDefinition &def, const Destination &destination)
{
    if (const auto it = destination.values.find(def.id); it != destination.values.end())
        return convertSetting(it->second, def.type);
    return def.defaultValue;
}

// GetTable's mode per option (the legacy consumer's call, cited).
TableMode tableModeOf(std::string_view id)
{
    // findreplace.cpp:44-59 and SelectLines.cpp:36 read with
    // wxTOKEN_RET_EMPTY_ALL; the others with GetTable's default (STRTOK).
    static constexpr std::string_view retEmptyAll[] = {"find.recentFinds", "find.recentReplacements",
                                                       "findInSubs.recentFilters", "findInSubs.recentPaths",
                                                       "selectLines.recentSelections"};
    return std::ranges::find(retEmptyAll, id) != std::end(retEmptyAll) ? TableMode::ReturnEmptyAll : TableMode::Strtok;
}

// wxString::IsNumber-like: optional sign, then digits only (no blanks).
bool isPlainInteger(std::string_view s)
{
    if (!s.empty() && (s.front() == '+' || s.front() == '-'))
        s.remove_prefix(1);
    return !s.empty() && std::ranges::all_of(s, [](char c) { return c >= '0' && c <= '9'; });
}

struct OptionReading {
    SettingValue value;
    std::string problem; // malformed: legacy's reading kept in the reason
    std::string note;
};

// The option's typed value as legacy read it (config.cpp:156-198, 753-765).
OptionReading readOption(const SettingDefinition &def, std::string_view raw, bool windows)
{
    OptionReading r;
    switch (def.type) {
    case SettingType::Bool:
        // GetBool: exactly "true" (config.cpp:165-168).
        r.value = raw == "true";
        if (!raw.empty() && raw != "true" && raw != "false")
            r.problem = "legacy reads \"" + std::string(raw) + "\" as false (GetBool, config.cpp:165-168)";
        break;
    case SettingType::Int:
        // GetInt: wxAtoi (config.cpp:186-191), per build as hotkeyLabelNumber.
        r.value = std::int64_t(hotkeyLabelNumber(raw, windows));
        if (!raw.empty() && !isPlainInteger(raw))
            r.problem = "not a whole number; legacy's wxAtoi reads " + std::to_string(std::get<std::int64_t>(r.value)) +
                        " (config.cpp:186-191)";
        break;
    case SettingType::String:
        r.value = std::string(raw);
        break;
    case SettingType::StringList: {
        r.value = legacyTable(raw, tableModeOf(def.id));
        const bool table = raw.size() >= 4 && raw.starts_with("{\n") && raw.ends_with("\n}");
        if (!raw.empty() && !table && raw != "{\n}" && raw != "{}")
            r.problem = "not a brace table; legacy's GetTable reads " +
                        std::to_string(std::get<std::vector<std::string>>(r.value).size()) +
                        " entries from it (config.cpp:753-765)";
        break;
    }
    }
    // hikarisubApp.cpp:331-336: a language of "0" or "1" is read as none.
    if (def.legacyKey == "PROGRAM_LANGUAGE" && (raw == "0" || raw == "1")) {
        r.value = std::string();
        r.note = "legacy reads the language \"" + std::string(raw) + "\" as none at startup (hikarisubApp.cpp:331-336)";
    }
    return r;
}

bool isColourKey(std::string_view key)
{
    if (std::ranges::find(themeColourKeys(), key) != themeColourKeys().end())
        return true;
    return std::ranges::any_of(colourAliases(), [&](const NameAlias &a) { return a.from == key || a.to == key; });
}

const ConfigAlias *configAliasOf(std::string_view name)
{
    const auto all = configAliases();
    const auto it = std::ranges::find(all, name, &ConfigAlias::from);
    return it == all.end() ? nullptr : &*it;
}

const NameAlias *hotkeyAliasOf(std::string_view name)
{
    const auto all = hotkeyAliases();
    const auto it = std::ranges::find(all, name, &NameAlias::from);
    return it == all.end() ? nullptr : &*it;
}

std::string windowLetter(int type)
{
    return type >= 0 && type < kHotkeyWindows ? std::string(1, kWindowLetters[std::size_t(type)]) : std::string("?");
}

} // namespace

// ---- tables

std::span<const KnownSource> knownSources()
{
    static constexpr KnownSource table[] = {
        {SourceKind::Config, "Config/Config.txt"},
        {SourceKind::AudioConfig, "Config/AudioConfig.txt"},
        {SourceKind::Hotkeys, "Config/Hotkeys.txt"},
        {SourceKind::AudioHotkeys, "Config/AudioHotkeys.txt"},
        {SourceKind::Rules, "Config/Rules.txt"},
        {SourceKind::UserDictionary, "Dictionary/UserDic.udic"},
    };
    return table;
}

bool isRetiredOption(std::string_view legacyKey)
{
    // User decision 2026-10-05: no main toolbar.
    return legacyKey == "TOOLBAR_IDS" || legacyKey == "TOOLBAR_ALIGNMENT";
}

bool isRetiredAction(int id)
{
    // User decision 2026-10-05: GLOBAL_VIDEO_INDEXING is retired.
    return id == hotkeyIdOf("GLOBAL_VIDEO_INDEXING");
}

bool isPathSetting(std::string_view id)
{
    static constexpr std::string_view ids[] = {"fonts.externalDirectory", "fontCollector.directory",
                                               "automation.scriptEditor"};
    return std::ranges::find(ids, id) != std::end(ids);
}

bool isPathListSetting(std::string_view id)
{
    static constexpr std::string_view ids[] = {"recent.subtitles", "recent.video", "recent.audio", "recent.keyframes",
                                               "findInSubs.recentPaths"};
    return std::ranges::find(ids, id) != std::end(ids);
}

bool isForeignPath(std::string_view path, bool windowsHost)
{
    if (path.empty())
        return false;
    const bool drive = path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':';
    const bool unc = path.starts_with("\\\\");
    if (!windowsHost)
        return drive || unc;
    return path.front() == '/' && !path.starts_with("//");
}

std::string_view dispositionName(Disposition d)
{
    switch (d) {
    case Disposition::Read:
        return "read";
    case Disposition::Change:
        return "change";
    case Disposition::Unchanged:
        return "unchanged";
    case Disposition::Missing:
        return "missing";
    case Disposition::Superseded:
        return "superseded";
    case Disposition::Unresolved:
        return "unresolved";
    case Disposition::Excluded:
        return "excluded";
    case Disposition::Retired:
        return "retired";
    }
    return "";
}

// ---- decoding

DecodedText decodeSettingsText(std::string_view bytes, ReadBy readBy, std::optional<Interpretation> interpretation)
{
    DecodedText out;
    std::optional<std::string> text;
    auto foldWindows = [&](std::string_view b) {
        // The Windows build's text-mode read: it stopped at Ctrl+Z and turned
        // CRLF into LF before decoding.
        std::string s(b.substr(0, b.find('\x1A')));
        std::string folded;
        folded.reserve(s.size());
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n')
                continue;
            folded += s[i];
        }
        return folded;
    };
    if (bytes.starts_with("\xEF\xBB\xBF")) {
        out.evidence.utf8Bom = true;
        std::string body = readBy == ReadBy::Windows ? foldWindows(bytes.substr(3)) : std::string(bytes.substr(3));
        out.evidence.validUtf8 = validUtf8(body);
        if (out.evidence.validUtf8) {
            text = std::move(body);
            out.evidence.interpretation = "UTF-8";
        }
    } else if (bytes.starts_with("\xFF\xFE") || bytes.starts_with("\xFE\xFF")) {
        // wxConvAuto's UTF-16 BOMs; here the line ends are folded after
        // decoding (the byte-level fold garbles UTF-16, F1-utf16-crlf).
        out.evidence.utf16Bom = true;
        const bool big = bytes.starts_with("\xFE\xFF");
        if (auto decoded = fromUtf16(bytes.substr(2), big)) {
            text = readBy == ReadBy::Windows ? foldWindows(*decoded) : std::move(*decoded);
            out.evidence.interpretation = big ? "UTF-16BE" : "UTF-16LE";
        }
    } else {
        std::string body = readBy == ReadBy::Windows ? foldWindows(bytes) : std::string(bytes);
        out.evidence.validUtf8 = validUtf8(body);
        if (out.evidence.validUtf8) {
            text = std::move(body);
            out.evidence.interpretation = "UTF-8";
        } else if (interpretation) {
            text = fromSingleByte(body, *interpretation);
            out.evidence.interpretation = *interpretation == Interpretation::Latin1 ? "Latin-1" : "Windows-1252";
        }
    }
    out.evidence.decoded = text.has_value();
    out.text = std::move(text);
    return out;
}

// ---- Config.txt / AudioConfig.txt

bool configNeedsConversion(std::string_view header)
{
    // version = fullVersion.AfterFirst('v').BeforeFirst(' ') (config.cpp:855).
    const std::string_view version = beforeFirst(afterFirst(header, 'v'), ' ');
    if (parseSemVer(version))
        return false;
    // wxSplit(version, '.'): four parts, the last a number of at least 1142.
    std::vector<std::string_view> parts;
    if (!version.empty()) {
        std::size_t pos = 0;
        while (true) {
            const auto dot = version.find('.', pos);
            parts.push_back(version.substr(pos, dot == std::string_view::npos ? std::string_view::npos : dot - pos));
            if (dot == std::string_view::npos)
                break;
            pos = dot + 1;
        }
    }
    if (parts.size() != 4)
        return true;
    // wxString::ToLong: wcstol over the whole text (leading blanks allowed).
    std::string_view last = trimLeft(parts[3]);
    long long build = 0;
    const char *begin = last.data();
    if (!last.empty() && last.front() == '+')
        ++begin;
    const auto [end, ec] = std::from_chars(begin, last.data() + last.size(), build);
    if (last.empty() || ec != std::errc() || end != last.data() + last.size())
        return true;
    return build < 1142;
}

namespace {

// ConvertConfig on one line: its converted text (which may hold several
// lines) and the old name when it renamed one.
std::pair<std::string, std::string> convertConfigLine(std::string token)
{
    // wxRegEx("^([^=]*)") matches the text up to the first '='.
    const std::string match(beforeFirst(token, '='));
    const ConfigAlias *alias = configAliasOf(match);
    if (!alias)
        return {std::move(token), {}};
    if (token.starts_with("ToolbarIDs")) {
        // The toolbar ids, '|'-separated, renamed through the hotkey table
        // into a brace table (ConfigConverter.cpp:517-533).
        const std::string vals(afterFirst(token, '='));
        std::string out = std::string(alias->to) + "={\n";
        for (const auto &id : strtokLines([&] {
                 std::string s = vals;
                 std::ranges::replace(s, '|', '\n');
                 return s;
             }(),
                                          1)) {
            const NameAlias *h = hotkeyAliasOf(id.text);
            out += "\t" + (h ? std::string(h->to) : id.text) + "\n";
        }
        out += "}";
        return {std::move(out), match};
    }
    token = std::string(alias->to) + token.substr(match.size());
    if (!alias->delimiter.empty()) {
        // token.Replace("=", "={\n\t", false), then every delimiter "\n\t".
        if (const auto eq = token.find('='); eq != std::string::npos)
            token.replace(eq, 1, "={\n\t");
        std::string out;
        for (std::size_t i = 0; i < token.size();) {
            if (token.compare(i, alias->delimiter.size(), alias->delimiter) == 0) {
                out += "\n\t";
                i += alias->delimiter.size();
            } else {
                out += token[i++];
            }
        }
        token = out + "\n}";
    }
    return {std::move(token), match};
}

} // namespace

std::string convertConfigText(std::string_view text, std::string_view progname, std::map<int, std::string> *renames)
{
    // ConvertConfig (ConfigConverter.cpp:503-551): "[progname]\r\n", then each
    // non-empty line, converted, followed by "\n".
    std::string result = "[" + std::string(progname) + "]\r\n";
    int outLine = 2;
    for (auto &token : strtokLines(text, 1)) {
        auto [converted, from] = convertConfigLine(std::move(token.text));
        if (renames && !from.empty())
            (*renames)[outLine] = from;
        outLine += int(std::ranges::count(converted, '\n')) + 1;
        result += converted + "\n";
    }
    return result;
}

ConfigFile readConfigFile(std::string_view text)
{
    ConfigFile file;
    std::string txt(text);
    // ver = txt.BeforeFirst(']').Mid(1) (config.cpp:556, 873).
    file.header = std::string(mid(beforeFirst(txt, ']'), 1));
    if (txt.ends_with(kCrashMarker)) {
        txt.resize(txt.size() - kCrashMarker.size());
        file.crashed = true;
    }
    // txt.AfterFirst('\n'): the first line is never an option.
    txt = std::string(afterFirst(txt, '\n'));

    // The tokens SetRawOptions reads, with their lines in the file and the
    // names ConfigConverter replaced.
    struct Piece {
        int line;
        std::string text;
        std::string renamedFrom;
        bool converterHeader = false;
    };
    std::vector<Piece> pieces;
    if (configNeedsConversion(file.header)) {
        file.converted = true;
        // ConvertConfig's header line is one more token SetRawOptions counts.
        pieces.push_back({1, "[" + std::string(kConverterProgname) + "]\r", {}, true});
        for (auto &token : strtokLines(txt, 2)) {
            auto [converted, from] = convertConfigLine(std::move(token.text));
            for (auto &sub : strtokLines(converted, token.line))
                pieces.push_back({token.line, std::move(sub.text), from});
        }
    } else {
        for (auto &token : strtokLines(txt, 2))
            pieces.push_back({token.line, std::move(token.text), {}});
    }

    // SetRawOptions (config.cpp:120-153) and CatchValsLabs (config.cpp:259-268).
    auto catchValsLabs = [&](std::string_view line, int lineNo, const std::string &from) {
        ConfigRecord r;
        r.line = lineNo;
        r.label = std::string(trimRight(beforeFirst(line, '=')));
        r.value = std::string(afterFirst(line, '='));
        r.renamedFrom = from;
        file.records.push_back(std::move(r));
    };
    bool block = false;
    std::string textBlock;
    int blockLine = 0;
    std::string blockFrom;
    for (const auto &piece : pieces) {
        const std::string &token = piece.text;
        if (token.ends_with('{')) {
            if (!block) {
                blockLine = piece.line;
                blockFrom = piece.renamedFrom;
            }
            block = true;
            textBlock += token + "\n";
            continue;
        }
        if (block) {
            if (token == "}") {
                textBlock += token;
                block = false;
                catchValsLabs(textBlock, blockLine, blockFrom);
                textBlock.clear();
                ++file.count;
            } else {
                textBlock += token + "\n";
            }
            continue;
        }
        const std::string_view trimmed = trim(token);
        if (!trimmed.empty()) {
            ++file.count;
            if (!piece.converterHeader)
                catchValsLabs(trimmed, piece.line, piece.renamedFrom);
        }
    }
    if (block)
        file.unterminatedBlock = textBlock;
    return file;
}

std::vector<std::string> legacyTable(std::string_view value, TableMode mode)
{
    std::vector<std::string> out;
    const std::size_t length = codePoints(value);
    if (length <= 4)
        return out;
    // strtbl.Mid(2, length - 4), split at '\n', each token's Mid(1).
    const std::string_view body = mid(value, 2, length - 4);
    std::size_t pos = 0;
    while (true) {
        const auto end = body.find('\n', pos);
        const std::string_view token = body.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
        if (mode == TableMode::ReturnEmptyAll || !token.empty())
            out.emplace_back(mid(token, 1));
        if (end == std::string_view::npos)
            break;
        pos = end + 1;
    }
    // wxTOKEN_RET_EMPTY_ALL on an empty body returns nothing.
    if (mode == TableMode::ReturnEmptyAll && body.empty())
        out.clear();
    return out;
}

// ---- Hotkeys.txt / AudioHotkeys.txt

HotkeyFile readHotkeyFile(std::string_view text, bool windows)
{
    HotkeyFile file;
    // wxStringTokenizer hk(acctxt, "\n", wxTOKEN_STRTOK) (Hotkeys.cpp:248).
    std::vector<Token> tokens = strtokLines(text, 1);
    std::size_t first = 0;
    if (text.starts_with('[') && !tokens.empty()) {
        const std::string &token = tokens.front().text;
        file.header = std::string(trimRight(token));
        // int first = token.find("."); ver = token.Mid(first + 5).BeforeFirst(' ')
        // (Hotkeys.cpp:257-261): the build number of "0.8.0.build".
        if (const auto dot = token.find('.'); dot != std::string::npos) {
            const std::string_view ver = beforeFirst(mid(token, codePoints(std::string_view(token).substr(0, dot)) + 5), ' ');
            file.version = hotkeyLabelNumber(ver, windows);
            file.accepted = file.version > 487;
            file.converted = file.version < 1141;
        }
        first = 1;
    }
    for (std::size_t i = first; i < tokens.size(); ++i) {
        std::string token = tokens[i].text;
        std::string renamedFrom;
        if (file.converted) {
            // ConvertHotkeys: wxRegEx("^([^ ]*) (.)=") renamed through the
            // table, N -> S and W -> V for the renamed ones (ConfigConverter.cpp:587-600).
            const auto space = token.find(' ');
            if (space != std::string::npos && space + 2 < token.size() && token[space + 2] == '=') {
                const std::string label = token.substr(0, space);
                if (const NameAlias *alias = hotkeyAliasOf(label)) {
                    char mark = token[space + 1];
                    mark = mark == 'N' ? 'S' : mark == 'W' ? 'V' : mark;
                    token = std::string(alias->to) + " " + mark + "=" + token.substr(space + 3);
                    renamedFrom = label;
                }
            }
        }
        const std::string_view t = trim(token);
        HotkeyRecord r;
        r.line = tokens[i].line;
        r.text = std::string(t);
        r.renamedFrom = renamedFrom;
        if (t.starts_with("Script")) {
            // name = token.BeforeFirst('=', &rest) (Hotkeys.cpp:281-289).
            r.script = true;
            r.label = std::string(trim(beforeFirst(t, '=')));
            r.accel = std::string(trim(afterFirst(t, '=')));
            r.window = GlobalHotkey;
        } else {
            // Values = AfterFirst(' ') trimmed; type = "GSEVA".find(Values[0]);
            // Values.Remove(0, 2); Labels = BeforeFirst(' ') (Hotkeys.cpp:293-297).
            std::string_view values = trim(afterFirst(t, ' '));
            if (!values.empty()) {
                const auto letter = kWindowLetters.find(values.front());
                r.window = letter == std::string_view::npos ? -1 : int(letter);
            }
            r.accel = values.size() > 2 ? std::string(values.substr(2)) : std::string();
            r.label = std::string(trim(beforeFirst(t, ' ')));
        }
        if (!r.accel.empty())
            ++file.count;
        file.records.push_back(std::move(r));
    }
    return file;
}

// ---- Receipts and plans

bool Receipt::sameIdentity(const Receipt &other) const
{
    return sources == other.sources && mappingVersion == other.mappingVersion &&
           destinationProfile == other.destinationProfile;
}

const PlanRow *Plan::row(std::string_view id) const
{
    const auto it = std::ranges::find(rows, id, &PlanRow::id);
    return it == rows.end() ? nullptr : &*it;
}

namespace {

std::string sourceAt(std::string_view path, int line)
{
    return line > 0 ? std::string(path) + ":" + std::to_string(line) : std::string(path);
}

std::string evidenceText(const TextEvidence &e)
{
    if (!e.decoded)
        return e.utf8Bom ? "a UTF-8 BOM before bytes that are not UTF-8" : "not UTF-8: choose how to read it";
    std::string out = e.interpretation;
    if (e.utf8Bom || e.utf16Bom)
        out += " with a BOM";
    return out;
}

const SourceFile *findSource(const std::vector<SourceFile> &sources, SourceKind kind)
{
    const auto it = std::ranges::find(sources, kind, &SourceFile::kind);
    return it == sources.end() ? nullptr : &*it;
}

// The proposal for a row the user may import: keep what the user set,
// import over a default, and follow an earlier import of the same sources.
void propose(PlanRow &row, bool currentIsUsers, const PlanOptions &options, const Receipt &identity)
{
    row.selectable = true;
    row.proposedImport = !currentIsUsers;
    if (currentIsUsers)
        row.reason += (row.reason.empty() ? "" : "; ") + std::string("your current value is kept unless you choose to import");
    const Receipt *previous = options.previous;
    if (!previous || previous->mappingVersion != kMappingVersion)
        return;
    if (const auto it = previous->imported.find(row.id); it != previous->imported.end()) {
        // Not edited since the last import: the new legacy value may follow.
        if (row.current && *row.current == it->second) {
            row.proposedImport = true;
        } else {
            row.proposedImport = false;
            row.reason += (row.reason.empty() ? "" : "; ") + std::string("changed since the last import; kept");
        }
    } else if (previous->kept.contains(row.id) && previous->sameIdentity(identity)) {
        row.proposedImport = false; // the same plan again: the same choice
    }
}

struct Effective {
    const ConfigRecord *record = nullptr;
    std::string path;
};

} // namespace

Plan buildPlan(const std::vector<SourceFile> &sources, const Destination &destination, const PlanOptions &options)
{
    Plan plan;
    const bool windows = options.readBy == ReadBy::Windows;
    for (const auto &s : sources)
        plan.sources.emplace_back(s.path, s.sha256);
    std::ranges::sort(plan.sources);
    Receipt identity;
    identity.sources = plan.sources;
    if (options.previous)
        identity.destinationProfile = options.previous->destinationProfile;

    auto add = [&](PlanRow row) -> PlanRow & { return plan.rows.emplace_back(std::move(row)); };
    auto fileRow = [&](const SourceFile &s, Disposition d, std::string raw, std::string reason) -> PlanRow & {
        PlanRow row;
        row.id = "file:" + s.path;
        row.kind = RowKind::File;
        row.source = s.path;
        row.sourceKey = s.path;
        row.raw = std::move(raw);
        row.destination = "";
        row.disposition = d;
        row.reason = std::move(reason);
        return add(std::move(row));
    };

    // ---- Config.txt and AudioConfig.txt

    // Effective records by legacy label, in the order legacy set them.
    std::map<std::string, Effective, std::less<>> effective;
    std::vector<PlanRow> supersededRows;
    std::set<std::string, std::less<>> seenLabels;
    std::string configHeader;
    bool configRead = false, audioRead = false;
    std::vector<std::shared_ptr<ConfigFile>> configFiles;
    auto supersede = [&](const Effective &e, std::string reason) {
        PlanRow row;
        row.id = "superseded:" + sourceAt(e.path, e.record->line);
        row.kind = RowKind::Setting;
        row.source = sourceAt(e.path, e.record->line);
        row.sourceKey = e.record->label;
        row.raw = e.record->value;
        if (const auto *def = findLegacySetting(e.record->label))
            row.destination = std::string(def->id);
        row.disposition = Disposition::Superseded;
        row.reason = std::move(reason);
        supersededRows.push_back(std::move(row));
    };
    auto readConfigSource = [&](const SourceFile &s, bool audio) {
        auto decoded = decodeSettingsText(s.bytes, options.readBy, options.interpretation);
        if (!decoded.text) {
            plan.ambiguousEncoding = plan.ambiguousEncoding || !decoded.evidence.utf8Bom;
            fileRow(s, Disposition::Unresolved, {},
                    evidenceText(decoded.evidence) + "; nothing is read from it until an interpretation is chosen");
            return;
        }
        auto parsed = std::make_shared<ConfigFile>(readConfigFile(*decoded.text));
        configFiles.push_back(parsed); // the records outlive this lambda
        std::string reason = evidenceText(decoded.evidence) + "; " + std::to_string(parsed->count) + " records";
        if (parsed->converted)
            reason += "; an old header: ConfigConverter's renames applied, the file left unchanged (config.cpp:561-564)";
        if (parsed->crashed)
            reason += "; ends with legacy's crash marker";
        if (!parsed->unterminatedBlock.empty())
            reason += "; a block without its closing \"}\" is dropped (SetRawOptions, config.cpp:129-145)";
        const bool shortFile = parsed->count <= 10;
        if (!audio) {
            configRead = true;
            configHeader = parsed->header;
            if (shortFile)
                reason += windows ? "; legacy refused to start with 10 or fewer records (config.cpp:569-577, "
                                    "hikarisubApp.cpp:320): nothing from it was in effect"
                                  : "; legacy loaded its defaults over a file of 10 or fewer records (config.cpp:569-574)";
        } else {
            audioRead = true;
            if (shortFile)
                reason += "; legacy reported \"Cannot load audio configuration\" for 10 or fewer records but kept "
                          "their values (config.cpp:883, HikariSubFrame.cpp:2172)";
        }
        fileRow(s, Disposition::Read, parsed->header, reason);

        if (audio) {
            // LoadAudioOpts: a header other than the running build's
            // (Config.txt's) resets the audio options to their defaults
            // before reading (config.cpp:875).
            if (configRead && parsed->header != configHeader)
                for (auto it = effective.begin(); it != effective.end();) {
                    const bool reset = std::ranges::find(audioDefaultKeys(), it->first) != audioDefaultKeys().end();
                    if (reset) {
                        supersede(it->second, "AudioConfig.txt's header differs, so legacy reset the audio options to "
                                              "their defaults (config.cpp:875)");
                        it = effective.erase(it);
                    } else {
                        ++it;
                    }
                }
        }
        for (const auto &record : parsed->records) {
            Effective e{&record, s.path};
            if (!audio && shortFile) {
                const bool defaulted =
                    std::ranges::find(mainDefaultKeys(), record.label) != mainDefaultKeys().end();
                if (windows || defaulted) {
                    seenLabels.insert(record.label);
                    supersede(e, windows ? "legacy refused to start with this file (10 or fewer records)"
                                         : "legacy loaded its default over it (10 or fewer records, config.cpp:569-574)");
                    continue;
                }
            }
            if (const auto it = effective.find(record.label); it != effective.end()) {
                supersede(it->second, "set again at " + sourceAt(s.path, record.line) + "; legacy keeps the last");
                it->second = e;
            } else {
                effective.emplace(record.label, e);
            }
            seenLabels.insert(record.label);
        }
    };
    const SourceFile *config = findSource(sources, SourceKind::Config);
    const SourceFile *audioConfig = findSource(sources, SourceKind::AudioConfig);
    if (config)
        readConfigSource(*config, false);
    if (audioConfig) {
        readConfigSource(*audioConfig, true);
    } else if (configRead) {
        // LoadAudioOpts without the file: LoadDefaultAudioConfig (config.cpp:868-870).
        for (auto it = effective.begin(); it != effective.end();) {
            if (std::ranges::find(audioDefaultKeys(), it->first) != audioDefaultKeys().end()) {
                supersede(it->second, "without AudioConfig.txt legacy loads the audio defaults (config.cpp:868-870)");
                it = effective.erase(it);
            } else {
                ++it;
            }
        }
    }

    // One row per effective record.
    std::vector<std::pair<const ConfigRecord *, std::string>> ordered;
    for (const auto &[label, e] : effective)
        ordered.emplace_back(e.record, e.path);
    std::ranges::sort(ordered, [](const auto &a, const auto &b) {
        return a.second != b.second ? a.second > b.second /* Config before AudioConfig */ : a.first->line < b.first->line;
    });
    std::set<std::string, std::less<>> rowSettings;
    for (const auto &[record, path] : ordered) {
        PlanRow row;
        row.kind = RowKind::Setting;
        row.source = sourceAt(path, record->line);
        row.sourceKey = record->label;
        row.raw = record->value;
        if (!record->renamedFrom.empty())
            row.reason = "renamed from " + record->renamedFrom + " (ConfigConverter)";
        auto because = [&](std::string text) {
            row.reason = row.reason.empty() ? std::move(text) : row.reason + "; " + text;
        };
        const SettingDefinition *def = findLegacySetting(record->label);
        if (isRetiredOption(record->label)) {
            row.id = "setting:" + record->label;
            row.destination = def ? std::string(def->id) : std::string();
            row.disposition = Disposition::Retired;
            because("retired: the rewrite has no main toolbar (user decision, 2026-10-05)");
            add(std::move(row));
            continue;
        }
        if (def && def->disposition == SettingDisposition::Excluded) {
            row.id = "setting:" + record->label;
            row.destination = std::string(def->id);
            row.disposition = Disposition::Excluded;
            because("legacy themes are not imported; the theme layer owns appearance");
            add(std::move(row));
            continue;
        }
        if (!def) {
            row.id = "unknown:" + row.source;
            if (isColourKey(record->label)) {
                row.disposition = Disposition::Excluded;
                because("a theme colour: per-colour theme settings are gone, the theme layer owns appearance");
            } else {
                row.disposition = Disposition::Unresolved;
                if (configAliasOf(record->label))
                    because("an old option name; legacy renames it only under an old header (ConfigConverter)");
                else
                    because("unknown option: legacy ignored it (GetCONFIGValue gives 0)");
            }
            add(std::move(row));
            continue;
        }
        row.id = "setting:" + std::string(def->id);
        row.destination = std::string(def->id);
        rowSettings.insert(std::string(def->id));
        OptionReading reading = readOption(*def, record->value, windows);
        if (!reading.note.empty())
            because(reading.note);
        const SettingValue current = effectiveValue(*def, destination);
        row.current = current;
        if (!reading.problem.empty()) {
            row.value = reading.value;
            row.disposition = Disposition::Unresolved;
            because(reading.problem);
            add(std::move(row));
            continue;
        }
        row.value = convertSetting(reading.value, def->type);
        if (def->disposition == SettingDisposition::Unresolved)
            because("kept for review: its use in the rewrite is not settled (the migration map's preserve-unresolved)");
        // Paths: a foreign-platform one stays unresolved; missing ones are
        // reported and kept, never replaced.
        if (isPathSetting(def->id)) {
            const std::string &path = std::get<std::string>(*row.value);
            if (isForeignPath(path, destination.windowsHost)) {
                row.disposition = Disposition::Unresolved;
                row.unresolvedPaths.push_back(path);
                because("a path of the other platform; left unresolved");
                add(std::move(row));
                continue;
            }
            if (!path.empty() && destination.pathExists && !destination.pathExists(path)) {
                row.unresolvedPaths.push_back(path);
                because("the path does not exist here; kept as it is");
            }
        } else if (isPathListSetting(def->id)) {
            int foreign = 0, missing = 0;
            for (const auto &path : std::get<std::vector<std::string>>(*row.value)) {
                if (isForeignPath(path, destination.windowsHost)) {
                    row.unresolvedPaths.push_back(path);
                    ++foreign;
                } else if (!path.empty() && destination.pathExists && !destination.pathExists(path)) {
                    row.unresolvedPaths.push_back(path);
                    ++missing;
                }
            }
            if (foreign)
                because(std::to_string(foreign) + " of the other platform, kept unresolved in the list");
            if (missing)
                because(std::to_string(missing) + " missing here, kept in the list");
        }
        const bool set = destination.values.contains(def->id);
        if (*row.value == current) {
            row.disposition = Disposition::Unchanged;
        } else {
            row.disposition = Disposition::Change;
            propose(row, set, options, identity);
        }
        add(std::move(row));
    }
    for (auto &row : supersededRows)
        add(std::move(row));
    // Options the files leave out (the registry's legacy options).
    for (const auto &def : settingDefinitions()) {
        if (def.legacyKey.empty() || def.disposition == SettingDisposition::Excluded || isRetiredOption(def.legacyKey))
            continue;
        if (seenLabels.contains(def.legacyKey) || rowSettings.contains(def.id))
            continue;
        if (!(configRead || audioRead))
            continue;
        PlanRow row;
        row.id = "setting:" + std::string(def.id);
        row.kind = RowKind::Setting;
        row.source = def.legacyAudioFile ? "Config/AudioConfig.txt" : "Config/Config.txt";
        row.sourceKey = std::string(def.legacyKey);
        row.destination = std::string(def.id);
        row.current = effectiveValue(def, destination);
        row.disposition = Disposition::Missing;
        row.reason = "not in the legacy files; the current value stays";
        add(std::move(row));
    }

    // ---- Hotkeys.txt and AudioHotkeys.txt

    HotkeyMap currentMap;
    {
        auto lines = [&](std::string_view id) -> std::optional<std::vector<std::string>> {
            const auto it = destination.values.find(id);
            if (it == destination.values.end())
                return std::nullopt;
            return std::get<std::vector<std::string>>(convertSetting(it->second, SettingType::StringList));
        };
        if (auto main = lines(kHotkeysSetting))
            readHotkeyLines(currentMap, *main, windows);
        else
            loadDefaultHotkeys(currentMap, false);
        if (auto audio = lines(kAudioHotkeysSetting))
            readHotkeyLines(currentMap, *audio, windows);
        else
            loadDefaultHotkeys(currentMap, true);
    }
    std::map<std::string, std::string, std::less<>> currentMacros; // legacy name -> keys
    if (const auto it = destination.values.find(kAutomationHotkeysSetting); it != destination.values.end())
        for (const auto &rowText : std::get<std::vector<std::string>>(convertSetting(it->second, SettingType::StringList))) {
            const auto tab = rowText.find('\t');
            const std::string name = rowText.substr(0, tab);
            const std::string rest = tab == std::string::npos ? std::string() : rowText.substr(tab + 1);
            const std::string keys = rest.substr(0, rest.find('\t'));
            if (!keys.empty())
                currentMacros[name] = keys;
        }

    struct LegacyBinding {
        const HotkeyRecord *record;
        std::string path;
    };
    std::map<HotkeyId, LegacyBinding> legacyBindings;
    std::map<std::string, LegacyBinding, std::less<>> legacyMacros;
    std::vector<std::shared_ptr<HotkeyFile>> hotkeyFiles;
    std::vector<std::pair<const HotkeyRecord *, std::string>> hotkeyRows; // in file order
    bool mainAccepted = false, audioAccepted = false, mainShort = false, audioShort = false;
    auto readHotkeySource = [&](const SourceFile &s, bool audio) {
        auto decoded = decodeSettingsText(s.bytes, options.readBy, options.interpretation);
        if (!decoded.text) {
            plan.ambiguousEncoding = plan.ambiguousEncoding || !decoded.evidence.utf8Bom;
            fileRow(s, Disposition::Unresolved, {},
                    evidenceText(decoded.evidence) + "; nothing is read from it until an interpretation is chosen");
            return;
        }
        auto parsed = std::make_shared<HotkeyFile>(readHotkeyFile(*decoded.text, windows));
        hotkeyFiles.push_back(parsed);
        std::string reason = evidenceText(decoded.evidence) + "; " + std::to_string(parsed->count) + " records";
        if (!parsed->accepted) {
            fileRow(s, Disposition::Unresolved, parsed->header,
                    reason + "; legacy found the header outdated and replaced the file with its defaults "
                             "(Hotkeys.cpp:250-277): its records stay unresolved");
        } else {
            if (parsed->converted)
                reason += "; an old header: ConvertHotkeys' renames applied, the file left unchanged (Hotkeys.cpp:263-268)";
            if (parsed->count <= 10)
                reason += "; 10 or fewer records: legacy loaded its defaults over them, the importer reads them "
                          "(C04-short-file, approved 2026-09-27)";
            fileRow(s, Disposition::Read, parsed->header, reason);
            (audio ? audioAccepted : mainAccepted) = true;
            (audio ? audioShort : mainShort) = parsed->count <= 10;
        }
        for (const auto &record : parsed->records)
            hotkeyRows.emplace_back(&record, s.path);
        if (!parsed->accepted)
            return;
        for (const auto &record : parsed->records) {
            if (record.accel.empty())
                continue;
            if (record.script) {
                if (auto it = legacyMacros.find(record.label); it != legacyMacros.end())
                    it->second = {&record, s.path};
                else
                    legacyMacros.emplace(record.label, LegacyBinding{&record, s.path});
                continue;
            }
            std::string_view digits = record.label;
            if (!digits.empty() && (digits.front() == '-' || digits.front() == '+'))
                digits.remove_prefix(1);
            const bool number = std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; });
            const int id = number ? hotkeyLabelNumber(record.label, windows) : hotkeyIdOf(record.label);
            legacyBindings[HotkeyId{id, record.window}] = {&record, s.path};
        }
    };
    const SourceFile *hotkeys = findSource(sources, SourceKind::Hotkeys);
    const SourceFile *audioHotkeys = findSource(sources, SourceKind::AudioHotkeys);
    if (hotkeys)
        readHotkeySource(*hotkeys, false);
    if (audioHotkeys)
        readHotkeySource(*audioHotkeys, true);

    // Shortcut rows, then conflicts.
    std::vector<std::size_t> shortcutRows;
    for (const auto &[record, path] : hotkeyRows) {
        PlanRow row;
        row.source = sourceAt(path, record->line);
        row.raw = record->text;
        row.sourceKey = record->label;
        if (!record->renamedFrom.empty())
            row.reason = "renamed from " + record->renamedFrom + " (ConvertHotkeys)";
        auto because = [&](std::string text) {
            row.reason = row.reason.empty() ? std::move(text) : row.reason + "; " + text;
        };
        const bool fileAccepted = (path == "Config/AudioHotkeys.txt") ? audioAccepted : mainAccepted;
        row.id = "record:" + row.source;
        if (!fileAccepted) {
            row.kind = record->script ? RowKind::Macro : RowKind::Shortcut;
            row.disposition = Disposition::Unresolved;
            because("legacy replaced this file with its defaults (outdated header)");
            add(std::move(row));
            continue;
        }
        if (record->accel.empty()) {
            row.kind = record->script ? RowKind::Macro : RowKind::Shortcut;
            row.disposition = Disposition::Unresolved;
            because("a blank binding: legacy skipped it, and it is no proof of unbinding (Hotkeys.cpp:282, 298)");
            add(std::move(row));
            continue;
        }
        if (record->script) {
            row.kind = RowKind::Macro;
            const auto it = legacyMacros.find(record->label);
            if (it->second.record != record) {
                row.disposition = Disposition::Superseded;
                because("bound again at " + sourceAt(it->second.path, it->second.record->line) + "; legacy keeps the last");
                add(std::move(row));
                continue;
            }
            const auto alias = aliasOfLegacyName(record->label);
            if (!alias) {
                row.disposition = Disposition::Unresolved;
                because("not a \"Script <file>-<ordinal>\" name the automation hotkeys can resolve");
                add(std::move(row));
                continue;
            }
            row.id = "macro:" + record->label;
            row.destination = *alias;
            row.value = portableKeys(record->accel);
            const auto current = currentMacros.find(record->label);
            if (current != currentMacros.end())
                row.current = current->second;
            because("resolved through the script file name and macro ordinal when scripts load (S44-macro-alias); "
                    "the scripts are not run to import it");
            if (destination.macroProblem)
                if (const std::string problem = destination.macroProblem(*alias); !problem.empty())
                    because(problem + ": it stays visibly unresolved until that is fixed");
            if (row.current && *row.current == *row.value) {
                row.disposition = Disposition::Unchanged;
            } else {
                row.disposition = Disposition::Change;
                // Automation hotkeys' legacy import (automation_hotkeys_controller.cpp:244-252):
                // a bound macro and keys another macro holds are kept.
                bool taken = false;
                for (const auto &[name, keys] : currentMacros)
                    taken = taken || (name != record->label && keys == std::get<std::string>(*row.value));
                if (taken)
                    because("another macro already has these keys");
                propose(row, current != currentMacros.end() || taken, options, identity);
            }
            add(std::move(row));
            continue;
        }
        row.kind = RowKind::Shortcut;
        std::string_view digits = record->label;
        if (!digits.empty() && (digits.front() == '-' || digits.front() == '+'))
            digits.remove_prefix(1);
        const bool number = std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; });
        const int id = number ? hotkeyLabelNumber(record->label, windows) : hotkeyIdOf(record->label);
        const HotkeyId key{id, record->window};
        row.binding = key;
        const auto symbol = hotkeySymbol(id);
        if (record->window < 0) {
            row.disposition = Disposition::Unresolved;
            because("no window letter G, S, E, V or A: legacy could not keep it (O2-window-letter)");
            add(std::move(row));
            continue;
        }
        if (id == 0 || symbol.empty() || id < 100 || id >= kFirstScriptHotkey) {
            row.disposition = Disposition::Unresolved;
            if (!record->renamedFrom.empty())
                because("its converter target " + record->label + " is no action (ConfigConverter.cpp:491-492)");
            else if (hotkeyAliasOf(record->label))
                because("an old action name; legacy renames it only under an old header (ConvertHotkeys)");
            else
                because(number ? "no action has this number" : "unknown action: legacy read it as id 0");
            add(std::move(row));
            continue;
        }
        const auto legacy = legacyBindings.find(key);
        if (legacy == legacyBindings.end() || legacy->second.record != record) {
            row.disposition = Disposition::Superseded;
            if (legacy != legacyBindings.end())
                because("bound again at " + sourceAt(legacy->second.path, legacy->second.record->line) +
                        "; legacy keeps the last");
            add(std::move(row));
            continue;
        }
        row.id = "shortcut:" + std::string(symbol) + ":" + windowLetter(record->window);
        row.destination = std::string(symbol) + " (" + std::string(hotkeyWindowName(record->window)) + ")";
        if (isRetiredAction(id)) {
            row.disposition = Disposition::Retired;
            because("retired: the rewrite has no FFMS2 indexing command (user decision, 2026-10-05)");
            add(std::move(row));
            continue;
        }
        if (const std::string invalid = invalidHotkeyKey(record->accel); !invalid.empty()) {
            row.disposition = Disposition::Unresolved;
            because("\"" + invalid + "\" is no key legacy could install (Hotkeys.cpp:372-374)");
            add(std::move(row));
            continue;
        }
        row.value = record->accel;
        const auto current = currentMap.find(key);
        const std::string currentAccel = current == currentMap.end() ? std::string() : current->second.accel;
        row.current = currentAccel;
        if (qtKeysOfAccel(record->accel).empty())
            because("legacy's key has no Qt sequence here; kept as legacy text");
        if (currentAccel == record->accel) {
            row.disposition = Disposition::Unchanged;
        } else {
            row.disposition = Disposition::Change;
            propose(row, currentAccel != defaultHotkey(key), options, identity);
        }
        shortcutRows.push_back(plan.rows.size());
        add(std::move(row));
    }
    // Default bindings a parsed file leaves out: legacy had them unbound (a
    // short file under C04 is read without the defaults legacy added).
    std::vector<std::size_t> missingRows;
    auto missingDefaults = [&](bool audio, bool shortFile, const std::string &path) {
        HotkeyMap defaults;
        loadDefaultHotkeys(defaults, audio);
        for (const auto &[key, hotkey] : defaults) {
            if (legacyBindings.contains(key))
                continue;
            const auto current = currentMap.find(key);
            const std::string currentAccel = current == currentMap.end() ? std::string() : current->second.accel;
            PlanRow row;
            row.kind = RowKind::Shortcut;
            row.binding = key;
            row.id = "shortcut:" + std::string(hotkeySymbol(key.id)) + ":" + windowLetter(key.type);
            row.source = path;
            row.sourceKey = std::string(hotkeySymbol(key.id));
            row.destination = std::string(hotkeySymbol(key.id)) + " (" + std::string(hotkeyWindowName(key.type)) + ")";
            row.value = std::string();
            row.current = currentAccel;
            row.disposition = Disposition::Missing;
            if (isRetiredAction(key.id)) {
                row.disposition = Disposition::Retired;
                row.reason = "retired (user decision, 2026-10-05)";
                add(std::move(row));
                continue;
            }
            if (currentAccel.empty()) {
                row.reason = "not in the legacy file and unbound here";
                add(std::move(row));
                continue;
            }
            row.reason = shortFile ? "not in the legacy file (legacy added its default " + hotkey.accel +
                                         " to a short file); choose it to unbind"
                                   : "not in the legacy file: legacy had it unbound (Hotkeys.cpp:298-309), which "
                                     "is no proof of a deliberate unbinding; choose it to unbind";
            row.selectable = true;
            row.proposedImport = false;
            missingRows.push_back(plan.rows.size());
            add(std::move(row));
        }
    };
    if (mainAccepted)
        missingDefaults(false, mainShort, "Config/Hotkeys.txt");
    if (audioAccepted)
        missingDefaults(true, audioShort, "Config/AudioHotkeys.txt");
    // Conflicts in a window: a legacy chord another binding holds here.
    for (const std::size_t i : shortcutRows) {
        PlanRow &row = plan.rows[i];
        if (row.disposition != Disposition::Change)
            continue;
        const std::string &accel = std::get<std::string>(*row.value);
        for (const auto &[key, hotkey] : currentMap) {
            if (key == row.binding || key.type != row.binding.type || hotkey.accel != accel)
                continue;
            // Replaced by an imported legacy binding of its own: no conflict.
            if (const auto legacy = legacyBindings.find(key); legacy != legacyBindings.end() &&
                                                               legacy->second.record->accel != accel)
                continue;
            const std::string holder = std::string(hotkeySymbol(key.id));
            const bool users = hotkey.accel != defaultHotkey(key);
            if (users) {
                row.proposedImport = false;
                row.reason += (row.reason.empty() ? "" : "; ") + std::string("your binding of ") + holder +
                              " has these keys: review before importing";
            } else {
                row.reason += (row.reason.empty() ? "" : "; ") + std::string("the default binding of ") + holder +
                              " has these keys";
                // Legacy gave the chord to this action: its missing default row
                // proposes the unbinding.
                for (const std::size_t m : missingRows)
                    if (plan.rows[m].binding == key) {
                        plan.rows[m].proposedImport = true;
                        plan.rows[m].reason += "; legacy gave its keys to " + std::string(hotkeySymbol(row.binding.id));
                    }
            }
        }
    }

    // ---- Authored collections: Rules.txt, UserDic.udic, dictionaries

    auto collection = [&](const SourceFile &s, const std::string &destPath) {
        PlanRow row;
        row.kind = RowKind::Collection;
        row.id = "collection:" + destPath;
        row.source = s.path;
        row.sourceKey = s.path;
        row.destination = destPath;
        row.files = {destPath};
        row.value = s.sha256;
        const auto it = destination.files.find(destPath);
        if (it != destination.files.end())
            row.current = it->second.sha256;
        if (it != destination.files.end() && it->second.bytes == s.bytes) {
            row.disposition = Disposition::Unchanged;
        } else {
            row.disposition = Disposition::Change;
            row.reason = "carried over byte for byte (R6-dictionary-location; nothing in it is rewritten or run)";
            propose(row, it != destination.files.end(), options, identity);
        }
        add(std::move(row));
    };
    if (const SourceFile *rules = findSource(sources, SourceKind::Rules)) {
        plan.files["Rules.txt"] = rules->bytes;
        fileRow(*rules, Disposition::Read, {}, "the Misspells window's rules, read the legacy way when it opens");
        collection(*rules, "Rules.txt");
    }
    if (const SourceFile *dic = findSource(sources, SourceKind::UserDictionary)) {
        plan.files["Dictionary/UserDic.udic"] = dic->bytes;
        fileRow(*dic, Disposition::Read, {}, "the personal dictionary");
        collection(*dic, "Dictionary/UserDic.udic");
    }
    {
        // Dictionary pairs: <name>.dic with <name>.aff.
        std::map<std::string, std::pair<const SourceFile *, const SourceFile *>, std::less<>> pairs;
        for (const auto &s : sources) {
            if (s.kind != SourceKind::Dictionary)
                continue;
            const std::string name = s.path.substr(s.path.rfind('/') + 1);
            const auto dot = name.rfind('.');
            const std::string stem = name.substr(0, dot);
            const std::string ext = dot == std::string::npos ? std::string() : name.substr(dot);
            if (ext == ".dic")
                pairs[stem].first = &s;
            else if (ext == ".aff")
                pairs[stem].second = &s;
        }
        for (const auto &[stem, pair] : pairs) {
            const SourceFile *any = pair.first ? pair.first : pair.second;
            if (!pair.first || !pair.second) {
                PlanRow row;
                row.kind = RowKind::Collection;
                row.id = "collection:Dictionary/" + stem;
                row.source = any->path;
                row.sourceKey = any->path;
                row.disposition = Disposition::Unresolved;
                row.reason = "a dictionary needs both its .dic and .aff file";
                add(std::move(row));
                continue;
            }
            const std::string dicPath = "Dictionary/" + stem + ".dic", affPath = "Dictionary/" + stem + ".aff";
            plan.files[dicPath] = pair.first->bytes;
            plan.files[affPath] = pair.second->bytes;
            PlanRow row;
            row.kind = RowKind::Collection;
            row.id = "collection:Dictionary/" + stem;
            row.source = pair.first->path + ", " + pair.second->path;
            row.sourceKey = "Dictionary/" + stem;
            row.destination = "Dictionary/" + stem;
            row.files = {dicPath, affPath};
            row.value = pair.first->sha256 + "+" + pair.second->sha256;
            const auto d = destination.files.find(dicPath), a = destination.files.find(affPath);
            const bool present = d != destination.files.end() || a != destination.files.end();
            if (d != destination.files.end() && a != destination.files.end())
                row.current = d->second.sha256 + "+" + a->second.sha256;
            const bool bundled = destination.bundledDictionaries.contains(stem);
            if (row.current && *row.current == *row.value) {
                row.disposition = Disposition::Unchanged;
            } else {
                row.disposition = Disposition::Change;
                row.reason = "a Hunspell dictionary from the legacy Dictionary folder, carried over byte for byte "
                             "(R6-dictionary-location)";
                if (bundled)
                    row.reason += "; the program ships a dictionary of this name, which this copy would shadow";
                propose(row, present || bundled, options, identity);
            }
            add(std::move(row));
        }
    }

    // ---- Themes: excluded, named.
    for (const auto &theme : options.themeFiles) {
        PlanRow row;
        row.kind = RowKind::File;
        row.id = "file:" + theme;
        row.source = theme;
        row.sourceKey = theme;
        row.disposition = Disposition::Excluded;
        row.reason = "legacy themes are not imported: the theme layer owns appearance and per-colour theme "
                     "settings are gone";
        add(std::move(row));
    }
    return plan;
}

std::set<std::string, std::less<>> proposedRows(const Plan &plan)
{
    std::set<std::string, std::less<>> out;
    for (const auto &row : plan.rows)
        if (row.selectable && row.proposedImport)
            out.insert(row.id);
    return out;
}

Profile profileOf(const Destination &destination)
{
    Profile p;
    p.values = destination.values;
    for (const auto &[path, file] : destination.files)
        p.files[path] = file.bytes;
    return p;
}

Profile applyPlan(const Plan &plan, const std::set<std::string, std::less<>> &chosen, const Destination &destination)
{
    Profile p = profileOf(destination);
    const bool windows = destination.windowsHost;
    HotkeyMap map;
    bool mapRead = false, mainTouched = false, audioTouched = false;
    std::map<std::string, std::string, std::less<>> macroRows; // name -> the stored row
    bool macrosRead = false, macrosTouched = false;
    for (const auto &row : plan.rows) {
        if (!row.selectable || !chosen.contains(row.id) || !row.value)
            continue;
        switch (row.kind) {
        case RowKind::Setting:
            if (const auto *def = findSetting(row.destination))
                p.values[std::string(def->id)] = convertSetting(*row.value, def->type);
            break;
        case RowKind::Shortcut: {
            if (!mapRead) {
                auto lines = [&](std::string_view id) -> std::optional<std::vector<std::string>> {
                    const auto it = destination.values.find(id);
                    if (it == destination.values.end())
                        return std::nullopt;
                    return std::get<std::vector<std::string>>(convertSetting(it->second, SettingType::StringList));
                };
                if (auto main = lines(kHotkeysSetting))
                    readHotkeyLines(map, *main, windows);
                else
                    loadDefaultHotkeys(map, false);
                if (auto audio = lines(kAudioHotkeysSetting))
                    readHotkeyLines(map, *audio, windows);
                else
                    loadDefaultHotkeys(map, true);
                mapRead = true;
            }
            const std::string &accel = std::get<std::string>(*row.value);
            if (accel.empty())
                map.erase(row.binding);
            else
                map[row.binding] = Hotkey{std::string(), accel};
            (row.binding.type == AudioHotkey ? audioTouched : mainTouched) = true;
            break;
        }
        case RowKind::Macro: {
            if (!macrosRead) {
                if (const auto it = destination.values.find(kAutomationHotkeysSetting); it != destination.values.end())
                    for (const auto &text :
                         std::get<std::vector<std::string>>(convertSetting(it->second, SettingType::StringList)))
                        macroRows[text.substr(0, text.find('\t'))] = text;
                macrosRead = true;
            }
            // "legacy name\tkeys\tmacro\tsha256" with no registration recorded:
            // it resolves through the alias alone (automation_hotkeys_controller.cpp:43-81).
            const std::string name = row.sourceKey;
            macroRows[name] = name + "\t" + std::get<std::string>(*row.value) + "\t\t";
            macrosTouched = true;
            break;
        }
        case RowKind::Collection:
            for (const auto &f : row.files)
                if (const auto it = plan.files.find(f); it != plan.files.end())
                    p.files[f] = it->second;
            break;
        case RowKind::File:
            break;
        }
    }
    if (mainTouched)
        p.values[std::string(kHotkeysSetting)] = hotkeyLines(map, false);
    if (audioTouched)
        p.values[std::string(kAudioHotkeysSetting)] = hotkeyLines(map, true);
    if (macrosTouched) {
        std::vector<std::string> rows;
        for (const auto &[name, text] : macroRows)
            rows.push_back(text);
        p.values[std::string(kAutomationHotkeysSetting)] = rows;
    }
    return p;
}

Receipt receiptOf(const Plan &plan, const std::set<std::string, std::less<>> &chosen, std::string destinationProfile)
{
    Receipt r;
    r.sources = plan.sources;
    r.mappingVersion = plan.mappingVersion;
    r.destinationProfile = std::move(destinationProfile);
    for (const auto &row : plan.rows) {
        if (!row.selectable || !row.value)
            continue;
        if (chosen.contains(row.id))
            r.imported[row.id] = *row.value;
        else
            r.kept.insert(row.id);
    }
    return r;
}

} // namespace hikari::application::settings_import
