#include "hikari/core/extradata.h"

#include "text_util.h"

#include <algorithm>
#include <limits>
#include <map>
#include <random>
#include <ranges>
#include <set>
#include <string>

namespace hikari::core {

using namespace detail;

namespace {

bool escaped(char8_t c)
{
    return c <= 0x1F || c == u8'#' || c == u8',' || c == u8':' || c == u8'|';
}

std::u8string inlineEncode(u8sv in)
{
    static constexpr char8_t kHex[] = u8"0123456789ABCDEF";
    std::u8string out;
    for (char8_t c : in) {
        if (escaped(c)) {
            out += u8'#';
            out += kHex[(c >> 4) & 0xF];
            out += kHex[c & 0xF];
        } else {
            out += c;
        }
    }
    return out;
}

int hexDigit(char8_t c)
{
    if (c >= u8'0' && c <= u8'9')
        return c - u8'0';
    if (c >= u8'a' && c <= u8'f')
        return c - u8'a' + 10;
    if (c >= u8'A' && c <= u8'F')
        return c - u8'A' + 10;
    return -1;
}

// As Aegisub's strtol over the two characters: invalid digits end the number.
std::u8string inlineDecode(u8sv in)
{
    std::u8string out;
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != u8'#' || i + 2 >= in.size()) {
            out += in[i];
            continue;
        }
        const int high = hexDigit(in[i + 1]), low = hexDigit(in[i + 2]);
        out += static_cast<char8_t>(high < 0 ? 0 : low < 0 ? high : high * 16 + low);
        i += 2;
    }
    return out;
}

std::u8string uuEncode(u8sv in)
{
    std::u8string out;
    for (std::size_t pos = 0; pos < in.size(); pos += 3) {
        unsigned char src[3] = {0, 0, 0};
        for (std::size_t k = 0; k < 3 && pos + k < in.size(); ++k)
            src[k] = static_cast<unsigned char>(in[pos + k]);
        const unsigned char dst[4] = {static_cast<unsigned char>(src[0] >> 2),
                                      static_cast<unsigned char>(((src[0] & 0x3) << 4) | (src[1] >> 4)),
                                      static_cast<unsigned char>(((src[1] & 0xF) << 2) | (src[2] >> 6)),
                                      static_cast<unsigned char>(src[2] & 0x3F)};
        const std::size_t chars = std::min<std::size_t>(in.size() - pos + 1, 4);
        for (std::size_t k = 0; k < chars; ++k)
            out += static_cast<char8_t>(dst[k] + 33);
    }
    return out;
}

std::u8string uuDecode(u8sv in)
{
    std::u8string out;
    for (std::size_t pos = 0; pos + 1 < in.size();) {
        std::size_t count = 0;
        unsigned char src[4] = {0, 0, 0, 0};
        while (count < 4 && pos < in.size()) {
            const char8_t c = in[pos++];
            if (c && c != u8'\n' && c != u8'\r')
                src[count++] = static_cast<unsigned char>(c - 33);
        }
        if (count > 1)
            out += static_cast<char8_t>((src[0] << 2) | (src[1] >> 4));
        if (count > 2)
            out += static_cast<char8_t>(((src[1] & 0xF) << 4) | (src[2] >> 2));
        if (count > 3)
            out += static_cast<char8_t>(((src[2] & 0x3) << 6) | src[3]);
    }
    return out;
}

std::optional<std::uint32_t> number(u8sv digits)
{
    if (digits.empty())
        return std::nullopt;
    std::uint64_t value = 0;
    for (char8_t c : digits) {
        if (c < u8'0' || c > u8'9')
            return std::nullopt;
        value = value * 10 + static_cast<std::uint64_t>(c - u8'0');
        if (value > std::numeric_limits<std::uint32_t>::max())
            return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

std::uint32_t idAboveAllNamed(const Document &document)
{
    std::uint32_t next = 0;
    for (const auto &entry : document.extradata())
        next = std::max(next, entry.id + 1);
    for (const auto *line : document.lines())
        for (const auto id : line->extradataIds)
            next = std::max(next, id + 1);
    return next;
}

const Extradata *entryOf(const std::vector<ExtradataEntry> &table, std::uint32_t id)
{
    const auto it = std::ranges::find(table, id, &ExtradataEntry::id);
    return it == table.end() ? nullptr : &it->data;
}

}

std::optional<ExtradataEntry> parseExtradataLine(std::u8string_view line)
{
    u8sv rest = trim(line);
    if (!startsWith(rest, u8"Data:"))
        return std::nullopt;
    rest = trimLeft(rest.substr(5));
    const auto idEnd = rest.find(u8',');
    if (idEnd == u8sv::npos)
        return std::nullopt;
    const auto id = number(rest.substr(0, idEnd));
    rest = rest.substr(idEnd + 1);
    const auto keyEnd = rest.find(u8',');
    if (!id || keyEnd == 0 || keyEnd == u8sv::npos || keyEnd + 1 >= rest.size())
        return std::nullopt;
    ExtradataEntry entry{*id, {inlineDecode(rest.substr(0, keyEnd)), {}}};
    const char8_t kind = rest[keyEnd + 1];
    const u8sv value = rest.substr(keyEnd + 2);
    if (kind == u8'e')
        entry.data.value = inlineDecode(value);
    else if (kind == u8'u')
        entry.data.value = uuDecode(value);
    return entry;
}

std::u8string extradataLine(const ExtradataEntry &entry)
{
    const std::string id = std::to_string(entry.id);
    std::u8string out = u8"Data: " + std::u8string(id.begin(), id.end()) + u8',' + inlineEncode(entry.data.key) + u8',';
    const std::u8string escapedValue = inlineEncode(entry.data.value);
    if (4 * entry.data.value.size() < 3 * escapedValue.size())
        out += u8'u' + uuEncode(entry.data.value);
    else
        out += u8'e' + escapedValue;
    return out;
}

std::optional<ExtradataPrefix> extradataPrefix(std::u8string_view text)
{
    if (!startsWith(text, u8"{="))
        return std::nullopt;
    ExtradataPrefix prefix;
    std::size_t pos = 1;
    while (pos < text.size() && text[pos] == u8'=') {
        std::size_t end = pos + 1;
        while (end < text.size() && text[end] >= u8'0' && text[end] <= u8'9')
            ++end;
        const auto id = number(text.substr(pos + 1, end - pos - 1));
        if (!id)
            return std::nullopt;
        prefix.ids.push_back(*id);
        pos = end;
    }
    if (pos >= text.size() || text[pos] != u8'}')
        return std::nullopt;
    prefix.length = pos + 1;
    return prefix;
}

std::u8string extradataPrefixText(const std::vector<std::uint32_t> &ids)
{
    if (ids.empty())
        return {};
    std::u8string out = u8"{";
    for (const auto id : ids) {
        const std::string digits = std::to_string(id);
        out += u8'=' + std::u8string(digits.begin(), digits.end());
    }
    return out + u8'}';
}

std::vector<Extradata> resolveExtradata(const std::vector<ExtradataEntry> &table,
                                        const std::vector<std::uint32_t> &ids)
{
    std::vector<Extradata> out;
    for (const auto id : ids)
        if (const auto *entry = entryOf(table, id))
            out.push_back(*entry);
    return out;
}

std::vector<Extradata> extradataByKey(const std::vector<Extradata> &extra)
{
    std::map<std::u8string, std::u8string> byKey;
    for (const auto &e : extra)
        byKey[e.key] = e.value;
    std::vector<Extradata> out;
    for (auto &[key, value] : byKey)
        out.push_back({key, value});
    return out;
}

std::optional<std::u8string> extradataValue(const std::vector<Extradata> &extra, std::u8string_view key)
{
    const auto found = std::ranges::find(extra | std::views::reverse, key, &Extradata::key);
    if (found == std::ranges::end(extra | std::views::reverse))
        return std::nullopt;
    return found->value;
}

std::vector<Extradata> withExtradata(std::vector<Extradata> extra, std::u8string_view key, std::u8string value)
{
    if (std::ranges::count(extra, key, &Extradata::key) == 1 && extradataValue(extra, key) == value)
        return extra;
    extra = withoutExtradata(std::move(extra), key);
    extra.push_back({std::u8string(key), std::move(value)});
    return extra;
}

std::vector<Extradata> withoutExtradata(std::vector<Extradata> extra, std::u8string_view key)
{
    std::erase_if(extra, [&](const Extradata &e) { return e.key == key; });
    return extra;
}

namespace {

constexpr std::u8string_view kBase32 = u8"abcdefghijklmnopqrstuvwxyz234567";

std::optional<std::u8string> karaToken(const LineRecord &line, std::u8string_view key)
{
    auto value = extradataValue(line.extra, key);
    if (!value || value->size() != 10 || !value->starts_with(u8"1;") ||
        !std::ranges::all_of(value->substr(2), [](char8_t c) { return kBase32.find(c) != std::u8string_view::npos; }))
        return std::nullopt;
    return value->substr(2);
}

}

void linkGeneratedLines(const std::vector<LineRecord *> &lines)
{
    std::map<std::u8string, LineId> sources;
    for (const auto *line : lines)
        if (auto token = karaToken(*line, kKaraSourceKey))
            sources.emplace(std::move(*token), line->id);
    for (auto *line : lines)
        if (const auto token = karaToken(*line, kKaraFromKey))
            if (const auto found = sources.find(*token); found != sources.end() && found->second != line->id)
                line->generatedFrom = found->second;
}

std::u8string randomKaraToken()
{
    thread_local std::mt19937_64 random{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> letter(0, kBase32.size() - 1);
    std::u8string token(8, u8'a');
    for (auto &c : token)
        c = kBase32[letter(random)];
    return token;
}

void markGeneratedLines(Document &document, const std::function<std::u8string()> &newToken)
{
    const auto lines = document.lines();
    std::map<LineId, const LineRecord *> byId;
    std::set<std::u8string> held;
    std::map<std::u8string, LineId> firstHolder;
    for (const auto *line : lines) {
        byId.emplace(line->id, line);
        if (auto token = karaToken(*line, kKaraFromKey))
            held.insert(std::move(*token));
        if (auto token = karaToken(*line, kKaraSourceKey)) {
            held.insert(*token);
            firstHolder.emplace(std::move(*token), line->id);
        }
    }
    std::map<LineId, std::u8string> tokens;
    for (const auto *line : lines) {
        const auto source = byId.find(line->generatedFrom);
        if (!line->generatedFrom.value || source == byId.end() || tokens.contains(source->first))
            continue;
        auto token = karaToken(*source->second, kKaraSourceKey);
        if (!token || firstHolder.at(*token) != source->first) {
            do
                token = newToken();
            while (held.contains(*token));
            held.insert(*token);
        }
        tokens.emplace(source->first, std::move(*token));
    }
    std::vector<std::pair<LineId, std::vector<Extradata>>> edits;
    for (const auto *line : lines) {
        auto extra = line->extra;
        if (const auto own = tokens.find(line->id); own != tokens.end())
            extra = withExtradata(std::move(extra), kKaraSourceKey, u8"1;" + own->second);
        if (const auto from = tokens.find(line->generatedFrom); line->generatedFrom.value && from != tokens.end())
            extra = withExtradata(withoutExtradata(std::move(extra), kKaraSourceKey), kKaraFromKey, u8"1;" + from->second);
        if (extra != line->extra)
            edits.emplace_back(line->id, std::move(extra));
    }
    for (auto &[id, extra] : edits)
        document.editLine(id, [&](LineRecord &line) { line.extra = std::move(extra); });
}

std::u8string extradataClipboard(const std::vector<std::vector<Extradata>> &rows)
{
    std::u8string out;
    for (const auto &row : rows) {
        for (std::size_t i = 0; i < row.size(); ++i)
            out += (i ? u8"," : u8"") + inlineEncode(row[i].key) + u8':' + inlineEncode(row[i].value);
        out += u8'\n';
    }
    return out;
}

std::vector<std::vector<Extradata>> parseExtradataClipboard(std::u8string_view text)
{
    std::vector<std::vector<Extradata>> rows;
    while (!text.empty()) {
        const auto end = std::min(text.find(u8'\n'), text.size());
        std::vector<Extradata> row;
        for (u8sv pairs = text.substr(0, end); !pairs.empty();) {
            const auto comma = std::min(pairs.find(u8','), pairs.size());
            const u8sv pair = pairs.substr(0, comma);
            const auto colon = pair.find(u8':');
            if (colon == u8sv::npos)
                return {};
            row.push_back({inlineDecode(pair.substr(0, colon)), inlineDecode(pair.substr(colon + 1))});
            pairs.remove_prefix(std::min(comma + 1, pairs.size()));
        }
        rows.push_back(std::move(row));
        text.remove_prefix(std::min(end + 1, text.size()));
    }
    return rows;
}

ExtradataPlan planExtradata(const Document &document)
{
    const auto &table = document.extradata();
    std::uint32_t next = idAboveAllNamed(document);
    ExtradataPlan plan;
    const auto idOf = [&](const Extradata &value) {
        for (const auto &entry : table)
            if (entry.data == value)
                return entry.id;
        for (const auto &entry : plan.added)
            if (entry.data == value)
                return entry.id;
        plan.added.push_back({next, value});
        return next++;
    };
    for (const auto *line : document.lines()) {
        if (line->extra.empty() && line->extradataIds.empty())
            continue;
        if (resolveExtradata(table, line->extradataIds) == line->extra) {
            plan.ids[line->id] = line->extradataIds;
            continue;
        }
        std::set<std::uint32_t> ids;
        for (const auto &value : line->extra)
            ids.insert(idOf(value));
        plan.ids[line->id] = {ids.begin(), ids.end()};
    }
    return plan;
}

}
